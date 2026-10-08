// 코드 패널의 AI 탭. 설치된 에이전트 프로그램을 가상 터미널에서 고치지 않고 그대로 실행하고, 출력도 그대로 보여 준다
import { useCallback, useEffect, useRef, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { Terminal } from '@xterm/xterm';
import { FitAddon } from '@xterm/addon-fit';
import { Unicode11Addon } from '@xterm/addon-unicode11';
import '@xterm/xterm/css/xterm.css';
import { Play, RotateCw, Square } from 'lucide-react';
import { IconButton } from './Panels';

// 편집기 MCP 서버에 붙는 정보. config는 Claude Code의 --mcp-config 형식 JSON
type Mcp = { config: string; url: string; token: string };

// 드롭다운에 나오는 에이전트. 새 에이전트는 여기에 더한다.
// program은 PATH에서 찾고, 없으면 search 폴더의 하위 폴더에서 찾는다. launch는 MCP 서버에 붙는 실행 인자와 환경 변수를 만든다.
// 편집기는 에이전트를 고치지 않고 실행만 한다. 로그인은 에이전트가 직접 하고, 편집기는 로그인 정보를 읽거나 확인하지 않는다
type Agent = {
    id: string; label: string; program: string; search?: string[]; install: string;
    launch(mcp: Mcp): { args: string[]; env?: Record<string, string> };
};

const agents: Agent[] = [
    {
        id: 'claude-code', label: 'Claude Code', program: 'claude', install: 'https://code.claude.com/docs/en/setup',
        launch: (mcp) => ({ args: ['--mcp-config', mcp.config] }),
    },
    {
        // Codex 데스크톱 앱은 CLI를 %LOCALAPPDATA%/OpenAI/Codex/bin/<업데이트마다 바뀌는 폴더>에 둔다.
        // 토큰은 실행 인자에 드러나지 않게 환경 변수로 넘긴다
        id: 'codex', label: 'Codex', program: 'codex', search: ['%LOCALAPPDATA%/OpenAI/Codex/bin'], install: 'https://developers.openai.com/codex/cli',
        launch: (mcp) => ({
            // --no-daemon: 앱에 든 CLI는 앱의 공유 백그라운드 서버에 붙으려다 실패하므로 혼자 실행한다
            args: ['--no-daemon', '-c', `mcp_servers.templide.url="${mcp.url}"`, '-c', 'mcp_servers.templide.bearer_token_env_var="TEMPLIDE_MCP_TOKEN"'],
            env: { TEMPLIDE_MCP_TOKEN: mcp.token },
        }),
    },
];

type Status = 'idle' | 'running' | 'exited' | 'failed';

// visible은 AI 탭이 보이는지. 탭을 바꿔도 세션과 터미널은 그대로 둔다.
// session은 이 패널이 있는 파일 탭. 에이전트는 MCP 주소 /mcp/<session>으로 그 탭의 문서만 고친다
export function AgentPanel({ visible, folder, session }: { visible: boolean; folder: string | null; session: string }) {
    const [agentId, setAgentId] = useState(agents[0].id);
    const [status, setStatus] = useState<Status>('idle');
    const [message, setMessage] = useState<string | null>(null);
    const host = useRef<HTMLDivElement>(null);
    const terminal = useRef<Terminal | null>(null);
    const fit = useRef<FitAddon | null>(null);
    const running = useRef<string | null>(null); // 지금 실행 중인 에이전트 id
    const run = useRef(0);                         // 그 실행의 번호. 이전 실행의 출력과 끝 알림은 버린다
    const early = useRef<{ run: number; data: string }[]>([]); // 번호를 받기 전에 온 출력
    const agent = agents.find((each) => each.id === agentId)!;
    const sessionId = `${session}/${agent.id}`; // 편집기(Rust)의 세션 이름. 파일 탭마다 따로 실행한다

    useEffect(() => {
        const term = new Terminal({
            fontFamily: 'Consolas, "Malgun Gothic", monospace',
            fontSize: 13,
            cursorBlink: true,
            theme: { background: '#1e1e1e' },
            allowProposedApi: true, // unicode 폭 표를 고르는 데 필요하다
        });
        // 에이전트(Codex 등)와 같은 글자 폭 표를 써야 이모지, 기호가 있는 줄을 다시 그릴 때 글자가 섞이지 않는다
        term.loadAddon(new Unicode11Addon());
        term.unicode.activeVersion = '11';
        const fitAddon = new FitAddon();
        term.loadAddon(fitAddon);
        term.open(host.current!);
        terminal.current = term;
        fit.current = fitAddon;
        const input = term.onData((data) => {
            if (running.current) {
                invoke('agent_write', { id: running.current, data }).catch(() => {});
            }
        });
        const resize = term.onResize(({ cols, rows }) => {
            if (running.current) {
                invoke('agent_resize', { id: running.current, cols, rows }).catch(() => {});
            }
        });
        const output = listen<{ run: number; data: string }>('agent-output', ({ payload }) => {
            if (payload.run === run.current) {
                term.write(payload.data);
            } else if (run.current === 0 && running.current) {
                early.current.push(payload);
            }
        });
        const exit = listen<{ run: number; code: number | null }>('agent-exit', ({ payload }) => {
            if (payload.run === run.current) {
                running.current = null;
                setStatus('exited');
                term.write(`\r\n\x1b[90m[끝났습니다${payload.code != null ? ` (코드 ${payload.code})` : ''}]\x1b[0m\r\n`);
            }
        });
        return () => {
            input.dispose();
            resize.dispose();
            output.then((off) => off());
            exit.then((off) => off());
            if (running.current) {
                invoke('agent_stop', { id: running.current }).catch(() => {});
            }
            term.dispose();
        };
    }, []);

    // 탭이 보이거나 패널 크기가 바뀌면 터미널 칸 수를 맞춘다
    useEffect(() => {
        if (!visible || !host.current) {
            return;
        }
        const observer = new ResizeObserver(() => {
            if (host.current && host.current.offsetWidth > 0) {
                fit.current?.fit();
            }
        });
        observer.observe(host.current);
        terminal.current?.focus();
        return () => observer.disconnect();
    }, [visible]);

    const start = useCallback(async () => {
        const term = terminal.current;
        if (!term) {
            return;
        }
        if (running.current) {
            const previous = running.current;
            running.current = null;
            await invoke('agent_stop', { id: previous }).catch(() => {});
        }
        term.reset();
        fit.current?.fit();
        setMessage(null);
        try {
            run.current = 0;
            early.current = [];
            running.current = sessionId;
            const parsed = JSON.parse(await invoke<string>('mcp_config'));
            const server = parsed.mcpServers.templide;
            server.url += `/${encodeURIComponent(session)}`;
            const config = JSON.stringify(parsed);
            const { args, env } = agent.launch({ config, url: server.url, token: server.headers.Authorization.replace(/^Bearer /, '') });
            run.current = await invoke<number>('agent_start', { id: sessionId, program: agent.program, search: agent.search ?? [], args, env: env ?? {}, cwd: folder, cols: term.cols, rows: term.rows });
            early.current.filter((each) => each.run === run.current).forEach((each) => term.write(each.data));
            early.current = [];
            setStatus('running');
            term.focus();
        } catch (error) {
            running.current = null;
            setStatus('failed');
            setMessage(`${agent.label}을(를) 실행하지 못했습니다. 설치되어 있는지 확인해 주세요. (${error})`);
        }
    }, [agent, folder, session, sessionId]);

    const stop = useCallback(() => {
        if (running.current) {
            invoke('agent_stop', { id: running.current }).catch(() => {});
        }
    }, []);

    const choose = (id: string) => {
        stop();
        running.current = null;
        setAgentId(id);
        setStatus('idle');
        setMessage(null);
        terminal.current?.reset();
    };

    return (
        <div className="agent" style={{ display: visible ? 'flex' : 'none' }}>
            <div className="agent-bar">
                <select value={agentId} onChange={(event) => choose(event.target.value)} title="에이전트">
                    {agents.map((each) => <option key={each.id} value={each.id}>{each.label}</option>)}
                </select>
                {status === 'running'
                    ? <>
                        <IconButton icon={RotateCw} title="다시 시작" onClick={start} />
                        <IconButton icon={Square} title="멈추기" onClick={stop} danger />
                    </>
                    : <IconButton icon={Play} title={`${agent.label} 실행`} onClick={start}>실행</IconButton>}
                <span className="agent-folder muted" title={folder ?? undefined}>{folder ?? '파일을 열지 않아 홈 폴더에서 실행합니다'}</span>
                {message && (
                    <span className="agent-message">
                        {message}
                        <button onClick={() => invoke('open_path', { path: agent.install, reveal: false })}>설치 안내</button>
                    </span>
                )}
            </div>
            <div className="agent-terminal">
                <div ref={host} className="agent-host" />
                {status === 'idle' && (
                    <div className="agent-idle">
                        <button className="play-button" onClick={start}><Play size={14} fill="currentColor" /> {agent.label} 실행</button>
                        <span className="muted">설치된 {agent.label}을(를) 문서 폴더에서 그대로 실행합니다. 로그인과 요금은 본인 계정을 따릅니다.</span>
                    </div>
                )}
            </div>
        </div>
    );
}

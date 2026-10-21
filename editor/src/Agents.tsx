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
import { t } from './i18n';
import { getSettings, useSettings } from './settings';
import { openSettings } from './settingsWindow';
import { isLocalProvider, ollamaHost, profileLabel, providerLabels, type AgentId, type AgentProfile } from './agentProfiles';

// 편집기 MCP 서버에 붙는 정보. config는 Claude Code의 --mcp-config 형식 JSON
type Mcp = { config: string; url: string; token: string };

// 프로필에서 고를 수 있는 에이전트 프로그램. 새 에이전트는 여기와 agentProfiles.ts에 더한다.
// program은 PATH에서 찾고, 없으면 search 폴더의 하위 폴더에서 찾는다. launch는 MCP 서버와 프로필의 제공자, 모델에 맞춘 실행 인자와 환경 변수를 만든다.
// version이면 실행 전에 program --version의 출력을 물어 launch에 넘긴다 (그 밖에는 빈 문자열).
// 편집기는 에이전트를 고치지 않고 실행만 한다. 로그인은 에이전트가 직접 하고, 편집기는 로그인 정보를 읽거나 확인하지 않는다
type Agent = {
    id: AgentId; label: string; program: string; search?: string[]; install: string; version?: boolean;
    launch(mcp: Mcp, profile: AgentProfile, version: string): { args: string[]; env?: Record<string, string> };
};

export const agents: Agent[] = [
    {
        id: 'claude-code', label: 'Claude Code', program: 'claude', install: 'https://code.claude.com/docs/en/setup',
        launch: (mcp, profile) => {
            const model = profile.model.trim();
            return { args: ['--mcp-config', mcp.config, ...(model ? ['--model', model] : [])] };
        },
    },
    {
        // Codex 데스크톱 앱은 CLI를 %LOCALAPPDATA%/OpenAI/Codex/bin/<업데이트마다 바뀌는 폴더>에 둔다.
        // 토큰은 실행 인자에 드러나지 않게 환경 변수로 넘긴다
        id: 'codex', label: 'Codex', program: 'codex', search: ['%LOCALAPPDATA%/OpenAI/Codex/bin'], install: 'https://developers.openai.com/codex/cli',
        launch: (mcp, profile) => {
            const model = profile.model.trim();
            // --no-daemon: 앱에 든 CLI는 앱의 공유 백그라운드 서버에 붙으려다 실패하므로 혼자 실행한다
            const args = ['--no-daemon', '-c', `mcp_servers.templide.url="${mcp.url}"`, '-c', 'mcp_servers.templide.bearer_token_env_var="TEMPLIDE_MCP_TOKEN"'];
            if (model) {
                args.push('-m', model);
            }
            return { args, env: { TEMPLIDE_MCP_TOKEN: mcp.token } };
        },
    },
    {
        // 이 컴퓨터의 Ollama, LM Studio로 모델을 돌린다. 설정은 OPENCODE_CONFIG_CONTENT로 넘기며(ollama launch opencode와 같은 방식),
        // OpenCode의 설정 파일은 고치지 않는다. 이 설정이 설정 파일보다 앞서므로 모델은 프로필의 것을 쓰고, 토큰은 {env:}로 환경 변수에서 읽게 한다.
        // 2.x는 설정 형식이 다르고, 기본으로 여러 실행이 함께 쓰는 백그라운드 서버에 붙는다. 그 서버는 처음 실행의 설정(그때의 MCP 주소와 토큰)을
        // 계속 쓰므로 --standalone으로 혼자 실행한다(1.x는 이 인자를 받지 않는다). 2.x는 MCP 도구를 Code Mode(execute 안에서 코드로 부르기)로 감싸는데,
        // 작은 모델은 그 코드를 잘 쓰지 못하므로 끄고 도구를 바로 부르게 한다
        id: 'opencode', label: 'OpenCode', program: 'opencode', install: 'https://opencode.ai/docs/', version: true,
        launch: (mcp, profile, version) => {
            const model = profile.model.trim();
            const baseURL = profile.provider === 'ollama' ? `${ollamaHost(profile)}/v1` : 'http://127.0.0.1:1234/v1'; // LM Studio 로컬 서버의 기본 주소
            if (Number(/(\d+)\.\d+/.exec(version)?.[1]) >= 2) { // 2.x는 'opencode v2.0.32', 1.x는 '1.19.3'
                const config = {
                    providers: { [profile.provider]: { package: 'aisdk:@ai-sdk/openai-compatible', name: providerLabels[profile.provider], settings: { baseURL }, models: { [model]: { name: model } } } },
                    model: { providerID: profile.provider, model },
                    mcp: { servers: { templide: { type: 'remote', url: mcp.url, oauth: false, codemode: false, headers: { Authorization: 'Bearer {env:TEMPLIDE_MCP_TOKEN}' } } } },
                };
                return { args: ['--standalone'], env: { OPENCODE_CONFIG_CONTENT: JSON.stringify(config), TEMPLIDE_MCP_TOKEN: mcp.token } };
            }
            const config = {
                $schema: 'https://opencode.ai/config.json',
                provider: { [profile.provider]: { npm: '@ai-sdk/openai-compatible', name: providerLabels[profile.provider], options: { baseURL }, models: { [model]: { name: model } } } },
                model: `${profile.provider}/${model}`,
                mcp: { templide: { type: 'remote', url: mcp.url, oauth: false, headers: { Authorization: 'Bearer {env:TEMPLIDE_MCP_TOKEN}' } } },
            };
            return { args: [], env: { OPENCODE_CONFIG_CONTENT: JSON.stringify(config), TEMPLIDE_MCP_TOKEN: mcp.token } };
        },
    },
];

type Status = 'idle' | 'running' | 'exited' | 'failed';

// visible은 AI 탭이 보이는지. 탭을 바꿔도 세션과 터미널은 그대로 둔다.
// session은 이 패널이 있는 파일 탭. 에이전트는 MCP 주소 /mcp/<session>으로 그 탭의 문서만 고친다
export function AgentPanel({ visible, folder, session }: { visible: boolean; folder: string | null; session: string }) {
    const { profiles, default: defaultProfile } = useSettings().agent;
    const [profileId, setProfileId] = useState(() => getSettings().agent.default); // 기본 프로필은 설정
    const [status, setStatus] = useState<Status>('idle');
    const [message, setMessage] = useState<{ text: string; fix: 'install' | 'settings' } | null>(null); // fix는 메시지 옆 단추
    const host = useRef<HTMLDivElement>(null);
    const terminal = useRef<Terminal | null>(null);
    const fit = useRef<FitAddon | null>(null);
    const running = useRef<string | null>(null); // 지금 실행 중인 에이전트 id
    const run = useRef(0);                         // 그 실행의 번호. 이전 실행의 출력과 끝 알림은 버린다
    const early = useRef<{ run: number; data: string }[]>([]); // 번호를 받기 전에 온 출력
    // 고른 프로필을 설정에서 지웠으면 기본 프로필로
    const profile = profiles.find((each) => each.id === profileId) ?? profiles.find((each) => each.id === defaultProfile) ?? profiles[0];
    const agent = agents.find((each) => each.id === profile.agent)!;
    const sessionId = `${session}/${profile.id}`; // 편집기(Rust)의 세션 이름. 파일 탭마다 따로 실행한다

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
                term.write(`\r\n\x1b[90m[${t('끝났습니다')}${payload.code != null ? t(' (코드 {0})', payload.code) : ''}]\x1b[0m\r\n`);
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
        if (isLocalProvider(profile.provider) && !profile.model.trim()) {
            setStatus('failed');
            setMessage({ text: t('이 프로필에 모델이 없습니다. 설정의 AI 에이전트에서 모델을 정해 주세요.'), fix: 'settings' });
            return;
        }
        try {
            run.current = 0;
            early.current = [];
            running.current = sessionId;
            const parsed = JSON.parse(await invoke<string>('mcp_config'));
            const server = parsed.mcpServers.templide;
            server.url += `/${encodeURIComponent(session)}`;
            const config = JSON.stringify(parsed);
            const program = getSettings().agent.paths[agent.id] || agent.program;
            const search = agent.search ?? [];
            const version = agent.version ? await invoke<string>('program_version', { program, search }).catch(() => '') : '';
            const { args, env } = agent.launch({ config, url: server.url, token: server.headers.Authorization.replace(/^Bearer /, '') }, profile, version);
            run.current = await invoke<number>('agent_start', { id: sessionId, program, search, args, env: env ?? {}, cwd: folder, cols: term.cols, rows: term.rows });
            early.current.filter((each) => each.run === run.current).forEach((each) => term.write(each.data));
            early.current = [];
            setStatus('running');
            term.focus();
        } catch (error) {
            running.current = null;
            setStatus('failed');
            setMessage({ text: t('{0}을(를) 실행하지 못했습니다. 설치되어 있는지 확인해 주세요. ({1})', agent.label, error), fix: 'install' });
        }
    }, [agent, profile, folder, session, sessionId]);

    const stop = useCallback(() => {
        if (running.current) {
            invoke('agent_stop', { id: running.current }).catch(() => {});
        }
    }, []);

    const choose = (id: string) => {
        stop();
        running.current = null;
        setProfileId(id);
        setStatus('idle');
        setMessage(null);
        terminal.current?.reset();
    };

    return (
        <div className="agent" style={{ display: visible ? 'flex' : 'none' }}>
            <div className="agent-bar">
                <select value={profile.id} onChange={(event) => choose(event.target.value)} title={t('프로필')}>
                    {profiles.map((each) => <option key={each.id} value={each.id}>{profileLabel(each)}</option>)}
                </select>
                {status === 'running'
                    ? <>
                        <IconButton icon={RotateCw} title={t('다시 시작')} onClick={start} />
                        <IconButton icon={Square} title={t('멈추기')} onClick={stop} danger />
                    </>
                    : <IconButton icon={Play} title={t('{0} 실행', agent.label)} onClick={start}>{t('실행')}</IconButton>}
                <span className="agent-folder muted" title={folder ?? undefined}>{folder ?? t('열려 있는 파일이 없어 홈 폴더에서 실행합니다.')}</span>
                {message && (
                    <span className="agent-message">
                        {message.text}
                        {message.fix === 'install'
                            ? <button onClick={() => invoke('open_path', { path: agent.install, reveal: false })}>{t('설치 안내')}</button>
                            : <button onClick={() => openSettings()}>{t('설정 열기')}</button>}
                    </span>
                )}
            </div>
            <div className="agent-terminal">
                <div ref={host} className="agent-host" />
                {status === 'idle' && (
                    <div className="agent-idle">
                        <button className="play-button" onClick={start}><Play size={14} fill="currentColor" /> {t('{0} 실행', agent.label)}</button>
                        <span className="muted">{isLocalProvider(profile.provider)
                            ? t('설치된 {0}을(를) 문서 폴더에서 실행합니다. 모델은 이 컴퓨터의 {1}에서 돌립니다.', agent.label, providerLabels[profile.provider])
                            : t('설치된 {0}을(를) 문서 폴더에서 실행합니다. 로그인과 요금은 본인 계정을 사용합니다.', agent.label)}</span>
                    </div>
                )}
            </div>
        </div>
    );
}

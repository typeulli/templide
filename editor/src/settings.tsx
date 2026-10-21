/**
 * 설정 창을 구성한다.
 * 로고를 누르면 창이 열리고, 변경한 값은 즉시 적용한 뒤 저장한다.
 */
// 컴파일러 경로는 Rust의 compiler_use가 프로세스를 다시 시작하고 저장한다.
import { useCallback, useEffect, useId, useRef, useState, type MouseEvent, type ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import { invoke } from '@tauri-apps/api/core';
import { emit, listen } from '@tauri-apps/api/event';
import { getVersion } from '@tauri-apps/api/app';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { ask, open as openDialog } from '@tauri-apps/plugin-dialog';
import { Bot, ChevronDown, ChevronRight, CircleAlert, CircleCheck, Code, Cpu, FolderOpen, Globe, Info, Keyboard, LoaderCircle, Plus, Presentation, RotateCcw, Trash2, TriangleAlert, WandSparkles, type LucideIcon } from 'lucide-react';
import logo from '../../assets/icon/templide.svg';
import license from '../../LICENSE?raw';
import { agents } from './Agents';
import { agentLabels, agentProviders, defaultOllamaHost, defaultProfiles, isLocalProvider, modelSuggestions, ollamaHost, profileLabel, providerLabels, type AgentId, type AgentProfile, type ProviderId } from './agentProfiles';
import { languages, t, useLang, type LanguageSetting } from './i18n';
import { encodingHint, encodingLabel, encodingNames } from './encodings';
import { effectiveFmtValue, fmtGroups, fmtLabels, type FmtChange, type FmtState } from './fmtSettings';
import type { FmtOption } from './lsp';
import { editorLimits, initSettings, resetSettings, updateSettings, useSettings } from './settings';
import { ShortcutSettings } from './ShortcutSettings';
import './styles.css';
import './settings.css';

type Page = 'general' | 'editor' | 'format' | 'compiler' | 'agent' | 'show' | 'shortcuts' | 'about';

const isWindows = navigator.userAgent.includes('Windows');

// stack이면 설명 아래에 입력칸을 넓게 둔다 (경로)
function Row({ title, hint, stack, code, children }: { title: string; hint?: string; stack?: boolean; code?: string; children: ReactNode }) {
    return (
        <div className={'setting-row' + (stack ? ' stack' : '')}>
            <div className="setting-text">
                <div className="setting-title">{title}{code && <span className="setting-code">{code}</span>}</div>
                {hint && <div className="setting-hint">{hint}</div>}
            </div>
            <div className="setting-control">{children}</div>
        </div>
    );
}

function Switch({ checked, onChange }: { checked: boolean; onChange(checked: boolean): void }) {
    return (
        <label className="switch">
            <input type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} />
            <span />
        </label>
    );
}

// 입력을 마치면(Enter, 칸을 벗어남) 범위 안의 정수로 맞춰 적용한다
function NumberField({ value, min, max, onChange }: { value: number; min: number; max: number; onChange(value: number): void }) {
    const [draft, setDraft] = useState(String(value));
    useEffect(() => setDraft(String(value)), [value]);
    const commit = () => {
        const parsed = Math.round(Number(draft));
        const next = Number.isFinite(parsed) && draft.trim() !== '' ? Math.min(Math.max(parsed, min), max) : value;
        setDraft(String(next));
        if (next !== value) {
            onChange(next);
        }
    };
    return (
        <input className="number-input" type="number" min={min} max={max} value={draft}
            onChange={(event) => setDraft(event.target.value)} onBlur={commit} onKeyDown={(event) => event.key === 'Enter' && commit()} />
    );
}

function GeneralPage() {
    const settings = useSettings();
    return (
        <>
            <Row title={t('언어')} hint={t('화면에 보이는 글의 언어입니다. 바로 바뀝니다.')}>
                <select value={settings.language} onChange={(event) => updateSettings((next) => ({ ...next, language: event.target.value as LanguageSetting }))}>
                    <option value="system">{t('시스템 설정 따르기')}</option>
                    {languages.map((each) => <option key={each.id} value={each.id}>{each.name}</option>)}
                </select>
            </Row>
            <Row title={t('코드 패널을 펼친 채로 시작')} hint={t('파일을 열 때 아래의 코드 패널을 펼쳐 둡니다.')}>
                <Switch checked={settings.general.codePanelOpen} onChange={(checked) => updateSettings((next) => ({ ...next, general: { ...next.general, codePanelOpen: checked } }))} />
            </Row>
            <Row title={t('기본 인코딩')} hint={t('BOM이 없는 파일을 열 때 쓰는 인코딩입니다. 열린 파일의 인코딩은 코드 패널 오른쪽 위의 인코딩 표시를 눌러 바꿀 수 있습니다.')}>
                <select value={settings.general.defaultEncoding} onChange={(event) => updateSettings((next) => ({ ...next, general: { ...next.general, defaultEncoding: event.target.value } }))}>
                    {encodingNames.map((name) => <option key={name} value={name}>{`${encodingLabel(name)} (${encodingHint(name)})`}</option>)}
                </select>
            </Row>
            <Row title={t('인코딩 자동 추측')} hint={t('BOM이 없고 UTF-8이 아닌 파일의 인코딩을 추측해서 엽니다. 추측이 틀릴 수 있으므로 열 때 알려 줍니다.')}>
                <Switch checked={settings.general.autoGuessEncoding} onChange={(checked) => updateSettings((next) => ({ ...next, general: { ...next.general, autoGuessEncoding: checked } }))} />
            </Row>
        </>
    );
}

function EditorPage() {
    const settings = useSettings();
    const change = (part: Partial<typeof settings.editor>) => updateSettings((next) => ({ ...next, editor: { ...next.editor, ...part } }));
    return (
        <>
            <Row title={t('글자 크기')}>
                <NumberField value={settings.editor.fontSize} min={editorLimits.fontSize[0]} max={editorLimits.fontSize[1]} onChange={(fontSize) => change({ fontSize })} />
            </Row>
            <Row title={t('탭 크기')} hint={t('Tab 키 하나가 차지하는 칸 수입니다.')}>
                <NumberField value={settings.editor.tabSize} min={editorLimits.tabSize[0]} max={editorLimits.tabSize[1]} onChange={(tabSize) => change({ tabSize })} />
            </Row>
            <Row title={t('줄 바꿈')} hint={t('긴 줄을 창 너비에서 접어 보여 줍니다.')}>
                <Switch checked={settings.editor.wordWrap} onChange={(wordWrap) => change({ wordWrap })} />
            </Row>
            <Row title={t('줄 번호')}>
                <Switch checked={settings.editor.lineNumbers} onChange={(lineNumbers) => change({ lineNumbers })} />
            </Row>
            <Row title={t('미니맵')} hint={t('코드 전체를 작게 보여 주는 오른쪽 지도입니다.')}>
                <Switch checked={settings.editor.minimap} onChange={(minimap) => change({ minimap })} />
            </Row>
        </>
    );
}

type CompilerInfo = { path: string | null; source: string | null; auto: string; setting: string | null; error: string | null };
type Outcome = { busy: true } | { busy: false; ok: boolean; message: string };

const executableFilters = isWindows ? [{ name: 'templide', extensions: ['exe'] }] : undefined;

// compiler_use가 돌려준 오류를 설명한다. 알 수 없으면 원문 그대로
function explainCompilerError(error: unknown): string {
    const text = String(error);
    if (text.endsWith(': not a file')) {
        return t('파일을 찾을 수 없습니다: {0}', text.slice(0, -': not a file'.length));
    }
    if (text.includes('did not answer')) {
        return t('templide 컴파일러가 아닌 것 같습니다 (응답이 없습니다): {0}', text);
    }
    return text;
}

// 포맷. 편집기 창에서 보이는 탭의 문서에 적힌 #fmt를 보여 주고, 바꾸면 그 문서에 #fmt 줄을 적는다 (편집기 창과는 이벤트로 주고받는다. Shell.tsx)
function FormatPage() {
    const [state, setState] = useState<FmtState | null>(null); // null이면 아직 편집기 창의 답이 없다
    const shown = useRef('');
    useEffect(() => {
        let stopped = false;
        const off = listen<FmtState>('fmt-state', (event) => {
            const text = JSON.stringify(event.payload);
            if (!stopped && text !== shown.current) {
                shown.current = text;
                setState(event.payload);
            }
        });
        // 편집기에서 탭을 바꾸거나 코드를 고친 것도 따라가도록 잠깐마다 다시 묻는다
        const query = () => {
            emit('fmt-query').catch(() => {});
        };
        query();
        const timer = window.setInterval(query, 1000);
        window.addEventListener('focus', query);
        return () => {
            stopped = true;
            window.clearInterval(timer);
            window.removeEventListener('focus', query);
            off.then((stop) => stop());
        };
    }, []);

    // 곧바로 화면에 보이고(편집기 창의 답을 기다리지 않는다), 편집기 창에는 문서에 적어 달라고 부탁한다
    const change = (next: FmtChange) => {
        emit('fmt-change', next).catch((error) => console.error('fmt-change', error));
        setState((current) => {
            if (!current || 'defaults' in next) {
                return current;
            }
            const written = { ...current.written };
            if (next.value === null) {
                delete written[next.name];
            } else {
                written[next.name] = next.value;
            }
            return { ...current, written, count: Math.max(current.count, 1) };
        });
    };

    if (state === null) {
        return <p className="setting-hint">{t('편집기 창에서 문서의 설정을 받는 중입니다...')}</p>;
    }
    if (state.document === null) {
        return <div className="settings-empty">{t('열려 있는 .tlide 문서가 없습니다. 문서를 열면 그 문서의 #fmt 포맷 설정을 여기서 고칠 수 있습니다.')}</div>;
    }

    const control = (option: FmtOption) => {
        const value = effectiveFmtValue(state, option);
        const labels = fmtLabels[option.name]?.values ?? {};
        const nameOf = (choice: string) => labels[choice] ? t(labels[choice]) : choice;
        const set = (next: string) => change({ name: option.name, value: next });
        switch (option.kind) {
            case 'integer':
                return <NumberField value={Number(value)} min={option.min} max={option.max} onChange={(number) => set(String(number))} />;
            case 'boolean':
                return <Switch checked={value === 'true'} onChange={(checked) => set(String(checked))} />;
            case 'choice':
                return (
                    <select value={value} onChange={(event) => set(event.target.value)}>
                        {option.choices.map((choice) => <option key={choice} value={choice}>{nameOf(choice)}</option>)}
                    </select>
                );
            case 'keepOrInteger':
                return (
                    <select value={value} onChange={(event) => set(event.target.value)}>
                        <option value="keep">{nameOf('keep')}</option>
                        {Array.from({ length: option.max - option.min + 1 }, (_, index) => String(option.min + index)).map((number) => <option key={number} value={number}>{number}</option>)}
                    </select>
                );
        }
    };
    const row = (option: FmtOption) => {
        const label = fmtLabels[option.name];
        return (
            <Row key={option.name} title={label ? t(label.title) : option.name} hint={label ? label.hint && t(label.hint) : option.description} code={option.name}>
                <div className="fmt-control">
                    {control(option)}
                    <button className="icon-only" disabled={state.written[option.name] === undefined} title={t('기본값으로 되돌리기 (문서에서 이 옵션의 #fmt 줄을 지웁니다)')}
                        onClick={() => change({ name: option.name, value: null })}><RotateCcw size={13} /></button>
                </div>
            </Row>
        );
    };
    const grouped = new Set(fmtGroups.flatMap((group) => group.names));
    const others = state.options.filter((option) => !grouped.has(option.name));
    return (
        <>
            <p className="setting-hint">{t('지금 편집기에서 보이는 문서 {0}의 포맷 설정입니다. 값을 바꾸면 그 문서에 #fmt 줄이 적히고, 코드의 마법봉 단추(포맷)가 이 설정대로 정리합니다. 실행 취소로 되돌릴 수 있습니다.', state.document)}</p>
            {state.count === 0 && (
                <div className="notice warning">
                    <TriangleAlert size={14} />
                    <span>{t('이 문서에는 #fmt 설정이 없어 포맷하지 않습니다. 아래에서 값을 하나라도 바꾸면 #fmt 줄이 생기고 그때부터 포맷합니다.')}</span>
                    <button className="plain-button" onClick={() => change({ defaults: true })}>{t('기본 설정 넣기')}</button>
                </div>
            )}
            {fmtGroups.map((group) => {
                const options = group.names.flatMap((name) => state.options.filter((option) => option.name === name));
                return options.length > 0 && <section key={group.title}><h2 className="setting-group">{t(group.title)}</h2>{options.map(row)}</section>;
            })}
            {others.length > 0 && <section><h2 className="setting-group">{t('기타')}</h2>{others.map(row)}</section>}
        </>
    );
}

function CompilerPage() {
    const [info, setInfo] = useState<CompilerInfo | null>(null);
    const [draft, setDraft] = useState('');
    const [outcome, setOutcome] = useState<Outcome | null>(null);

    const load = useCallback(async () => {
        const next = await invoke<CompilerInfo>('compiler_info');
        setInfo(next);
        return next;
    }, []);
    useEffect(() => {
        load().then((next) => setDraft(next.setting ?? ''));
        const off = listen('compiler-restarted', () => load());
        return () => {
            off.then((stop) => stop());
        };
    }, [load]);

    const apply = async (path: string | null) => {
        setOutcome({ busy: true });
        try {
            const next = await invoke<CompilerInfo>('compiler_use', { path });
            setInfo(next);
            setDraft(path ?? '');
            setOutcome({ busy: false, ok: true, message: path ? t('컴파일러를 바꿨습니다. 열려 있는 문서를 새 컴파일러로 다시 분석합니다.') : t('기본 컴파일러를 씁니다. 열려 있는 문서를 다시 분석합니다.') });
        } catch (error) {
            setOutcome({ busy: false, ok: false, message: explainCompilerError(error) });
        }
    };
    const browse = async () => {
        const chosen = await openDialog({ multiple: false, directory: false, filters: executableFilters });
        if (typeof chosen === 'string') {
            setDraft(chosen);
            setOutcome(null);
        }
    };
    const sources: Record<string, string> = {
        setting: t('설정에서 지정'),
        env: t('TEMPLIDE_EXE 환경 변수'),
        installed: t('설치된 컴파일러'),
        development: t('개발용 빌드 (cmake-build-release)'),
    };
    const using = info?.setting ?? ''; // 설정에 적힌 경로. 실행하지 못했어도 그대로 보인다
    const busy = !!outcome && outcome.busy;
    return (
        <>
            <Row title={t('현재 컴파일러')}>
                <div className="path-box">
                    <div className="path-value" title={info?.path ?? ''}>{info?.path ?? '…'}</div>
                    {info?.source && <div className="setting-hint">{sources[info.source] ?? info.source}</div>}
                </div>
            </Row>
            {info?.error && <div className="notice warning"><CircleAlert size={14} /> {t('설정에 적은 컴파일러를 실행하지 못해 기본 컴파일러로 시작했습니다.')} {info.error}</div>}
            <Row stack title={t('컴파일러 경로')} hint={t('비워 두면 편집기와 함께 설치된 컴파일러를 사용합니다. 컴파일러 실행 파일과 같은 위치의 packages 및 libs 폴더도 함께 사용합니다.')}>
                <div className="path-edit">
                    <input className="path-input" value={draft} placeholder={info?.auto ?? ''} spellCheck={false}
                        onChange={(event) => { setDraft(event.target.value); setOutcome(null); }}
                        onKeyDown={(event) => event.key === 'Enter' && draft.trim() !== using && apply(draft.trim() || null)} />
                    <button className="plain-button" onClick={browse} disabled={busy}><FolderOpen size={14} /> {t('찾아보기')}</button>
                </div>
            </Row>
            <div className="setting-actions">
                <button className="primary-button" disabled={busy || draft.trim() === using} onClick={() => apply(draft.trim() || null)}>
                    {busy ? <LoaderCircle size={14} className="spin" /> : null} {t('적용')}
                </button>
                <button className="plain-button" disabled={busy || !info?.setting} onClick={() => apply(null)}><RotateCcw size={13} /> {t('기본 컴파일러로')}</button>
            </div>
            {outcome && !outcome.busy && (
                <div className={'notice ' + (outcome.ok ? 'ok' : 'error')}>
                    {outcome.ok ? <CircleCheck size={14} /> : <CircleAlert size={14} />} {outcome.message}
                </div>
            )}
        </>
    );
}

function AgentPage() {
    const settings = useSettings();
    const [open, setOpen] = useState<string | null>(null); // 펼친 프로필 id. 한 번에 하나만 펼친다
    const setPath = (id: string, path: string) => updateSettings((next) => {
        if (path.trim()) {
            next.agent.paths[id] = path.trim();
        } else {
            delete next.agent.paths[id];
        }
        return next;
    });
    const browse = async (id: string) => {
        const chosen = await openDialog({ multiple: false, directory: false });
        if (typeof chosen === 'string') {
            setPath(id, chosen);
        }
    };
    const changeProfile = (profile: AgentProfile) => updateSettings((next) => {
        next.agent.profiles = next.agent.profiles.map((each) => each.id === profile.id ? profile : each);
        return next;
    });
    const removeProfile = (id: string) => updateSettings((next) => {
        next.agent.profiles = next.agent.profiles.filter((each) => each.id !== id);
        return next;
    });
    // 새 프로필은 펼쳐서 바로 고치게 한다
    const addProfile = () => {
        const id = crypto.randomUUID();
        updateSettings((next) => {
            next.agent.profiles.push({ id, name: '', agent: 'claude-code', provider: 'anthropic', model: '', host: '' });
            return next;
        });
        setOpen(id);
    };
    // 기본 프로필 두 개를 처음 값으로 되돌리고(지웠으면 다시 만든다) 맨 위에 둔다. 직접 만든 프로필은 그대로 둔다
    const restoreProfiles = () => updateSettings((next) => {
        const own = next.agent.profiles.filter((each) => !defaultProfiles.some((preset) => preset.id === each.id));
        next.agent.profiles = [...structuredClone(defaultProfiles), ...own];
        return next;
    });
    const profiles = settings.agent.profiles;
    return (
        <>
            <section>
                <div className="setting-group profile-group">
                    <h2>{t('프로필')}</h2>
                    <button className="plain-button" onClick={addProfile}><Plus size={14} /> {t('추가')}</button>
                    <button className="icon-only" title={t('기본 프로필 복원')} onClick={restoreProfiles}><RotateCcw size={14} /></button>
                </div>
                {profiles.map((each) => (
                    <ProfileItem key={each.id} profile={each} open={open === each.id} isDefault={each.id === settings.agent.default} deletable={profiles.length > 1}
                        onToggle={() => setOpen(open === each.id ? null : each.id)} onChange={changeProfile} onDelete={() => removeProfile(each.id)}
                        onDefault={() => updateSettings((next) => ({ ...next, agent: { ...next.agent, default: each.id } }))} />
                ))}
            </section>
            <section>
                <h2 className="setting-group">{t('실행 파일')}</h2>
                {agents.map((each) => (
                    <div key={each.id} className="agent-path">
                        <span>{each.label}</span>
                        <AgentPath value={settings.agent.paths[each.id] ?? ''} placeholder={t('PATH에서 {0} 찾기', each.program)} onCommit={(path) => setPath(each.id, path)} />
                        <button className="icon-only" title={t('찾아보기')} onClick={() => browse(each.id)}><FolderOpen size={15} /></button>
                    </div>
                ))}
            </section>
        </>
    );
}

// Ollama에 설치된 모델 중 도구 호출을 지원하는 것. host가 null이면 묻지 않는다.
// 창으로 돌아오면 다시 묻는다 (그사이에 ollama pull로 모델을 받았을 수 있다)
function useOllamaModels(host: string | null) {
    const [state, setState] = useState<{ models: string[] } | { error: string } | null>(null);
    useEffect(() => {
        setState(null);
        if (host === null) {
            return;
        }
        let stopped = false;
        const load = () => {
            invoke<string[]>('ollama_models', { host }).then(
                (models) => !stopped && setState({ models }),
                (error) => !stopped && setState({ error: String(error) }),
            );
        };
        load();
        window.addEventListener('focus', load);
        return () => {
            stopped = true;
            window.removeEventListener('focus', load);
        };
    }, [host]);
    return state;
}

// 프로필 목록의 한 줄. 줄을 누르면 아래에 칸이 펼쳐지고, 펼친 동안 굵은 이름을 눌러 바로 고친다
function ProfileItem({ profile, open, isDefault, deletable, onToggle, onChange, onDelete, onDefault }: {
    profile: AgentProfile; open: boolean; isDefault: boolean; deletable: boolean;
    onToggle(): void; onChange(profile: AgentProfile): void; onDelete(): void; onDefault(): void;
}) {
    const local = isLocalProvider(profile.provider);
    const host = profile.provider === 'ollama' ? ollamaHost(profile) : null;
    const ollama = useOllamaModels(host);
    const suggestions = ollama && 'models' in ollama ? ollama.models : modelSuggestions[profile.provider] ?? [];
    const agentLabel = agentLabels[profile.agent];
    // 제공자를 바꾸면 모델 이름도 맞지 않으므로 비운다. 에이전트를 바꿔도 제공자가 그대로면 모델을 둔다
    const setAgent = (agent: AgentId) => {
        const provider = agentProviders[agent].includes(profile.provider) ? profile.provider : agentProviders[agent][0];
        onChange({ ...profile, agent, provider, model: provider === profile.provider ? profile.model : '' });
    };
    const notes: { text: string; problem?: boolean }[] = [];
    if (!local) {
        notes.push({ text: t('로그인과 요금은 {0} 안에서 본인 계정으로 합니다.', agentLabel) });
    } else {
        notes.push({ text: profile.provider === 'ollama'
            ? t('모델 목록에는 도구 호출(tools)을 지원하는 모델만 표시됩니다. 컨텍스트 길이는 64K 이상을 권장합니다.')
            : t('LM Studio에서 로컬 서버를 켜 두어야 합니다. 컨텍스트 길이는 64K 이상을 권장합니다.') });
        if (ollama && 'error' in ollama) {
            notes.push({ text: t('Ollama({0})에 연결하지 못해 모델 목록을 가져오지 못했습니다. ({1})', host, ollama.error), problem: true });
        } else if (ollama && ollama.models.length === 0) {
            notes.push({ text: t('도구 호출을 지원하는 모델이 설치되어 있지 않습니다. ollama pull로 받아 주세요.'), problem: true });
        }
        if (!profile.model.trim()) {
            notes.push({ text: t('모델을 정해 주세요.'), problem: true });
        }
    }
    const auto = profileLabel({ ...profile, name: '' });
    const problem = notes.some((note) => note.problem);
    // 줄 안의 이름 칸과 버튼을 누른 것은 펼치기로 치지 않는다 (화살표 버튼은 줄로 넘긴다)
    const own = (event: MouseEvent) => event.stopPropagation();
    return (
        <div className={'profile' + (open ? ' open' : '')}>
            <div className="profile-row" onClick={onToggle}>
                <button className="profile-chevron" aria-expanded={open}>{open ? <ChevronDown size={15} /> : <ChevronRight size={15} />}</button>
                {open
                    ? <ProfileName profile={profile} onClick={own} onCommit={(name) => onChange({ ...profile, name })} />
                    : <span className="profile-title">{profileLabel(profile)}{profile.name.trim() && profile.name.trim() !== auto && <span className="profile-summary">{auto}</span>}</span>}
                {problem && <CircleAlert size={14} className="profile-problem" />}
                {isDefault
                    ? <span className="profile-badge" title={t('코드 패널의 AI 탭을 열 때 처음 고르는 프로필입니다.')}>{t('기본')}</span>
                    : <button className="profile-default" title={t('코드 패널의 AI 탭을 열 때 처음 고르는 프로필입니다.')} onClick={(event) => { own(event); onDefault(); }}>{t('기본으로')}</button>}
            </div>
            {open && (
                <div className="profile-body">
                    <div className="profile-fields">
                        <label><span>{t('에이전트')}</span>
                            <select value={profile.agent} onChange={(event) => setAgent(event.target.value as AgentId)}>
                                {agents.map((each) => <option key={each.id} value={each.id}>{each.label}</option>)}
                            </select>
                        </label>
                        {agentProviders[profile.agent].length > 1 && (
                            <label><span>{t('제공자')}</span>
                                <select value={profile.provider} onChange={(event) => onChange({ ...profile, provider: event.target.value as ProviderId, model: '' })}>
                                    {agentProviders[profile.agent].map((each) => <option key={each} value={each}>{providerLabels[each]}</option>)}
                                </select>
                            </label>
                        )}
                        <label className="wide"><span>{t('모델')}</span>
                            <TextField value={profile.model} placeholder={local ? '' : t('{0} 기본 모델', agentLabel)} suggestions={suggestions} onCommit={(model) => onChange({ ...profile, model: model.trim() })} />
                        </label>
                        {profile.provider === 'ollama' && (
                            <label className="wide"><span>{t('Ollama 주소')}</span>
                                <TextField value={profile.host} placeholder={defaultOllamaHost} onCommit={(value) => onChange({ ...profile, host: value.trim() })} />
                            </label>
                        )}
                    </div>
                    {notes.map((note) => <div key={note.text} className={note.problem ? 'setting-problem' : 'setting-hint'}>{note.text}</div>)}
                    <div className="profile-actions">
                        <button className="plain-button danger" disabled={!deletable} title={deletable ? undefined : t('프로필이 하나뿐이라 지울 수 없습니다.')} onClick={onDelete}><Trash2 size={14} /> {t('프로필 지우기')}</button>
                    </div>
                </div>
            )}
        </div>
    );
}

// 프로필의 굵은 제목. 눌러서 바로 이름을 고친다 (Enter, 칸을 벗어나면 저장하고 Esc는 취소).
// 비우거나 조합으로 만든 이름을 적으면 이름을 적지 않은 것으로 보고, 조합이 바뀌면 이름도 따라 바뀐다.
// 글자를 고치지 않고 나가면 그대로 둔다 (기본 프로필의 이름 'Claude Code'는 조합으로 만든 이름과 같다)
function ProfileName({ profile, onClick, onCommit }: { profile: AgentProfile; onClick(event: MouseEvent): void; onCommit(name: string): void }) {
    const auto = profileLabel({ ...profile, name: '' });
    const [draft, setDraft] = useState<string | null>(null); // null이면 고치는 중이 아니다
    const cancelled = useRef(false);
    const commit = () => {
        const untouched = draft === null || cancelled.current || draft.trim() === profileLabel(profile);
        const name = untouched ? profile.name : draft.trim() === auto ? '' : draft.trim();
        cancelled.current = false;
        setDraft(null);
        if (name !== profile.name) {
            onCommit(name);
        }
    };
    return (
        <input className="profile-name" value={draft ?? profileLabel(profile)} placeholder={auto} spellCheck={false} title={t('눌러서 이름 고치기')}
            onFocus={() => setDraft(profileLabel(profile))} onChange={(event) => setDraft(event.target.value)} onBlur={commit} onClick={onClick}
            onKeyDown={(event) => {
                if (event.key === 'Enter') {
                    event.currentTarget.blur();
                } else if (event.key === 'Escape') {
                    event.preventDefault(); // 설정 창이 닫히지 않게
                    cancelled.current = true;
                    event.currentTarget.blur();
                }
            }} />
    );
}

// 입력을 마치면(Enter, 칸을 벗어남, 추천 목록에서 고름) 저장한다
function TextField({ value, placeholder, suggestions, onCommit }: { value: string; placeholder?: string; suggestions?: string[]; onCommit(value: string): void }) {
    const [draft, setDraft] = useState(value);
    const list = useId();
    useEffect(() => setDraft(value), [value]);
    const commit = (next: string) => next !== value && onCommit(next);
    return (
        <>
            <input className="text-input" value={draft} placeholder={placeholder} spellCheck={false} list={suggestions?.length ? list : undefined}
                onChange={(event) => {
                    setDraft(event.target.value);
                    if ((event.nativeEvent as InputEvent).inputType === 'insertReplacementText') {
                        commit(event.target.value);
                    }
                }}
                onBlur={() => commit(draft)} onKeyDown={(event) => event.key === 'Enter' && commit(draft)} />
            {!!suggestions?.length && <datalist id={list}>{suggestions.map((each) => <option key={each} value={each} />)}</datalist>}
        </>
    );
}

// 입력을 마치면(Enter, 칸을 벗어남) 저장한다
function AgentPath({ value, placeholder, onCommit }: { value: string; placeholder: string; onCommit(path: string): void }) {
    const [draft, setDraft] = useState(value);
    useEffect(() => setDraft(value), [value]);
    return (
        <input className="path-input" value={draft} placeholder={placeholder} spellCheck={false} onChange={(event) => setDraft(event.target.value)}
            onBlur={() => draft.trim() !== value && onCommit(draft)} onKeyDown={(event) => event.key === 'Enter' && onCommit(draft)} />
    );
}

function ShowPage() {
    const settings = useSettings();
    return (
        <Row title={t('종료 화면 보이기')} hint={t('마지막 슬라이드에서 한 번 더 넘기면 쇼가 끝났다는 검은 화면을 보여 주고, 한 번 더 넘기면 닫습니다. 끄면 바로 닫습니다.')}>
            <Switch checked={settings.show.endScreen} onChange={(endScreen) => updateSettings((next) => ({ ...next, show: { ...next.show, endScreen } }))} />
        </Row>
    );
}

function AboutPage() {
    const [version, setVersion] = useState('');
    useEffect(() => {
        getVersion().then(setVersion);
    }, []);
    // 이미 열려 있으면 그 창을 앞으로 가져온다
    const openLicenses = async () => {
        const existing = await WebviewWindow.getByLabel('licenses');
        if (existing) {
            await existing.setFocus();
            return;
        }
        new WebviewWindow('licenses', { url: 'licenses.html', title: t('오픈소스 라이선스'), width: 760, height: 640, center: true, focus: true });
    };
    // 파일이 아직 없으면 지금 설정으로 만든 뒤 탐색기에서 보여 준다
    const showFile = async () => {
        const path = await invoke<string | null>('settings_path');
        if (path) {
            if (!await invoke<boolean>('path_exists', { path })) {
                await invoke('settings_save', { settings: await invoke('settings_load') });
            }
            await invoke('open_path', { path, reveal: true });
        }
    };
    const reset = async () => {
        if (!await ask(t('모든 설정을 처음 값으로 되돌릴까요? 컴파일러 경로도 기본값으로 돌아갑니다.'), { title: t('설정 초기화'), kind: 'warning', okLabel: t('초기화'), cancelLabel: t('취소') })) {
            return;
        }
        resetSettings();
        const info = await invoke<CompilerInfo>('compiler_info');
        if (info.setting) {
            await invoke('compiler_use', { path: null }).catch((error) => console.error('compiler_use', error));
        }
    };
    return (
        <>
            <div className="about">
                <img src={logo} alt="templide" width={64} height={64} />
                <div>
                    <div className="about-name">templide {version}</div>
                    <div className="setting-hint">Copyright (c) 2026 typeulli</div>
                </div>
            </div>
            <div className="setting-actions">
                <button className="plain-button" onClick={openLicenses}>{t('오픈소스 라이선스')}</button>
                <button className="plain-button" onClick={showFile}><FolderOpen size={14} /> {t('설정 파일 위치 열기')}</button>
                <button className="plain-button danger" onClick={reset}><RotateCcw size={13} /> {t('모든 설정 초기화')}</button>
            </div>
            <details className="license">
                <summary>{t('templide 라이선스')}</summary>
                <pre>{license}</pre>
            </details>
        </>
    );
}

function SettingsApp() {
    useLang();
    const settings = useSettings();
    const [page, setPage] = useState<Page>('general');
    const pages: { id: Page; icon: LucideIcon; title: string }[] = [
        { id: 'general', icon: Globe, title: t('일반') },
        { id: 'editor', icon: Code, title: t('코드 편집기') },
        { id: 'format', icon: WandSparkles, title: t('포맷') },
        { id: 'compiler', icon: Cpu, title: t('컴파일러') },
        { id: 'agent', icon: Bot, title: t('AI 에이전트') },
        { id: 'show', icon: Presentation, title: t('슬라이드 쇼') },
        { id: 'shortcuts', icon: Keyboard, title: t('단축키') },
        { id: 'about', icon: Info, title: t('정보') },
    ];
    useEffect(() => {
        document.title = t('설정');
        getCurrentWindow().setTitle(t('설정'));
    }, [settings.language]);
    useEffect(() => {
        const key = (event: KeyboardEvent) => {
            if (event.key === 'Escape' && !event.defaultPrevented) {
                getCurrentWindow().close();
            }
        };
        window.addEventListener('keydown', key);
        return () => window.removeEventListener('keydown', key);
    }, []);
    return (
        <div className="settings">
            <nav className="settings-nav">
                {pages.map(({ id, icon: Icon, title }) => (
                    <button key={id} className={page === id ? 'active' : ''} onClick={() => setPage(id)}><Icon size={16} strokeWidth={1.8} /> {title}</button>
                ))}
            </nav>
            <main className="settings-body">
                <h1>{pages.find((each) => each.id === page)!.title}</h1>
                {page === 'general' && <GeneralPage />}
                {page === 'editor' && <EditorPage />}
                {page === 'format' && <FormatPage />}
                {page === 'compiler' && <CompilerPage />}
                {page === 'agent' && <AgentPage />}
                {page === 'show' && <ShowPage />}
                {page === 'shortcuts' && <ShortcutSettings />}
                {page === 'about' && <AboutPage />}
            </main>
        </div>
    );
}

initSettings().catch((error) => console.error('settings', error)).finally(() => {
    createRoot(document.getElementById('root')!).render(<SettingsApp />);
    // 창은 숨긴 채로 만든다(settingsWindow.ts). 화면을 한 번 그린 뒤에 보여 준다
    requestAnimationFrame(() => requestAnimationFrame(() => getCurrentWindow().show()));
});

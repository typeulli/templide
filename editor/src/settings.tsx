// 설정 창. 로고를 누르면 열린다. 바꾼 값은 곧바로 적용하고 저장한다 (settings.ts).
// 컴파일러 경로는 Rust(compiler_use)가 컴파일러를 다시 띄우고 저장한다
import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { getVersion } from '@tauri-apps/api/app';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { ask, open as openDialog } from '@tauri-apps/plugin-dialog';
import { Bot, CircleAlert, CircleCheck, Code, Cpu, FolderOpen, Globe, Info, Keyboard, LoaderCircle, Presentation, RotateCcw, type LucideIcon } from 'lucide-react';
import logo from '../../assets/icon/templide.svg';
import license from '../../LICENSE?raw';
import { agents } from './Agents';
import { languages, t, useLang, type LanguageSetting } from './i18n';
import { editorLimits, initSettings, resetSettings, updateSettings, useSettings } from './settings';
import { ShortcutSettings } from './ShortcutSettings';
import './styles.css';
import './settings.css';

type Page = 'general' | 'editor' | 'compiler' | 'agent' | 'show' | 'shortcuts' | 'about';

const isWindows = navigator.userAgent.includes('Windows');

// stack이면 설명 아래에 입력칸을 넓게 둔다 (경로)
function Row({ title, hint, stack, children }: { title: string; hint?: string; stack?: boolean; children: ReactNode }) {
    return (
        <div className={'setting-row' + (stack ? ' stack' : '')}>
            <div className="setting-text">
                <div className="setting-title">{title}</div>
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
            <Row stack title={t('컴파일러 경로')} hint={t('비워 두면 편집기와 함께 설치된 컴파일러를 씁니다. 컴파일러는 자기 옆의 packages, libs 폴더를 함께 씁니다.')}>
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
    return (
        <>
            <Row title={t('기본 에이전트')} hint={t('코드 패널의 AI 탭을 열 때 처음 고르는 에이전트입니다.')}>
                <select value={settings.agent.default} onChange={(event) => updateSettings((next) => ({ ...next, agent: { ...next.agent, default: event.target.value } }))}>
                    {agents.map((each) => <option key={each.id} value={each.id}>{each.label}</option>)}
                </select>
            </Row>
            {agents.map((each) => (
                <Row key={each.id} stack title={t('{0} 실행 파일', each.label)} hint={t('비워 두면 PATH에서 {0}을(를) 찾습니다.', each.program)}>
                    <div className="path-edit">
                        <AgentPath value={settings.agent.paths[each.id] ?? ''} onCommit={(path) => setPath(each.id, path)} />
                        <button className="plain-button" onClick={() => browse(each.id)}><FolderOpen size={14} /> {t('찾아보기')}</button>
                    </div>
                </Row>
            ))}
        </>
    );
}

// 입력을 마치면(Enter, 칸을 벗어남) 저장한다
function AgentPath({ value, onCommit }: { value: string; onCommit(path: string): void }) {
    const [draft, setDraft] = useState(value);
    useEffect(() => setDraft(value), [value]);
    return (
        <input className="path-input" value={draft} spellCheck={false} onChange={(event) => setDraft(event.target.value)}
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

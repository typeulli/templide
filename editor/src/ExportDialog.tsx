// 내보내기 창. 문서의 target들을 체크박스로 고르고, 고른 것만 차례로 만든다. 모두 만들면 onDone을 부르고(창을 닫는다),
// 하나라도 실패하면 오류를 볼 수 있게 열어 둔다.
// 편집기에서는 내보내기 버튼으로 띄우고, 탐색기의 'templide 내보내기'(export.tsx)에서는 창 전체로 쓴다
import { useEffect, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { CircleAlert, CircleCheck, FileOutput, LoaderCircle, TriangleAlert, X } from 'lucide-react';
import './export.css';
import { t, tn, useLang } from './i18n';

export type ExportTarget = { name: string; type: string; path: string };
export type BuildResult = { path?: string; errors?: string[]; error?: string; warnings?: string[] };

type Status = { state: 'building' } | { state: 'done'; result: BuildResult };

// 경고는 "파일:줄:칸: warning: 내용"이다. "줄 N: 내용"으로 보인다
const warningText = (line: string) => {
    const match = /^.*:(\d+):\d+: warning: (.*)$/.exec(line);
    return match ? t('줄 {0}: {1}', match[1], match[2]) : line;
};

export function ExportDialog({ fileName, load, build, onClose, onDone, standalone = false }: {
    fileName: string;
    load(): Promise<{ targets?: ExportTarget[]; error?: string }>;
    build(target: string): Promise<BuildResult>;
    onClose(): void;
    onDone(results: BuildResult[]): void; // 고른 target을 모두 만들었을 때
    standalone?: boolean; // 창 전체를 쓰는 내보내기 창 (탐색기에서 연 것)
}) {
    useLang(); // 언어를 바꾸면 다시 그린다
    const [targets, setTargets] = useState<ExportTarget[] | null>(null);
    const [loadError, setLoadError] = useState<string | null>(null);
    const [checked, setChecked] = useState<Set<string>>(new Set());
    const [status, setStatus] = useState<Record<string, Status>>({});
    const [running, setRunning] = useState(false);

    useEffect(() => {
        load().then((result) => {
            if (result.error || !result.targets) {
                setLoadError(result.error ?? t('target을 읽을 수 없습니다'));
                return;
            }
            setTargets(result.targets);
            setChecked(new Set(result.targets.map((target) => target.name)));
        }).catch((error) => setLoadError(String(error)));
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    useEffect(() => {
        const key = (event: KeyboardEvent) => {
            if (event.key === 'Escape' && !running) {
                onClose();
            }
        };
        window.addEventListener('keydown', key);
        return () => window.removeEventListener('keydown', key);
    }, [onClose, running]);

    const toggle = (name: string) => setChecked((old) => {
        const next = new Set(old);
        if (!next.delete(name)) {
            next.add(name);
        }
        return next;
    });
    const all = !!targets && targets.length > 0 && checked.size === targets.length;

    // 고른 target을 하나씩 만든다. 하나가 실패해도 나머지는 계속한다
    const run = async () => {
        if (!targets) {
            return;
        }
        setRunning(true);
        setStatus({});
        const results: BuildResult[] = [];
        for (const target of targets.filter((each) => checked.has(each.name))) {
            setStatus((old) => ({ ...old, [target.name]: { state: 'building' } }));
            const result = await build(target.name).catch((error) => ({ error: String(error) }) as BuildResult);
            results.push(result);
            setStatus((old) => ({ ...old, [target.name]: { state: 'done', result } }));
        }
        setRunning(false);
        if (results.every((result) => !result.error && !result.errors?.length)) {
            onDone(results);
        }
    };

    const body = (
        <div className={'export' + (standalone ? ' standalone' : '')} onMouseDown={(event) => event.stopPropagation()}>
            <header className="export-header">
                <FileOutput size={16} />
                <span className="export-title">{t('내보내기')}</span>
                <span className="export-file" title={fileName}>{fileName}</span>
                {!standalone && <button className="export-close" title={t('닫기 (Esc)')} disabled={running} onClick={onClose}><X size={16} /></button>}
            </header>
            <div className="export-body">
                {loadError && <div className="export-message error"><CircleAlert size={14} /> {loadError === 'The document has errors' ? t('코드에 오류가 있어 내보낼 수 없습니다') : loadError}</div>}
                {!loadError && !targets && <div className="export-message"><LoaderCircle size={14} className="spin" /> {t('target을 읽는 중')}</div>}
                {targets && targets.length === 0 && (
                    <div className="export-message">{tn('target이 없습니다. {0}처럼 추가해 주세요', <code>target out {'{'} path = "out.pptx"; type = pptx; {'}'}</code>)}</div>
                )}
                {targets && targets.length > 0 && (
                    <>
                        <label className="export-all">
                            <input type="checkbox" checked={all} disabled={running}
                                onChange={() => setChecked(all ? new Set() : new Set(targets.map((target) => target.name)))} />
                            {t('모두 선택')}
                        </label>
                        <div className="export-list">
                            {targets.map((target) => {
                                const current = status[target.name];
                                const result = current?.state === 'done' ? current.result : null;
                                const failed = !!result && (!!result.error || !!result.errors?.length);
                                return (
                                    <div key={target.name} className="export-item">
                                        <label className="export-row">
                                            <input type="checkbox" checked={checked.has(target.name)} disabled={running} onChange={() => toggle(target.name)} />
                                            <span className="export-name">{target.name}</span>
                                            <span className="export-type">{target.type}</span>
                                            <span className="export-path" title={target.path}>{target.path}</span>
                                            {current?.state === 'building' && <LoaderCircle size={14} className="spin" />}
                                            {result && !failed && <CircleCheck size={14} className="ok" />}
                                            {failed && <CircleAlert size={14} className="fail" />}
                                        </label>
                                        {result && !failed && result.path && (
                                            <div className="export-actions">
                                                <button onClick={() => invoke('open_path', { path: result.path, reveal: false })}>{t('열기')}</button>
                                                <button onClick={() => invoke('open_path', { path: result.path, reveal: true })}>{t('폴더에서 보기')}</button>
                                            </div>
                                        )}
                                        {failed && <div className="export-error">{result!.error ?? result!.errors!.join('\n')}</div>}
                                        {result?.warnings?.map((warning, i) => (
                                            <div key={i} className="export-warning"><TriangleAlert size={12} /> {warningText(warning)}</div>
                                        ))}
                                    </div>
                                );
                            })}
                        </div>
                    </>
                )}
            </div>
            <footer className="export-footer">
                <button className="export-secondary" disabled={running} onClick={onClose}>{t('닫기')}</button>
                <button className="export-primary" disabled={running || !targets || checked.size === 0} onClick={run}>
                    {running ? t('만드는 중…') : t('내보내기 ({0})', checked.size)}
                </button>
            </footer>
        </div>
    );
    return standalone ? body : <div className="export-overlay" onMouseDown={() => !running && onClose()}>{body}</div>;
}

// .tasset 묶음 탭. 안의 그림, 비디오, 오디오를 보고, 파일을 넣고, 이름을 바꾸고, 지운다. 바꾼 것은 바로 묶음 파일에 쓴다 (되돌릴 수 없다).
// 이름을 바꾸거나 지울 때는 열린 .tlide 탭에서 그 파일을 쓰는 곳을 컴파일러(templide/asset_uses)에 물어 보여 준다
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import { convertFileSrc } from '@tauri-apps/api/core';
import { ask, open as openDialog } from '@tauri-apps/plugin-dialog';
import { CircleAlert, File, Film, FolderOpen, Image as ImageIcon, LoaderCircle, Music, Package, PenLine, Plus, Trash2, X } from 'lucide-react';
import { IconButton } from './Panels';
import { lsp, type Range } from './lsp';
import type { DocumentHandle } from './App';
import { t, useLang } from './i18n';

// 열린 .tlide 탭
export type OpenDocument = { tab: string; name: string; handle: DocumentHandle };

type Entry = { name: string; size: number };
// 열린 탭에서 묶음 안의 파일을 쓰는 곳. newText는 이름을 바꿀 때 range에 넣을 글자
type Use = { tab: string; file: string; range: Range; preview: string; newText?: string };

type Kind = 'image' | 'video' | 'audio' | 'other';
const kindOf = (name: string): Kind => {
    const extension = name.split('.').pop()!.toLowerCase();
    return ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg'].includes(extension) ? 'image'
        : ['mp4', 'webm', 'mov'].includes(extension) ? 'video'
        : ['mp3', 'wav', 'm4a', 'ogg'].includes(extension) ? 'audio' : 'other';
};
const kindNames = (): Record<Kind, string> => ({ image: t('그림'), video: t('비디오'), audio: t('오디오'), other: t('파일') });
const kindIcons = { image: ImageIcon, video: Film, audio: Music, other: File };

const sizeText = (bytes: number) => bytes < 1024 ? `${bytes} B` : bytes < 1024 * 1024 ? `${(bytes / 1024).toFixed(1)} KB` : `${(bytes / 1024 / 1024).toFixed(1)} MB`;

export function AssetView({ path, active, documents, onReveal, onChanged, onPick }: {
    path: string;
    active: boolean;
    documents(): OpenDocument[];
    onReveal(tab: string, range: Range): void; // 그 탭의 코드에서 range를 보여 준다
    onChanged(): void;                          // 묶음이 바뀌었다 (열린 문서를 다시 컴파일한다)
    onPick(): void;                             // 열 파일을 고른다
}) {
    useLang(); // 언어를 바꾸면 다시 그린다
    const [entries, setEntries] = useState<Entry[] | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [selected, setSelected] = useState<string | null>(null);
    const [previews, setPreviews] = useState<Record<string, string>>({}); // 파일 이름 -> 풀어 둔 파일의 주소
    const [renaming, setRenaming] = useState<string | null>(null); // 이름 칸에 적는 중인 새 이름
    const [busy, setBusy] = useState(false);
    const [notice, setNotice] = useState<string | null>(null);
    const [refactor, setRefactor] = useState<{ entry: string; to: string; uses: Use[]; checked: Set<number> } | null>(null);
    const [removal, setRemoval] = useState<{ entry: string; uses: Use[] } | null>(null);
    const requested = useRef(new Set<string>()); // 풀어 달라고 이미 부탁한 파일
    const fileName = path.split(/[\\/]/).pop()!;

    const load = useCallback(async () => {
        const result = await lsp.request<{ entries?: Entry[]; error?: string }>('templide/asset_list', { bundle: path });
        if (!result.entries) {
            setError(result.error ?? t('묶음을 열 수 없습니다'));
            setEntries(null);
            return;
        }
        setError(null);
        setEntries(result.entries);
        setSelected((current) => current && result.entries!.some((entry) => entry.name === current) ? current : null);
    }, [path]);

    // 보일 때마다 다시 읽는다. 다른 탭에서 그림을 넣으면 묶음이 바뀐다
    useEffect(() => {
        if (active) {
            load();
        }
    }, [active, load]);

    // 그림은 목록에서도 보이도록, 고른 파일은 미리 보기에 쓰도록 임시 폴더에 풀어 둔다. 묶음이 바뀌면 다시 푼다
    useEffect(() => {
        requested.current.clear();
        setPreviews({});
    }, [entries]);
    useEffect(() => {
        const wanted = (entries ?? []).filter((entry) => kindOf(entry.name) === 'image' || entry.name === selected).map((entry) => entry.name);
        for (const name of wanted) {
            if (requested.current.has(name)) {
                continue;
            }
            requested.current.add(name);
            lsp.request<{ path?: string }>('templide/asset_extract', { bundle: path, entry: name }).then((result) => {
                if (result.path) {
                    setPreviews((old) => ({ ...old, [name]: convertFileSrc(result.path!) }));
                }
            });
        }
    }, [entries, selected, path]);

    const showNotice = (text: string) => {
        setNotice(text);
        window.setTimeout(() => setNotice((current) => current === text ? null : current), 6000);
    };

    // 열린 .tlide 탭에서 entry를 쓰는 곳. to가 있으면 그 이름으로 바꾼 글자도 받는다
    const usesOf = async (entry: string, to?: string): Promise<Use[]> => {
        const found: Use[] = [];
        for (const document of documents()) {
            document.handle.flush();
            const uri = document.handle.uri();
            if (!uri) {
                continue;
            }
            const result = await lsp.request<{ uses: Omit<Use, 'tab' | 'file'>[] }>('templide/asset_uses', { uri, bundle: path, entry, ...(to !== undefined ? { to } : {}) });
            found.push(...result.uses.map((use) => ({ ...use, tab: document.tab, file: document.name })));
        }
        return found;
    };

    const add = async () => {
        const files = await openDialog({
            multiple: true,
            filters: [{ name: t('그림, 비디오, 오디오'), extensions: ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg', 'mp4', 'webm', 'mp3', 'wav', 'm4a'] }, { name: t('모든 파일'), extensions: ['*'] }],
        });
        const list = Array.isArray(files) ? files : typeof files === 'string' ? [files] : [];
        if (list.length === 0) {
            return;
        }
        setBusy(true);
        let last: string | null = null;
        for (const source of list) {
            const added = await lsp.request<{ name?: string; error?: string }>('templide/asset_add', { bundle: path, source });
            if (added.name) {
                last = added.name;
            } else {
                showNotice(added.error ?? t('{0}을(를) 넣을 수 없습니다', source));
            }
        }
        setBusy(false);
        await load();
        if (last) {
            setSelected(last);
            onChanged();
        }
    };

    // 이름을 바꾸고, 고른 자리의 코드를 새 이름으로 고친다 (저장은 각 탭에서)
    const rename = async (entry: string, to: string, uses: Use[]) => {
        setBusy(true);
        const result = await lsp.request<{ error?: string }>('templide/asset_rename', { bundle: path, entry, to });
        setBusy(false);
        if (result.error) {
            showNotice(result.error);
            return;
        }
        const failed: string[] = [];
        for (const document of documents()) {
            const edits = uses.filter((use) => use.tab === document.tab && use.newText !== undefined).map((use) => ({ range: use.range, newText: use.newText! }));
            if (edits.length > 0 && !document.handle.edit(edits)) {
                failed.push(document.name);
            }
        }
        if (failed.length > 0) {
            showNotice(t('코드를 고치지 못한 탭이 있습니다: {0}', failed.join(', ')));
        }
        await load();
        setSelected(to);
        onChanged();
    };

    const startRename = () => {
        if (selected) {
            setRenaming(selected);
        }
    };

    const submitRename = async () => {
        const entry = selected;
        const to = (renaming ?? '').trim().replace(/\\/g, '/').replace(/^\/+/, '');
        setRenaming(null);
        if (!entry || !to || to === entry) {
            return;
        }
        if (entries?.some((each) => each.name !== entry && each.name.toLowerCase() === to.toLowerCase())) {
            showNotice(t('{0}은(는) 이미 묶음에 있습니다', to));
            return;
        }
        const uses = await usesOf(entry, to);
        if (uses.length === 0) {
            await rename(entry, to, []);
        } else {
            setRefactor({ entry, to, uses, checked: new Set(uses.map((_, i) => i)) });
        }
    };

    const remove = async (entry: string) => {
        setBusy(true);
        const result = await lsp.request<{ error?: string }>('templide/asset_remove', { bundle: path, entry });
        setBusy(false);
        if (result.error) {
            showNotice(result.error);
            return;
        }
        await load();
        setSelected(null);
        onChanged();
    };

    const startRemove = async () => {
        const entry = selected;
        if (!entry) {
            return;
        }
        const uses = await usesOf(entry);
        if (uses.length > 0) {
            setRemoval({ entry, uses });
        } else if (await ask(t('{0}을(를) 묶음에서 지울까요? 되돌릴 수 없습니다.', entry), { title: 'templide', kind: 'warning', okLabel: t('지우기'), cancelLabel: t('취소') })) {
            await remove(entry);
        }
    };

    // F2 이름 바꾸기, Delete 지우기 (이 탭이 보일 때, 입력 칸 밖에서)
    const keys = useRef({ startRename, startRemove });
    keys.current = { startRename, startRemove };
    useEffect(() => {
        if (!active) {
            return;
        }
        const keydown = (event: KeyboardEvent) => {
            if ((event.target as HTMLElement).closest('input, textarea, select, video, audio') || refactor || removal) {
                return;
            }
            if (event.key === 'F2') {
                event.preventDefault();
                keys.current.startRename();
            } else if (event.key === 'Delete') {
                event.preventDefault();
                keys.current.startRemove();
            }
        };
        window.addEventListener('keydown', keydown);
        return () => window.removeEventListener('keydown', keydown);
    }, [active, refactor, removal]);

    const current = entries?.find((entry) => entry.name === selected) ?? null;
    const total = (entries ?? []).reduce((sum, entry) => sum + entry.size, 0);

    return (
        <div className="asset">
            <header className="toolbar asset-toolbar" data-tauri-drag-region>
                <span className="tool-group">
                    <IconButton icon={FolderOpen} title={t('열기')} onClick={onPick} />
                </span>
                <span className="file" title={path}><Package size={15} className="asset-file-icon" /> {fileName}</span>
                {entries && <span className="muted asset-summary">{t('파일 {0}개 · {1}', entries.length, sizeText(total))}</span>}
                <span className="separator" />
                <span className="tool-group">
                    <IconButton icon={Plus} title={t('파일 넣기')} disabled={!!error || busy} onClick={add} />
                    <IconButton icon={PenLine} title={t('이름 바꾸기 (F2)')} disabled={!current || busy} onClick={startRename} />
                    <IconButton icon={Trash2} title={t('지우기 (Delete)')} disabled={!current || busy} onClick={startRemove} danger />
                </span>
                {busy && <LoaderCircle size={15} className="spin" />}
                <span className="spacer" data-tauri-drag-region />
            </header>
            <div className="asset-body">
                <div className="asset-grid" onMouseDown={(event) => event.target === event.currentTarget && setSelected(null)}>
                    {error && <div className="asset-message error"><CircleAlert size={14} /> {error}</div>}
                    {entries && entries.length === 0 && <div className="asset-message">{t('묶음이 비어 있습니다. 파일 넣기(+)로 그림, 비디오, 오디오를 넣으세요')}</div>}
                    {entries?.map((entry) => {
                        const kind = kindOf(entry.name);
                        const Icon = kindIcons[kind];
                        return (
                            <button key={entry.name} className={'asset-card' + (entry.name === selected ? ' selected' : '')} title={entry.name}
                                onClick={() => { setRenaming(null); setSelected(entry.name); }} onDoubleClick={() => setRenaming(entry.name)}>
                                <span className="asset-thumb">
                                    {kind === 'image' && previews[entry.name] ? <img src={previews[entry.name]} alt="" draggable={false} /> : <Icon size={28} />}
                                </span>
                                <span className="asset-name">{entry.name}</span>
                                <span className="asset-size">{sizeText(entry.size)}</span>
                            </button>
                        );
                    })}
                </div>
                <aside className="asset-detail">
                    {current ? (
                        <>
                            <div className="asset-preview">
                                {!previews[current.name] && kindOf(current.name) !== 'other' && <LoaderCircle size={18} className="spin" />}
                                {previews[current.name] && kindOf(current.name) === 'image' && <img src={previews[current.name]} alt="" draggable={false} />}
                                {previews[current.name] && kindOf(current.name) === 'video' && <video key={previews[current.name]} src={previews[current.name]} controls />}
                                {previews[current.name] && kindOf(current.name) === 'audio' && <audio key={previews[current.name]} src={previews[current.name]} controls />}
                                {kindOf(current.name) === 'other' && <File size={40} className="muted" />}
                            </div>
                            {renaming !== null ? (
                                <input className="asset-rename" autoFocus value={renaming} onChange={(event) => setRenaming(event.target.value)}
                                    onFocus={(event) => {
                                        // 확장자 앞까지만 고른다
                                        const dot = event.target.value.lastIndexOf('.');
                                        event.target.setSelectionRange(0, dot > 0 ? dot : event.target.value.length);
                                    }}
                                    onKeyDown={(event) => {
                                        if (event.key === 'Enter') {
                                            submitRename();
                                        } else if (event.key === 'Escape') {
                                            setRenaming(null);
                                        }
                                    }}
                                    onBlur={() => setRenaming(null)} />
                            ) : (
                                <button className="asset-detail-name" title={t('이름 바꾸기 (F2)')} onClick={startRename}>{current.name}</button>
                            )}
                            <div className="asset-detail-info muted">{kindNames()[kindOf(current.name)]} · {sizeText(current.size)}</div>
                        </>
                    ) : <div className="props-empty">{t('파일을 고르면')}<br />{t('여기서 미리 봅니다')}</div>}
                </aside>
            </div>
            {notice && (
                <div className="toast error asset-toast">
                    <span className="toast-message">{notice}</span>
                    <IconButton icon={X} title={t('닫기')} onClick={() => setNotice(null)} />
                </div>
            )}
            {refactor && (
                <div className="export-overlay" onMouseDown={() => setRefactor(null)}>
                    <div className="export refactor" onMouseDown={(event) => event.stopPropagation()}>
                        <header className="export-header">
                            <PenLine size={16} />
                            <span className="export-title">{t('이름 바꾸기')}</span>
                            <span className="export-file">{refactor.entry} → {refactor.to}</span>
                        </header>
                        <div className="export-body">
                            <p className="refactor-text">{t('열린 탭에서 이 파일을 쓰는 곳이 {0}개 있습니다. 새 이름으로 고칠 곳을 고르세요. 고친 탭은 저장해야 파일에 남습니다.', refactor.uses.length)}</p>
                            <UseList uses={refactor.uses} render={(use, i) => (
                                <label key={i} className="refactor-row">
                                    <input type="checkbox" checked={refactor.checked.has(i)} onChange={() => setRefactor((old) => {
                                        const checked = new Set(old!.checked);
                                        if (!checked.delete(i)) {
                                            checked.add(i);
                                        }
                                        return { ...old!, checked };
                                    })} />
                                    <span className="refactor-line">{t('줄 {0}', use.range.start.line + 1)}</span>
                                    <Preview use={use} />
                                </label>
                            )} />
                        </div>
                        <footer className="export-footer">
                            <button className="export-secondary" onClick={() => setRefactor(null)}>{t('취소')}</button>
                            <button className="export-primary" onClick={() => {
                                const { entry, to, uses, checked } = refactor;
                                setRefactor(null);
                                rename(entry, to, uses.filter((_, i) => checked.has(i)));
                            }}>{t('이름 바꾸기 (코드 {0}곳 고침)', refactor.checked.size)}</button>
                        </footer>
                    </div>
                </div>
            )}
            {removal && (
                <div className="export-overlay" onMouseDown={() => setRemoval(null)}>
                    <div className="export refactor" onMouseDown={(event) => event.stopPropagation()}>
                        <header className="export-header">
                            <Trash2 size={16} />
                            <span className="export-title">{t('지우기')}</span>
                            <span className="export-file">{removal.entry}</span>
                        </header>
                        <div className="export-body">
                            <p className="refactor-text">{t('열린 탭에서 이 파일을 쓰는 곳이 {0}개 있습니다. 지우면 이 코드들은 오류가 납니다. 누르면 그 자리로 갑니다.', removal.uses.length)}</p>
                            <UseList uses={removal.uses} render={(use, i) => (
                                <button key={i} className="refactor-row link" onClick={() => {
                                    setRemoval(null);
                                    onReveal(use.tab, use.range);
                                }}>
                                    <span className="refactor-line">{t('줄 {0}', use.range.start.line + 1)}</span>
                                    <Preview use={use} />
                                </button>
                            )} />
                        </div>
                        <footer className="export-footer">
                            <button className="export-secondary" onClick={() => setRemoval(null)}>{t('취소')}</button>
                            <button className="export-primary danger" onClick={() => {
                                const entry = removal.entry;
                                setRemoval(null);
                                remove(entry);
                            }}>{t('그래도 지우기')}</button>
                        </footer>
                    </div>
                </div>
            )}
        </div>
    );
}

// 쓰는 곳을 탭(파일)마다 묶어 보여 준다
function UseList({ uses, render }: { uses: Use[]; render(use: Use, index: number): ReactNode }) {
    const files = [...new Set(uses.map((use) => use.tab))];
    return (
        <div className="refactor-list">
            {files.map((tab) => (
                <div key={tab} className="refactor-file">
                    <div className="refactor-file-name">{uses.find((use) => use.tab === tab)!.file}</div>
                    {uses.map((use, i) => use.tab === tab ? render(use, i) : null)}
                </div>
            ))}
        </div>
    );
}

// 그 줄의 코드. 바뀌는 부분은 지운 줄과 새 글자로 보인다
function Preview({ use }: { use: Use }) {
    const { start, end } = use.range;
    const stop = end.line === start.line ? end.character : use.preview.length;
    return (
        <code className="refactor-preview">
            {use.preview.slice(0, start.character).trimStart()}
            <span className={use.newText !== undefined ? 'refactor-old' : 'refactor-hit'}>{use.preview.slice(start.character, stop)}</span>
            {use.newText !== undefined && <span className="refactor-new">{use.newText}</span>}
            {use.preview.slice(stop)}
        </code>
    );
}

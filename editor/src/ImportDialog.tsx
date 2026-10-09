// pptx 불러오기 창. pptx마다 슬라이드 마스터와 슬라이드의 요소를 트리로 보여 주고, 고른 것만 .tlide(그림과 미디어는 .tasset)로 바꾼다.
// pptx를 읽고 바꾸는 일은 컴파일러가 한다 (templide/pptx_tree, templide/pptx_import). 탐색기에서 연 창(import.tsx) 전체를 쓴다
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { getCurrentWebview } from '@tauri-apps/api/webview';
import { save } from '@tauri-apps/plugin-dialog';
import {
    ArrowRightLeft, Boxes, Brush, ChartColumn, ChevronDown, ChevronRight, CircleAlert, CircleCheck, ExternalLink, FileInput, FileText,
    Film, FolderOpen, Image, LayoutTemplate, Layers, LoaderCircle, MessageSquare, Minus, Network, Package, PaintBucket, Palette, PenTool,
    Presentation, Shapes, Sigma, Box, Sparkles, Square, SquareDashed, StickyNote, Table, TriangleAlert, Type, Volume2, Waypoints, X,
    type LucideIcon,
} from 'lucide-react';
import { animationCategories, effectNames, label, objectNames, optionNames, startNames } from './labels';
import './import.css';
import { t, useLang } from './i18n';

// templide/pptx_tree가 주는 트리의 노드. server/server.h 참고
type ImportNode = {
    id: string;
    kind: 'file' | 'masters' | 'master' | 'layout' | 'slides' | 'slide' | 'element' | 'animation' | 'background' | 'transition' | 'notes' | 'comments';
    label: string;
    detail?: string;
    object?: string;
    animation?: { category: string; effect: string; option: string; start: 'on_click' | 'with_previous' | 'after_previous' };
    supported: boolean;
    reason?: string;
    requires?: string[]; // 이 노드를 고르면 함께 골라야 하는 노드 (slide -> layout)
    usedBy?: string[];   // 이 개체 틀을 물려받는 slide의 요소. 그중 하나라도 골랐으면 이 노드를 뺄 수 없다
    children?: ImportNode[];
};

type ImportResult = { tlide?: string; tasset?: string; warnings?: string[]; errors?: string[]; error?: string };

// 파일 하나의 트리. checked는 직접 고른 노드들이고, 제목 노드(file, masters, slides)는 아래에 고른 것이 있으면 고른 것으로 본다
type Tree = {
    key: string; // 같은 파일인지 가리는 소문자 경로
    path: string;
    name: string;
    state: 'loading' | 'ready' | 'error';
    error?: string;
    root?: ImportNode;
    checked: Set<string>;
    expanded: Set<string>;
    output: string; // 만들 .tlide의 경로
    running?: boolean;
    result?: ImportResult;
};

const extensions = ['.pptx', '.pptm', '.ppsx', '.potx'];

// labels.ts에 없는, templide가 지원하지 않는 개체의 이름
const extraObjectNames = (): Record<string, string> => ({
    table: t('표'), chart: t('차트'), smartart: 'SmartArt', ole: t('OLE 개체'), ink: t('잉크'), equation: t('수식'), model3d: t('3D 모델'),
});

const objectIcons: Record<string, LucideIcon> = {
    text_box: Type, shape: Shapes, image: Image, line: Minus, connector: Waypoints, freeform: PenTool, group: Boxes, placeholder: SquareDashed,
    video: Film, audio: Volume2, table: Table, chart: ChartColumn, smartart: Network, ole: Package, ink: Brush, equation: Sigma, model3d: Box,
};

const kindIcons: Record<string, LucideIcon> = {
    file: FileText, masters: LayoutTemplate, master: Palette, layout: LayoutTemplate, slides: Layers, slide: Presentation, animation: Sparkles,
    background: PaintBucket, transition: ArrowRightLeft, notes: StickyNote, comments: MessageSquare,
};

const fileName = (path: string) => path.split(/[\\/]/).pop() ?? path;
const isHeading = (node: ImportNode) => node.kind === 'file' || node.kind === 'masters' || node.kind === 'slides';

// 받침이 있으면 '을', 없으면 '를'
function objectParticle(word: string): string {
    const last = word.trim().charCodeAt(word.trim().length - 1);
    return last >= 0xAC00 && last <= 0xD7A3 && (last - 0xAC00) % 28 !== 0 ? '을' : '를';
}

// 트리 색인

type Index = {
    nodes: Map<string, ImportNode>;
    parent: Map<string, string>;
    below: Map<string, string[]>; // 노드 아래의 고를 수 있는(지원하는) 노드들. 제목 노드는 빠진다
    disabled: Set<string>;        // 지원하지 않거나 조상이 지원하지 않는 노드
};

const indexes = new WeakMap<ImportNode, Index>();

function indexOf(root: ImportNode): Index {
    const cached = indexes.get(root);
    if (cached) {
        return cached;
    }
    const index: Index = { nodes: new Map(), parent: new Map(), below: new Map(), disabled: new Set() };
    const visit = (node: ImportNode, parent: ImportNode | null, disabled: boolean): string[] => {
        index.nodes.set(node.id, node);
        if (parent) {
            index.parent.set(node.id, parent.id);
        }
        const off = disabled || !node.supported;
        if (off) {
            index.disabled.add(node.id);
        }
        const below: string[] = [];
        for (const child of node.children ?? []) {
            const selectable = visit(child, node, off);
            if (!index.disabled.has(child.id) && !isHeading(child)) {
                below.push(child.id);
            }
            below.push(...selectable);
        }
        index.below.set(node.id, below);
        return below;
    };
    visit(root, null, false);
    indexes.set(root, index);
    return index;
}

function isChecked(index: Index, checked: Set<string>, node: ImportNode): boolean {
    return isHeading(node) ? (index.below.get(node.id) ?? []).some((id) => checked.has(id)) : checked.has(node.id);
}

// 처음에는 지원하는 노드를 모두 고른다
function initialChecked(root: ImportNode): Set<string> {
    const index = indexOf(root);
    return new Set([...index.nodes.values()].filter((node) => !index.disabled.has(node.id) && !isHeading(node)).map((node) => node.id));
}

// 고르면 아래의 노드와 조상을 모두 고르고, 빼면 아래의 노드를 모두 뺀다 (잠긴 개체 틀도 함께 빠진다)
function toggled(index: Index, checked: Set<string>, node: ImportNode): Set<string> {
    const next = new Set(checked);
    if (isChecked(index, checked, node)) {
        next.delete(node.id);
        for (const id of index.below.get(node.id) ?? []) {
            next.delete(id);
        }
        return next;
    }
    if (!isHeading(node)) {
        next.add(node.id);
    }
    for (const id of index.below.get(node.id) ?? []) {
        next.add(id);
    }
    for (let up = index.parent.get(node.id); up; up = index.parent.get(up)) {
        if (!isHeading(index.nodes.get(up)!)) {
            next.add(up);
        }
    }
    return next;
}

// 고른 slide가 쓰는 개체 틀이면 그 slide들의 수
function lockCount(index: Index, checked: Set<string>, node: ImportNode): number {
    const slides = new Set<string>();
    for (const id of node.usedBy ?? []) {
        if (!checked.has(id)) {
            continue;
        }
        let slide = id;
        for (let up: string | undefined = id; up; up = index.parent.get(up)) {
            if (index.nodes.get(up)?.kind === 'slide') {
                slide = up;
                break;
            }
        }
        slides.add(slide);
    }
    return slides.size;
}

type Problem = { message: string; target: string };

// 고른 slide가 쓰는 레이아웃(requires)을 고르지 않았으면 문제다
function problemsOf(index: Index, checked: Set<string>): Problem[] {
    const problems: Problem[] = [];
    for (const node of index.nodes.values()) {
        if (node.kind !== 'slide' || !checked.has(node.id)) {
            continue;
        }
        for (const id of node.requires ?? []) {
            if (checked.has(id)) {
                continue;
            }
            const required = index.nodes.get(id);
            const what = required?.kind === 'master' ? t('슬라이드 마스터') : t('레이아웃');
            const name = required?.label ?? id;
            problems.push({ message: t('{0}: {1} \'{2}\'{3} 함께 골라야 합니다', node.label, what, name, objectParticle(name)), target: id });
        }
    }
    return problems;
}

function slideCount(index: Index, checked: Set<string>): [number, number] {
    const slides = [...index.nodes.values()].filter((node) => node.kind === 'slide' && !index.disabled.has(node.id));
    return [slides.filter((node) => checked.has(node.id)).length, slides.length];
}

// 노드에 보일 글. 애니메이션은 "나타내기 · 페이드 · 클릭할 때"처럼 짓는다
function nodeText(node: ImportNode): string {
    const animation = node.animation;
    if (node.kind === 'animation' && animation) {
        const parts = [label(Object.fromEntries(animationCategories()), animation.category), label(effectNames, animation.effect)];
        if (animation.option) {
            parts.push(label(optionNames, animation.option));
        }
        parts.push(label(startNames, animation.start));
        return parts.join(' · ');
    }
    return node.label;
}

function nodeIcon(node: ImportNode): LucideIcon {
    if (node.kind === 'element') {
        return objectIcons[node.object ?? ''] ?? Square;
    }
    return kindIcons[node.kind] ?? Square;
}

// 같은 폴더의 같은 이름(.tlide와 .tasset)이 이미 있거나 이 창의 다른 파일이 쓰면 이름 뒤에 -2, -3을 붙인다
async function defaultOutput(path: string, taken: Set<string>): Promise<string> {
    const separator = path.includes('\\') ? '\\' : '/';
    const cut = path.lastIndexOf(separator);
    const folder = cut >= 0 ? path.slice(0, cut + 1) : '';
    const stem = fileName(path).replace(/\.[^.]*$/, '');
    for (let number = 1; ; ++number) {
        const name = number === 1 ? stem : `${stem}-${number}`;
        const tlide = `${folder}${name}.tlide`;
        if (taken.has(tlide.toLowerCase())) {
            continue;
        }
        const [tlideExists, tassetExists] = await Promise.all([
            invoke<boolean>('path_exists', { path: tlide }),
            invoke<boolean>('path_exists', { path: `${folder}${name}.tasset` }),
        ]);
        if (!tlideExists && !tassetExists) {
            return tlide;
        }
    }
}

// 체크 상자

function Check({ checked, indeterminate, disabled, title, onChange }: { checked: boolean; indeterminate: boolean; disabled: boolean; title?: string; onChange(): void }) {
    const ref = useRef<HTMLInputElement>(null);
    useEffect(() => {
        if (ref.current) {
            ref.current.indeterminate = indeterminate;
        }
    }, [indeterminate]);
    return <input ref={ref} type="checkbox" checked={checked} disabled={disabled} title={title} onChange={onChange} />;
}

// 창

export function ImportDialog({ ready, request, onTitle, onClose }: {
    ready: Promise<void>; // 컴파일러가 준비되면 끝난다
    request<T = any>(method: string, params: unknown): Promise<T>;
    onTitle(title: string): void;
    onClose(): void;
}) {
    useLang(); // 언어를 바꾸면 다시 그린다
    const [trees, setTrees] = useState<Tree[]>([]);
    const treesRef = useRef<Tree[]>([]);
    const [running, setRunning] = useState(false);
    const [dropping, setDropping] = useState(false);
    const [notice, setNotice] = useState<string | null>(null);
    const [flash, setFlash] = useState<string | null>(null); // 문제를 눌러 찾아간 노드 (트리 key + id)
    const listRef = useRef<HTMLDivElement>(null);
    const outputs = useRef<Promise<void>>(Promise.resolve()); // 저장할 곳을 정하는 차례

    // 목록은 treesRef가 기준이다. 파일을 더하는 일이 여러 번 겹쳐도 앞의 변경을 잃지 않게 언제나 ref를 고친 뒤 화면에 넘긴다
    const replace = useCallback((next: Tree[]) => {
        treesRef.current = next;
        setTrees(next);
    }, []);
    const update = useCallback((key: string, change: (tree: Tree) => Partial<Tree>) => {
        replace(treesRef.current.map((tree) => tree.key === key ? { ...tree, ...change(tree) } : tree));
    }, [replace]);

    const showNotice = useCallback((message: string) => {
        setNotice(message);
        window.setTimeout(() => setNotice((current) => current === message ? null : current), 5000);
    }, []);

    // pptx들을 목록에 더하고 트리를 읽는다. 이미 있는 파일은 건너뛴다
    const addFiles = useCallback(async (paths: string[]) => {
        const wanted = paths.filter((path) => extensions.some((extension) => path.toLowerCase().endsWith(extension)));
        if (wanted.length < paths.length) {
            showNotice(t('pptx, pptm, ppsx, potx 파일만 불러올 수 있습니다'));
        }
        for (const path of wanted) {
            const key = path.toLowerCase();
            if (treesRef.current.some((tree) => tree.key === key)) {
                continue;
            }
            replace([...treesRef.current, { key, path, name: fileName(path), state: 'loading', checked: new Set(), expanded: new Set(), output: '' }]);
            // 저장할 곳은 하나씩 차례로 정해서 같은 이름의 pptx(a.pptx, a.potx)가 같은 .tlide를 쓰지 않게 한다
            outputs.current = outputs.current.then(async () => {
                const taken = new Set(treesRef.current.map((each) => each.output.toLowerCase()).filter(Boolean));
                const output = await defaultOutput(path, taken).catch(() => path.replace(/\.[^.\\/]*$/, '') + '.tlide');
                update(key, () => ({ output }));
            });
            (async () => {
                try {
                    await ready;
                    const result = await request<{ tree?: ImportNode; error?: string }>('templide/pptx_tree', { path });
                    if (!result.tree) {
                        update(key, () => ({ state: 'error', error: result.error ?? t('pptx를 읽을 수 없습니다') }));
                        return;
                    }
                    const root = result.tree;
                    const slides = root.children?.find((child) => child.kind === 'slides');
                    update(key, () => ({ state: 'ready', root, checked: initialChecked(root), expanded: new Set([root.id, ...(slides ? [slides.id] : [])]) }));
                } catch (error) {
                    update(key, () => ({ state: 'error', error: String(error) }));
                }
            })();
        }
    }, [ready, request, replace, update, showNotice]);

    // 명령줄과 다른 프로세스(탐색기에서 여러 파일을 고른 것)가 보낸 파일, 창에 끌어다 놓은 파일
    useEffect(() => {
        const stops: (() => void)[] = [];
        let closed = false;
        const pull = async () => addFiles(await invoke<string[]>('import_files'));
        (async () => {
            stops.push(await listen('import-add', () => { pull(); }));
            stops.push(await getCurrentWebview().onDragDropEvent((event) => {
                const payload = event.payload;
                if (payload.type === 'enter' || payload.type === 'over') {
                    setDropping(true);
                } else if (payload.type === 'leave') {
                    setDropping(false);
                } else if (payload.type === 'drop') {
                    setDropping(false);
                    addFiles(payload.paths);
                }
            }));
            if (closed) {
                stops.forEach((stop) => stop());
                return;
            }
            await pull();
        })();
        return () => {
            closed = true;
            stops.forEach((stop) => stop());
        };
    }, [addFiles]);

    const firstName = trees[0]?.name ?? '';
    useEffect(() => {
        onTitle(firstName ? t('templide 불러오기 - {0}', firstName) : t('templide 불러오기'));
    }, [firstName, onTitle]);

    useEffect(() => {
        const key = (event: KeyboardEvent) => {
            if (event.key === 'Escape' && !running) {
                onClose();
            }
        };
        window.addEventListener('keydown', key);
        return () => window.removeEventListener('keydown', key);
    }, [onClose, running]);

    // 문제를 누르면 그 노드가 보이게 펼치고 그 자리로 간다
    const reveal = (tree: Tree, id: string) => {
        const index = indexOf(tree.root!);
        update(tree.key, (current) => {
            const expanded = new Set(current.expanded);
            for (let up = index.parent.get(id); up; up = index.parent.get(up)) {
                expanded.add(up);
            }
            return { expanded };
        });
        const mark = `${tree.key}\n${id}`;
        setFlash(mark);
        window.setTimeout(() => setFlash((current) => current === mark ? null : current), 1600);
        requestAnimationFrame(() => requestAnimationFrame(() => {
            listRef.current?.querySelector(`[data-row="${CSS.escape(mark)}"]`)?.scrollIntoView({ block: 'center', behavior: 'smooth' });
        }));
    };

    const changeOutput = async (tree: Tree) => {
        const chosen = await save({ title: t('만들 .tlide 파일'), defaultPath: tree.output || undefined, filters: [{ name: 'templide', extensions: ['tlide'] }] });
        if (chosen) {
            update(tree.key, () => ({ output: /\.tlide$/i.test(chosen) ? chosen : `${chosen}.tlide` }));
        }
    };

    // 고른 것이 있는 파일을 하나씩 바꾼다. 실패해도 다음 파일은 계속한다. 모두 바뀌면 창을 닫고, 실패가 있으면 오류를 보이려고 둔다
    const run = async () => {
        setRunning(true);
        let failed = false;
        for (const tree of treesRef.current) {
            if (tree.state !== 'ready' || tree.checked.size === 0) {
                continue;
            }
            update(tree.key, () => ({ running: true, result: undefined }));
            let result: ImportResult;
            try {
                result = await request<ImportResult>('templide/pptx_import', { path: tree.path, selection: [...tree.checked], output: tree.output });
            } catch (error) {
                result = { error: String(error) };
            }
            update(tree.key, () => ({ running: false, result }));
            failed ||= !!result.error;
        }
        setRunning(false);
        if (!failed) {
            onClose();
        }
    };

    const readyTrees = trees.filter((tree) => tree.state === 'ready' && tree.root);
    const problemCount = readyTrees.reduce((sum, tree) => sum + problemsOf(indexOf(tree.root!), tree.checked).length, 0);
    const chosen = readyTrees.filter((tree) => tree.checked.size > 0).length;

    const renderNode = (tree: Tree, index: Index, node: ImportNode, depth: number): ReactNode => {
        const disabled = index.disabled.has(node.id);
        const checked = !disabled && isChecked(index, tree.checked, node);
        const indeterminate = checked && (index.below.get(node.id) ?? []).some((id) => !tree.checked.has(id));
        const locks = lockCount(index, tree.checked, node);
        const children = node.children ?? [];
        const open = tree.expanded.has(node.id);
        const Icon = nodeIcon(node);
        const mark = `${tree.key}\n${node.id}`;
        const objectName = node.kind === 'element' && node.object ? label({ ...objectNames, ...extraObjectNames() }, node.object) : null;
        const lockTitle = locks > 0 ? t('슬라이드 {0}개가 이 개체 틀을 씁니다', locks) : undefined;
        return (
            <div key={node.id}>
                <div className={'import-row' + (disabled ? ' unsupported' : '') + (flash === mark ? ' flash' : '')} data-row={mark} style={{ paddingLeft: 8 + depth * 18 }}
                    title={disabled ? node.reason : lockTitle}>
                    {children.length > 0 ? (
                        <button className="import-chevron" onClick={() => update(tree.key, (current) => {
                            const expanded = new Set(current.expanded);
                            if (!expanded.delete(node.id)) {
                                expanded.add(node.id);
                            }
                            return { expanded };
                        })}>{open ? <ChevronDown size={14} /> : <ChevronRight size={14} />}</button>
                    ) : <span className="import-chevron" />}
                    <label className="import-label">
                        <Check checked={checked} indeterminate={indeterminate} disabled={disabled || running || (checked && locks > 0)} title={disabled ? node.reason : lockTitle}
                            onChange={() => update(tree.key, (current) => ({ checked: toggled(index, current.checked, node) }))} />
                        <Icon size={14} className="import-icon" />
                        <span className="import-text">{nodeText(node)}</span>
                        {objectName && <span className="import-tag">{objectName}</span>}
                        {node.detail && <span className="import-detail">{node.detail}</span>}
                        {disabled && node.reason && <span className="import-reason">{node.reason}</span>}
                    </label>
                </div>
                {open && children.map((child) => renderNode(tree, index, child, depth + 1))}
            </div>
        );
    };

    const renderTree = (tree: Tree) => {
        const index = tree.root ? indexOf(tree.root) : null;
        const root = tree.root;
        const problems = index ? problemsOf(index, tree.checked) : [];
        const [picked, total] = index ? slideCount(index, tree.checked) : [0, 0];
        const open = !!root && tree.expanded.has(root.id);
        return (
            <section key={tree.key} className="import-card">
                <header className="import-card-header">
                    {root ? (
                        <button className="import-chevron" onClick={() => update(tree.key, (current) => {
                            const expanded = new Set(current.expanded);
                            if (!expanded.delete(root.id)) {
                                expanded.add(root.id);
                            }
                            return { expanded };
                        })}>{open ? <ChevronDown size={14} /> : <ChevronRight size={14} />}</button>
                    ) : <span className="import-chevron" />}
                    {root && index && (
                        <Check checked={isChecked(index, tree.checked, root)} indeterminate={isChecked(index, tree.checked, root) && (index.below.get(root.id) ?? []).some((id) => !tree.checked.has(id))}
                            disabled={running} onChange={() => update(tree.key, (current) => ({ checked: toggled(index, current.checked, root) }))} />
                    )}
                    <FileText size={15} className="import-icon" />
                    <span className="import-file" title={tree.path}>{tree.name}</span>
                    {index && <span className="import-count">{t('슬라이드 {0}/{1}개', picked, total)}</span>}
                    {(tree.state === 'loading' || tree.running) && <LoaderCircle size={14} className="spin" />}
                    <span className="spacer" />
                    <button className="import-icon-button" title={t('목록에서 빼기')} disabled={running}
                        onClick={() => replace(treesRef.current.filter((each) => each.key !== tree.key))}><X size={14} /></button>
                </header>
                <div className="import-output">
                    <span className="import-output-label">{t('저장할 곳')}</span>
                    <span className="import-output-path" title={tree.output}>{tree.output || '…'}</span>
                    <button className="import-small-button" disabled={running || !tree.output} onClick={() => changeOutput(tree)}><FolderOpen size={13} /> {t('변경')}</button>
                </div>
                {tree.state === 'loading' && <div className="import-message"><LoaderCircle size={14} className="spin" /> {t('pptx를 읽는 중')}</div>}
                {tree.state === 'error' && <div className="import-message error"><CircleAlert size={14} /> {tree.error}</div>}
                {tree.result && <ResultView result={tree.result} />}
                {problems.length > 0 && (
                    <div className="import-problems">
                        {problems.map((problem, i) => (
                            <button key={i} className="import-problem" onClick={() => reveal(tree, problem.target)}><CircleAlert size={13} /> {problem.message}</button>
                        ))}
                    </div>
                )}
                {root && index && open && (
                    <div className="import-tree">
                        {(root.children ?? []).map((child) => renderNode(tree, index, child, 0))}
                    </div>
                )}
            </section>
        );
    };

    return (
        <div className="import">
            <header className="import-header">
                <FileInput size={16} />
                <span className="import-title">{t('pptx 불러오기')}</span>
                <span className="import-subtitle">{t('고른 슬라이드와 요소만 .tlide로 바꿉니다')}</span>
            </header>
            <div className="import-list" ref={listRef}>
                {trees.length === 0 && (
                    <div className="import-empty">
                        <FileInput size={28} />
                        <span>{t('pptx 파일을 이 창에 끌어다 놓으세요')}</span>
                    </div>
                )}
                {trees.map(renderTree)}
            </div>
            <footer className="import-footer">
                {notice && <span className="import-notice"><TriangleAlert size={13} /> {notice}</span>}
                {!notice && problemCount > 0 && <span className="import-notice error"><CircleAlert size={13} /> {t('함께 골라야 하는 레이아웃이 {0}개 있습니다', problemCount)}</span>}
                <span className="spacer" />
                <button className="import-secondary" disabled={running} onClick={onClose}>{t('닫기')}</button>
                <button className="import-primary" disabled={running || chosen === 0 || problemCount > 0} onClick={run}>
                    {running ? t('바꾸는 중…') : t('불러오기 ({0})', chosen)}
                </button>
            </footer>
            {dropping && <div className="import-drop"><FileInput size={32} /><span>{t('놓으면 불러올 목록에 더합니다')}</span></div>}
        </div>
    );
}

// 바꾼 결과. 만든 파일, 코드의 오류(파일은 만들었다), 빼거나 비슷하게 바꾼 것(경고)
function ResultView({ result }: { result: ImportResult }) {
    const [all, setAll] = useState(false);
    if (result.error) {
        return <div className="import-message error"><CircleAlert size={14} /> {result.error}</div>;
    }
    const warnings = result.warnings ?? [];
    const shown = all ? warnings : warnings.slice(0, 4);
    return (
        <div className="import-result">
            <div className="import-result-head">
                <CircleCheck size={14} className="ok" />
                <span className="import-result-paths">
                    {[result.tlide, result.tasset].filter(Boolean).map((path) => <span key={path} title={path}>{fileName(path!)}</span>)}
                </span>
                <span className="spacer" />
                {result.tlide && (
                    <>
                        <button className="import-small-button" onClick={() => invoke('open_in_editor', { path: result.tlide })}><ExternalLink size={13} /> {t('편집기에서 열기')}</button>
                        <button className="import-small-button" onClick={() => invoke('open_path', { path: result.tlide, reveal: true })}><FolderOpen size={13} /> {t('폴더에서 보기')}</button>
                    </>
                )}
            </div>
            {result.errors && result.errors.length > 0 && (
                <div className="import-errors">
                    <div className="import-errors-title"><CircleAlert size={13} /> {t('만든 코드에 오류가 있습니다 (파일은 만들었습니다)')}</div>
                    {result.errors.map((line, i) => <div key={i} className="import-error-line">{line}</div>)}
                </div>
            )}
            {warnings.length > 0 && (
                <div className="import-warnings">
                    {shown.map((line, i) => <div key={i} className="import-warning"><TriangleAlert size={12} /> {line}</div>)}
                    {warnings.length > 4 && (
                        <button className="link-button" onClick={() => setAll(!all)}>{all ? t('접기') : t('{0}개 더 보기', warnings.length - 4)}</button>
                    )}
                </div>
            )}
        </div>
    );
}

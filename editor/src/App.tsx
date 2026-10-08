import { useCallback, useEffect, useRef, useState, useSyncExternalStore, type CSSProperties, type PointerEvent as ReactPointerEvent } from 'react';
import { convertFileSrc, invoke } from '@tauri-apps/api/core';
import { message, open as openDialog, save as saveDialog } from '@tauri-apps/plugin-dialog';
import { emitTo, listen } from '@tauri-apps/api/event';
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { getVersion } from '@tauri-apps/api/app';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { lsp, pathToUri, type DeckResult, type Diagnostic, type Origin, type Range, type Schema, type TargetWarning } from './lsp';
import { SlideView, findElement, selectionIn, type EditingRun, type Point, type Resize } from './SlideView';
import { FormatBar, IconButton, Thumbnail, type StyleProperty, type Toggle } from './Panels';
import { ElementPanel, SlidePanel, type Pickers } from './Properties';
import { AnimationPane, type AnimationOp } from './Animations';
import { quote, type SetValue } from './Fields';
import { AgentPanel } from './Agents';
import { ExportDialog, type BuildResult, type ExportTarget } from './ExportDialog';
import type { EditorAccess } from './mcp';
import { attachDocument } from './navigation';
import { FontPopup } from './FontPicker';
import logo from '../../assets/icon/templide.svg';
import license from '../../LICENSE?raw';
import {
    Blend, Bot, ChevronDown, ChevronUp, CircleAlert, CircleCheck, CodeXml, Copy, FileOutput, FilePlus, Film, FolderOpen, Image as ImageIcon, Maximize, Minus,
    Music, PanelBottomClose, PanelBottomOpen, PanelRightClose, PanelRightOpen, PenTool, Play, Plus, Redo2, Save, Slash, Sparkles, Spline, Square, Trash2, TriangleAlert, Type, Undo2, X,
} from 'lucide-react';

type MonacoModule = typeof import('./monaco');

type AssetKind = 'image' | 'video' | 'audio';

// 묶음 안 파일에 붙일 이름으로 쓸 수 없는 낱말 (키워드, 내장 이름, 함수 이름)
const reservedNames = [
    'slide', 'put', 'template', 'style', 'object', 'var', 'master', 'case', 'target', 'if', 'else', 'for', 'in', 'enum', 'transition', 'animate', 'group',
    'as', 'theme', 'section', 'review', 'comment', 'bullets', 'numbers', 'dashes', 'paragraphs', 'true', 'false', 'color', 'int', 'float', 'string', 'text',
    'bool', 'ref', 'asset', 'by', 'default', 'image', 'video', 'audio', 'file', 'link', 'action', 'run', 'program', 'macro', 'action_file', 'linear',
    'radial', 'pattern', 'hex', 'rgb', 'rgba',
];

// 묶음 안 파일 이름(logo-1.png)에서 이름(logo_1)을 만든다. 영문자, 숫자, _만 쓸 수 있고 숫자로 시작할 수 없다
function identifierOf(entry: string, kind: AssetKind): string {
    const stem = entry.split('/').pop()!.replace(/\.[^.]*$/, '').replace(/[^A-Za-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
    return !stem ? kind : /^[0-9]/.test(stem) ? `${kind}_${stem}` : stem;
}

// folder 안의 파일이면 folder 기준 상대 경로('/'로 나눈다), 아니면 절대 경로
function relativeTo(folder: string, file: string): string {
    const prefix = folder.replace(/[\\/]+$/, '') + '\\';
    const inside = file.toLowerCase().startsWith(prefix.toLowerCase()) || file.toLowerCase().startsWith(prefix.replace(/\\/g, '/').toLowerCase());
    return (inside ? file.slice(prefix.length) : file).replace(/\\/g, '/');
}
type Editor = import('monaco-editor/editor/editor.api').editor.IStandaloneCodeEditor;
type ServerEdit = { uri: string; range: Range; newText: string };

// LSP 범위(0부터)를 Monaco 범위(1부터)로
const toMonaco = (range: Range) => ({
    startLineNumber: range.start.line + 1,
    startColumn: range.start.character + 1,
    endLineNumber: range.end.line + 1,
    endColumn: range.end.character + 1,
});

const contains = (range: Range, line: number, character: number) =>
    (line > range.start.line || (line === range.start.line && character >= range.start.character)) &&
    (line < range.end.line || (line === range.end.line && character <= range.end.character));

// 파일 탭 하나의 손잡이. 창(Shell)이 저장, 묶음 이름 바꾸기에 따른 코드 고치기 등을 이것으로 부탁한다
export type DocumentHandle = {
    path(): string | null;
    uri(): string | null;
    dirty(): boolean;
    save(): Promise<boolean>; // 저장했으면 true
    flush(): void;            // 입력 중인 내용을 컴파일러에 보낸다
    edit(edits: { range: Range; newText: string }[]): boolean; // 코드를 고친다. 한 번의 실행 취소로 되돌릴 수 있다
    reveal(range: Range): void; // 코드에서 그 자리를 보여 준다
    refresh(): void;          // 다시 컴파일한 덱을 받는다 (묶음이 바뀌었을 때)
};

type DocumentProps = {
    tab: string;          // 탭 id. AI 탭의 에이전트는 MCP로 이 탭의 문서를 고친다
    file: string | null;  // 처음에 열 파일. 없으면 빈 탭
    active: boolean;      // 보이는 탭
    accesses: Map<string, EditorAccess>; // 탭 id -> MCP 도구가 보는 상태
    register(handle: DocumentHandle | null): void;
    onInfo(info: { path: string | null; dirty: boolean }): void; // 탭에 보일 파일과 저장하지 않은 변경
    onOpen(file: string): void; // 파일을 탭으로 연다 (새 파일, 열기)
};

// 파일 탭 하나의 편집 화면. 창(Shell)이 탭마다 하나씩 띄우고 보이지 않는 탭은 숨겨 둔다
export function App({ tab, file, active, accesses, register, onInfo, onOpen }: DocumentProps) {
    const [path, setPath] = useState<string | null>(null);
    const [text, setText] = useState('');
    const [deck, setDeck] = useState<DeckResult | null>(null);
    const [deckError, setDeckError] = useState<string | null>(null);
    const [page, setPage] = useState(0);
    const [target, setTarget] = useState<string | null>(null);
    const [diagnostics, setDiagnostics] = useState<Diagnostic[]>([]);
    const [selected, setSelected] = useState<string | null>(null);
    const [dirty, setDirty] = useState(false);
    const [timings, setTimings] = useState<string[]>([]);
    const [monacoReady, setMonacoReady] = useState(false);
    const [notice, setNotice] = useState<string | null>(null);
    const [codeOpen, setCodeOpen] = useState(true); // 코드 패널은 처음부터 보인다
    const propsOpen = usePropsOpen(); // 오른쪽 패널 (속성)
    const [bottomTab, setBottomTab] = useState<BottomTab>('code'); // 코드 패널에서 보이는 탭
    const [sizes, setSizes] = useState<Sizes>(loadSizes); // 경계를 끌어 바꾼 패널 크기
    const sizesRef = useRef(sizes);
    sizesRef.current = sizes;
    const storeSizes = useCallback(() => saveSizes(sizesRef.current), []);
    const [zoom, setZoom] = useState<number | null>(null); // null이면 화면에 맞춘다
    const [fitScale, setFitScale] = useState(1);
    const [exportOpen, setExportOpen] = useState(false); // 내보낼 target을 고르는 창
    const [toast, setToast] = useState<{ message: string; path?: string; error?: boolean; warnings?: string[] } | null>(null);
    const [schema, setSchema] = useState<Schema | null>(null); // 속성 패널이 쓰는 object, enum, 애니메이션, 전환 목록
    const [paneOpen, setPaneOpen] = useState(false);            // 애니메이션 창
    const [preview, setPreview] = useState<{ transition: boolean; key: number } | null>(null); // 캔버스에서 재생하는 중
    const [mode, setMode] = useState<'connect' | 'draw' | null>(null); // 연결선의 두 번째 개체 고르기, 자유형 그리기
    const connectFrom = useRef<string | null>(null);
    // 고른 요소의 put 문장 시작을 따라가는 Monaco 장식. element id는 문장의 위치로 만들어지므로
    // 앞의 코드가 바뀌면 id도 바뀐다. 다시 컴파일한 뒤 장식이 옮겨 간 자리의 요소를 다시 고른다
    const anchor = useRef<{ decorations: string[]; head: string; rest: string } | null>(null);
    // 복사한 요소(id) 또는 잘라 낸 put 문장의 원문
    type Clip = { kind: 'copy'; id: string; page: number } | { kind: 'cut'; text: string };
    const clipboard = useRef<Clip | null>(null);
    const openFileRef = useRef<(file: string) => Promise<void>>(async () => {}); // 아래에서 만드는 openFile
    const activeRef = useRef(active);
    activeRef.current = active;
    const onOpenRef = useRef(onOpen);
    onOpenRef.current = onOpen;
    const onInfoRef = useRef(onInfo);
    onInfoRef.current = onInfo;
    const detachDocument = useRef<(() => void) | null>(null); // 이름 찾기, 자동 완성에서 이 편집기를 뗀다
    // 코드의 font("...") 옆 ▾를 눌러 연 폰트 목록. range는 따옴표 안의 글자
    const [fontPopup, setFontPopup] = useState<{ x: number; y: number; range: import('monaco-editor/editor/editor.api').IRange; current: string } | null>(null);

    const uri = useRef<string | null>(null);
    const version = useRef(1);       // 컴파일러에 보낸 문서의 판
    const deckVersion = useRef(0);   // 지금 덱을 만든 문서의 판
    const pending = useRef(0);       // 아직 보내지 않은 입력의 타이머
    const immediate = useRef(false); // 캔버스에서 고친 것은 기다리지 않고 바로 다시 컴파일한다
    const targetRef = useRef<string | null>(null);
    const monaco = useRef<MonacoModule | null>(null);
    const editor = useRef<Editor | null>(null);
    const codeHost = useRef<HTMLDivElement>(null);
    const firstSlide = useRef(false);
    const noticeTimer = useRef(0);
    const editing = useRef<EditingRun | null>(null);
    const selectingFromCanvas = useRef(false); // 캔버스에서 고른 요소를 코드에서 선택하는 중
    const newElement = useRef<{ page: number; before: Set<string> } | null>(null); // 넣은 요소를 다시 컴파일한 뒤 고른다

    // 이벤트 처리기에서 지금 값을 읽으려고 ref에도 둔다
    const deckRef = useRef<DeckResult | null>(null);
    const textRef = useRef('');
    const diagnosticsRef = useRef<Diagnostic[]>([]);
    const warningsRef = useRef<TargetWarning[]>([]);
    const pathRef = useRef<string | null>(null);
    const dirtyRef = useRef(false);
    const mcpAccess = useRef<EditorAccess | null>(null); // AI 탭의 MCP 도구가 보는 지금 상태
    const idTrack = useRef<{ ids: string[]; decorations: string[] }>({ ids: [], decorations: [] }); // 요소 id마다 코드 위치의 장식
    const idAliases = useRef(new Map<string, string>()); // 코드를 고쳐 바뀐 요소 id: 전 id -> 지금 id
    const lastRenamed = useRef<Record<string, string>>({}); // 마지막으로 다시 컴파일하며 바뀐 id
    const diskText = useRef<string | null>(null); // 마지막으로 읽거나 저장한 파일 내용. 밖에서 바뀌었는지 볼 때 쓴다
    const selectedRef = useRef<string | null>(null);
    const pageRef = useRef(0);
    // 슬라이드를 옮기거나 넣은 뒤 보여 줄 슬라이드. 지금 덱에서 바로 고르면 옛 순서의 슬라이드가 잠깐 보이므로 새 덱이 오면 고른다
    const nextPage = useRef<number | null>(null);
    selectedRef.current = selected;
    pageRef.current = page;
    deckRef.current = deck;
    textRef.current = text;
    diagnosticsRef.current = diagnostics;
    // 고른 target의 경고. 코드에 오류가 있으면 덱이 옛것이므로 보이지 않는다
    const warnings = deck && !deckError ? (deck.targetWarnings ?? []).filter((warning) => warning.uri === uri.current) : [];
    warningsRef.current = warnings;
    pathRef.current = path;
    dirtyRef.current = dirty;

    const showNotice = useCallback((message: string) => {
        setNotice(message);
        window.clearTimeout(noticeTimer.current);
        noticeTimer.current = window.setTimeout(() => setNotice(null), 5000);
    }, []);

    const mark = useCallback(async (label: string) => {
        const ms = await invoke<number>('mark', { label });
        setTimings((list) => [...list, `${label} ${Math.round(ms)}ms`]);
    }, []);

    // 요소 id는 코드 위치로 만들어지므로 앞의 코드를 고치면 바뀐다. 요소마다 코드 위치에 Monaco 장식을 두고,
    // 다시 컴파일하면 장식이 옮겨 간 자리의 요소를 같은 요소로 보고 전 id -> 지금 id를 적어 둔다 (MCP 도구가 전 id도 받도록)
    const retrackIds = useCallback((result: DeckResult) => {
        const model = editor.current?.getModel();
        if (!model || pending.current !== 0) {
            return; // 덱을 만든 뒤에 코드가 또 바뀌어 자리를 맞출 수 없다. 장식은 그대로 두고 다음 덱에서 맞춘다
        }
        const rest = (id: string) => id.split('/').slice(2).join('/');
        const located = Object.entries(result.elements).filter(([, info]) => info.source.uri === uri.current && info.source.range);
        const byPlace = new Map(located.map(([id, info]) => [`${info.source.range!.start.line}:${info.source.range!.start.character}:${rest(id)}`, id]));
        const { ids, decorations } = idTrack.current;
        const renamed: Record<string, string> = {};
        ids.forEach((old, i) => {
            const range = model.getDecorationRange(decorations[i]);
            const now = range && byPlace.get(`${range.startLineNumber - 1}:${range.startColumn - 1}:${rest(old)}`);
            if (now && now !== old) {
                renamed[old] = now;
            }
        });
        // 전에 적어 둔 것도 지금 id로 잇는다. 없어진 요소와, 지금 다른 요소의 id가 된 것은 지운다
        const aliases = idAliases.current;
        for (const [old, current] of aliases) {
            const next = renamed[current] ?? current;
            if (result.elements[next]) {
                aliases.set(old, next);
            } else {
                aliases.delete(old);
            }
        }
        for (const [old, now] of Object.entries(renamed)) {
            aliases.set(old, now);
        }
        for (const old of [...aliases.keys()]) {
            if (result.elements[old]) {
                aliases.delete(old);
            }
        }
        lastRenamed.current = renamed;
        idTrack.current = {
            ids: located.map(([id]) => id),
            decorations: model.deltaDecorations(decorations, located.map(([, info]) => {
                const start = info.source.range!.start;
                return {
                    range: { startLineNumber: start.line + 1, startColumn: start.character + 1, endLineNumber: start.line + 1, endColumn: start.character + 2 },
                    options: { stickiness: 1 }, // 가장자리에 글자를 넣어도 늘어나지 않는다
                };
            })),
        };
    }, []);

    // 문서 전체가 바뀌어(파일 열기, 다시 읽기) 전 id를 이을 수 없을 때
    const resetIds = useCallback(() => {
        const model = editor.current?.getModel();
        idTrack.current = { ids: [], decorations: model ? model.deltaDecorations(idTrack.current.decorations, []) : [] };
        idAliases.current.clear();
    }, []);

    const refreshDeck = useCallback(async () => {
        if (!uri.current) {
            return;
        }
        const requested = version.current;
        const result = await lsp.request<DeckResult>('templide/deck', { uri: uri.current, target: targetRef.current });
        if (requested !== version.current) {
            return; // 그 사이에 문서가 또 바뀌었다
        }
        if (result.error) {
            // 오류가 있으면 마지막으로 그린 덱을 그대로 둔다
            setDeckError(result.error);
            return;
        }
        deckVersion.current = requested;
        setDeckError(null);
        setDeck(result);
        retrackIds(result);
        const model = editor.current?.getModel();
        const kept = anchor.current;
        if (selectedRef.current && !result.elements[selectedRef.current] && model && kept && kept.decorations.length) {
            const range = model.getDecorationRange(kept.decorations[0]);
            const match = range && Object.entries(result.elements).find(([id, info]) => {
                const start = info.source.range?.start;
                const parts = id.split('/');
                return info.source.uri === uri.current && start && start.line === range.startLineNumber - 1 && start.character === range.startColumn - 1
                    && parts[0] === kept.head && parts.slice(2).join('/') === kept.rest;
            });
            setSelected(match ? match[0] : null);
        }
        lsp.request<Schema & { error?: string }>('templide/schema', { uri: uri.current }).then((next) => {
            if (!next.error) {
                setSchema((current) => JSON.stringify(current) === JSON.stringify(next) ? current : next);
            }
        });
        if (newElement.current) {
            const { page: inserted, before } = newElement.current;
            const added = Object.keys(result.elements).find((id) => id.startsWith(`${inserted + 1}/`) && !before.has(id));
            if (added) {
                newElement.current = null;
                setSelected(added);
            }
        }
        const last = Math.max(result.deck.slides.length - 1, 0);
        const wanted = nextPage.current;
        nextPage.current = null;
        setPage((current) => Math.min(wanted ?? current, last));
    }, [retrackIds]);

    // 덱이 지금 문서에서 만든 것이고 오류가 없을 때만 캔버스에서 고칠 수 있다 (element id가 원문 위치로 만들어지기 때문)
    const fresh = () => pending.current === 0 && deckVersion.current === version.current && !deckError;

    const sendChange = useCallback(async () => {
        pending.current = 0;
        if (!uri.current || !editor.current) {
            return;
        }
        version.current += 1;
        lsp.notify('textDocument/didChange', { textDocument: { uri: uri.current, version: version.current }, contentChanges: [{ text: editor.current.getValue() }] });
        await refreshDeck();
    }, [refreshDeck]);

    const flushChange = useCallback(() => {
        if (pending.current === 0 || !uri.current || !editor.current) {
            return;
        }
        window.clearTimeout(pending.current);
        pending.current = 0;
        version.current += 1;
        lsp.notify('textDocument/didChange', { textDocument: { uri: uri.current, version: version.current }, contentChanges: [{ text: editor.current.getValue() }] });
        refreshDeck();
    }, [refreshDeck]);

    // 컴파일러가 돌려준 원문 수정을 Monaco에 넣는다. 한 번의 실행 취소로 되돌릴 수 있다
    const applyEdits = useCallback((edits: ServerEdit[]) => {
        const instance = editor.current!;
        if (edits.some((edit) => edit.uri !== uri.current)) {
            showNotice('다른 파일에 적힌 값이라 여기서 고칠 수 없습니다. 그 파일을 열어 고쳐 주세요');
            return false;
        }
        immediate.current = true;
        instance.pushUndoStop();
        instance.executeEdits('canvas', edits.map((edit) => ({ range: toMonaco(edit.range), text: edit.newText, forceMoveMarkers: true })));
        instance.pushUndoStop();
        return true;
    }, [showNotice]);

    const requestEdit = useCallback(async (edits: object[]): Promise<boolean> => {
        if (!uri.current || !editor.current) {
            return false;
        }
        if (!fresh()) {
            showNotice('다시 컴파일하는 중이거나 코드에 오류가 있어 지금은 캔버스에서 고칠 수 없습니다');
            return false;
        }
        // 값과 함께 온 선언(declare)은 모아서 파일 위쪽에 넣는 op 하나로 보낸다
        const declarations = (edits as { declare?: string }[]).map((each) => each.declare ?? '').join('');
        const ops = [...(declarations ? [{ op: 'declare', text: declarations }] : []), ...(edits as { declare?: string }[]).map(({ declare: _, ...rest }) => rest)];
        const result = await lsp.request<{ edits?: ServerEdit[]; error?: string }>('templide/edit', { uri: uri.current, target: targetRef.current, edits: ops });
        if (result.error) {
            showNotice(result.error);
            return false;
        }
        return applyEdits(result.edits ?? []);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [applyEdits, showNotice, deckError]);

    const onMove = useCallback((id: string, dx: number, dy: number) => requestEdit([{ op: 'move', id, dx, dy }]), [requestEdit]);
    const onResize = useCallback((id: string, change: Resize, instance: boolean) =>
        requestEdit(Object.entries(change).map(([name, value]) => ({ op: 'set', id, name, length: value, instance }))), [requestEdit]);
    const onText = useCallback((id: string, paragraph: number, run: number, value: string) =>
        requestEdit([{ op: 'text', id, paragraph, run, text: value }]), [requestEdit]);

    const onSetProperty = useCallback((name: string, value: SetValue) => {
        if (selectedRef.current) {
            requestEdit([{ op: 'set', id: selectedRef.current, name, ...value }]);
        }
    }, [requestEdit]);

    // 속성을 지워 기본값으로 되돌린다
    const onUnsetProperty = useCallback((name: string) => {
        if (selectedRef.current) {
            requestEdit([{ op: 'unset', id: selectedRef.current, name }]);
        }
    }, [requestEdit]);

    const onAnimation = useCallback((op: AnimationOp) => requestEdit([{ op: 'animation', page: pageRef.current + 1, ...op }]), [requestEdit]);
    const onSlide = useCallback((op: object) => requestEdit([{ ...op, page: pageRef.current + 1 }]), [requestEdit]);
    const onDocument = useCallback((op: object) => requestEdit([op]), [requestEdit]);
    const startPreview = useCallback((transition: boolean) => setPreview({ transition, key: Date.now() }), []);
    const stopPreview = useCallback(() => setPreview(null), []);

    // 코드의 한 부분 (layout 식, 목록 식 등)
    const readSource = useCallback((origin: Origin): string | null => {
        const model = editor.current?.getModel();
        if (!model || !origin.range || origin.uri !== uri.current) {
            return null;
        }
        return model.getValueInRange(toMonaco(origin.range));
    }, []);

    // 슬라이드 가운데에 w x h 크기로 새 요소를 넣고, 다시 컴파일한 뒤 그 요소를 고른다. declare는 함께 넣을 파일 위쪽의 선언이다
    const insertObject = useCallback(async (object: string, w: number, h: number, extra: object[], declare = '') => {
        const current = deckRef.current;
        if (!current) {
            return;
        }
        const at = (value: number) => Math.max(0, Math.round(value));
        const properties: object[] = [
            ...extra,
            { name: 'x', length: at((current.width - w) / 2) }, { name: 'y', length: at((current.height - h) / 2) },
            { name: 'width', length: Math.round(w) }, { name: 'height', length: Math.round(h) },
        ];
        const before = new Set(Object.keys(current.elements).filter((id) => id.startsWith(`${pageRef.current + 1}/`)));
        newElement.current = { page: pageRef.current, before };
        if (!await requestEdit([{ op: 'insert', page: pageRef.current + 1, object, properties, declare }])) {
            newElement.current = null;
        }
    }, [requestEdit]);

    const insert = useCallback((object: 'text_box' | 'shape') => object === 'text_box'
        ? insertObject('text_box', 400, 80, [{ name: 'text', string: '새 글상자' }])
        : insertObject('shape', 300, 200, [{ name: 'kind', enum: 'rect' }]), [insertObject]);

    // 파일들을 기본 묶음(asset "..." default;)에 넣고, 파일마다 이름을 붙인 선언(image 이름 = asset("...");)을 만든다.
    // 기본 묶음이 없으면 새 묶음을 저장할 곳을 묻고 asset 문도 함께 만든다. codes는 파일마다 붙인 이름이다
    const addAssets = useCallback(async (files: { kind: AssetKind; file: { source: string } | { base64: string; name: string } }[]): Promise<{ codes: string[]; declare: string } | null> => {
        if (!pathRef.current || !uri.current) {
            showNotice('먼저 파일을 저장하거나 열어 주세요');
            return null;
        }
        const current = await lsp.request<Schema & { error?: string }>('templide/schema', { uri: uri.current });
        if (current.error) {
            showNotice('코드를 아직 분석하지 못해 파일을 넣을 수 없습니다');
            return null;
        }
        const folder = pathRef.current.replace(/[\\/][^\\/]*$/, '');
        let declare = '';
        let bundle: string;
        let reference: (entry: string) => string;
        const target = current.assets.find((asset) => asset.default);
        if (target) {
            bundle = target.bundle;
            reference = (entry) => !target.hasBy ? `asset(${quote(entry)})`
                : target.namespaces.length > 0 ? `asset(${quote(target.namespaces[0] + '.' + entry)})` : `file(${quote(target.written + '/' + entry)})`;
        } else {
            const stem = pathRef.current.split(/[\\/]/).pop()!.replace(/\.[^.]*$/, '');
            const chosen = await saveDialog({ title: '그림과 미디어를 담을 묶음 만들기', defaultPath: `${folder}\\${stem}.tasset`, filters: [{ name: 'Templide Asset', extensions: ['tasset'] }] });
            if (!chosen) {
                return null;
            }
            bundle = chosen;
            declare += `asset ${quote(relativeTo(folder, chosen))} default;\n`;
            reference = (entry) => `asset(${quote(entry)})`;
        }
        const taken = new Set([...reservedNames, ...Object.keys(current.constants), ...Object.keys(current.styles), ...Object.keys(current.objects),
            ...Object.keys(current.templates), ...Object.keys(current.enums), ...Object.keys(current.masters), ...current.themes]);
        const codes: string[] = [];
        for (const { kind, file } of files) {
            const added = await lsp.request<{ name?: string; error?: string }>('templide/asset_add', { bundle, ...file });
            if (!added.name) {
                showNotice(added.error ?? '파일을 묶음에 넣을 수 없습니다');
                return null;
            }
            let name = identifierOf(added.name, kind);
            for (let number = 2; taken.has(name); ++number) {
                name = `${identifierOf(added.name, kind)}_${number}`;
            }
            taken.add(name);
            codes.push(name);
            declare += `${kind} ${name} = ${reference(added.name)};\n`;
        }
        return { codes, declare };
    }, [showNotice]);

    // 파일을 골라 기본 묶음에 넣는다
    const pickAsset = useCallback(async (kind: AssetKind, title: string, extensions: string[]): Promise<SetValue | null> => {
        const file = await openDialog({ multiple: false, filters: [{ name: title, extensions }] });
        if (typeof file !== 'string') {
            return null;
        }
        const added = await addAssets([{ kind, file: { source: file } }]);
        return added ? { code: added.codes[0], declare: added.declare } : null;
    }, [addAssets]);

    // 그림을 기본 묶음에 넣고, 비율을 지켜 슬라이드의 절반 안에 들어가게 넣는다
    const insertImage = useCallback(async () => {
        const current = deckRef.current;
        if (!current || !pathRef.current) {
            return;
        }
        const file = await openDialog({ multiple: false, filters: [{ name: '그림', extensions: ['png', 'jpg', 'jpeg', 'gif', 'bmp'] }] });
        if (typeof file !== 'string') {
            return;
        }
        try {
            const url = await invoke<string>('read_data_url', { path: file });
            const size = await new Promise<{ w: number; h: number }>((resolve) => {
                const image = new Image();
                image.onload = () => resolve({ w: image.naturalWidth, h: image.naturalHeight });
                image.onerror = () => resolve({ w: 400, h: 300 });
                image.src = url;
            });
            const added = await addAssets([{ kind: 'image', file: { source: file } }]);
            if (!added) {
                return;
            }
            const fit = Math.min(1, current.width / 2 / size.w, current.height / 2 / size.h);
            await insertObject('image', size.w * fit, size.h * fit, [{ name: 'data', code: added.codes[0] }], added.declare);
        } catch (error) {
            showNotice(String(error));
        }
    }, [insertObject, addAssets, showNotice]);

    // 비디오(url)의 크기와 첫 장면(0.1초)을 읽는다. 첫 장면은 png의 base64다
    const probeVideo = useCallback(async (url: string): Promise<{ w: number; h: number; poster: string | null }> => {
        const video = document.createElement('video');
        video.muted = true;
        video.preload = 'auto';
        video.src = url;
        const loaded = await new Promise<boolean>((resolve) => {
            video.onloadeddata = () => resolve(true);
            video.onerror = () => resolve(false);
            setTimeout(() => resolve(false), 8000);
        });
        if (!loaded) {
            return { w: 640, h: 360, poster: null };
        }
        await new Promise<void>((resolve) => {
            video.onseeked = () => resolve();
            video.currentTime = Math.min(0.1, video.duration || 0);
            setTimeout(resolve, 3000);
        });
        const canvas = document.createElement('canvas');
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;
        let poster: string | null = null;
        try {
            canvas.getContext('2d')!.drawImage(video, 0, 0);
            poster = canvas.toDataURL('image/png').split(',')[1];
        } catch (error) {
            showNotice(`표지 그림을 만들 수 없습니다: ${error}`);
        }
        return { w: video.videoWidth || 640, h: video.videoHeight || 360, poster };
    }, [showNotice]);

    const pickers: Pickers = {
        image: () => pickAsset('image', '그림', ['png', 'jpg', 'jpeg', 'gif', 'bmp']),
        media: (kind) => kind === 'video' ? pickAsset('video', '비디오', ['mp4', 'webm']) : pickAsset('audio', '오디오', ['mp3', 'wav', 'm4a']),
        sound: () => pickAsset('audio', '소리 (wav)', ['wav']),
        poster: async (id) => {
            // 캔버스의 비디오는 디스크 파일(묶음 안의 파일은 풀어 둔 것)을 "file:경로"로 가리킨다
            const element = findElement(deckRef.current?.deck.slides.flatMap((slide: { els: unknown[] }) => slide.els) ?? [], id);
            const source = typeof element?.src === 'string' && element.src.startsWith('file:') ? element.src.slice(5) : null;
            if (!source) {
                return null;
            }
            const { poster } = await probeVideo(convertFileSrc(source));
            if (!poster) {
                return null;
            }
            const stem = source.split(/[\\/]/).pop()!.replace(/\.[^.]*$/, '');
            const added = await addAssets([{ kind: 'image', file: { base64: poster, name: `${stem}-poster.png` } }]);
            return added ? { code: added.codes[0], declare: added.declare } : null;
        },
    };

    // 비디오: 기본 묶음에 넣고 비율을 지켜 슬라이드의 절반 안에 넣는다. 첫 장면을 표지 그림으로 쓴다
    const insertVideo = useCallback(async () => {
        const current = deckRef.current;
        const file = await openDialog({ multiple: false, filters: [{ name: '비디오', extensions: ['mp4', 'webm'] }] });
        if (!current || typeof file !== 'string') {
            return;
        }
        const { w, h, poster } = await probeVideo(convertFileSrc(file));
        const stem = file.split(/[\\/]/).pop()!.replace(/\.[^.]*$/, '');
        const added = await addAssets([
            { kind: 'video', file: { source: file } },
            ...(poster ? [{ kind: 'image' as const, file: { base64: poster, name: `${stem}-poster.png` } }] : []),
        ]);
        if (!added) {
            return;
        }
        const fit = Math.min(1, current.width / 2 / w, current.height / 2 / h);
        await insertObject('video', w * fit, h * fit, [{ name: 'data', code: added.codes[0] }, ...(poster ? [{ name: 'poster', code: added.codes[1] }] : [])], added.declare);
    }, [addAssets, probeVideo, insertObject]);

    const insertAudio = useCallback(async () => {
        const picked = await pickAsset('audio', '오디오', ['mp3', 'wav', 'm4a']);
        if (picked) {
            await insertObject('audio', 48, 48, [{ name: 'data', code: picked.code }], picked.declare);
        }
    }, [pickAsset, insertObject]);

    // 선은 가운데 가로로, 배경 흐림은 가운데 사각형으로 넣는다
    const insertLine = useCallback(async () => {
        const current = deckRef.current;
        if (!current) {
            return;
        }
        const x = Math.round(current.width / 2 - 150);
        const y = Math.round(current.height / 2);
        const before = new Set(Object.keys(current.elements).filter((id) => id.startsWith(`${pageRef.current + 1}/`)));
        newElement.current = { page: pageRef.current, before };
        const properties = [{ name: 'x1', length: x }, { name: 'y1', length: y }, { name: 'x2', length: x + 300 }, { name: 'y2', length: y }];
        if (!await requestEdit([{ op: 'insert', page: pageRef.current + 1, object: 'line', properties }])) {
            newElement.current = null;
        }
    }, [requestEdit]);

    // 연결선: 고른 개체에서 시작해 다음에 누르는 개체로 잇는다. 이름이 없는 개체에는 이름(as)을 붙인다
    const connect = useCallback(async (from: string, to: string) => {
        const current = deckRef.current;
        if (!current) {
            return;
        }
        const used = new Set(Object.values(current.elements).map((each) => each.name).filter(Boolean));
        const edits: object[] = [];
        const nameOf = (id: string) => {
            const info = current.elements[id];
            if (info.name) {
                return info.name;
            }
            let number = 1;
            while (used.has(`${info.object}${number}`)) {
                number += 1;
            }
            const name = `${info.object}${number}`;
            used.add(name);
            edits.push({ op: 'name', id, name });
            return name;
        };
        const a = nameOf(from);
        const b = nameOf(to);
        edits.push({ op: 'insert', page: pageRef.current + 1, object: 'connector', properties: [{ name: 'from', enum: a }, { name: 'to', enum: b }] });
        await requestEdit(edits);
    }, [requestEdit]);

    const startConnect = useCallback(() => {
        const id = selectedRef.current;
        const info = id ? deckRef.current?.elements[id] : null;
        if (!id || !info || ['line', 'connector', 'group'].includes(info.object)) {
            showNotice('연결선을 시작할 개체(도형, 글상자, 그림 등)를 먼저 고르세요');
            return;
        }
        connectFrom.current = id;
        setMode('connect');
        showNotice('연결할 다른 개체를 누르세요. Esc로 그만둡니다');
    }, [showNotice]);

    // 자유형: 그린 점들을 (x, y)에서 잰 path로
    const onDraw = useCallback(async (drawn: Point[] | null) => {
        setMode(null);
        // 두 번 눌러 끝내면 마지막 점이 두 번 들어간다
        const points = (drawn ?? []).filter((point, i, all) => i === 0 || point.x !== all[i - 1].x || point.y !== all[i - 1].y);
        if (points.length < 2) {
            return;
        }
        const x = Math.min(...points.map((point) => point.x));
        const y = Math.min(...points.map((point) => point.y));
        const path = points.map((point, i) => `${i === 0 ? 'M' : 'L'} ${point.x - x} ${point.y - y}`).join(' ');
        const current = deckRef.current;
        const before = new Set(Object.keys(current?.elements ?? {}).filter((id) => id.startsWith(`${pageRef.current + 1}/`)));
        newElement.current = { page: pageRef.current, before };
        if (!await requestEdit([{ op: 'insert', page: pageRef.current + 1, object: 'freeform', properties: [{ name: 'x', length: x }, { name: 'y', length: y }, { name: 'path', string: path }] }])) {
            newElement.current = null;
        }
    }, [requestEdit]);

    // 슬라이드 쇼는 따로 띄운 창에서 templide.js(reveal.js)로 보여 준다. 창이 준비되면 덱을 넘긴다
    const startShow = useCallback(async (fromStart: boolean) => {
        const current = deckRef.current;
        if (!current) {
            return;
        }
        const start = fromStart ? 0 : pageRef.current;
        // run(...) 동작이 부르는 함수. 저장된 파일을 읽어 쇼 창에서 덱보다 먼저 실행한다
        let script: string | null = null;
        if (current.script) {
            try {
                script = await invoke<string>('read_text', { path: current.script });
            } catch (error) {
                showNotice(`script를 읽을 수 없어 run 동작 없이 보여 줍니다: ${current.script}`);
            }
        }
        (await WebviewWindow.getByLabel('show'))?.close();
        const unlisten = await listen('show-ready', () => {
            unlisten();
            emitTo('show', 'show-deck', { deck: current.deck, page: start, script });
        });
        const window = new WebviewWindow('show', { url: 'show.html', title: '슬라이드 쇼', fullscreen: true, focus: true });
        window.once('tauri://error', (event) => {
            unlisten();
            showNotice(`슬라이드 쇼 창을 열 수 없습니다: ${JSON.stringify(event.payload)}`);
        });
    }, [showNotice]);

    // 고른 target의 파일을 저장하지 않은 내용으로 만든다
    const exportTarget = useCallback(async () => {
        if (!uri.current) {
            return;
        }
        flushChange();
        const result = await lsp.request<{ path?: string; errors?: string[]; error?: string; warnings?: string[] }>('templide/build', { uri: uri.current, target: targetRef.current });
        // 경고는 "파일:줄:칸: warning: 내용"이다. 토스트에는 "줄 N: 내용"으로 보인다
        const warnings = (result.warnings ?? []).map((line) => {
            const match = /^.*:(\d+):\d+: warning: (.*)$/.exec(line);
            return match ? `줄 ${match[1]}: ${match[2]}` : line;
        });
        if (result.error || result.errors?.length) {
            setToast({ message: result.error ?? result.errors!.join('\n'), error: true, warnings });
        } else {
            setToast({ message: `만들었습니다: ${result.path}`, path: result.path, warnings });
        }
        return result;
    }, [flushChange]);

    // 새 .tlide를 만들고 연다
    const newFile = useCallback(async () => {
        const file = await saveDialog({ title: '새 발표 자료', defaultPath: '새 발표.tlide', filters: [{ name: 'templide', extensions: ['tlide'] }] });
        if (!file) {
            return;
        }
        const name = file.split(/[\\/]/).pop()!.replace(/\.tlide$/i, '');
        const content = [
            '#include <std/stddef>',
            '',
            'slide {',
            '    put text_box { text = "제목을 입력하세요"; x = 80px; y = 280px; width = 1120px; height = 120px; anchor = middle; }',
            '}',
            '',
            `target out { path = "${name.replace(/"/g, '')}.pptx"; type = pptx; }`,
            '',
        ].join('\n');
        await invoke('write_text', { path: file, text: content });
        onOpenRef.current(file);
    }, []);

    // 복사한 요소를 지금 slide에 붙인다. 같은 slide면 조금 옮긴다. 잘라 낸 것은 원래 자리 그대로 붙인다
    const paste = useCallback(async (source: Clip | null) => {
        if (!source || !deckRef.current) {
            return;
        }
        if (source.kind === 'cut') {
            const before = new Set(Object.keys(deckRef.current.elements).filter((id) => id.startsWith(`${pageRef.current + 1}/`)));
            newElement.current = { page: pageRef.current, before };
            if (!await requestEdit([{ op: 'paste', page: pageRef.current + 1, text: source.text }])) {
                newElement.current = null;
            }
            return;
        }
        if (!deckRef.current.elements[source.id]) {
            showNotice('복사한 요소를 찾을 수 없습니다. 코드가 바뀌어 다시 복사해야 합니다');
            return;
        }
        const offset = source.page === pageRef.current ? 20 : 0;
        const before = new Set(Object.keys(deckRef.current.elements).filter((id) => id.startsWith(`${pageRef.current + 1}/`)));
        newElement.current = { page: pageRef.current, before };
        if (!await requestEdit([{ op: 'copy', id: source.id, page: pageRef.current + 1, dx: offset, dy: offset }])) {
            newElement.current = null;
        }
    }, [requestEdit, showNotice]);

    // 고른 요소를 만든 put 문장의 원문을 기억하고 지운다
    const cut = useCallback(async () => {
        const id = selectedRef.current;
        const info = id ? deckRef.current?.elements[id] : null;
        const model = editor.current?.getModel();
        if (!id || !info || !model) {
            return;
        }
        if (info.source.kind !== 'block' || !info.source.range || info.source.uri !== uri.current) {
            showNotice('이 요소는 잘라 낼 수 없습니다. 코드에서 고쳐 주세요');
            return;
        }
        const text = model.getValueInRange(toMonaco(info.source.range));
        if (await requestEdit([{ op: 'delete', id }])) {
            clipboard.current = { kind: 'cut', text };
            setSelected(null);
            showNotice(info.fromTemplate ? '템플릿을 넣은 put 전체를 잘라 냈습니다. Ctrl+V로 붙입니다' : '잘라 냈습니다. Ctrl+V로 붙입니다');
        }
    }, [requestEdit, showNotice]);

    const order = useCallback((direction: string) => {
        if (selectedRef.current) {
            requestEdit([{ op: 'order', id: selectedRef.current, direction }]);
        }
    }, [requestEdit]);

    // slide 추가, 복제, 삭제, 순서. 끝나면 보던 slide를 따라간다
    const slideAction = useCallback(async (action: 'add' | 'duplicate' | 'delete' | 'up' | 'down', current = pageRef.current) => {
        nextPage.current = action === 'add' || action === 'duplicate' || action === 'down' ? current + 1 : Math.max(0, current - 1);
        if (await requestEdit([{ op: 'slide', action, page: current + 1 }])) {
            setSelected(null);
        } else {
            nextPage.current = null;
        }
    }, [requestEdit]);

    // 썸네일을 끌어 놓은 자리로 옮긴다. to는 옮긴 뒤의 자리 (0부터)
    const moveSlide = useCallback(async (from: number, to: number) => {
        if (from === to) {
            return;
        }
        nextPage.current = to;
        if (await requestEdit([{ op: 'slide', action: 'move', page: from + 1, to: to + 1 }])) {
            setSelected(null);
        } else {
            nextPage.current = null;
        }
    }, [requestEdit]);

    const remove = useCallback(async () => {
        const id = selectedRef.current;
        if (!id) {
            return;
        }
        if (deckRef.current?.elements[id]?.fromTemplate) {
            showNotice('템플릿이 만든 요소라 그 템플릿을 넣은 put 전체를 지웁니다');
        }
        if (await requestEdit([{ op: 'delete', id }])) {
            setSelected(null);
        }
    }, [requestEdit, showNotice]);

    // 서식을 넣을 run들. 글자를 고치는 중이면 그 run의 선택 범위, 아니면 고른 요소의 모든 글자
    const styleTargets = useCallback(() => {
        const run = editing.current;
        if (run) {
            if (run.node.innerText.replace(/\r/g, '') !== run.original) {
                showNotice('먼저 Enter로 글자 수정을 끝낸 뒤 서식을 넣어 주세요');
                return null;
            }
            const range = selectionIn(run.node);
            const whole = !range || (range.start === 0 && range.end >= run.original.length);
            run.cancel();
            const origin = deckRef.current?.elements[run.id]?.text?.[run.paragraph]?.[run.run];
            return { id: run.id, runs: [{ p: run.paragraph, r: run.run, origin, range: whole ? null : range }] };
        }
        const id = selectedRef.current;
        const info = id ? deckRef.current?.elements[id] : null;
        const runs = (info?.text ?? []).flatMap((paragraph, p) => paragraph.map((origin, r) => ({ p, r, origin, range: null as { start: number; end: number } | null })))
            .filter(({ origin }) => origin.kind === 'literal' && origin.text);
        if (!id || runs.length === 0) {
            showNotice('서식을 넣을 수 있는 글자가 없습니다. 글자를 두 번 눌러 고르거나, 코드에서 고쳐 주세요');
            return null;
        }
        return { id, runs };
    }, [showNotice]);

    // 켜고 끄는 서식은 고른 글자가 모두 이미 켜져 있으면 끈다 (일부만 고른 글자는 켠다)
    const applyStyle = useCallback(async (properties: StyleProperty[], toggle?: Toggle) => {
        const targets = styleTargets();
        if (!targets) {
            return;
        }
        let chosen = properties;
        if (toggle && targets.runs.every(({ range }) => !range)) {
            const el = findElement(deckRef.current?.deck.slides[pageRef.current]?.els ?? [], targets.id);
            const on = targets.runs.every(({ p, r }) => el?.tx?.ps?.[p]?.rs?.[r]?.[toggle.key]);
            if (on) {
                chosen = [{ ...toggle.off, unset: true }];
            }
        }
        await requestEdit(targets.runs.map(({ p, r, range }) => ({ op: 'style', id: targets.id, paragraph: p, run: r, ...(range ?? {}), properties: chosen })));
    }, [requestEdit, styleTargets]);

    const clearStyle = useCallback(async () => {
        const targets = styleTargets();
        if (!targets) {
            return;
        }
        const styled = targets.runs.filter(({ origin }) => origin?.styled);
        if (styled.length === 0) {
            showNotice('지울 인라인 서식이 없습니다. 이름 있는 style로 넣은 서식은 코드에서 고쳐 주세요');
            return;
        }
        await requestEdit(styled.map(({ p, r }) => ({ op: 'unstyle', id: targets.id, paragraph: p, run: r })));
    }, [requestEdit, styleTargets, showNotice]);

    // 오류는 빨간 밑줄, 고른 target의 경고는 노란 밑줄
    const setMarkers = useCallback((list: Diagnostic[], warningList: Diagnostic[] = warningsRef.current) => {
        const model = editor.current?.getModel();
        if (!monaco.current || !model) {
            return;
        }
        const { MarkerSeverity } = monaco.current.monaco;
        monaco.current.monaco.editor.setModelMarkers(model, 'templide', [
            // 컴파일러의 경고(severity 2, sRGB 밖의 색 등)는 노란 밑줄
            ...list.map((d) => ({ ...toMonaco(d.range), message: d.message, severity: d.severity === 2 ? MarkerSeverity.Warning : MarkerSeverity.Error })),
            ...warningList.map((d) => ({ ...toMonaco(d.range), message: d.message, severity: MarkerSeverity.Warning })),
        ]);
    }, []);

    const warningKey = warnings.map((warning) => `${warning.range.start.line}:${warning.range.start.character}:${warning.message}`).join('\n');
    useEffect(() => {
        setMarkers(diagnosticsRef.current, warningsRef.current);
    }, [warningKey, setMarkers]);

    // 오류나 경고 위치로 코드 커서를 옮긴다
    const revealProblem = useCallback((problem: Diagnostic) => {
        setCodeOpen(true);
        setBottomTab('code');
        const instance = editor.current;
        if (!instance) {
            return;
        }
        const position = { lineNumber: problem.range.start.line + 1, column: problem.range.start.character + 1 };
        instance.setPosition(position);
        instance.revealPositionInCenter(position);
        instance.focus();
    }, []);

    // 파일 내용으로 바꾼다. Ctrl+Z로 되돌릴 수 있게 Monaco의 편집으로 넣는다
    const reloadFromDisk = useCallback((content: string) => {
        diskText.current = content;
        const instance = editor.current;
        const model = instance?.getModel();
        if (!instance || !model) {
            if (pathRef.current) {
                openFileRef.current(pathRef.current);
            }
            return;
        }
        resetIds();
        instance.pushUndoStop();
        model.pushEditOperations([], [{ range: model.getFullModelRange(), text: content }], () => null);
        instance.pushUndoStop();
        setDirty(false);
    }, [resetIds]);

    // AI 탭의 에이전트가 MCP로 코드를 고친다. 한 번의 실행 취소로 되돌릴 수 있고, 다시 컴파일될 때까지 기다린다
    const editDocument = useCallback(async (oldText: string, newText: string, all: boolean) => {
        const instance = editor.current;
        const model = instance?.getModel();
        if (!instance || !model || !uri.current) {
            return { error: 'No document is open in the editor' };
        }
        const eol = model.getEOL();
        const from = oldText.replace(/\r?\n/g, eol);
        const to = newText.replace(/\r?\n/g, eol);
        const value = model.getValue();
        const offsets: number[] = [];
        for (let at = value.indexOf(from); at >= 0; at = value.indexOf(from, at + from.length)) {
            offsets.push(at);
        }
        if (offsets.length === 0) {
            return { error: 'old_string was not found. Check the current source with read_document' };
        }
        if (offsets.length > 1 && !all) {
            return { error: `old_string occurs ${offsets.length} times. Add surrounding text to make it unique, or use replace_all` };
        }
        lastRenamed.current = {};
        instance.pushUndoStop();
        instance.executeEdits('agent', offsets.map((at) => {
            const start = model.getPositionAt(at);
            const end = model.getPositionAt(at + from.length);
            return { range: { startLineNumber: start.lineNumber, startColumn: start.column, endLineNumber: end.lineNumber, endColumn: end.column }, text: to, forceMoveMarkers: true };
        }));
        instance.pushUndoStop();
        window.clearTimeout(pending.current);
        await sendChange();
        await new Promise((done) => setTimeout(done, 100)); // 오류 알림과 화면 갱신을 기다린다
        return { count: offsets.length, renamed: lastRenamed.current };
    }, [sendChange]);

    const save = useCallback(async (): Promise<boolean> => {
        if (!pathRef.current || !editor.current) {
            return false;
        }
        // VS Code처럼, 연 뒤에 다른 프로그램이 파일을 고쳤으면 덮어쓸지 묻는다
        const disk = await invoke<string>('read_text', { path: pathRef.current }).catch(() => null);
        if (disk !== null && diskText.current !== null && disk !== diskText.current) {
            const choice = await message('이 파일이 편집기 밖에서 바뀌었습니다. 편집기의 내용으로 덮어쓸까요?', {
                title: '파일이 바뀌었습니다', kind: 'warning', buttons: { yes: '덮어쓰기', no: '파일 내용으로 되돌리기', cancel: '취소' },
            });
            if (choice === 'No' || choice === '파일 내용으로 되돌리기') {
                reloadFromDisk(disk);
                return false;
            }
            if (choice !== 'Yes' && choice !== '덮어쓰기') {
                return false;
            }
        }
        const content = editor.current.getValue();
        await invoke('write_text', { path: pathRef.current, text: content });
        diskText.current = content;
        setDirty(false);
        return true;
    }, [reloadFromDisk]);

    const loadMonaco = useCallback(async () => {
        if (monaco.current) {
            return;
        }
        monaco.current = await import('./monaco');
        const instance = monaco.current.monaco.editor.create(codeHost.current!, {
            value: textRef.current,
            language: 'tlide',
            automaticLayout: true,
            fontFamily: 'Consolas, "Malgun Gothic", monospace',
            fontSize: 14,
            minimap: { enabled: false },
            theme: 'templide',
        });
        editor.current = instance;
        instance.onDidChangeModelContent(() => {
            setDirty(true);
            window.clearTimeout(pending.current);
            // 입력이 잠깐 멈추면 저장하지 않은 내용으로 다시 컴파일한다
            pending.current = window.setTimeout(sendChange, immediate.current ? 0 : 50);
            immediate.current = false;
        });
        // 커서가 있는 slide를 보여 주고, 커서가 put 문장 안에 있으면 그 요소를 캔버스에서 고른다.
        // 캔버스에서 골라 코드를 선택한 것과 편집으로 커서가 밀린 것(modelChange)은 다시 캔버스로 돌려보내지 않는다
        instance.onDidChangeCursorPosition((event) => {
            if (selectingFromCanvas.current || event.source === 'modelChange') {
                return;
            }
            const current = deckRef.current;
            const line = event.position.lineNumber - 1;
            const character = event.position.column - 1;
            const slides = current?.slides ?? [];
            const index = slides.findIndex((slide) => slide.source.range && contains(slide.source.range, line, character));
            if (index >= 0) {
                setPage(index);
            }
            // 커서를 감싼 put 문장 가운데 가장 작은 것
            let best: string | null = null;
            let bestSize = Infinity;
            for (const [id, info] of Object.entries(current?.elements ?? {})) {
                const range = info.source.range;
                if (info.source.kind !== 'block' || !range || info.source.uri !== uri.current || !contains(range, line, character)) {
                    continue;
                }
                const size = (range.end.line - range.start.line) * 100000 + (range.end.character - range.start.character);
                if (size < bestSize) {
                    best = id;
                    bestSize = size;
                }
            }
            if (best) {
                setPage(Number(best.split(/[/#]/)[0]) - 1);
            }
            setSelected(best);
        });
        // addCommand는 마지막에 만든 편집기에 걸리므로, 탭마다 있는 편집기에는 그 편집기에만 붙는 동작으로 둔다
        instance.addAction({ id: 'templide.save', label: '저장', keybindings: [monaco.current.monaco.KeyMod.CtrlCmd | monaco.current.monaco.KeyCode.KeyS], run: () => { save(); } });
        detachDocument.current = attachDocument(monaco.current.monaco, lsp, instance, {
            uri: () => uri.current,
            flush: flushChange,
            // ▾ 아래에 폰트 목록을 연다
            pickFont: (range) => {
                const at = instance.getScrolledVisiblePosition({ lineNumber: range.endLineNumber, column: range.endColumn + 2 });
                const box = instance.getDomNode()?.getBoundingClientRect();
                const model = instance.getModel();
                if (at && box && model) {
                    setFontPopup({ x: box.left + at.left, y: box.top + at.top + at.height + 2, range, current: model.getValueInRange(range) });
                }
            },
        });
        setMonacoReady(true);
        setMarkers(diagnosticsRef.current);
        if (deckRef.current && deckVersion.current === version.current) {
            retrackIds(deckRef.current); // 덱이 Monaco보다 먼저 나왔으면 여기서 id 추적을 시작한다
        }
        await mark('monaco');
    }, [sendChange, flushChange, setMarkers, mark, save, retrackIds]);

    const openFile = useCallback(async (file: string) => {
        const content = await invoke<string>('read_text', { path: file });
        if (uri.current) {
            lsp.notify('textDocument/didClose', { textDocument: { uri: uri.current } });
        }
        window.clearTimeout(pending.current);
        pending.current = 0;
        resetIds();
        uri.current = pathToUri(file);
        version.current = 1;
        firstSlide.current = false;
        setPath(file);
        setText(content);
        setPage(0);
        setSelected(null);
        targetRef.current = null;
        setTarget(null);
        lsp.notify('textDocument/didOpen', { textDocument: { uri: uri.current, languageId: 'tlide', version: 1, text: content } });
        if (editor.current) {
            // setValue도 내용 변경이므로 바로 보낼 didChange를 막는다
            editor.current.setValue(content);
            window.clearTimeout(pending.current);
            pending.current = 0;
        }
        setDirty(false);
        diskText.current = content;
        invoke('watch_file', { path: file }).catch((error) => console.error('watch_file', error));
        await refreshDeck();
    }, [refreshDeck, resetIds]);

    openFileRef.current = openFile;

    // 고른 요소의 put 문장 시작에 장식을 둔다
    useEffect(() => {
        const model = editor.current?.getModel();
        if (!model) {
            return;
        }
        const info = selected ? deck?.elements[selected] : null;
        const start = info?.source.uri === uri.current ? info?.source.range?.start : undefined;
        const old = anchor.current?.decorations ?? [];
        if (!selected || !start) {
            anchor.current = { decorations: model.deltaDecorations(old, []), head: '', rest: '' };
            return;
        }
        const parts = selected.split('/');
        const decorations = model.deltaDecorations(old, [{
            range: { startLineNumber: start.line + 1, startColumn: start.character + 1, endLineNumber: start.line + 1, endColumn: start.character + 2 },
            options: { stickiness: 1 }, // 가장자리에 글자를 넣어도 늘어나지 않는다
        }]);
        anchor.current = { decorations, head: parts[0], rest: parts.slice(2).join('/') };
    }, [selected, deck, monacoReady]);

    // 시작: 탭의 파일을 열고, 첫 슬라이드를 그린 뒤에 Monaco를 불러온다. 컴파일러 연결, MCP, 창 닫기 확인은 창(Shell)이 한다
    useEffect(() => {
        const offDiagnostics = lsp.onNotification('textDocument/publishDiagnostics', (params) => {
            if (params.uri === uri.current) {
                setDiagnostics(params.diagnostics);
                setMarkers(params.diagnostics);
            }
        });
        // 연 파일이 밖에서 바뀌면: 저장하지 않은 변경이 없으면 다시 읽고, 있으면 알리기만 한다 (저장할 때 묻는다)
        let changeTimer = 0;
        const offChanged = listen<string>('file-changed', ({ payload }) => {
            if (payload !== pathRef.current) {
                return;
            }
            window.clearTimeout(changeTimer);
            changeTimer = window.setTimeout(async () => {
                const content = await invoke<string>('read_text', { path: payload }).catch(() => null);
                if (content === null || content === diskText.current) {
                    return;
                }
                if (dirtyRef.current) {
                    showNotice('파일이 편집기 밖에서 바뀌었습니다. 저장할 때 어떻게 할지 묻습니다');
                } else {
                    reloadFromDisk(content);
                }
            }, 200);
        });
        (async () => {
            if (file) {
                await openFile(file);
            } else {
                await mark('window');
                loadMonaco();
            }
        })().catch((error) => setDeckError(String(error)));
        // 탭을 닫으면 컴파일러에 알리고 파일 감시와 편집기를 정리한다
        return () => {
            offDiagnostics();
            offChanged.then((off) => off());
            window.clearTimeout(changeTimer);
            window.clearTimeout(pending.current);
            if (uri.current) {
                lsp.notify('textDocument/didClose', { textDocument: { uri: uri.current } });
            }
            if (pathRef.current) {
                invoke('unwatch_file', { path: pathRef.current }).catch(() => {});
            }
            detachDocument.current?.();
            editor.current?.dispose();
            accesses.delete(tab);
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    // 다른 탭으로 가면 자유형 그리기를 그만둔다 (그리기는 창 전체의 키를 듣는다)
    useEffect(() => {
        if (!active) {
            setMode((current) => current === 'draw' ? null : current);
        }
    }, [active]);

    // 캔버스에서 고친 것도 코드의 실행 취소 기록에 있으므로, 포커스가 코드 밖에 있어도 Ctrl+Z, Ctrl+Y는 Monaco로 보낸다
    useEffect(() => {
        const keydown = (event: KeyboardEvent) => {
            const target = event.target as HTMLElement;
            if (!activeRef.current || !editor.current || target.closest('.code-host, input, select, textarea, [contenteditable="true"]')) {
                return;
            }
            if (event.key === 'F5') {
                event.preventDefault();
                startShow(!event.shiftKey);
                return;
            }
            if (event.key === 'Escape') {
                connectFrom.current = null;
                setMode((current) => current === 'connect' ? null : current);
                setPreview(null);
            }
            if (event.key === 'Delete' && !event.ctrlKey && !event.metaKey) {
                event.preventDefault();
                // 썸네일을 눌러 슬라이드 목록에 포커스가 있으면 그 슬라이드를 지운다
                if (target.closest('.thumbs')) {
                    slideAction('delete');
                } else {
                    remove();
                }
                return;
            }
            if (!(event.ctrlKey || event.metaKey)) {
                return;
            }
            const key = event.key.toLowerCase();
            const redo = key === 'y' || (key === 'z' && event.shiftKey);
            if (key === 'z' || redo) {
                event.preventDefault();
                editor.current.trigger('canvas', redo ? 'redo' : 'undo', null);
            } else if (key === 's') {
                event.preventDefault();
                save();
            } else if (key === 'c' && selectedRef.current) {
                event.preventDefault();
                clipboard.current = { kind: 'copy', id: selectedRef.current, page: pageRef.current };
                showNotice('요소를 복사했습니다. Ctrl+V로 붙입니다');
            } else if (key === 'x' && selectedRef.current) {
                event.preventDefault();
                cut();
            } else if (key === 'v') {
                event.preventDefault();
                paste(clipboard.current);
            } else if (key === 'd' && selectedRef.current) {
                event.preventDefault();
                paste({ kind: 'copy', id: selectedRef.current, page: pageRef.current });
            } else if (key === '0') {
                event.preventDefault();
                setZoom(null);
            }
        };
        window.addEventListener('keydown', keydown);
        return () => window.removeEventListener('keydown', keydown);
    }, [save, remove, slideAction, startShow, paste, cut, showNotice]);

    // 고른 파일마다 탭을 연다. .tasset은 묶음 보기 탭으로 열린다
    const pickFile = useCallback(async () => {
        const files = await openDialog({ multiple: true, filters: [{ name: 'templide (.tlide, .tasset)', extensions: ['tlide', 'tasset'] }] });
        for (const each of Array.isArray(files) ? files : typeof files === 'string' ? [files] : []) {
            onOpenRef.current(each);
        }
    }, []);

    // 요소를 누르면 그 요소를 만든 put을 코드에서 보여 준다
    const select = useCallback((id: string | null) => {
        if (connectFrom.current) {
            const from = connectFrom.current;
            connectFrom.current = null;
            setMode(null);
            if (id && id !== from) {
                connect(from, id);
            }
            return;
        }
        setSelected(id);
        const info = id ? deckRef.current?.elements[id] : null;
        if (info && info.source.range && editor.current) {
            const range = toMonaco(info.source.range);
            selectingFromCanvas.current = true;
            editor.current.setSelection(range);
            editor.current.revealRangeInCenterIfOutsideViewport(range);
            selectingFromCanvas.current = false;
        }
    }, [connect]);

    const onFirstPaint = useCallback(() => {
        if (!firstSlide.current) {
            firstSlide.current = true;
            mark('first-slide').then(loadMonaco);
        }
    }, [mark, loadMonaco]);

    // 그릴 슬라이드가 없거나(슬라이드가 없는 문서) 처음부터 코드에 오류가 있어 덱이 없으면 첫 그림을 기다리지 않고 코드 편집기를 띄운다
    useEffect(() => {
        if ((deck && deck.deck.slides.length === 0) || (!deck && deckError)) {
            onFirstPaint();
        }
    }, [deck, deckError, onFirstPaint]);

    const slideCount = deck?.deck.slides.length ?? 0;
    const errors = diagnostics.filter((diagnostic) => diagnostic.severity !== 2);
    const compileWarnings = diagnostics.filter((diagnostic) => diagnostic.severity === 2); // sRGB 밖의 색 등
    const errorCount = errors.length;
    const warningCount = compileWarnings.length + warnings.length;
    const ready = !!deck && monacoReady;
    const textSelected = !!selected && !!deck?.elements[selected]?.text;
    const fileName = path ? path.split(/[\\/]/).pop() : null;
    const undo = (redo: boolean) => editor.current?.trigger('toolbar', redo ? 'redo' : 'undo', null);
    mcpAccess.current = {
        path,
        dirty,
        get text() { return editor.current?.getValue() ?? null; },
        deck,
        stale: !!deckError,
        page: Math.min(page, Math.max(slideCount - 1, 0)),
        selected,
        diagnostics,
        warnings,
        edit: editDocument,
        goto: setPage,
        select: (id) => {
            setPage(Number(id.split(/[/#]/)[0]) - 1);
            setSelected(id);
        },
        build: exportTarget,
        resolve: (id) => deck?.elements[id] ? id : idAliases.current.get(id) ?? null,
    };
    accesses.set(tab, mcpAccess.current);
    // 손잡이는 처음 한 번 넘기므로 지금 함수를 ref로 읽는다
    const latest = useRef({ save, flushChange, applyEdits, revealProblem, refreshDeck });
    latest.current = { save, flushChange, applyEdits, revealProblem, refreshDeck };
    useEffect(() => {
        register({
            path: () => pathRef.current,
            uri: () => uri.current,
            dirty: () => dirtyRef.current,
            save: () => latest.current.save(),
            flush: () => latest.current.flushChange(),
            edit: (edits) => !!editor.current && !!uri.current && latest.current.applyEdits(edits.map((edit) => ({ uri: uri.current!, ...edit }))),
            reveal: (range) => latest.current.revealProblem({ range, message: '', severity: 1 }),
            refresh: () => { latest.current.refreshDeck(); },
        });
        return () => register(null);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);
    useEffect(() => {
        onInfoRef.current({ path, dirty });
    }, [path, dirty]);
    const folder = path ? path.replace(/[\\/][^\\/]*$/, '') : null; // AI 탭의 에이전트를 실행하는 폴더
    // 다른 탭을 누르면 그 탭을 펼치고, 보이는 탭을 다시 누르면 패널을 접는다
    const openTab = (tab: BottomTab) => {
        if (tab === bottomTab) {
            setCodeOpen((open) => !open);
        } else {
            setBottomTab(tab);
            setCodeOpen(true);
        }
    };
    return (
        <div className={'app' + (codeOpen ? '' : ' code-closed') + (paneOpen ? ' pane-open' : '')} style={{
            '--slides-w': `${sizes.slides}px`, '--props-w': propsOpen ? `${sizes.props}px` : '0px', '--pane-w': `${sizes.pane}px`, '--code-h': `${sizes.code}px`,
        } as CSSProperties}>
            {/* 빈 곳을 끌면 창이 움직이고, 두 번 누르면 최대화한다. 로고와 창 버튼은 위의 탭 줄(Shell)에 있다 */}
            <header className="toolbar" data-tauri-drag-region>
                <span className="tool-group">
                    <IconButton icon={FilePlus} title="새 파일" onClick={newFile} />
                    <IconButton icon={FolderOpen} title="열기" onClick={pickFile} />
                    <IconButton icon={Save} title="저장 (Ctrl+S)" onClick={save} disabled={!path || !dirty} />
                    <IconButton icon={FileOutput} title="내보내기 (만들 target 고르기)" disabled={!deck} onClick={() => setExportOpen(true)} />
                </span>
                <span className="separator" />
                {/* 글자가 있는 개체를 고르면 서식 막대를, 아니면 넣기 버튼을 보여 준다 */}
                {textSelected ? <FormatBar enabled={monacoReady} onStyle={applyStyle} onClear={clearStyle} /> : <span className="tool-group">
                    <IconButton icon={Type} title="글상자 넣기" disabled={!ready} onClick={() => insert('text_box')} />
                    <IconButton icon={Square} title="도형 넣기" disabled={!ready} onClick={() => insert('shape')} />
                    <IconButton icon={ImageIcon} title="그림 넣기" disabled={!ready} onClick={insertImage} />
                    <IconButton icon={Film} title="비디오 넣기 (mp4, webm)" disabled={!ready} onClick={insertVideo} />
                    <InsertMenu disabled={!ready} items={[
                        { icon: Music, label: '오디오', title: 'mp3, wav, m4a', onClick: insertAudio },
                        { icon: Slash, label: '선', title: '가운데에 가로선', onClick: insertLine },
                        { icon: Spline, label: '연결선', title: '고른 개체에서 다음에 누르는 개체로 잇습니다', onClick: startConnect, disabled: !selected },
                        { icon: PenTool, label: '자유형 그리기', title: '누를 때마다 점, 두 번 눌러 끝내기, Esc 취소', onClick: () => setMode('draw') },
                        { icon: Blend, label: '배경 흐림', title: '뒤의 배경을 흐리게 비추는 유리 효과', onClick: () => insertObject('backdrop', 300, 200, [{ name: 'kind', enum: 'roundRect' }, { name: 'blur', length: 16 }]) },
                    ]} active={mode !== null} />
                </span>}
                <span className="separator" />
                <span className="tool-group">
                    <IconButton icon={Undo2} title="실행 취소 (Ctrl+Z)" disabled={!monacoReady} onClick={() => undo(false)} />
                    <IconButton icon={Redo2} title="다시 실행 (Ctrl+Y)" disabled={!monacoReady} onClick={() => undo(true)} />
                    <IconButton icon={Trash2} title="선택한 요소 지우기 (Delete)" disabled={!selected || !monacoReady} onClick={remove} danger />
                </span>
                <span className="spacer" data-tauri-drag-region />
                {/* 오른쪽 패널은 고른 개체가 있으면 개체 설정, 없으면 슬라이드 설정을 보인다 */}
                <IconButton icon={propsOpen ? PanelRightClose : PanelRightOpen} title={propsOpen ? '오른쪽 패널 닫기' : '오른쪽 패널 열기'} onClick={() => setPropsOpen(!propsOpen)} />
                <IconButton icon={Sparkles} title="애니메이션 창" disabled={!deck} active={paneOpen} onClick={() => setPaneOpen(!paneOpen)} />
                <TargetPicker deck={deck} target={target} onTarget={(name) => { targetRef.current = name; setTarget(name); refreshDeck(); }} />
                <button className="play-button" disabled={!deck} title="슬라이드 쇼 (F5: 처음부터, Shift+F5: 지금 슬라이드부터)" onClick={() => startShow(false)}>
                    <Play size={14} fill="currentColor" /> 슬라이드 쇼
                </button>
            </header>
            <nav className="slides">
                <SlideList deck={deck} count={slideCount} page={Math.min(page, slideCount - 1)} ready={ready} onPage={setPage} onAction={slideAction} onMove={moveSlide} />
            </nav>
            <main className="canvas-area">
                {deck && slideCount > 0 && (
                    <SlideView deck={deck} index={Math.min(page, slideCount - 1)} selected={selected} editable={monacoReady && !deckError} editing={editing}
                        zoom={zoom} onZoom={(next, fit) => { setFitScale(fit); setZoom(next); }}
                        onSelect={select} onPaint={onFirstPaint} onMove={onMove} onResize={onResize} onText={onText} onNotice={showNotice} stale={!!deckError}
                        preview={preview} onPreviewEnd={stopPreview} drawing={mode === 'draw'} onDraw={onDraw} />
                )}
                {deck && (
                    <div className="zoom-control">
                        <IconButton icon={Minus} title="축소" onClick={() => setZoom(Math.max(0.1, (zoom ?? fitScale) / 1.25))} />
                        <button className="zoom-value" title="화면에 맞춤 (Ctrl+0)" onClick={() => setZoom(null)}>{zoom === null ? '맞춤' : `${Math.round(zoom * 100)}%`}</button>
                        <IconButton icon={Plus} title="확대" onClick={() => setZoom(Math.min(4, (zoom ?? fitScale) * 1.25))} />
                        <IconButton icon={Maximize} title="화면에 맞춤" onClick={() => setZoom(null)} />
                    </div>
                )}
                {exportOpen && uri.current && (
                    <ExportDialog fileName={fileName ?? ''}
                        load={async () => {
                            flushChange();
                            return lsp.request<{ targets?: ExportTarget[]; error?: string }>('templide/targets', { uri: uri.current });
                        }}
                        build={(name) => lsp.request<BuildResult>('templide/build', { uri: uri.current, target: name })}
                        onClose={() => setExportOpen(false)}
                        onDone={(results) => {
                            // 창을 닫고 만든 파일을 알린다. 하나면 열기, 폴더에서 보기 버튼도 보인다
                            setExportOpen(false);
                            const paths = results.map((result) => result.path!);
                            const warnings = results.flatMap((result) => result.warnings ?? []).map((line) => {
                                const match = /^.*:(\d+):\d+: warning: (.*)$/.exec(line);
                                return match ? `줄 ${match[1]}: ${match[2]}` : line;
                            });
                            setToast({ message: `만들었습니다: ${paths.join(', ')}`, path: paths.length === 1 ? paths[0] : undefined, warnings });
                        }} />
                )}
                {toast && (
                    <div className={'toast' + (toast.error ? ' error' : '')}>
                        <span className="toast-message">
                            {toast.message}
                            {toast.warnings && toast.warnings.length > 0 && (
                                <span className="toast-warnings">
                                    <span className="toast-warnings-title"><TriangleAlert size={13} /> 이 target에서 빠진 것 {toast.warnings.length}개</span>
                                    {toast.warnings.map((warning, i) => <span key={i} className="toast-warning">{warning}</span>)}
                                </span>
                            )}
                        </span>
                        {toast.path && <button onClick={() => invoke('open_path', { path: toast.path, reveal: false })}>열기</button>}
                        {toast.path && <button onClick={() => invoke('open_path', { path: toast.path, reveal: true })}>폴더에서 보기</button>}
                        <IconButton icon={X} title="닫기" onClick={() => setToast(null)} />
                    </div>
                )}
            </main>
            {!propsOpen ? null : deck && selected && deck.elements[selected]
                ? <ElementPanel key={selected} deck={deck} schema={schema} page={Math.min(page, slideCount - 1)} id={selected} info={deck.elements[selected]} pickers={pickers}
                    readSource={readSource} onSet={onSetProperty} onUnset={onUnsetProperty} onOrder={order} onAnimation={onAnimation} onPreview={() => startPreview(false)} />
                : deck && slideCount > 0
                    ? <SlidePanel deck={deck} schema={schema} page={Math.min(page, slideCount - 1)} pickers={pickers} readSource={readSource}
                        onSlide={onSlide} onDocument={onDocument} onPreview={startPreview} />
                    : <aside className="props"><div className="props-empty">파일을 열면<br />여기서 속성을 바꿀 수 있습니다</div></aside>}
            {paneOpen && deck && slideCount > 0 && (
                <AnimationPane deck={deck} schema={schema} page={Math.min(page, slideCount - 1)} selected={selected} playing={!!preview}
                    onSelect={select} onOp={onAnimation} onPreview={() => startPreview(false)} onStop={stopPreview} onClose={() => setPaneOpen(false)} />
            )}
            <section className="code">
                <header className="code-header">
                    <button className={'code-tab' + (bottomTab === 'code' ? ' active' : '')} onClick={() => openTab('code')}>
                        <CodeXml size={15} /> 코드
                    </button>
                    <button className={'code-tab' + (bottomTab === 'ai' ? ' active' : '')} onClick={() => openTab('ai')}>
                        <Bot size={15} /> AI
                    </button>
                    <button className={'code-tab' + (bottomTab === 'problems' ? ' active' : '')} onClick={() => openTab('problems')}>
                        {errorCount > 0
                            ? <span className="error-count">{errorCount}</span>
                            : warningCount > 0
                                ? <span className="warning-count">{warningCount}</span>
                                : <CircleCheck size={15} className="problems-ok" />}
                        문제
                    </button>
                    <span className="spacer" />
                    <span className="code-status">
                        {notice
                            ? <span className="notice"><CircleAlert size={13} /> {notice}</span>
                            : deckError && deckError !== 'The document has errors' && <span className="error"><CircleAlert size={13} /> {deckError}</span>}
                        {deck?.warnings.length ? <span className="warning" title={deck.warnings.join('\n')}>html 미리보기 경고 {deck.warnings.length}개</span> : null}
                        <span className="timings">{timings.join(' · ')}</span>
                    </span>
                    <IconButton icon={codeOpen ? PanelBottomClose : PanelBottomOpen} title={codeOpen ? '패널 접기' : '패널 펼치기'} onClick={() => setCodeOpen((open) => !open)} />
                </header>
                <div className="code-body">
                    <div ref={codeHost} className="code-host" style={{ display: monacoReady && bottomTab === 'code' ? 'block' : 'none' }} />
                    {!monacoReady && bottomTab === 'code' && <pre className="code-placeholder">{text}</pre>}
                    <AgentPanel visible={codeOpen && bottomTab === 'ai'} folder={folder} session={tab} />
                    {bottomTab === 'problems' && (
                        <div className="problems">
                            {errorCount === 0 && warningCount === 0 && <div className="problems-empty"><CircleCheck size={13} /> 오류 없음</div>}
                            {errors.map((diagnostic, i) => (
                                <button key={'e' + i} className="problems-item error" onClick={() => revealProblem(diagnostic)}>
                                    <CircleAlert size={13} />
                                    <span className="problems-message">{diagnostic.message}</span>
                                    <span className="problems-line">줄 {diagnostic.range.start.line + 1}</span>
                                </button>
                            ))}
                            {compileWarnings.map((warning, i) => (
                                <button key={'c' + i} className="problems-item warning" onClick={() => revealProblem(warning)}>
                                    <TriangleAlert size={13} />
                                    <span className="problems-message">{warning.message}</span>
                                    <span className="problems-line">줄 {warning.range.start.line + 1}</span>
                                </button>
                            ))}
                            {warnings.map((warning, i) => (
                                <button key={'w' + i} className="problems-item warning" title={`${deck?.target} target이 빼고 만드는 것`} onClick={() => revealProblem(warning)}>
                                    <TriangleAlert size={13} />
                                    <span className="problems-message">{warning.message}</span>
                                    <span className="problems-line">줄 {warning.range.start.line + 1}</span>
                                </button>
                            ))}
                        </div>
                    )}
                </div>
            </section>
            {fontPopup && (
                <FontPopup x={fontPopup.x} y={fontPopup.y} current={fontPopup.current} onClose={() => setFontPopup(null)} onPick={(name) => {
                    const instance = editor.current;
                    setFontPopup(null);
                    if (instance) {
                        instance.pushUndoStop();
                        instance.executeEdits('font', [{ range: fontPopup.range, text: quote(name).slice(1, -1) }]);
                        instance.pushUndoStop();
                        instance.focus();
                    }
                }} />
            )}
            {/* 패널 경계. 끌면 크기가 바뀌고, 캔버스에는 적어도 200px을 남긴다 */}
            <Resizer area="slides" edge="right" value={sizes.slides} min={120}
                max={() => window.innerWidth - (propsOpen ? sizes.props : 0) - (paneOpen ? sizes.pane : 0) - 200} onChange={(slides) => setSizes((old) => ({ ...old, slides }))} onEnd={storeSizes} />
            {propsOpen && (
                <Resizer area="props" edge="left" value={sizes.props} min={200}
                    max={() => window.innerWidth - sizes.slides - (paneOpen ? sizes.pane : 0) - 200} onChange={(props) => setSizes((old) => ({ ...old, props }))} onEnd={storeSizes} />
            )}
            {paneOpen && deck && slideCount > 0 && (
                <Resizer area="pane" edge="left" value={sizes.pane} min={200}
                    max={() => window.innerWidth - sizes.slides - (propsOpen ? sizes.props : 0) - 200} onChange={(pane) => setSizes((old) => ({ ...old, pane }))} onEnd={storeSizes} />
            )}
            {codeOpen && (
                <Resizer area="code" edge="top" value={sizes.code} min={80}
                    max={() => window.innerHeight - 248} onChange={(code) => setSizes((old) => ({ ...old, code }))} onEnd={storeSizes} />
            )}
        </div>
    );
}

type BottomTab = 'code' | 'ai' | 'problems';

// 패널 크기. 다음에 열 때도 쓰도록 이 컴퓨터에 적어 둔다

type Sizes = { slides: number; props: number; pane: number; code: number };
const sizesKey = 'templide.panel-sizes';

function loadSizes(): Sizes {
    const defaults = { slides: 184, props: 256, pane: 268, code: Math.max(140, Math.round(window.innerHeight * 0.34)) };
    try {
        return { ...defaults, ...JSON.parse(localStorage.getItem(sizesKey) ?? '{}') };
    } catch {
        return defaults;
    }
}

function saveSizes(sizes: Sizes) {
    try {
        localStorage.setItem(sizesKey, JSON.stringify(sizes));
    } catch {
        // 적지 못하면 이번 실행에서만 쓴다
    }
}

// 오른쪽 패널(속성)을 보이는지. 모든 탭이 함께 쓰고, 다음에 열 때도 쓰도록 이 컴퓨터에 적어 둔다

const propsKey = 'templide.props-open';
let propsOpenValue = (() => {
    try {
        return localStorage.getItem(propsKey) !== 'false';
    } catch {
        return true;
    }
})();
const propsListeners = new Set<() => void>();

function setPropsOpen(open: boolean) {
    propsOpenValue = open;
    try {
        localStorage.setItem(propsKey, String(open));
    } catch {
        // 적지 못하면 이번 실행에서만 쓴다
    }
    propsListeners.forEach((listener) => listener());
}

function usePropsOpen(): boolean {
    return useSyncExternalStore((listener) => {
        propsListeners.add(listener);
        return () => {
            propsListeners.delete(listener);
        };
    }, () => propsOpenValue);
}

// 패널 경계의 손잡이. 패널과 같은 grid 칸에 겹쳐 그 가장자리(edge)에 둔다
function Resizer({ area, edge, value, min, max, onChange, onEnd }: {
    area: string; edge: 'left' | 'right' | 'top'; value: number; min: number; max(): number; onChange(value: number): void; onEnd(): void;
}) {
    const [dragging, setDragging] = useState(false);
    const start = useRef({ at: 0, value: 0 });
    const vertical = edge === 'top';
    const place: CSSProperties = edge === 'right' ? { gridArea: area, justifySelf: 'end', marginRight: -3 }
        : edge === 'left' ? { gridArea: area, justifySelf: 'start', marginLeft: -3 }
        : { gridArea: area, alignSelf: 'start', marginTop: -3 };
    const down = (event: ReactPointerEvent) => {
        if (event.button !== 0) {
            return;
        }
        event.preventDefault();
        event.currentTarget.setPointerCapture(event.pointerId);
        start.current = { at: vertical ? event.clientY : event.clientX, value };
        setDragging(true);
        document.body.classList.add(vertical ? 'resizing-row' : 'resizing-col');
    };
    const move = (event: ReactPointerEvent) => {
        if (!dragging) {
            return;
        }
        // 오른쪽 경계는 오른쪽으로, 왼쪽과 위 경계는 왼쪽과 위로 끌면 커진다
        const delta = (vertical ? event.clientY : event.clientX) - start.current.at;
        const next = Math.round(start.current.value + (edge === 'right' ? delta : -delta));
        onChange(Math.max(min, Math.min(Math.max(min, max()), next)));
    };
    const up = () => {
        if (!dragging) {
            return;
        }
        setDragging(false);
        document.body.classList.remove('resizing-row', 'resizing-col');
        onEnd();
    };
    return <div className={'resizer ' + (vertical ? 'row' : 'col') + (dragging ? ' dragging' : '')} style={place}
        onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} />;
}

// 툴바의 "더 넣기" 메뉴
type InsertItem = { icon: typeof Plus; label: string; title: string; onClick(): void; disabled?: boolean };

function InsertMenu({ items, disabled, active }: { items: InsertItem[]; disabled: boolean; active: boolean }) {
    const [open, setOpen] = useState(false);
    const anchor = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        if (!open) {
            return;
        }
        const down = (event: MouseEvent) => {
            if (!anchor.current?.contains(event.target as Node)) {
                setOpen(false);
            }
        };
        window.addEventListener('mousedown', down);
        return () => window.removeEventListener('mousedown', down);
    }, [open]);
    return (
        <span className="menu-anchor" ref={anchor}>
            <IconButton icon={Plus} title="더 넣기 (오디오, 선, 연결선, 자유형, 배경 흐림)" disabled={disabled} active={open || active} onClick={() => setOpen(!open)} />
            {open && (
                <span className="menu list-menu insert-menu">
                    {items.map(({ icon: Icon, label, title, onClick, disabled: off }) => (
                        <button key={label} title={title} disabled={off} onClick={() => { setOpen(false); onClick(); }}><Icon size={15} /> {label}</button>
                    ))}
                </span>
            )}
        </span>
    );
}

// 왼쪽 슬라이드 목록. 끌어서 순서를 바꾸고, 오른쪽 클릭 메뉴로 옮기기, 복제, 삭제를 한다
type SlideAction = 'add' | 'duplicate' | 'delete' | 'up' | 'down';

function SlideList({ deck, count, page, ready, onPage, onAction, onMove }: {
    deck: DeckResult | null; count: number; page: number; ready: boolean;
    onPage(index: number): void; onAction(action: SlideAction, index?: number): void; onMove(from: number, to: number): void;
}) {
    // 썸네일 끌기. Tauri 창은 HTML 드래그 앤 드롭을 파일 놓기로 가로채므로 포인터 이벤트로 직접 끈다
    const [drag, setDrag] = useState<{ from: number; x: number; y: number; dx: number; dy: number } | null>(null);
    const [over, setOver] = useState<number | null>(null); // 끌어 놓을 자리. i번째 썸네일 앞이 i, 맨 끝이 count
    const list = useRef<HTMLDivElement>(null);
    const ghost = useRef<HTMLDivElement>(null);
    const dragged = useRef(false); // 끈 뒤에 오는 click으로 슬라이드가 바뀌지 않게 한다
    const [menu, setMenu] = useState<{ index: number; x: number; y: number } | null>(null);
    const menuRef = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        if (!menu) {
            return;
        }
        const down = (event: MouseEvent) => {
            if (!menuRef.current?.contains(event.target as Node)) {
                setMenu(null);
            }
        };
        const key = (event: KeyboardEvent) => event.key === 'Escape' && setMenu(null);
        window.addEventListener('mousedown', down);
        window.addEventListener('keydown', key);
        return () => {
            window.removeEventListener('mousedown', down);
            window.removeEventListener('keydown', key);
        };
    }, [menu]);
    // 누른 채 4px 넘게 움직이면 끌기 시작
    const pointerDown = (event: ReactPointerEvent<HTMLDivElement>, index: number) => {
        if (event.button !== 0 || !ready) {
            return;
        }
        const startX = event.clientX;
        const startY = event.clientY;
        const box = event.currentTarget.querySelector('.thumb-box')!.getBoundingClientRect();
        dragged.current = false;
        let target: number | null = null;
        const move = (moved: PointerEvent) => {
            if (!dragged.current && Math.hypot(moved.clientX - startX, moved.clientY - startY) < 4) {
                return;
            }
            dragged.current = true;
            setDrag({ from: index, x: moved.clientX, y: moved.clientY, dx: startX - box.left, dy: startY - box.top });
            const host = list.current;
            if (!host) {
                return;
            }
            // 목록 위아래 가장자리에 가면 스크롤한다
            const frame = host.getBoundingClientRect();
            if (moved.clientY < frame.top + 30) {
                host.scrollTop -= 12;
            } else if (moved.clientY > frame.bottom - 30) {
                host.scrollTop += 12;
            }
            const slots = [...host.querySelectorAll<HTMLElement>('.thumb-slot')];
            const at = slots.findIndex((slot) => {
                const rect = slot.getBoundingClientRect();
                return moved.clientY < rect.top + rect.height / 2;
            });
            target = at < 0 ? slots.length : at;
            setOver(target);
        };
        const up = () => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            window.removeEventListener('pointercancel', cancel);
            if (dragged.current && target !== null) {
                const to = target > index ? target - 1 : target;
                if (to !== index) {
                    onMove(index, to);
                }
            }
            setDrag(null);
            setOver(null);
        };
        const cancel = () => {
            target = null;
            up();
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        window.addEventListener('pointercancel', cancel);
    };
    // 커서를 따라다니는 썸네일. 끄는 썸네일의 그림을 그대로 복사해 쓴다
    const from = drag?.from;
    useEffect(() => {
        if (from === undefined || !ghost.current) {
            return;
        }
        const source = list.current?.querySelectorAll('.thumb-slot')[from]?.querySelector('.thumb-box');
        ghost.current.replaceChildren(...(source ? [source.cloneNode(true)] : []));
    }, [from]);
    const run = (action: SlideAction) => {
        const index = menu!.index;
        setMenu(null);
        onAction(action, index);
    };
    return (
        <div ref={list} className={'thumbs' + (drag ? ' sorting' : '')}>
            {deck && Array.from({ length: count }, (_, i) => (
                <div key={i} className={'thumb-slot' + (drag && over === i ? ' drop-before' : '') + (drag && over === count && i === count - 1 ? ' drop-after' : '') + (drag?.from === i ? ' dragging' : '')}
                    onPointerDown={(event) => pointerDown(event, i)} onDragStart={(event) => event.preventDefault()}
                    onClickCapture={(event) => {
                        if (dragged.current) {
                            event.stopPropagation();
                            dragged.current = false;
                        }
                    }}
                    onContextMenu={(event) => {
                        event.preventDefault();
                        onPage(i);
                        setMenu({ index: i, x: event.clientX, y: event.clientY });
                    }}>
                    <Thumbnail deck={deck} index={i} current={i === page} onClick={() => onPage(i)} />
                </div>
            ))}
            <button className="add-slide" disabled={!ready} title="지금 슬라이드 뒤에 새 슬라이드" onClick={() => onAction('add')}>
                <Plus size={16} /> 새 슬라이드
            </button>
            {drag && <div ref={ghost} className="thumb-ghost" style={{ left: drag.x - drag.dx, top: drag.y - drag.dy }} />}
            {menu && (
                <span ref={menuRef} className="menu list-menu insert-menu context-menu" style={{ left: menu.x, top: menu.y }}>
                    <button disabled={!ready || menu.index === 0} onClick={() => run('up')}><ChevronUp size={15} /> 위로</button>
                    <button disabled={!ready || menu.index >= count - 1} onClick={() => run('down')}><ChevronDown size={15} /> 아래로</button>
                    <button disabled={!ready} onClick={() => run('duplicate')}><Copy size={15} /> 복제</button>
                    <button disabled={!ready} className="danger" onClick={() => run('delete')}><Trash2 size={15} /> 삭제 <span className="shortcut">Del</span></button>
                </span>
            )}
        </div>
    );
}

// macOS는 창 버튼(신호등)을 시스템이 상단 바 왼쪽에 그린다 (tauri.macos.conf.json)
export const isMac = navigator.userAgent.includes('Mac');

// 로고를 누르면 버전, templide의 라이선스, 오픈소스 라이선스 창을 여는 메뉴
export function AppMenu() {
    const [open, setOpen] = useState(false);
    const [version, setVersion] = useState('');
    const anchor = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        getVersion().then(setVersion);
    }, []);
    useEffect(() => {
        if (!open) {
            return;
        }
        const down = (event: MouseEvent) => {
            if (!anchor.current?.contains(event.target as Node)) {
                setOpen(false);
            }
        };
        window.addEventListener('mousedown', down);
        return () => window.removeEventListener('mousedown', down);
    }, [open]);
    // 이미 열려 있으면 그 창을 앞으로 가져온다
    const openLicenses = async () => {
        setOpen(false);
        const existing = await WebviewWindow.getByLabel('licenses');
        if (existing) {
            await existing.setFocus();
            return;
        }
        new WebviewWindow('licenses', { url: 'licenses.html', title: '오픈소스 라이선스', width: 760, height: 640, center: true, focus: true });
    };
    return (
        <span className="menu-anchor" ref={anchor}>
            <button className="logo-button" title="templide" onClick={() => setOpen(!open)}>
                <img className="logo" src={logo} alt="templide" draggable={false} />
            </button>
            {open && (
                <span className="menu list-menu app-menu">
                    <span className="app-menu-title">templide {version}</span>
                    <button onClick={() => { setOpen(false); message(license, { title: 'templide 라이선스', kind: 'info' }); }}>templide 라이선스</button>
                    <button onClick={openLicenses}>오픈소스 라이선스</button>
                </span>
            )}
        </span>
    );
}

// 창 제목 줄이 없으므로 최소화, 최대화, 닫기 버튼을 상단 바 끝에 둔다. 닫기는 저장하지 않은 변경을 묻는 onCloseRequested를 거친다
export function WindowControls() {
    const [maximized, setMaximized] = useState(false);
    useEffect(() => {
        const window = getCurrentWindow();
        const update = () => window.isMaximized().then(setMaximized);
        update();
        const unlisten = window.onResized(update);
        return () => {
            unlisten.then((off) => off());
        };
    }, []);
    return (
        <span className="window-controls">
            <button title="최소화" onClick={() => getCurrentWindow().minimize()}><Minus size={16} /></button>
            <button title={maximized ? '이전 크기로' : '최대화'} onClick={() => getCurrentWindow().toggleMaximize()}>
                {maximized ? <Copy size={13} /> : <Square size={13} />}
            </button>
            <button className="close" title="닫기" onClick={() => getCurrentWindow().close()}><X size={16} /></button>
        </span>
    );
}

// 슬라이드 쇼 버튼 왼쪽의 target 고르기. 캔버스와 슬라이드 쇼는 고른 target의 크기와 설정을 쓴다. target이 없으면 없다고만 보여 준다
function TargetPicker({ deck, target, onTarget }: { deck: DeckResult | null; target: string | null; onTarget(name: string): void }) {
    if (!deck) {
        return null;
    }
    if (deck.targets.length === 0) {
        return <span className="target-none muted" title={`target이 없어 ${deck.width} × ${deck.height}로 보여 줍니다`}>target 없음</span>;
    }
    return (
        <select className="target-select" value={target ?? deck.target ?? ''} title={`미리 볼 target (${deck.width} × ${deck.height})`} onChange={(event) => onTarget(event.target.value)}>
            {deck.targets.map((each) => <option key={each.name} value={each.name}>{each.name} ({each.type})</option>)}
        </select>
    );
}

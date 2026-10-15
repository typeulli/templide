/** 표의 셀 선택과 편집에 필요한 화면 상태와 계산 함수. */
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { Move } from 'lucide-react';
import type { ElementInfo, TableInfo } from './lsp';
import { t } from './i18n';
import { matchCommand } from './shortcuts';

export type Cell = { row: number; column: number };
/** anchor는 시작 셀이고 focus는 끝 셀이다. */
export type CellSel = { id: string; anchor: Cell; focus: Cell };
export type Rect = { r1: number; c1: number; r2: number; c2: number };
export type Box = { x: number; y: number; w: number; h: number };

/** 키보드로 수행하는 셀 편집 동작. */
export type CellAction = { type: 'clear' } | { type: 'undo' } | { type: 'redo' } | { type: 'paste'; rows: string[][] } | { type: 'format'; key: 'bold' | 'italic' | 'underline' };

export const cellKey = (row: number, column: number) => `${row},${column}`;

export function tableOf(info: ElementInfo | undefined): TableInfo | undefined {
    return info?.object === 'table' && info.table?.cells && info.table.cells.length > 0 ? info.table : undefined;
}

const rowCount = (table: TableInfo) => table.cells!.length;
const columnCount = (table: TableInfo) => table.cells![0].length;
const clamp = (value: number, low: number, high: number) => Math.min(Math.max(value, low), high);

/** 주어진 위치를 덮는 병합 영역의 왼쪽 위 셀을 찾는다. */
export function anchorOf(table: TableInfo, row: number, column: number): Cell {
    const cells = table.cells!;
    row = clamp(row, 0, rowCount(table) - 1);
    column = clamp(column, 0, columnCount(table) - 1);
    if (!cells[row][column].covered) {
        return { row, column };
    }
    for (let r = row; r >= 0; --r) {
        for (let c = column; c >= 0; --c) {
            const cell = cells[r][c];
            if (!cell.covered && r + cell.rowSpan > row && c + cell.columnSpan > column) {
                return { row: r, column: c };
            }
        }
    }
    return { row, column };
}

const spanOf = (table: TableInfo, cell: Cell): Rect => {
    const info = table.cells![cell.row][cell.column];
    return { r1: cell.row, c1: cell.column, r2: cell.row + info.rowSpan - 1, c2: cell.column + info.columnSpan - 1 };
};

const unite = (a: Rect, b: Rect): Rect => ({ r1: Math.min(a.r1, b.r1), c1: Math.min(a.c1, b.c1), r2: Math.max(a.r2, b.r2), c2: Math.max(a.c2, b.c2) });
const touches = (a: Rect, b: Rect) => a.r1 <= b.r2 && b.r1 <= a.r2 && a.c1 <= b.c2 && b.c1 <= a.c2;
const holds = (a: Rect, b: Rect) => a.r1 <= b.r1 && a.c1 <= b.c1 && a.r2 >= b.r2 && a.c2 >= b.c2;

/** 선택 범위. 병합 셀은 전체가 포함되도록 확장한다. */
export function rectOf(table: TableInfo, sel: Pick<CellSel, 'anchor' | 'focus'>): Rect {
    const first = anchorOf(table, sel.anchor.row, sel.anchor.column);
    const second = anchorOf(table, sel.focus.row, sel.focus.column);
    let rect = unite(spanOf(table, first), spanOf(table, second));
    for (let grown = true; grown;) {
        grown = false;
        for (const cell of anchorsIn(table, { r1: 0, c1: 0, r2: rowCount(table) - 1, c2: columnCount(table) - 1 })) {
            const span = spanOf(table, cell);
            if (touches(rect, span) && !holds(rect, span)) {
                rect = unite(rect, span);
                grown = true;
            }
        }
    }
    return rect;
}

/** 선택 범위에서 실제로 편집할 셀을 반환한다. */
export function anchorsIn(table: TableInfo, rect: Rect): Cell[] {
    const result: Cell[] = [];
    for (let row = rect.r1; row <= rect.r2; ++row) {
        for (let column = rect.c1; column <= rect.c2; ++column) {
            if (!table.cells![row][column].covered) {
                result.push({ row, column });
            }
        }
    }
    return result;
}

/** 표 크기가 바뀐 뒤 선택 범위를 유효한 위치로 맞춘다. */
export function clampSel(table: TableInfo, sel: CellSel): CellSel {
    const fit = (cell: Cell) => anchorOf(table, cell.row, cell.column);
    return { id: sel.id, anchor: fit(sel.anchor), focus: fit(sel.focus) };
}

// 방향키. 병합한 칸은 한 칸으로 셈하고, 늘리면(extend) 누른 칸은 그대로 둔다
export function moveFocus(table: TableInfo, sel: CellSel, dRow: number, dColumn: number, extend: boolean): CellSel {
    const focus = anchorOf(table, sel.focus.row, sel.focus.column);
    const info = table.cells![focus.row][focus.column];
    const row = clamp(dRow > 0 ? focus.row + info.rowSpan : dRow < 0 ? focus.row - 1 : focus.row, 0, rowCount(table) - 1);
    const column = clamp(dColumn > 0 ? focus.column + info.columnSpan : dColumn < 0 ? focus.column - 1 : focus.column, 0, columnCount(table) - 1);
    const next = anchorOf(table, row, column);
    return { id: sel.id, anchor: extend ? sel.anchor : next, focus: next };
}

// Tab. 줄 끝에서는 다음 줄 처음으로 간다. 표의 처음이나 끝에서는 그대로다
export function stepFocus(table: TableInfo, sel: CellSel, backward: boolean): CellSel {
    const focus = anchorOf(table, sel.focus.row, sel.focus.column);
    const info = table.cells![focus.row][focus.column];
    let { row, column } = focus;
    if (backward) {
        column -= 1;
        if (column < 0) {
            column = columnCount(table) - 1;
            row -= 1;
        }
    } else {
        column += info.columnSpan;
        if (column >= columnCount(table)) {
            column = 0;
            row += info.rowSpan;
        }
    }
    if (row < 0 || row >= rowCount(table)) {
        return sel;
    }
    const next = anchorOf(table, row, column);
    return { id: sel.id, anchor: next, focus: next };
}

export function selectAll(table: TableInfo, id: string): CellSel {
    return { id, anchor: { row: 0, column: 0 }, focus: { row: rowCount(table) - 1, column: columnCount(table) - 1 } };
}

// 합칠 수 없으면 그 이유
export function mergeProblem(table: TableInfo, rect: Rect): string | null {
    if ((rect.r2 - rect.r1 + 1) * (rect.c2 - rect.c1 + 1) <= 1) {
        return t('칸을 둘 이상 고르세요');
    }
    const cells = anchorsIn(table, rect);
    if (cells.length === 1) {
        return t('이미 하나로 합쳐진 칸입니다');
    }
    const others = cells.filter((cell) => !(cell.row === rect.r1 && cell.column === rect.c1) && (table.cells![cell.row][cell.column].rowSpan > 1 || table.cells![cell.row][cell.column].columnSpan > 1));
    return others.length > 0 ? t('합쳐진 칸이 섞여 있습니다. 먼저 나누어 주세요') : null;
}

export function splitTargets(table: TableInfo, rect: Rect): Cell[] {
    return anchorsIn(table, rect).filter((cell) => table.cells![cell.row][cell.column].rowSpan > 1 || table.cells![cell.row][cell.column].columnSpan > 1);
}

// 범위의 글. 칸은 탭, 줄은 줄바꿈으로 나눈다 (스프레드시트와 같은 모양)
export function textOfRange(table: TableInfo, rect: Rect): string {
    const lines: string[] = [];
    for (let row = rect.r1; row <= rect.r2; ++row) {
        const line: string[] = [];
        for (let column = rect.c1; column <= rect.c2; ++column) {
            line.push(table.cells![row][column].text ?? '');
        }
        lines.push(line.join('\t'));
    }
    return lines.join('\n');
}

// 칸이 고칠 수 없는 칸 목록이면 그 이유. 고칠 수 있으면 null
export function cellsLock(info: ElementInfo, lockOf: (origin: ElementInfo['source'] | undefined) => string | null): string | null {
    if (info.table?.editable !== false) {
        return null;
    }
    return lockOf(info.properties.cells) ?? (info.fromTemplate ? t('템플릿 정의 안에 적힌 값') : t('칸 목록이 코드에 그대로 적혀 있지 않음'));
}

// 칸 번호 표시용
export const cellName = (cell: Cell) => `${cell.row + 1}, ${cell.column + 1}`;

// 화면에 그려진 표에서 잰 것. 길이는 모두 슬라이드의 px이다 (행은 글이 길면 늘어나므로 DOM에서 잰다)
export type TableGeometry = {
    box: Box;
    columns: number[]; // 열 너비
    rows: number[];    // 행 높이 (실제)
    xs: number[];      // 열 경계의 x. 열 수 + 1개
    ys: number[];      // 행 경계의 y. 행 수 + 1개
};

export function gridOf(frame: HTMLElement, id: string): HTMLElement | null {
    return frame.querySelector<HTMLElement>(`[data-src="${CSS.escape(id)}"] .tl-table`);
}

const tracks = (value: string): number[] => value.split(/\s+/).map(parseFloat).filter(Number.isFinite);

export function measureTable(frame: HTMLElement, id: string, scale: number, table: TableInfo): TableGeometry | null {
    const grid = gridOf(frame, id);
    if (!grid) {
        return null;
    }
    const origin = frame.getBoundingClientRect();
    const rect = grid.getBoundingClientRect();
    const style = getComputedStyle(grid);
    const columns = tracks(style.gridTemplateColumns);
    const rows = tracks(style.gridTemplateRows);
    if (columns.length !== columnCount(table) || rows.length !== rowCount(table) || !grid.offsetWidth) {
        return null; // 숨겨진 탭 등에서는 트랙 크기가 풀리지 않는다
    }
    const unit = rect.width / scale / grid.offsetWidth; // 화면의 px을 슬라이드의 px로
    const x0 = (rect.left - origin.left) / scale;
    const y0 = (rect.top - origin.top) / scale;
    const cumulative = (start: number, sizes: number[]) => sizes.reduce((all, size) => [...all, all[all.length - 1] + size * unit], [start]);
    return {
        box: { x: x0, y: y0, w: rect.width / scale, h: rect.height / scale },
        columns: columns.map((size) => size * unit), rows: rows.map((size) => size * unit),
        xs: cumulative(x0, columns), ys: cumulative(y0, rows),
    };
}

// 글 길이 때문에 더 줄일 수 없는 행 높이
function rowMinimums(grid: HTMLElement): number[] {
    const saved = grid.style.gridTemplateRows;
    const count = tracks(getComputedStyle(grid).gridTemplateRows).length;
    grid.style.gridTemplateRows = Array(count).fill('auto').join(' ');
    const sizes = tracks(getComputedStyle(grid).gridTemplateRows);
    grid.style.gridTemplateRows = saved;
    return sizes;
}

// 화면 좌표(clientX, clientY)에 있는 칸. 표 밖이면 가장 가까운 가장자리 칸
export function cellAt(grid: HTMLElement, clientX: number, clientY: number): Cell | null {
    const box = grid.getBoundingClientRect();
    const x = clamp(clientX, box.left + 1, box.right - 1);
    const y = clamp(clientY, box.top + 1, box.bottom - 1);
    for (const node of grid.querySelectorAll<HTMLElement>('.tl-cell')) {
        const rect = node.getBoundingClientRect();
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
            const [row, column] = node.dataset.cell!.split(',').map(Number);
            return { row, column };
        }
    }
    return null;
}

export const sameSel = (a: CellSel | null, b: CellSel | null) =>
    a === b || (!!a && !!b && a.id === b.id && a.anchor.row === b.anchor.row && a.anchor.column === b.anchor.column && a.focus.row === b.focus.row && a.focus.column === b.focus.column);

const MIN_SIZE = 12;

// 열(행) 경계 boundary(그 줄과 다음 줄 사이)가 눈에 보이는 구간들 [시작, 끝). 병합한 칸이 가로지르는 곳은 빠진다
function segments(table: TableInfo, axis: 'columns' | 'rows', boundary: number): [number, number][] {
    const columns = axis === 'columns';
    const length = columns ? rowCount(table) : columnCount(table);
    const result: [number, number][] = [];
    let from = -1;
    for (let at = 0; at <= length; ++at) {
        let open = false;
        if (at < length) {
            const cell = columns ? anchorOf(table, at, boundary) : anchorOf(table, boundary, at);
            const info = table.cells![cell.row][cell.column];
            open = columns ? cell.column + info.columnSpan <= boundary + 1 : cell.row + info.rowSpan <= boundary + 1;
        }
        if (open && from < 0) {
            from = at;
        } else if (!open && from >= 0) {
            result.push([from, at]);
            from = -1;
        }
    }
    return result;
}

type LayerProps = {
    id: string;
    table: TableInfo;
    geometry: TableGeometry;
    frame: HTMLElement;
    scale: number;
    sel: CellSel | null;
    editable: boolean;          // 캔버스에서 고칠 수 있는 때 (덱이 최신이고 오류가 없다)
    lock: string | null;        // 칸을 고칠 수 없는 이유
    moveLock: string | null;    // 표를 옮길 수 없는 이유
    height: number;             // 표의 height 속성 (글이 늘리기 전)
    editToken: number;          // 바뀌면 고르고 있는 칸의 글 고치기를 연다 (두 번 누름)
    focusToken: number;         // 바뀌면 입력 받는 칸으로 포커스를 돌려준다
    onSel(sel: CellSel | null): void;
    onCommit(row: number, column: number, text: string): Promise<boolean>;
    onSizes(axis: 'columns' | 'rows', values: number[], height: number | null): Promise<boolean>;
    onAction(action: CellAction): void;
    onNotice(message: string): void;
    onGrip(event: React.PointerEvent): void;
};

type SizeDrag = { axis: 'columns' | 'rows'; index: number; start: number[]; next: number[] };

// 고른 표 위에 그리는 층: 칸 선택 표시, 열과 행 경계 손잡이, 옮기는 손잡이, 칸 입력창.
// 입력창(textarea)은 칸을 고르는 동안 늘 포커스를 받는다. 비어 있을 때 글자를 치면 칸의 글을 대신하는 고치기가 시작되고(한글 조합 포함),
// F2나 두 번 누르면 칸의 글을 담아 연다
export function TableLayer(props: LayerProps) {
    const { id, table, geometry, frame, scale, sel } = props;
    const input = useRef<HTMLTextAreaElement>(null);
    const [editing, setEditing] = useState(false);
    const editingRef = useRef(false);
    const original = useRef('');
    const [sizing, setSizing] = useState<SizeDrag | null>(null);
    const latest = useRef(props);
    latest.current = props;

    const rect = sel ? rectOf(table, sel) : null;
    const focus = sel ? anchorOf(table, sel.focus.row, sel.focus.column) : null;
    const cell = focus ? table.cells![focus.row][focus.column] : null;
    const rangeBox = rect ? {
        x: geometry.xs[rect.c1], w: geometry.xs[rect.c2 + 1] - geometry.xs[rect.c1],
        y: geometry.ys[rect.r1], h: geometry.ys[rect.r2 + 1] - geometry.ys[rect.r1],
    } : null;
    const focusBox = focus && cell ? {
        x: geometry.xs[focus.column], w: geometry.xs[focus.column + cell.columnSpan] - geometry.xs[focus.column],
        y: geometry.ys[focus.row], h: geometry.ys[focus.row + cell.rowSpan] - geometry.ys[focus.row],
    } : null;
    const several = !!rect && (rect.r2 > rect.r1 || rect.c2 > rect.c1);

    // 입력창을 고른 칸의 모양(글꼴, 크기, 색, 바탕, 안쪽 여백, 정렬)에 맞춘다. 칸 안에서 글이 제자리에 보이도록 세로 맞춤도 따른다
    const look = () => {
        const box = input.current;
        const current = latest.current;
        if (!box || !focus || !cell) {
            return;
        }
        const node = frame.querySelector<HTMLElement>(`[data-src="${CSS.escape(id)}"] .tl-cell[data-cell="${focus.row},${focus.column}"]`);
        const paragraph = node?.querySelector<HTMLElement>('.tl-p');
        const run = node?.querySelector<HTMLElement>('.tl-run');
        const text = paragraph ? getComputedStyle(paragraph) : null;
        const styled = run ? getComputedStyle(run) : text;
        const background = node ? getComputedStyle(node) : null;
        const style = box.style;
        style.fontFamily = text?.fontFamily ?? 'inherit';
        style.fontSize = text?.fontSize ?? '24px';
        style.lineHeight = text?.lineHeight ?? 'normal';
        style.color = styled?.color ?? '#000';
        style.fontWeight = styled?.fontWeight ?? 'normal';
        style.fontStyle = styled?.fontStyle ?? 'normal';
        style.letterSpacing = styled?.letterSpacing ?? 'normal';
        style.textAlign = cell.align;
        style.background = background && (background.backgroundImage !== 'none' || background.backgroundColor !== 'rgba(0, 0, 0, 0)') ? background.background : '#fff';
        const [left, top, right, bottom] = cell.padding;
        style.paddingLeft = `${left}px`;
        style.paddingRight = `${right}px`;
        if (editingRef.current && current.sel) {
            const height = focusBox!.h;
            box.style.height = 'auto';
            box.style.paddingTop = `${top}px`;
            box.style.paddingBottom = `${bottom}px`;
            const content = box.scrollHeight - top - bottom;
            const free = Math.max(0, height - top - bottom - content);
            const middle = cell.anchor === 'middle' || cell.anchor === 'ctr';
            const below = cell.anchor === 'bottom' || cell.anchor === 'b';
            box.style.paddingTop = `${top + (middle ? free / 2 : below ? free : 0)}px`;
            box.style.paddingBottom = `${bottom + (middle ? free / 2 : below ? 0 : free)}px`;
            box.style.height = `${Math.max(height, content + top + bottom)}px`;
        } else {
            style.paddingTop = `${top}px`;
            style.paddingBottom = `${bottom}px`;
        }
    };

    useLayoutEffect(look);

    // 고르는 동안 입력창이 포커스를 받는다
    useEffect(() => {
        if (sel) {
            input.current?.focus({ preventScroll: true });
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [sel === null, props.focusToken]);

    const begin = (prefill: boolean): boolean => {
        const box = input.current;
        if (!box || !focus || !cell) {
            return false;
        }
        if (props.lock) {
            props.onNotice(t('이 칸은 고칠 수 없습니다: {0}. 코드에서 고쳐 주세요', props.lock));
            box.value = '';
            return false;
        }
        if (!props.editable) {
            props.onNotice(t('다시 컴파일하는 중이거나 코드에 오류가 있어 지금은 캔버스에서 고칠 수 없습니다'));
            box.value = '';
            return false;
        }
        original.current = cell.text ?? '';
        if (prefill) {
            box.value = original.current;
        }
        editingRef.current = true;
        setEditing(true);
        box.focus({ preventScroll: true });
        if (prefill) {
            box.setSelectionRange(box.value.length, box.value.length);
        }
        requestAnimationFrame(look);
        return true;
    };

    const first = useRef(true);
    useEffect(() => {
        if (first.current) {
            first.current = false;
            return;
        }
        if (!editingRef.current) {
            begin(true);
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [props.editToken]);

    // 칸 고치기를 마친다. 고친 글은 서버에 보내고, move가 있으면 선택을 옮긴다. 다른 곳을 눌러 포커스가 나갔으면(blur) 포커스를 돌려주지 않는다
    const finish = async (commit: boolean, move?: CellSel, blur = false) => {
        const box = input.current;
        if (!box || !editingRef.current || !focus) {
            return;
        }
        editingRef.current = false;
        setEditing(false);
        const text = box.value.replace(/\r/g, '');
        box.value = '';
        if (move) {
            props.onSel(move);
        }
        if (commit && text !== original.current) {
            await props.onCommit(focus.row, focus.column, text);
        }
        if (!blur) {
            box.focus({ preventScroll: true });
        }
    };

    const onKeyDown = (event: React.KeyboardEvent<HTMLTextAreaElement>) => {
        if (event.nativeEvent.isComposing || event.keyCode === 229 || !sel) {
            return; // 조합 중의 Enter와 방향키는 조합을 마칠 뿐이다
        }
        const mod = event.ctrlKey || event.metaKey;
        const key = event.key;
        if (editingRef.current) {
            if (key === 'Enter') {
                if (event.shiftKey || event.altKey) {
                    event.preventDefault();
                    const box = event.currentTarget;
                    box.setRangeText('\n', box.selectionStart, box.selectionEnd, 'end');
                    look();
                } else {
                    event.preventDefault();
                    finish(true, moveFocus(table, sel, 1, 0, false));
                }
            } else if (key === 'Tab') {
                event.preventDefault();
                finish(true, stepFocus(table, sel, event.shiftKey));
            } else if (key === 'Escape') {
                event.preventDefault();
                event.stopPropagation();
                finish(false);
            }
            return;
        }
        const move = (dRow: number, dColumn: number) => {
            event.preventDefault();
            props.onSel(moveFocus(table, sel, dRow, dColumn, event.shiftKey));
        };
        if (mod) {
            const letter = key.toLowerCase();
            const format = matchCommand(event.nativeEvent, ['bold', 'italic', 'underline']);
            if (format === 'bold' || format === 'italic' || format === 'underline') {
                event.preventDefault();
                props.onAction({ type: 'format', key: format });
            } else if (letter === 'z' || letter === 'y') {
                event.preventDefault();
                props.onAction({ type: letter === 'y' || event.shiftKey ? 'redo' : 'undo' });
            } else if (letter === 'a') {
                event.preventDefault();
                props.onSel(selectAll(table, id));
            } else if ((letter === 'c' || letter === 'x') && rect) {
                // 고른 범위의 글을 입력창에 담아 선택하면 브라우저가 그대로 복사한다. 잘라내기는 복사한 뒤 칸을 비운다
                const box = event.currentTarget;
                box.value = textOfRange(table, rect);
                box.select();
                const cut = letter === 'x';
                setTimeout(() => {
                    box.value = '';
                    if (cut) {
                        props.onAction({ type: 'clear' });
                    }
                }, 0);
            }
            return;
        }
        switch (key) {
            case 'ArrowUp': move(-1, 0); break;
            case 'ArrowDown': move(1, 0); break;
            case 'ArrowLeft': move(0, -1); break;
            case 'ArrowRight': move(0, 1); break;
            case 'Enter': move(event.shiftKey ? -1 : 1, 0); break;
            case 'Tab':
                event.preventDefault();
                props.onSel(stepFocus(table, sel, event.shiftKey));
                break;
            case 'F2':
                event.preventDefault();
                begin(true);
                break;
            case 'Delete':
            case 'Backspace':
                event.preventDefault();
                props.onAction({ type: 'clear' });
                break;
            case 'Escape':
                event.preventDefault();
                props.onSel(null);
                break;
            case 'Home': {
                event.preventDefault();
                const target = anchorOf(table, focus!.row, 0);
                props.onSel({ id, anchor: event.shiftKey ? sel.anchor : target, focus: target });
                break;
            }
            case 'End': {
                event.preventDefault();
                const target = anchorOf(table, focus!.row, columnCount(table) - 1);
                props.onSel({ id, anchor: event.shiftKey ? sel.anchor : target, focus: target });
                break;
            }
        }
    };

    // 비어 있는 입력창에 글자가 들어오면(조합 시작 포함) 그 칸의 글을 바꾸는 고치기가 시작된 것이다
    const onInput = (event: React.FormEvent<HTMLTextAreaElement>) => {
        if (!editingRef.current) {
            const box = input.current!;
            const kind = (event.nativeEvent as InputEvent).inputType ?? '';
            if (kind.startsWith('delete') || kind.startsWith('history')) {
                box.value = ''; // 잘라내기나 입력창 자체의 실행 취소가 바꾼 것은 글자를 친 것이 아니다
                return;
            }
            const typed = box.value;
            if (!begin(false)) {
                return;
            }
            box.value = typed;
        }
        look();
    };

    const onCompositionStart = () => {
        if (!editingRef.current) {
            begin(false);
        }
    };

    const onPaste = (event: React.ClipboardEvent<HTMLTextAreaElement>) => {
        if (editingRef.current) {
            return; // 글을 고치는 중에는 입력창이 그대로 붙여 넣는다
        }
        event.preventDefault();
        const text = event.clipboardData.getData('text/plain').replace(/\r\n?/g, '\n').replace(/\n$/, '');
        if (text !== '') {
            props.onAction({ type: 'paste', rows: text.split('\n').map((line) => line.split('\t')) });
        }
    };

    // 열 너비와 행 높이 끌기. 경계 양쪽의 두 줄만 바뀌고 합은 그대로다. 미리 보기는 표의 grid 크기를 바로 바꿔 보여 주고,
    // 놓으면 sizes로 원문을 고친다 (다시 그려지면 새 표로 바뀐다)
    const startSizing = (event: React.PointerEvent, axis: 'columns' | 'rows', index: number) => {
        event.stopPropagation();
        event.preventDefault();
        const grid = gridOf(frame, id);
        if (!grid) {
            return;
        }
        const columns = axis === 'columns';
        const start = [...(columns ? geometry.columns : geometry.rows)];
        const floor = columns ? start.map(() => MIN_SIZE) : rowMinimums(grid).map((minimum) => Math.max(MIN_SIZE, minimum));
        const total = start[index] + start[index + 1];
        const origin = columns ? event.clientX : event.clientY;
        const saved = columns ? grid.style.gridTemplateColumns : grid.style.gridTemplateRows;
        let next = start;
        const apply = (sizes: number[]) => {
            if (columns) {
                grid.style.gridTemplateColumns = sizes.map((size) => `${size}px`).join(' ');
            } else {
                grid.style.gridTemplateRows = sizes.map((size) => `minmax(${size}px, auto)`).join(' ');
            }
        };
        const move = (moved: PointerEvent) => {
            const delta = ((columns ? moved.clientX : moved.clientY) - origin) / scale;
            const low = floor[index];
            const high = Math.max(low, total - floor[index + 1]);
            const size = clamp(start[index] + delta, low, high);
            next = start.map((each, i) => i === index ? size : i === index + 1 ? total - size : each);
            apply(next);
            setSizing({ axis, index, start, next });
        };
        const up = async () => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            window.removeEventListener('pointercancel', up);
            if (next === start || next.every((size, i) => Math.abs(size - start[i]) < 0.5)) {
                setSizing(null);
                return;
            }
            // 행이 글 때문에 표의 height보다 커졌으면 height도 따라 간다. 안 그러면 비율을 다시 맞출 때 놓은 자리가 틀어진다
            const sum = next.reduce((all, size) => all + size, 0);
            const grown = !columns && Math.abs(sum - latest.current.height) > 0.5;
            const done = await latest.current.onSizes(axis, next.map((size) => Math.round(size * 100) / 100), grown ? Math.round(sum) : null);
            if (!done) {
                if (columns) {
                    grid.style.gridTemplateColumns = saved;
                } else {
                    grid.style.gridTemplateRows = saved;
                }
            }
            setSizing(null);
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        window.addEventListener('pointercancel', up);
    };
    const hit = 8 / scale;
    const line = 2 / scale;
    const sizable = props.editable && !props.lock;
    // 끄는 동안 경계선 하나를 보여 준다
    const guide = sizing ? (sizing.axis === 'columns'
        ? { left: geometry.xs[sizing.index + 1] + (sizing.next[sizing.index] - sizing.start[sizing.index]) - line / 2, top: geometry.box.y, width: line, height: geometry.box.h }
        : { left: geometry.box.x, top: geometry.ys[sizing.index + 1] + (sizing.next[sizing.index] - sizing.start[sizing.index]) - line / 2, width: geometry.box.w, height: line }) : null;

    return (
        <>
            {rangeBox && (
                <div className={'cell-range' + (several ? ' several' : '')} style={{ left: rangeBox.x, top: rangeBox.y, width: rangeBox.w, height: rangeBox.h, borderWidth: line }} />
            )}
            {several && focusBox && <div className="cell-active" style={{ left: focusBox.x, top: focusBox.y, width: focusBox.w, height: focusBox.h, borderWidth: line }} />}
            {sizable && geometry.xs.slice(1, -1).flatMap((x, i) => segments(table, 'columns', i).map(([from, to]) => (
                <div key={`c${i}-${from}`} className="table-handle column" style={{ left: x - hit / 2, top: geometry.ys[from], width: hit, height: geometry.ys[to] - geometry.ys[from] }}
                    onPointerDown={(event) => startSizing(event, 'columns', i)} />
            )))}
            {sizable && geometry.ys.slice(1, -1).flatMap((y, i) => segments(table, 'rows', i).map(([from, to]) => (
                <div key={`r${i}-${from}`} className="table-handle row" style={{ left: geometry.xs[from], top: y - hit / 2, width: geometry.xs[to] - geometry.xs[from], height: hit }}
                    onPointerDown={(event) => startSizing(event, 'rows', i)} />
            )))}
            {guide && <div className="table-guide" style={guide} />}
            {!props.moveLock && props.editable && (
                <div className="table-grip" title={t('끌어서 표 옮기기')} style={{ left: geometry.box.x - 26 / scale, top: geometry.box.y - 26 / scale, width: 22 / scale, height: 22 / scale, borderRadius: 4 / scale, borderWidth: 1 / scale }}
                    onPointerDown={(event) => { event.stopPropagation(); props.onGrip(event); }}>
                    <Move size={14 / scale} strokeWidth={2} />
                </div>
            )}
            {sel && props.lock && rangeBox && (
                <div className="table-lock" style={{ left: geometry.box.x, top: geometry.box.y + geometry.box.h + 6 / scale, fontSize: 12 / scale, padding: `${2 / scale}px ${6 / scale}px`, borderRadius: 4 / scale }}>
                    {t('🔒 칸을 고칠 수 없음: {0}', props.lock)}
                </div>
            )}
            {sel && focusBox && (
                <textarea ref={input} className={'cell-proxy' + (editing ? ' editing' : '')} rows={1} spellCheck={false} autoComplete="off" autoCapitalize="off" aria-label={t('칸')}
                    style={{ left: focusBox.x, top: focusBox.y, width: focusBox.w, height: focusBox.h, outlineWidth: line }}
                    onKeyDown={onKeyDown} onInput={onInput} onCompositionStart={onCompositionStart} onPaste={onPaste}
                    onBlur={() => finish(true, undefined, true)} />
            )}
        </>
    );
}

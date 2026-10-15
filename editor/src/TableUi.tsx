/** 표 삽입, 셀 메뉴, 셀 속성에 사용하는 화면 구성 요소. */
import { useEffect, useRef, useState, type ReactNode } from 'react';
import {
    AlignCenter, AlignCenterVertical, AlignEndVertical, AlignJustify, AlignLeft, AlignRight, AlignStartVertical, BetweenHorizontalEnd, BetweenHorizontalStart,
    BetweenVerticalEnd, BetweenVerticalStart, Eraser, RemoveFormatting, Table2, TableCellsMerge, TableCellsSplit, Trash2, type LucideIcon,
} from 'lucide-react';
import type { ElementInfo, Origin, Schema, TableCellInfo, TableInfo } from './lsp';
import { ColorField, EnumField, NumberInput, PaintField, Row, Section, type SetValue } from './Fields';
import { IconButton } from './Panels';
import type { Pickers } from './Properties';
import { anchorsIn, mergeProblem, rectOf, splitTargets, type Cell, type CellSel, type Rect } from './Table';
import { propertyNames } from './labels';
import { lockOf } from './SlideView';
import { t } from './i18n';

export type TableCommand =
    | 'insertRowAbove' | 'insertRowBelow' | 'insertColumnLeft' | 'insertColumnRight' | 'deleteRows' | 'deleteColumns'
    | 'merge' | 'split' | 'clearText' | 'clearFormat';

/** 선택한 셀과 가능한 작업. */
export type TableState = {
    info: ElementInfo;
    table: TableInfo;
    sel: CellSel;
    rect: Rect;
    cells: Cell[];            // 고른 범위의 칸 (병합하면 가려지는 칸은 뺀다)
    rows: number;             // 표의 행 수와 열 수
    columns: number;
    lock: string | null;      // 칸 목록을 고칠 수 없는 이유
    styleLock: string | null; // 칸 서식을 쓸 수 없는 이유 (cell 블록을 둘 수 없는 표)
};

export function tableState(info: ElementInfo | undefined, table: TableInfo | undefined, sel: CellSel | null, lock: string | null): TableState | null {
    if (!info || !table || !sel) {
        return null;
    }
    const rect = rectOf(table, sel);
    return {
        info, table, sel, rect, cells: anchorsIn(table, rect), rows: table.cells!.length, columns: table.cells![0].length, lock,
        // 템플릿이 만든 표의 cell 블록은 템플릿 정의 안에 들어가 그 템플릿을 쓰는 모든 곳이 바뀌므로 쓸 수 없다
        styleLock: info.fromTemplate ? lockOf({ kind: 'locked', reason: 'template' }) : lockOf(info.source),
    };
}

/** 작업을 수행할 수 없는 이유를 반환한다. */
export function commandProblem(command: TableCommand, state: TableState): string | null {
    const locked = (reason: string | null) => reason ? t('🔒 {0}', reason) : null;
    switch (command) {
        case 'merge': return locked(state.lock) ?? mergeProblem(state.table, state.rect);
        case 'split': return locked(state.lock) ?? (splitTargets(state.table, state.rect).length > 0 ? null : t('합쳐진 칸이 아닙니다'));
        case 'deleteRows': return locked(state.lock) ?? (state.rect.r2 - state.rect.r1 + 1 >= state.rows ? t('표에는 행이 하나 이상 있어야 합니다') : null);
        case 'deleteColumns': return locked(state.lock) ?? (state.rect.c2 - state.rect.c1 + 1 >= state.columns ? t('표에는 열이 하나 이상 있어야 합니다') : null);
        case 'clearFormat': return locked(state.styleLock);
        default: return locked(state.lock);
    }
}

const commandTitle = (command: TableCommand, state: TableState | null): string => {
    const rows = state ? state.rect.r2 - state.rect.r1 + 1 : 1;
    const columns = state ? state.rect.c2 - state.rect.c1 + 1 : 1;
    switch (command) {
        case 'insertRowAbove': return t('위에 행 삽입');
        case 'insertRowBelow': return t('아래에 행 삽입');
        case 'insertColumnLeft': return t('왼쪽에 열 삽입');
        case 'insertColumnRight': return t('오른쪽에 열 삽입');
        case 'deleteRows': return rows > 1 ? t('행 {0}개 삭제', rows) : t('행 삭제');
        case 'deleteColumns': return columns > 1 ? t('열 {0}개 삭제', columns) : t('열 삭제');
        case 'merge': return t('칸 병합');
        case 'split': return t('칸 나누기');
        case 'clearText': return t('칸의 글 지우기 (Delete)');
        case 'clearFormat': return t('칸 서식 지우기');
    }
};

const commandIcons: Record<TableCommand, LucideIcon> = {
    insertRowAbove: BetweenHorizontalStart, insertRowBelow: BetweenHorizontalEnd, insertColumnLeft: BetweenVerticalStart, insertColumnRight: BetweenVerticalEnd,
    deleteRows: Trash2, deleteColumns: Trash2, merge: TableCellsMerge, split: TableCellsSplit, clearText: Eraser, clearFormat: RemoveFormatting,
};

/** 셀 선택 시 표시하는 도구 모음. */
export function TableToolbar({ state, onCommand }: { state: TableState | null; onCommand(command: TableCommand): void }) {
    const button = (command: TableCommand, label?: string) => {
        const problem = state ? commandProblem(command, state) : t('표의 칸을 먼저 고르세요');
        const title = commandTitle(command, state);
        return (
            <IconButton key={command} icon={commandIcons[command]} title={problem ? `${title}: ${problem}` : title} disabled={!!problem} onClick={() => onCommand(command)} danger={command.startsWith('delete')}>
                {label}
            </IconButton>
        );
    };
    return (
        <span className="tool-group table-group">
            {button('insertRowAbove')}{button('insertRowBelow')}{button('insertColumnLeft')}{button('insertColumnRight')}
            <span className="separator" />
            {button('deleteRows', t('행'))}{button('deleteColumns', t('열'))}
            <span className="separator" />
            {button('merge')}{button('split')}{button('clearText')}
        </span>
    );
}

/** 셀의 컨텍스트 메뉴. */
export function TableMenu({ x, y, state, onCommand, onClose }: { x: number; y: number; state: TableState; onCommand(command: TableCommand): void; onClose(): void }) {
    const ref = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        const down = (event: MouseEvent) => {
            if (!ref.current?.contains(event.target as Node)) {
                onClose();
            }
        };
        const key = (event: KeyboardEvent) => event.key === 'Escape' && onClose();
        window.addEventListener('mousedown', down);
        window.addEventListener('keydown', key);
        return () => {
            window.removeEventListener('mousedown', down);
            window.removeEventListener('keydown', key);
        };
    }, [onClose]);
    const item = (command: TableCommand) => {
        const Icon = commandIcons[command];
        const problem = commandProblem(command, state);
        return (
            <button key={command} disabled={!!problem} title={problem ?? undefined} className={command.startsWith('delete') ? 'danger' : undefined}
                onClick={() => { onClose(); onCommand(command); }}>
                <Icon size={15} /> {commandTitle(command, state)}
            </button>
        );
    };
    const rule = (key: string) => <span key={key} className="menu-rule" />;
    return (
        <span ref={ref} className="menu list-menu insert-menu context-menu table-menu" style={{ left: Math.min(x, window.innerWidth - 230), top: Math.min(y, window.innerHeight - 360) }}>
            {item('insertRowAbove')}{item('insertRowBelow')}{item('insertColumnLeft')}{item('insertColumnRight')}
            {rule('a')}
            {item('deleteRows')}{item('deleteColumns')}
            {rule('b')}
            {item('merge')}{item('split')}
            {rule('c')}
            {item('clearText')}{item('clearFormat')}
        </span>
    );
}

/** 표를 삽입할 행과 열을 선택하는 격자. */
const gridSize = 8;

export function TableGrid({ onPick }: { onPick(rows: number, columns: number): void }) {
    const [hover, setHover] = useState<[number, number] | null>(null);
    return (
        <span className="table-picker">
            <span className="table-grid" onMouseLeave={() => setHover(null)}>
                {Array.from({ length: gridSize * gridSize }, (_, i) => {
                    const row = Math.floor(i / gridSize) + 1;
                    const column = (i % gridSize) + 1;
                    return (
                        <button key={i} className={'table-grid-cell' + (hover && row <= hover[0] && column <= hover[1] ? ' on' : '')} aria-label={`${row} × ${column}`}
                            onMouseEnter={() => setHover([row, column])} onClick={() => onPick(row, column)} />
                    );
                })}
            </span>
            <span className="table-grid-label">{hover ? t('{0} × {1} 표', hover[0], hover[1]) : t('행 × 열을 고르세요')}</span>
        </span>
    );
}

// 툴바의 "표 넣기" 버튼. 누르면 행 × 열을 고르는 칸이 펼쳐진다
export function TableInsertButton({ disabled, onPick }: { disabled: boolean; onPick(rows: number, columns: number): void }) {
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
            <IconButton icon={Table2} title={t('표 넣기 (행 × 열을 고릅니다)')} disabled={disabled} active={open} onClick={() => setOpen(!open)} />
            {open && <span className="menu table-menu-picker"><TableGrid onPick={(rows, columns) => { setOpen(false); onPick(rows, columns); }} /></span>}
        </span>
    );
}

// 속성 패널 "칸" 묶음이 고치는 방법. 속성 하나를 고른 칸 모두에 쓰거나(set) 지운다(unset)
export type CellControls = {
    command(command: TableCommand): void;
    set(properties: { name: string; value: SetValue }[]): void;
    unset(names: string[]): void;
};

const sides = ['left', 'top', 'right', 'bottom'] as const;
type Side = typeof sides[number];

// 칸 서식 중 병합은 구조이므로 서식 지우기에서 뺀다
export const structural = new Set(['col_span', 'row_span']);

const sideNames = (): Record<Side, string> => ({ left: t('왼쪽'), top: t('위'), right: t('오른쪽'), bottom: t('아래') });

export function CellSection({ state, controls, schema, pickers }: { state: TableState; controls: CellControls; schema: Schema | null; pickers: Pickers }) {
    const [picked, setPicked] = useState<Side[]>([...sides]);
    const { table, cells } = state;
    const infos: TableCellInfo[] = cells.map((cell) => table.cells![cell.row][cell.column]);
    const first = infos[0];
    const styleOf = (cell: Cell) => table.cellStyles?.find((style) => style.row === cell.row && style.column === cell.column);
    // 고른 칸 중 어느 하나라도 코드에 적어 둔 속성이면 지울 수 있다
    const written = (names: string[]) => cells.some((cell) => names.some((name) => styleOf(cell)?.properties[name]?.statement));
    const lock = state.styleLock;
    const originOf = (...names: string[]): Origin => lock ? { kind: 'locked', reason: lock } : { kind: 'literal', statement: written(names) };
    const same = <T,>(read: (info: TableCellInfo) => T): T | undefined => infos.every((info) => JSON.stringify(read(info)) === JSON.stringify(read(first))) ? read(first) : undefined;
    const set = (name: string) => (value: SetValue) => controls.set([{ name, value }]);
    const unset = (...names: string[]) => () => controls.unset(names);
    const problem = (command: TableCommand) => commandProblem(command, state);
    const action = (command: TableCommand, icon: ReactNode) => (
        <button key={command} className="ghost-button" disabled={!!problem(command)} title={problem(command) ?? commandTitle(command, state)} onClick={() => controls.command(command)}>
            {icon} {commandTitle(command, state)}
        </button>
    );

    // 가로, 세로 맞춤
    const aligns: [string, LucideIcon, string][] = [['left', AlignLeft, t('왼쪽')], ['center', AlignCenter, t('가운데')], ['right', AlignRight, t('오른쪽')], ['justify', AlignJustify, t('양쪽')]];
    const anchors: [string, LucideIcon, string][] = [['top', AlignStartVertical, t('위')], ['middle', AlignCenterVertical, t('가운데')], ['bottom', AlignEndVertical, t('아래')]];
    const segmented = (name: string, list: [string, LucideIcon, string][], current: string | undefined) => (
        <span className="segmented compact">
            {list.map(([value, Icon, title]) => (
                <button key={value} className={current === value ? 'active' : ''} title={title} disabled={!!lock} onClick={() => controls.set([{ name, value: { enum: value } }])}><Icon size={15} /></button>
            ))}
        </span>
    );

    // 안쪽 여백: 모두 같으면 padding 하나, 아니면 변마다
    const padding = same((info) => info.padding);
    const paddingAll = padding && padding.every((value) => value === padding[0]) ? padding[0] : undefined;
    const paddingSides: [string, number][] = [['padding_left', 0], ['padding_top', 1], ['padding_right', 2], ['padding_bottom', 3]];

    // 테두리: 고른 변에 색과 두께를 쓴다. 네 변이면 line_color, 아니면 변마다 line_color_left 등
    const all = picked.length === 4;
    const borderNames = (kind: 'color' | 'width') => all ? [`line_${kind}`] : picked.map((side) => `line_${kind}_${side}`);
    const borderWritten = (kind: 'color' | 'width') => all ? [`line_${kind}`, ...sides.map((side) => `line_${kind}_${side}`)] : picked.map((side) => `line_${kind}_${side}`);
    const shown = same((info) => info.border[sides.indexOf(picked[0] ?? 'left')]);
    const toggleSide = (side: Side) => setPicked((now) => now.includes(side) ? (now.length > 1 ? now.filter((each) => each !== side) : now) : [...now, side]);

    return (
        <Section title={cells.length > 1 ? t('칸 ({0}개)', cells.length) : t('칸 ({0}, {1})', cells[0].row + 1, cells[0].column + 1)} lock={lock ?? state.lock}>
            <div className="tab-actions">
                {action('merge', <TableCellsMerge size={13} />)}
                {action('split', <TableCellsSplit size={13} />)}
                {action('clearText', <Eraser size={13} />)}
                {action('clearFormat', <RemoveFormatting size={13} />)}
            </div>
            <PaintField name="fill" label={propertyNames.fill} value={same((info) => info.fill) ?? first.fill} origin={originOf('fill')} onSet={set('fill')} onUnset={unset('fill')}
                patterns={schema?.enums.pattern_kind ?? []} pickImage={pickers.image} />
            <Row label={t('가로 맞춤')} lock={lock} origin={originOf('align')} onUnset={unset('align')}>{segmented('align', aligns, same((info) => info.align))}</Row>
            <Row label={t('세로 맞춤')} lock={lock} origin={originOf('anchor')} onUnset={unset('anchor')}>{segmented('anchor', anchors, same((info) => info.anchor))}</Row>
            <Row label={t('안쪽 여백')} lock={lock} origin={originOf('padding', ...paddingSides.map(([name]) => name))} onUnset={unset('padding', ...paddingSides.map(([name]) => name))}>
                <NumberInput value={paddingAll} measure="px" disabled={!!lock} placeholder={padding ? '–' : undefined} onCommit={(shown) => controls.set([{ name: 'padding', value: { length: shown } }])} />
            </Row>
            <div className="pair padding-sides">
                {paddingSides.map(([name, at]) => (
                    <label key={name} className="mini-field">
                        <span>{propertyNames[name]?.replace(/ ?여백$/, '') ?? name}</span>
                        <NumberInput value={padding?.[at]} measure="px" disabled={!!lock} onCommit={(shown) => controls.set([{ name, value: { length: shown } }])} />
                    </label>
                ))}
            </div>
            <div className="border-title">{t('테두리')}</div>
            <Row label={t('적용 위치')}>
                <span className="side-picker" title={t('테두리를 바꿀 변을 고릅니다 (가운데는 네 변 모두)')}>
                    <button className={'top' + (picked.includes('top') ? ' on' : '')} onClick={() => toggleSide('top')} aria-label={sideNames().top} title={sideNames().top} />
                    <button className={'left' + (picked.includes('left') ? ' on' : '')} onClick={() => toggleSide('left')} aria-label={sideNames().left} title={sideNames().left} />
                    <button className={'all' + (all ? ' on' : '')} onClick={() => setPicked(all ? ['left'] : [...sides])} aria-label={t('네 변')} title={t('네 변 모두')} />
                    <button className={'right' + (picked.includes('right') ? ' on' : '')} onClick={() => toggleSide('right')} aria-label={sideNames().right} title={sideNames().right} />
                    <button className={'bottom' + (picked.includes('bottom') ? ' on' : '')} onClick={() => toggleSide('bottom')} aria-label={sideNames().bottom} title={sideNames().bottom} />
                </span>
            </Row>
            <ColorField name="line_color" label={propertyNames.line_color} value={shown?.color} origin={originOf(...borderWritten('color'))}
                onSet={(value) => controls.set(borderNames('color').map((name) => ({ name, value })))} onUnset={unset(...borderWritten('color'))} />
            <Row label={propertyNames.line_width} lock={lock} origin={originOf(...borderWritten('width'))} onUnset={unset(...borderWritten('width'))}>
                <NumberInput value={shown?.width} measure="px" disabled={!!lock} onCommit={(value) => controls.set(borderNames('width').map((name) => ({ name, value: { length: value } })))} />
            </Row>
            <EnumField name="line_dash" label={propertyNames.line_dash} value={same((info) => info.border.find((line) => line)?.dash || 'solid')} origin={originOf('line_dash')}
                onSet={set('line_dash')} onUnset={unset('line_dash')} options={schema?.enums.dash_style ?? []} />
        </Section>
    );
}

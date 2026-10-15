// AI 탭의 에이전트에게 주는 MCP 도구. 서버(src-tauri/src/mcp.rs)가 tools/call을 "mcp-call" 이벤트로 넘기면
// 여기서 편집기 상태로 실행하고 mcp_reply로 돌려준다. 새 도구는 tools와 run에 함께 더한다
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { toPng } from 'html-to-image';
import type { DeckResult, Diagnostic, ElementInfo, Schema } from './lsp';
import { guide } from './mcpGuide';
import { findElement } from './SlideView';
import type { Rect } from './Table';

// App이 매 렌더마다 지금 상태로 새로 만들어 넘긴다
export type EditorAccess = {
    path: string | null;
    dirty: boolean;
    text: string | null; // Monaco의 내용 (저장하지 않은 것 포함). Monaco를 불러오기 전이면 null
    deck: DeckResult | null;
    stale: boolean;      // 코드에 오류가 있어 덱이 마지막으로 성공한 것
    page: number;        // 0부터
    selected: string | null;
    diagnostics: Diagnostic[];
    warnings: Diagnostic[];
    schema: Schema | null; // 마지막으로 분석에 성공한 문서의 이름들 (언어 설명과 자산 목록에 쓴다)
    // 코드 패널의 커서(줄과 칸은 1부터)와 고른 글. Monaco를 불러오기 전이면 null
    cursor(): { line: number; column: number; selection?: { from: number; to: number; text: string } } | null;
    cells: Rect | null; // 화면에서 고른 표의 칸 범위 (0부터)
    // 코드의 oldText를 newText로 바꾸고 다시 컴파일될 때까지 기다린다. 실패하면 이유를 돌려준다
    // renamed는 이 수정으로 바뀐 요소 id (전 id -> 지금 id)
    edit(oldText: string, newText: string, all: boolean): Promise<{ error?: string; count?: number; renamed?: Record<string, string> }>;
    goto(page: number): void;
    select(id: string): void;
    // 코드를 고쳐 바뀐 요소 id를 지금 id로. 없어진 요소면 null
    resolve(id: string): string | null;
    build(): Promise<{ path?: string; errors?: string[]; error?: string; warnings?: string[] } | undefined>;
};

// 편집기에 열린 문서 하나. current는 에이전트를 실행한 탭이고, 문서가 없는 탭(.tasset 등)은 목록에 없다
export type OpenDocument = { path: string | null; dirty?: boolean; current?: boolean };

type Content = { type: 'text'; text: string } | { type: 'image'; data: string; mimeType: string };
type Result = { content: Content[]; isError?: boolean };

const tools = [
    {
        name: 'get_editor_state',
        description: 'Overview: open files (files), this document, unsaved changes, current slide, selected element id, code cursor line, targets, code errors and warnings, and a short list of the elements of one slide (id, object, line, text, box [x, y, w, h] in px). Without page, the slide currently shown. verbose adds the written property values.',
        inputSchema: {
            type: 'object',
            properties: {
                page: { type: 'integer', minimum: 1, description: 'Slide number to list elements for (1-based)' },
                verbose: { type: 'boolean', description: 'Also list the property values written for each element' },
            },
        },
    },
    {
        name: 'get_selection',
        description: 'What the user is pointing at: code cursor and selected code, the element selected on the canvas (object, source lines, text, written property values, which values are locked and why), and for a table the selected cell range with the cells\' text and explicit formatting. Use it for "this", "the selected one", "here".',
        inputSchema: { type: 'object', properties: {} },
    },
    {
        name: 'get_language_guide',
        description: 'Reference for the .tlide language (syntax, text, units/colors, template, master, slide, animation, target, assets, table, widgets, workflow) and, from the compiler, the objects, properties, enums, style properties, transitions and animations available in this document. Call without topic for the topic list; an object, template, style or enum name gives its signature.',
        inputSchema: { type: 'object', properties: { topic: { type: 'string', description: 'Topic or name, e.g. "table", "objects", "text_box"' } } },
    },
    {
        name: 'get_resources',
        description: 'Files this document reads: #included files (package files are under the compiler\'s packages folder; read them to see widget templates), the .tasset bundles with their file names and which is the default, and the names given to images, videos and audios.',
        inputSchema: { type: 'object', properties: { entries: { type: 'integer', minimum: 1, description: 'Most file names to list per bundle (default 40)' } } },
    },
    {
        name: 'read_document',
        description: 'Read the .tlide source open in the editor, with line numbers. Includes unsaved changes.',
        inputSchema: {
            type: 'object',
            properties: {
                offset: { type: 'integer', minimum: 1, description: 'First line to read (1-based)' },
                limit: { type: 'integer', minimum: 1, description: 'Number of lines to read' },
            },
        },
    },
    {
        name: 'edit_document',
        description: 'Replace old_string with new_string in the source open in the editor. old_string must occur exactly once unless replace_all is set. The user can undo with Ctrl+Z. Returns compile errors after recompiling, and element ids that changed because their source moved (renamedIds: old id -> new id).',
        inputSchema: {
            type: 'object',
            properties: {
                old_string: { type: 'string', description: 'Exact text to replace, without line numbers' },
                new_string: { type: 'string', description: 'Replacement text' },
                replace_all: { type: 'boolean', description: 'Replace every occurrence of old_string' },
            },
            required: ['old_string', 'new_string'],
        },
    },
    {
        name: 'render_slide',
        description: 'Render one slide to PNG, the same as the editor preview. Videos may not be drawn. Without page, the current slide.',
        inputSchema: { type: 'object', properties: { page: { type: 'integer', minimum: 1, description: 'Slide number (1-based)' } } },
    },
    {
        name: 'goto_slide',
        description: 'Show that slide in the editor.',
        inputSchema: { type: 'object', properties: { page: { type: 'integer', minimum: 1 } }, required: ['page'] },
    },
    {
        name: 'select_element',
        description: 'Select an element in the editor (to point it out to the user). id is an element id from get_editor_state; ids from before an edit are also accepted.',
        inputSchema: { type: 'object', properties: { id: { type: 'string' } }, required: ['id'] },
    },
    {
        name: 'build',
        description: 'Build the output file (pptx etc.) of the selected target from the current, possibly unsaved, source.',
        inputSchema: { type: 'object', properties: {} },
    },
];

const text = (value: string): Result => ({ content: [{ type: 'text', text: value }] });
const fail = (value: string): Result => ({ content: [{ type: 'text', text: value }], isError: true });
const json = (value: unknown) => text(JSON.stringify(value)); // 들여쓰지 않는다. 에이전트가 읽는 토큰을 줄인다
const problems = (list: Diagnostic[]) => list.map((d) => ({ line: d.range.start.line + 1, message: d.message }));

const clip = (value: string, limit: number) => value.length > limit ? value.slice(0, limit) + '…' : value;

// 코드에 적은 속성 값. 적지 않은 기본값(block)은 뺀다. 표의 cells는 table 쪽에 따로 준다
function written(info: ElementInfo): Record<string, unknown> {
    const result: Record<string, unknown> = {};
    for (const [name, value] of Object.entries(info.values)) {
        if (name !== 'cells' && info.properties[name]?.kind !== 'block') {
            result[name] = value;
        }
    }
    return result;
}

// 코드의 어느 줄들에 적었는지 [처음, 끝] (1부터)
const linesOf = (info: ElementInfo) => info.source.range ? [info.source.range.start.line + 1, info.source.range.end.line + 1] : undefined;

// 값을 바꿀 수 없는 속성과 그 이유 (template 안에서 정한 개체는 통틀어 fromTemplate로 알린다)
function lockedOf(info: ElementInfo): Record<string, string> | undefined {
    if (info.fromTemplate) {
        return undefined;
    }
    const entries = Object.entries(info.properties).filter(([, origin]) => origin.kind === 'locked').map(([name, origin]) => [name, origin.reason ?? 'locked']);
    return entries.length ? Object.fromEntries(entries) : undefined;
}

// 고른 표 칸들의 글과 따로 정한 서식. 칸 번호는 코드의 cell(행, 열)처럼 1부터
function selectedCells(info: ElementInfo, rect: Rect) {
    const table = info.table;
    if (!table?.cells) {
        return undefined;
    }
    const cells: unknown[] = [];
    for (let row = rect.r1; row <= rect.r2; row++) {
        for (let column = rect.c1; column <= rect.c2; column++) {
            const cell = table.cells[row]?.[column];
            if (!cell || cell.covered || cells.length >= 30) {
                continue;
            }
            const style = table.cellStyles?.find((each) => each.row === row && each.column === column);
            cells.push({
                cell: `${row + 1},${column + 1}`,
                text: clip(cell.text ?? '', 80),
                span: cell.rowSpan > 1 || cell.columnSpan > 1 ? `${cell.rowSpan}x${cell.columnSpan}` : undefined,
                format: style ? style.values : undefined,
            });
        }
    }
    return { rows: table.rows, columns: table.columns, editable: table.editable, selection: { rows: [rect.r1 + 1, rect.r2 + 1], columns: [rect.c1 + 1, rect.c2 + 1] }, cells };
}

// 문서가 읽는 파일: #include한 파일과 .tasset 묶음, 이름 붙인 그림, 비디오, 오디오
function resources(schema: Schema, entries: number) {
    const packages = schema.files.filter((file) => file.package);
    const root = packages.map((file) => /^(.*[\\/]packages)[\\/]/i.exec(file.path)?.[1]).find(Boolean);
    return {
        packagesFolder: root,
        includedPackages: packages.map((file) => root ? file.path.slice(root.length + 1).replace(/\\/g, '/').replace(/\.tlide$/i, '') : file.path),
        documents: schema.files.filter((file) => !file.package).map((file) => file.path),
        assets: schema.assets.map((asset) => ({
            bundle: asset.bundle,
            default: asset.default || undefined,
            files: asset.entries.slice(0, entries),
            more: asset.entries.length > entries ? asset.entries.length - entries : undefined,
        })),
        named: Object.keys(schema.constants).length ? schema.constants : undefined,
    };
}

// 요소의 글자. 문단은 줄을 바꿔 잇는다
function elementText(deck: DeckResult, index: number, id: string): string | undefined {
    const el = findElement(deck.deck.slides[index]?.els ?? [], id);
    const value = el?.tx?.ps?.map((paragraph: any) => paragraph.rs.map((run: any) => run.t).join('')).join('\n');
    return value || undefined;
}

// 그린 슬라이드의 그림이 다 불러와질 때까지
async function loaded(root: HTMLElement) {
    await document.fonts.ready;
    await Promise.all(Array.from(root.querySelectorAll('img')).map((img) => img.complete ? null : new Promise((done) => {
        img.addEventListener('load', done, { once: true });
        img.addEventListener('error', done, { once: true });
    })));
}

async function render(deck: DeckResult, index: number): Promise<string> {
    // 화면 밖에 슬라이드 크기로 그린다
    const host = document.createElement('div');
    host.style.cssText = `position: fixed; left: -100000px; top: 0; width: ${deck.width}px; height: ${deck.height}px; overflow: hidden;`;
    document.body.appendChild(host);
    try {
        const { stage, fit } = window.Templide.renderSlide(deck.deck, index);
        host.appendChild(stage);
        fit();
        await loaded(stage);
        const url = await toPng(stage, { width: deck.width, height: deck.height, pixelRatio: 1, skipFonts: true });
        return url.slice(url.indexOf(',') + 1);
    } finally {
        host.remove();
    }
}

async function run(access: () => EditorAccess, documents: () => OpenDocument[], name: string, args: any): Promise<Result> {
    const state = access();
    const deck = state.deck;
    const count = deck?.deck.slides.length ?? 0;
    const pageOf = (value: unknown) => typeof value === 'number' ? value - 1 : state.page;
    switch (name) {
        case 'get_editor_state': {
            const index = pageOf(args.page);
            const slide = deck?.slides[index];
            const elements = deck ? Object.entries(deck.elements).filter(([id]) => Number(id.split(/[/#]/)[0]) === index + 1).map(([id, info]) => {
                const box = ['x', 'y', 'width', 'height'].map((key) => info.values[key]);
                return {
                    id, object: info.object, name: info.name, line: info.source.range ? info.source.range.start.line + 1 : undefined,
                    text: clip(elementText(deck, index, id) ?? '', 60) || undefined,
                    box: box.every((value) => typeof value === 'number') ? box.map((value) => Math.round(value as number)) : undefined,
                    values: args.verbose ? written(info) : undefined,
                };
            }) : [];
            const cursor = state.cursor();
            return json({
                files: documents(),
                path: state.path,
                dirty: state.dirty || undefined,
                slideCount: count,
                currentPage: state.page + 1,
                selected: state.selected ?? undefined,
                cursorLine: cursor?.line,
                target: deck?.target ?? undefined,
                targets: deck?.targets.map((each) => `${each.name}:${each.type}`),
                size: deck ? [deck.width, deck.height] : undefined,
                stale: state.stale || undefined,
                errors: problems(state.diagnostics),
                warnings: problems(state.warnings),
                slide: slide && { page: index + 1, section: slide.section, line: slide.source.range ? slide.source.range.start.line + 1 : undefined, notes: slide.notes ? clip(slide.notes, 200) : undefined, elements },
            });
        }
        case 'get_selection': {
            const cursor = state.cursor();
            const info = state.selected ? deck?.elements[state.selected] : undefined;
            return json({
                path: state.path,
                cursor: cursor ? { line: cursor.line, column: cursor.column } : undefined,
                // 화면에서 요소를 고르면 편집기가 그 요소의 코드를 고르므로, 요소와 같은 줄들이면 글을 되풀이하지 않는다
                codeSelection: cursor?.selection && !(info && linesOf(info)?.join() === `${cursor.selection.from},${cursor.selection.to}`)
                    ? { lines: [cursor.selection.from, cursor.selection.to], text: clip(cursor.selection.text, 800) }
                    : undefined,
                element: info && state.selected ? {
                    id: state.selected,
                    object: info.object,
                    name: info.name,
                    page: Number(state.selected.split(/[/#]/)[0]),
                    lines: linesOf(info),
                    text: deck ? clip(elementText(deck, Number(state.selected.split(/[/#]/)[0]) - 1, state.selected) ?? '', 400) || undefined : undefined,
                    fromTemplate: info.fromTemplate || undefined,
                    locked: lockedOf(info),
                    values: written(info),
                } : undefined,
                table: info?.table && state.cells ? selectedCells(info, state.cells) : undefined,
                note: !cursor && !info ? 'Nothing is selected' : undefined,
            });
        }
        case 'get_language_guide':
            return text(guide(typeof args.topic === 'string' ? args.topic : undefined, state.schema));
        case 'get_resources': {
            if (!state.schema) {
                return fail('The document has not been analyzed yet; fix the compile errors first');
            }
            return json(resources(state.schema, typeof args.entries === 'number' ? args.entries : 40));
        }
        case 'read_document': {
            if (state.text === null) {
                return fail('The editor is not ready yet');
            }
            const lines = state.text.split(/\r?\n/);
            const start = Math.max(1, args.offset ?? 1);
            const end = Math.min(lines.length, start - 1 + (args.limit ?? lines.length));
            const width = String(end).length;
            const body = lines.slice(start - 1, end).map((line, i) => `${String(start + i).padStart(width)}\t${line}`).join('\n');
            return text(`${state.path ?? '(unsaved document)'}${state.dirty ? ' (has unsaved changes)' : ''}\n${body}`);
        }
        case 'edit_document': {
            if (typeof args.old_string !== 'string' || typeof args.new_string !== 'string' || !args.old_string) {
                return fail('old_string and new_string are required');
            }
            const result = await state.edit(args.old_string, args.new_string, !!args.replace_all);
            if (result.error) {
                return fail(result.error);
            }
            // 다시 컴파일한 뒤의 상태
            const after = access();
            const errors = problems(after.diagnostics);
            return json({ replaced: result.count, renamedIds: result.renamed, errors, warnings: problems(after.warnings) });
        }
        case 'render_slide': {
            if (!deck || count === 0) {
                return fail('There are no slides to render');
            }
            const index = pageOf(args.page);
            if (index < 0 || index >= count) {
                return fail(`Slides are numbered 1 to ${count}`);
            }
            const data = await render(deck, index);
            const note = state.stale ? ' (the source has errors; this is the last successful render)' : '';
            return { content: [{ type: 'image', data, mimeType: 'image/png' }, { type: 'text', text: `Slide ${index + 1} of ${count}, ${deck.width} x ${deck.height}px${note}` }] };
        }
        case 'goto_slide': {
            const index = pageOf(args.page);
            if (index < 0 || index >= count) {
                return fail(`Slides are numbered 1 to ${count}`);
            }
            state.goto(index);
            return text(`Showing slide ${index + 1}`);
        }
        case 'select_element': {
            const id = typeof args.id === 'string' ? state.resolve(args.id) : null;
            if (!id) {
                return fail(`No element ${args.id}. Check ids with get_editor_state`);
            }
            state.select(id);
            return text(id === args.id ? `Selected ${id}` : `Selected ${id} (was ${args.id})`);
        }
        case 'build': {
            const result = await state.build();
            if (!result) {
                return fail('No document is open in the editor');
            }
            if (result.error || result.errors?.length) {
                return fail([result.error, ...(result.errors ?? []), ...(result.warnings ?? [])].filter(Boolean).join('\n'));
            }
            return json({ path: result.path, warnings: result.warnings });
        }
        default:
            return fail(`Unknown tool ${name}`);
    }
}

// 도구 목록을 서버에 알리고 호출을 받는다. session은 에이전트를 실행한 탭이다 (비었으면 보이는 탭)
export async function serveMcp(access: (session: string) => EditorAccess, documents: (session: string) => OpenDocument[]) {
    await invoke('mcp_set_tools', { tools });
    await listen<{ call: number; session?: string; name: string; arguments: any }>('mcp-call', async ({ payload }) => {
        let result: Result;
        try {
            result = await run(() => access(payload.session ?? ''), () => documents(payload.session ?? ''), payload.name, payload.arguments ?? {});
        } catch (error) {
            result = fail(String(error));
        }
        invoke('mcp_reply', { call: payload.call, result });
    });
}

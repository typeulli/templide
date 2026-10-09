// 이름 찾기: 마우스를 올린 이름의 정보, Ctrl+클릭(F12)으로 정의로 이동, 참조 찾기(Shift+F12), 같은 이름 강조,
// 이름 바꾸기(F2), 개요(Ctrl+Shift+O), 자동 완성, 색 상자와 색 선택기. 모두 컴파일러(textDocument/..., templide/colors)에 묻는다.
// font("...")의 글자는 그 폰트로 보이고, 옆의 ▾를 누르면 폰트를 고른다.
// 다른 파일(include한 파일, 패키지)의 정의는 편집기를 바꾸지 않고 읽기 전용 미리보기(peek)로 보여 준다.
// 탭마다 편집기가 있으므로 제공자는 한 번만 등록하고, 물어 온 모델로 그 탭의 문서를 찾는다
import { invoke } from '@tauri-apps/api/core';
import type * as Monaco from 'monaco-editor/editor/editor.api';
import { uriToPath, type Lsp, type Range } from './lsp';
import { t } from './i18n';

type Location = { uri: string; range: Range };
type OutlineSymbol = { name: string; detail?: string; kind: number; range: Range; selectionRange: Range; children?: OutlineSymbol[] };

// 탭에서 편집 중인 문서
type Document = {
    uri: () => string | null; // 컴파일러 쪽 URI
    flush: () => void;        // 입력 중인 내용을 먼저 컴파일러에 보낸다
    pickFont: (range: Monaco.IRange) => void; // font("...") 옆의 ▾를 눌렀다. range는 따옴표 안의 글자
};

type ServerColor = { range: Range; color: { r: number; g: number; b: number; a: number }; space: string; editable: boolean };

// font("이름"). 1번 묶음이 따옴표 안의 글자다
const fontCall = /\bfont\(\s*"((?:[^"\\\n]|\\.)*)"\s*\)/g;

// 이름마다 CSS 클래스를 하나 만들어 <style>에 더한다 (Monaco 장식은 클래스로만 모양을 바꾼다)
const classes = new Map<string, string>();
let sheet: HTMLStyleElement | null = null;
function classFor(key: string, rule: (name: string) => string): string {
    let name = classes.get(key);
    if (!name) {
        name = `tlide-c${classes.size}`;
        classes.set(key, name);
        sheet ??= document.head.appendChild(document.createElement('style'));
        sheet.append(rule(name) + '\n');
    }
    return name;
}
const cssString = (text: string) => '"' + text.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
const swatchClass = ({ r, g, b, a }: ServerColor['color']) => classFor(`swatch ${r},${g},${b},${a}`, (name) => `.${name} { background: rgba(${r}, ${g}, ${b}, ${a}); }`);
const fontClass = (family: string) => classFor(`font ${family}`, (name) => `.${name} { font-family: ${cssString(family)}, Consolas, "Malgun Gothic", monospace; }`);
// 모델의 font("...")들: 따옴표 안 글자의 범위와 이름, font(...) 전체의 범위와 끝 위치(offset)
function fontCalls(model: Monaco.editor.ITextModel) {
    const range = (from: number, to: number): Monaco.IRange => {
        const start = model.getPositionAt(from);
        const end = model.getPositionAt(to);
        return { startLineNumber: start.lineNumber, startColumn: start.column, endLineNumber: end.lineNumber, endColumn: end.column };
    };
    return [...model.getValue().matchAll(fontCall)].map((match) => {
        const open = match.index! + match[0].indexOf('"') + 1;
        const end = match.index! + match[0].length;
        return { content: range(open, open + match[1].length), whole: range(match.index!, end), end, name: unescape(match[1]) };
    });
}

// 문자열 리터럴의 \" 같은 이스케이프를 푼다
const unescape = (text: string) => text.replace(/\\(.)/g, (_, c: string) => c === 'n' ? '\n' : c === 't' ? '\t' : c);

const documents = new Map<Monaco.editor.ITextModel, Document>(); // 탭 편집기의 모델 -> 문서
// 탭 편집기의 모델 -> 테마 색 상자 장식. 모델에 바로 붙인 글자 끼워 넣기(before)는 다시 그려지지 않아 편집기의 장식 묶음을 쓴다
const themeBoxes = new Map<Monaco.editor.ITextModel, Monaco.editor.IEditorDecorationsCollection>();
const serverUris = new Map<string, string>(); // 미리보기용 모델의 URI(문자열) -> 컴파일러 쪽 URI
let registered = false;

// 탭의 편집기를 이름 찾기와 자동 완성에 붙인다. 돌려준 함수를 부르면 뗀다 (탭을 닫을 때)
export function attachDocument(m: typeof Monaco, lsp: Lsp, editor: Monaco.editor.IStandaloneCodeEditor, document: Document): () => void {
    if (!registered) {
        registered = true;
        register(m, lsp);
    }
    const model = editor.getModel()!;
    documents.set(model, document);
    themeBoxes.set(model, editor.createDecorationsCollection());
    // font("...")의 글자를 그 폰트로 보이고, 닫는 괄호 뒤에 ▾를 끼워 넣는다. 입력이 잠깐 멈추면 다시 찾는다
    const fonts = editor.createDecorationsCollection();
    let timer = 0;
    const showFonts = () => {
        const found: Monaco.editor.IModelDeltaDecoration[] = [];
        for (const call of fontCalls(model)) {
            if (call.name) {
                found.push({ range: call.content, options: { inlineClassName: fontClass(call.name), inlineClassNameAffectsLetterSpacing: true } });
            }
            found.push({
                range: call.whole,
                options: { after: { content: '\u25be', inlineClassName: 'tlide-font-button', inlineClassNameAffectsLetterSpacing: true } },
            });
        }
        fonts.set(found);
    };
    // ▾를 누르면 그 font("...")의 폰트 목록을 연다. Monaco의 inlay hint는 Ctrl+클릭으로만 명령을 실행하므로 직접 받는다
    const pressed = editor.onMouseDown((event) => {
        const injected = (event.target as { detail?: { injectedText?: { options?: { inlineClassName?: string | null } } } }).detail?.injectedText;
        if (event.target.type !== m.editor.MouseTargetType.CONTENT_TEXT || !injected?.options?.inlineClassName?.includes('tlide-font-button') || !event.target.position) {
            return;
        }
        const at = model.getOffsetAt(event.target.position);
        const call = fontCalls(model).find((each) => each.end === at);
        if (call) {
            event.event.preventDefault();
            event.event.stopPropagation();
            document.pickFont(call.content);
        }
    });
    showFonts();
    const changed = model.onDidChangeContent(() => {
        window.clearTimeout(timer);
        timer = window.setTimeout(showFonts, 150);
    });
    return () => {
        window.clearTimeout(timer);
        changed.dispose();
        pressed.dispose();
        documents.delete(model);
        themeBoxes.delete(model);
    };
}

function register(m: typeof Monaco, lsp: Lsp) {
    const toRange = (range: Range) => new m.Range(range.start.line + 1, range.start.character + 1, range.end.line + 1, range.end.character + 1);
    const toPosition = (position: Monaco.Position) => ({ line: position.lineNumber - 1, character: position.column - 1 });

    const loading = new Map<string, Promise<Monaco.editor.ITextModel | null>>(); // 컴파일러 쪽 URI -> 읽는 중인 모델

    const serverUri = (model: Monaco.editor.ITextModel) => documents.get(model)?.uri() ?? serverUris.get(model.uri.toString()) ?? null;

    // 다른 파일은 물을 때마다 디스크에서 다시 읽어 컴파일러가 본 내용과 맞춘다.
    // 마우스를 올릴 때와 누를 때 요청이 겹치므로 같은 파일은 한 번만 읽는다
    const load = async (uri: string) => {
        try {
            const text = await invoke<string>('read_text', { path: uriToPath(uri) });
            const resource = m.Uri.parse(uri);
            const existing = m.editor.getModel(resource);
            if (existing) {
                if (existing.getValue() !== text) {
                    existing.setValue(text);
                }
                return existing;
            }
            const model = m.editor.createModel(text, 'tlide', resource);
            serverUris.set(model.uri.toString(), uri);
            return model;
        } catch (error) {
            console.error('read_text', uri, error);
            return null;
        } finally {
            loading.delete(uri);
        }
    };

    // 다른 탭에서 열어 둔 파일이면 그 탭의 모델(저장하지 않은 내용)을 쓴다
    const modelFor = (uri: string): Promise<Monaco.editor.ITextModel | null> => {
        for (const [model, document] of documents) {
            if (document.uri() === uri) {
                return Promise.resolve(model);
            }
        }
        let pending = loading.get(uri);
        if (!pending) {
            pending = load(uri);
            loading.set(uri, pending);
        }
        return pending;
    };

    const toLocations = async (locations: Location[] | null) => {
        const result: Monaco.languages.Location[] = [];
        for (const location of locations ?? []) {
            const model = await modelFor(location.uri);
            if (model) {
                result.push({ uri: model.uri, range: toRange(location.range) });
            }
        }
        return result;
    };

    // 요청에 쓸 문서와 위치. 문서가 없으면 null
    const params = (model: Monaco.editor.ITextModel, position: Monaco.Position) => {
        const uri = serverUri(model);
        if (!uri) {
            return null;
        }
        documents.get(model)?.flush();
        return { textDocument: { uri }, position: toPosition(position) };
    };

    m.languages.registerHoverProvider('tlide', {
        provideHover: async (model, position) => {
            const request = params(model, position);
            const result = request && await lsp.request<{ contents: { value: string }; range: Range } | null>('textDocument/hover', request);
            return result ? { contents: [{ value: result.contents.value }], range: toRange(result.range) } : null;
        },
    });

    // 마지막으로 돌려준 다른 파일의 정의. 그 파일을 열려고 하면 미리보기로 바꾼다
    let lastForeign: string | null = null;
    m.languages.registerDefinitionProvider('tlide', {
        provideDefinition: async (model, position) => {
            const request = params(model, position);
            const locations = await toLocations(request && await lsp.request<Location[]>('textDocument/definition', request));
            lastForeign = locations.find((location) => location.uri.toString() !== model.uri.toString())?.uri.toString() ?? null;
            return locations;
        },
    });

    m.languages.registerReferenceProvider('tlide', {
        provideReferences: async (model, position, context) => {
            const request = params(model, position);
            return toLocations(request && await lsp.request<Location[]>('textDocument/references', { ...request, context }));
        },
    });

    m.languages.registerDocumentHighlightProvider('tlide', {
        provideDocumentHighlights: async (model, position) => {
            const request = params(model, position);
            const result = request && await lsp.request<{ range: Range; kind: number }[]>('textDocument/documentHighlight', request);
            // LSP의 DocumentHighlightKind는 1부터, Monaco는 0부터
            return (result ?? []).map((highlight) => ({ range: toRange(highlight.range), kind: highlight.kind - 1 }));
        },
    });

    m.languages.registerRenameProvider('tlide', {
        resolveRenameLocation: async (model, position) => {
            const request = params(model, position);
            if (!request || !documents.has(model)) {
                return { range: new m.Range(1, 1, 1, 1), text: '', rejectReason: t('편집 중인 문서에서만 이름을 바꿀 수 있습니다') };
            }
            try {
                const result = await lsp.request<{ range: Range; placeholder: string }>('textDocument/prepareRename', request);
                return { range: toRange(result.range), text: result.placeholder };
            } catch (error) {
                return { range: new m.Range(position.lineNumber, position.column, position.lineNumber, position.column), text: '', rejectReason: (error as Error).message };
            }
        },
        provideRenameEdits: async (model, position, newName) => {
            const request = params(model, position);
            if (!request) {
                return { edits: [], rejectReason: t('문서가 열려 있지 않습니다') };
            }
            try {
                const result = await lsp.request<{ changes: Record<string, { range: Range; newText: string }[]> }>('textDocument/rename', { ...request, newName });
                const edits: Monaco.languages.IWorkspaceTextEdit[] = [];
                for (const [uri, changes] of Object.entries(result.changes)) {
                    const target = await modelFor(uri);
                    for (const change of target ? changes : []) {
                        edits.push({ resource: target!.uri, textEdit: { range: toRange(change.range), text: change.newText }, versionId: undefined });
                    }
                }
                return { edits };
            } catch (error) {
                return { edits: [], rejectReason: (error as Error).message };
            }
        },
    });

    m.languages.registerDocumentSymbolProvider('tlide', {
        displayName: 'templide',
        provideDocumentSymbols: async (model) => {
            const uri = serverUri(model);
            if (!uri) {
                return [];
            }
            documents.get(model)?.flush();
            const convert = (symbol: OutlineSymbol): Monaco.languages.DocumentSymbol => ({
                name: symbol.name,
                detail: symbol.detail ?? '',
                kind: symbol.kind - 1, // LSP의 SymbolKind는 1부터, Monaco는 0부터
                tags: [],
                range: toRange(symbol.range),
                selectionRange: toRange(symbol.selectionRange),
                children: (symbol.children ?? []).map(convert),
            });
            const result = await lsp.request<OutlineSymbol[]>('textDocument/documentSymbol', { textDocument: { uri } });
            return result.map(convert);
        },
    });

    // 자동 완성은 컴파일러(textDocument/completion)에 묻는다. LSP의 종류 번호를 Monaco의 것으로 바꾼다
    const K = m.languages.CompletionItemKind;
    const kinds: Record<number, number> = {
        3: K.Function, 6: K.Variable, 7: K.Class, 9: K.Module, 10: K.Property, 12: K.Value, 14: K.Keyword, 15: K.Snippet,
        16: K.Color, 17: K.File, 20: K.EnumMember, 22: K.Struct,
    };
    m.languages.registerCompletionItemProvider('tlide', {
        triggerCharacters: ['.', '<', ' ', '=', '('],
        provideCompletionItems: async (model, position) => {
            const document = documents.get(model);
            const uri = document?.uri();
            if (!document || !uri) {
                return { suggestions: [] };
            }
            document.flush();
            const result = await lsp.request<{ items: any[] }>('textDocument/completion', {
                textDocument: { uri },
                position: toPosition(position),
            });
            return {
                suggestions: result.items.map((item) => ({
                    label: item.label,
                    kind: kinds[item.kind] ?? K.Text,
                    detail: item.detail,
                    insertText: item.textEdit.newText,
                    insertTextRules: item.insertTextFormat === 2 ? m.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
                    range: toRange(item.textEdit.range),
                    sortText: item.sortText,
                })),
            };
        },
    });

    // 색 상자와 색 선택기. 고칠 수 있는 색(값을 수로 적은 색 함수)은 Monaco가 상자를 그리고 누르면 색 선택기를 연다.
    // 테마 색(theme.accent1 등)은 고칠 수 없으므로 장식으로 상자만 그린다
    m.languages.registerColorProvider('tlide', {
        provideDocumentColors: async (model) => {
            const document = documents.get(model);
            const uri = document?.uri();
            if (!document || !uri) {
                return [];
            }
            document.flush();
            const result = await lsp.request<{ colors: ServerColor[] }>('templide/colors', { uri });
            if (model.isDisposed()) {
                return [];
            }
            const boxes: Monaco.editor.IModelDeltaDecoration[] = [];
            const colors: Monaco.languages.IColorInformation[] = [];
            for (const each of result.colors) {
                const range = toRange(each.range);
                if (each.editable) {
                    colors.push({ range, color: { red: each.color.r / 255, green: each.color.g / 255, blue: each.color.b / 255, alpha: each.color.a } });
                } else {
                    // Monaco의 색 상자와 같은 모양 (색 전체에 붙이고 글자 앞에 끼워 넣는다)
                    boxes.push({
                        range,
                        options: {
                            before: { content: '\u00a0', inlineClassName: 'tlide-swatch ' + swatchClass(each.color), inlineClassNameAffectsLetterSpacing: true },
                            hoverMessage: { value: t('테마 색입니다. 테마에서 바꿉니다') },
                        },
                    });
                }
            }
            themeBoxes.get(model)?.set(boxes);
            return colors;
        },
        // 첫 글자는 지금 적은 함수(hex, rgb, hsl, ... , oklch)의 것이다. 색 선택기 위쪽을 누르면 다음 함수로 바뀐다
        provideColorPresentations: async (model, info) => {
            const written = /^\s*([a-z]+)\s*\(/.exec(model.getValueInRange(info.range))?.[1] ?? 'hex';
            const space = written === 'rgba' ? 'rgb' : written === 'hsla' ? 'hsl' : written;
            const { red, green, blue, alpha } = info.color;
            const result = await lsp.request<{ labels: string[] }>('templide/color_presentations', {
                color: { r: Math.round(red * 255), g: Math.round(green * 255), b: Math.round(blue * 255), a: Math.round(alpha * 100) / 100 }, space,
            });
            return result.labels.map((label) => ({ label, textEdit: { range: info.range, text: label } }));
        },
    });

    // 다른 파일의 정의로 가려고 하면(Ctrl+클릭, F12) 그 자리에서 미리보기를 연다.
    // 같은 파일은 Monaco가 처리하도록 false를 돌려준다
    m.editor.registerEditorOpener({
        openCodeEditor: (source, resource) => {
            if (resource.toString() === source.getModel()?.uri.toString()) {
                return false;
            }
            if (resource.toString() === lastForeign) {
                lastForeign = null;
                source.trigger('navigation', 'editor.action.peekDefinition', null);
            }
            return true;
        },
    });

    // 미리보기에 보이는 다른 파일은 저장할 수 없으므로 고치지 못하게 한다
    m.editor.onDidCreateEditor((created) => {
        const update = () => {
            const model = created.getModel();
            const foreign = !!model && serverUris.has(model.uri.toString());
            created.updateOptions({ readOnly: foreign, readOnlyMessage: { value: t('다른 파일이라 여기서 고칠 수 없습니다. 그 파일을 열어 고쳐 주세요') } });
        };
        created.onDidChangeModel(update);
        update();
    });
}

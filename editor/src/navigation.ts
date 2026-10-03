// 이름 찾기: 마우스를 올린 이름의 정보, Ctrl+클릭(F12)으로 정의로 이동, 참조 찾기(Shift+F12), 같은 이름 강조,
// 이름 바꾸기(F2), 개요(Ctrl+Shift+O). 모두 컴파일러(textDocument/...)에 묻는다.
// 다른 파일(include한 파일, 패키지)의 정의는 편집기를 바꾸지 않고 읽기 전용 미리보기(peek)로 보여 준다
import { invoke } from '@tauri-apps/api/core';
import type * as Monaco from 'monaco-editor/editor/editor.api';
import { uriToPath, type Lsp, type Range } from './lsp';

type Location = { uri: string; range: Range };
type OutlineSymbol = { name: string; detail?: string; kind: number; range: Range; selectionRange: Range; children?: OutlineSymbol[] };

type Options = {
    lsp: Lsp;
    editor: Monaco.editor.IStandaloneCodeEditor;
    mainUri: () => string | null; // 편집 중인 문서의 컴파일러 쪽 URI
    flush: () => void;             // 입력 중인 내용을 먼저 컴파일러에 보낸다
};

export function registerNavigation(m: typeof Monaco, { lsp, editor, mainUri, flush }: Options) {
    const toRange = (range: Range) => new m.Range(range.start.line + 1, range.start.character + 1, range.end.line + 1, range.end.character + 1);
    const toPosition = (position: Monaco.Position) => ({ line: position.lineNumber - 1, character: position.column - 1 });

    // Monaco 모델 URI(문자열) -> 컴파일러 쪽 URI. 다른 파일은 미리보기용 모델을 만들어 둔다
    const serverUris = new Map<string, string>();
    const loading = new Map<string, Promise<Monaco.editor.ITextModel | null>>(); // 컴파일러 쪽 URI -> 읽는 중인 모델

    const serverUri = (model: Monaco.editor.ITextModel) => model === editor.getModel() ? mainUri() : serverUris.get(model.uri.toString()) ?? null;

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

    const modelFor = (uri: string): Promise<Monaco.editor.ITextModel | null> => {
        if (uri === mainUri()) {
            return Promise.resolve(editor.getModel());
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
        if (model === editor.getModel()) {
            flush();
        }
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
            if (!request || model !== editor.getModel()) {
                return { range: new m.Range(1, 1, 1, 1), text: '', rejectReason: '편집 중인 문서에서만 이름을 바꿀 수 있습니다' };
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
                return { edits: [], rejectReason: '문서가 열려 있지 않습니다' };
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
            if (model === editor.getModel()) {
                flush();
            }
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
            created.updateOptions({ readOnly: foreign, readOnlyMessage: { value: '다른 파일이라 여기서 고칠 수 없습니다. 그 파일을 열어 고쳐 주세요' } });
        };
        created.onDidChangeModel(update);
        update();
    });
}

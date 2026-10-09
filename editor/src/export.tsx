// 탐색기의 'templide 내보내기'로 연 창 (templide-editor --export <파일>). 편집기를 열지 않고 고른 target만 만든다
import { createRoot } from 'react-dom/client';
import { invoke } from '@tauri-apps/api/core';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { Lsp, pathToUri } from './lsp';
import { initSettings } from './settings';
import { ExportDialog, type BuildResult, type ExportTarget } from './ExportDialog';
import './styles.css';
import { t } from './i18n';

const lsp = new Lsp();

// 컴파일러(언어 서버)에 파일을 열고 target 목록을 받는다
async function load(uri: string, path: string) {
    await lsp.start();
    await lsp.request('initialize', { processId: null, rootUri: null, capabilities: {} });
    lsp.notify('initialized', {});
    const text = await invoke<string>('read_text', { path });
    lsp.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'tlide', version: 1, text } });
    return lsp.request<{ targets?: ExportTarget[]; error?: string }>('templide/targets', { uri });
}

async function main() {
    await initSettings().catch((error) => console.error('settings', error));
    const path = (await invoke<string | null>('export_file')) ?? '';
    const uri = pathToUri(path);
    const fileName = path.split(/[\\/]/).pop() ?? path;
    const window = getCurrentWindow();
    await window.setTitle(t('templide 내보내기 - {0}', fileName));
    createRoot(document.getElementById('root')!).render(
        <ExportDialog standalone fileName={fileName}
            load={() => load(uri, path)}
            build={(target) => lsp.request<BuildResult>('templide/build', { uri, target })}
            onClose={() => window.close()}
            onDone={() => window.close()} />,
    );
    // 창은 숨긴 채로 만든다(src-tauri/src/main.rs). 화면을 한 번 그린 뒤에 보여 준다
    requestAnimationFrame(() => requestAnimationFrame(() => window.show()));
}

main();

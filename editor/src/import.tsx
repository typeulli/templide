// 탐색기의 'templide 불러오기'로 연 창 (templide-editor --import <pptx>). 편집기를 열지 않고 pptx를 골라 .tlide로 바꾼다
import { createRoot } from 'react-dom/client';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { Lsp } from './lsp';
import { initSettings } from './settings';
import { ImportDialog } from './ImportDialog';
import './styles.css';

const lsp = new Lsp();

// 컴파일러(언어 서버)를 시작한다. pptx를 읽고 바꾸는 일은 컴파일러가 한다
async function start() {
    await lsp.start();
    await lsp.request('initialize', { processId: null, rootUri: null, capabilities: {} });
    lsp.notify('initialized', {});
}

const window = getCurrentWindow();
// 설정(언어)을 읽은 뒤에 그린다
initSettings().catch((error) => console.error('settings', error)).finally(() => {
    createRoot(document.getElementById('root')!).render(
        <ImportDialog ready={start()} request={(method, params) => lsp.request(method, params)}
            onTitle={(title) => window.setTitle(title)} onClose={() => window.close()} />,
    );
    // 창은 숨긴 채로 만든다(src-tauri/src/main.rs). 화면을 한 번 그린 뒤에 보여 준다
    requestAnimationFrame(() => requestAnimationFrame(() => window.show()));
});

// 슬라이드 쇼 창. 편집기 창이 덱을 "show-deck" 이벤트로 넘기면 templide.js(reveal.js)로 띄운다. Esc로 닫는다.
// 마지막 슬라이드의 마지막 단계에서 넘기면 PowerPoint처럼 끝 화면을 보여 주고, 한 번 더 넘기면 닫는다
import { emitTo, listen } from '@tauri-apps/api/event';
import { getCurrentWebviewWindow } from '@tauri-apps/api/webviewWindow';
import { convertFileSrc } from '@tauri-apps/api/core';

// 편집기 덱의 비디오, 오디오("file:경로")는 Tauri의 asset 주소로 읽는다
window.Templide.mediaUrl = (path) => convertFileSrc(path);

const current = getCurrentWebviewWindow();
const nextKeys = new Set(['ArrowRight', 'ArrowDown', 'PageDown', ' ', 'Enter', 'n', 'N']);
const previousKeys = new Set(['ArrowLeft', 'ArrowUp', 'PageUp', 'Backspace', 'p', 'P']);

let reveal: any = null;
let loop = false;

const end = document.createElement('div');
end.className = 'show-end';
end.textContent = '슬라이드 쇼가 끝났습니다. 넘기면 닫힙니다.';
end.style.cssText = 'position: fixed; inset: 0; z-index: 100; display: none; align-items: center; justify-content: center; '
    + 'background: #000; color: #fff; font: 24px "Malgun Gothic", "Segoe UI", sans-serif; cursor: default;';
document.body.appendChild(end);

const ended = () => end.style.display === 'flex';
const showEnd = (visible: boolean) => {
    end.style.display = visible ? 'flex' : 'none';
};

// 더 넘길 슬라이드도 단계(애니메이션)도 없다
const atEnd = () => reveal !== null && !loop && reveal.isLastSlide() && !reveal.availableFragments().next;

// 다음으로 넘기는 입력이면 true를 돌려주고 reveal.js에는 보내지 않는다
const next = (event: Event) => {
    if (ended()) {
        current.close();
    } else if (atEnd()) {
        showEnd(true);
    } else {
        return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
};

// reveal.js보다 먼저 받는다 (Esc는 reveal.js에서 개요 보기라 창을 닫는 데 쓴다)
window.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
        event.stopImmediatePropagation();
        current.close();
    } else if (nextKeys.has(event.key)) {
        next(event);
    } else if (previousKeys.has(event.key) && ended()) {
        event.preventDefault();
        event.stopImmediatePropagation();
        showEnd(false);
    }
}, true);

window.addEventListener('click', (event) => {
    if (event.button === 0 && !(event.target as HTMLElement).closest('a, button, .tl-link, .controls')) {
        next(event);
    }
}, true);

// end_show 동작은 슬라이드 쇼 창을 닫는다
window.addEventListener('templide-endshow', (event) => {
    event.preventDefault();
    current.close();
});

await listen<{ deck: any; page: number; script: string | null }>('show-deck', (event) => {
    // target의 script(run(...) 동작이 부르는 함수)를 덱보다 먼저 실행한다. 일반 스크립트처럼 함수가 전역에 생긴다
    if (event.payload.script) {
        const script = document.createElement('script');
        script.textContent = event.payload.script;
        document.body.appendChild(script);
    }
    loop = !!event.payload.deck.loop;
    reveal = window.Templide.mount(document.querySelector('.reveal')!, event.payload.deck);
    const go = () => reveal.slide(event.payload.page);
    if (reveal.isReady()) {
        go();
    } else {
        reveal.on('ready', go);
    }
});
await emitTo('main', 'show-ready');

import { createRoot } from 'react-dom/client';
import { convertFileSrc } from '@tauri-apps/api/core';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { App } from './App';
import './styles.css';

// 편집기 덱의 비디오, 오디오("file:경로")는 Tauri의 asset 주소로 읽는다
window.Templide.mediaUrl = (path) => convertFileSrc(path);

createRoot(document.getElementById('root')!).render(<App />);

// 창은 숨긴 채로 만든다(tauri.conf.json). 화면을 한 번 그린 뒤에 보여 주어 처음의 하얀 창을 없앤다
requestAnimationFrame(() => requestAnimationFrame(() => getCurrentWindow().show()));

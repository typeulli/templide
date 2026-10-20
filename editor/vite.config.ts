import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// 저장소의 libs(templide.js)를 그대로 화면의 정적 파일로 쓴다
export default defineConfig({
    plugins: [react()],
    publicDir: '../libs',
    clearScreen: false,
    // 로고는 저장소의 assets에서 읽는다
    // src-tauri(특히 Rust 빌드 산출물 target, 파일 수만 개)는 감시하지 않는다. 감시하면 개발 서버를 켤 때 파일마다 감시를 걸다가 10초 넘게 멈추고,
    // cargo가 빌드하는 동안에는 바뀌는 파일마다 서버가 일해서 첫 화면이 더 늦어진다. Rust 쪽은 tauri dev가 따로 감시해 다시 빌드한다
    server: { port: 5173, strictPort: true, fs: { allow: ['..'] }, watch: { ignored: ['**/src-tauri/**'] } },
    // 편집기(index.html), 슬라이드 쇼 창(show.html), 설정 창(settings.html), 오픈소스 라이선스 창(licenses.html), 탐색기에서 연 내보내기 창(export.html)과 pptx 불러오기 창(import.html)
    // 데스크톱 앱이라 파일을 디스크에서 읽으므로 큰 묶음(monaco 약 4MB)을 나누지 않는다. 그 경고만 끈다
    build: { target: 'chrome120', outDir: 'dist', emptyOutDir: true, chunkSizeWarningLimit: 5000, rollupOptions: { input: { main: 'index.html', show: 'show.html', settings: 'settings.html', licenses: 'licenses.html', export: 'export.html', import: 'import.html' } } },
});

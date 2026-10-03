import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// 저장소의 libs(templide.js)를 그대로 화면의 정적 파일로 쓴다
export default defineConfig({
    plugins: [react()],
    publicDir: '../libs',
    clearScreen: false,
    // 로고는 저장소의 assets에서 읽는다
    server: { port: 5173, strictPort: true, fs: { allow: ['..'] } },
    // 편집기(index.html), 슬라이드 쇼 창(show.html), 오픈소스 라이선스 창(licenses.html)
    // 데스크톱 앱이라 파일을 디스크에서 읽으므로 큰 묶음(monaco 약 4MB)을 나누지 않는다. 그 경고만 끈다
    build: { target: 'chrome120', outDir: 'dist', emptyOutDir: true, chunkSizeWarningLimit: 5000, rollupOptions: { input: { main: 'index.html', show: 'show.html', licenses: 'licenses.html' } } },
});

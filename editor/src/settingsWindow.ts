// 설정 창. 이미 열려 있으면 그 창을 앞으로 가져온다
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { t } from './i18n';

export async function openSettings() {
    const existing = await WebviewWindow.getByLabel('settings');
    if (existing) {
        await existing.show();
        await existing.setFocus();
        return;
    }
    // 화면을 한 번 그린 뒤에 보여 준다 (settings.tsx)
    new WebviewWindow('settings', {
        url: 'settings.html', title: t('설정'), width: 880, height: 640, minWidth: 680, minHeight: 460, center: true, focus: true, visible: false,
    });
}

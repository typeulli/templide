// 편집기 설정. 파일에 저장하는 일은 Rust(src-tauri/src/settings.rs)가 하고, 여기서는 설정의 모양과 기본값을 정한다.
// 설정 창에서 바꾸면 열려 있는 모든 창이 "settings-changed"로 받아 같은 값을 쓴다.
// 컴파일러 경로는 여기에 두지 않는다. 컴파일러를 띄우는 Rust(compiler_use)가 따로 저장한다
import { useSyncExternalStore } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { resolveLanguage, setLanguage, type LanguageSetting } from './i18n';

export type Settings = {
    language: LanguageSetting;
    general: {
        codePanelOpen: boolean; // 코드 패널을 펼친 채로 시작한다
    };
    editor: {
        fontSize: number;
        wordWrap: boolean;
        minimap: boolean;
        lineNumbers: boolean;
        tabSize: number;
    };
    show: {
        endScreen: boolean; // 마지막에서 한 번 더 넘기면 끝 화면을 보여 준다
    };
    agent: {
        default: string;               // AI 탭을 열 때 처음 고르는 에이전트 id
        paths: Record<string, string>; // 에이전트 id -> 실행 파일 경로. 없으면 PATH에서 찾는다
    };
    // 명령 id -> 키. 기본값에서 바꾼 명령만 있고, 빈 배열이면 키를 모두 뗀 것이다 (shortcuts.ts)
    shortcuts: Record<string, string[]>;
};

export const defaultSettings: Settings = {
    language: 'system',
    general: { codePanelOpen: true },
    editor: { fontSize: 14, wordWrap: false, minimap: false, lineNumbers: true, tabSize: 4 },
    show: { endScreen: true },
    agent: { default: 'claude-code', paths: {} },
    shortcuts: {},
};

export const editorLimits = { fontSize: [8, 40], tabSize: [1, 8] } as const;

// 저장된 값과 기본값을 합친다. 파일을 손으로 고쳐 모양이 틀려도 그 항목만 기본값으로 돌아간다
function merge<T>(base: T, stored: unknown): T {
    if (Array.isArray(base) || base === null || typeof base !== 'object') {
        return typeof stored === typeof base ? stored as T : base;
    }
    const given = stored !== null && typeof stored === 'object' ? stored as Record<string, unknown> : {};
    const out: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(base)) {
        out[key] = merge(value, given[key]);
    }
    return out as T;
}

function parse(stored: unknown): Settings {
    const merged = merge(defaultSettings, stored);
    const given = (stored ?? {}) as Partial<Settings>;
    // 값을 정할 수 없는 모양은 직접 거른다
    if (!['system', 'ko', 'en'].includes(merged.language)) {
        merged.language = defaultSettings.language;
    }
    const [minSize, maxSize] = editorLimits.fontSize;
    const [minTab, maxTab] = editorLimits.tabSize;
    merged.editor.fontSize = Math.min(Math.max(Math.round(merged.editor.fontSize) || defaultSettings.editor.fontSize, minSize), maxSize);
    merged.editor.tabSize = Math.min(Math.max(Math.round(merged.editor.tabSize) || defaultSettings.editor.tabSize, minTab), maxTab);
    merged.shortcuts = Object.fromEntries(
        Object.entries(given.shortcuts ?? {}).filter(([, keys]) => Array.isArray(keys) && keys.every((key) => typeof key === 'string')),
    ) as Settings['shortcuts'];
    merged.agent.paths = Object.fromEntries(
        Object.entries(given.agent?.paths ?? {}).filter(([, path]) => typeof path === 'string' && path.trim() !== ''),
    ) as Settings['agent']['paths'];
    return merged;
}

let current: Settings = defaultSettings;
const listeners = new Set<() => void>();

function apply(next: Settings) {
    current = next;
    setLanguage(resolveLanguage(next.language));
    listeners.forEach((listener) => listener());
}

export const getSettings = () => current;

// 창을 그리기 전에 한 번 부른다. 저장된 설정을 읽어 언어를 정하고, 다른 창이 바꾼 설정을 받기 시작한다
export async function initSettings(): Promise<void> {
    apply(parse(await invoke('settings_load').catch(() => ({}))));
    await listen<unknown>('settings-changed', (event) => apply(parse(event.payload)));
}

// 바꾼 값을 곧바로 쓰고 파일에도 저장한다
export function updateSettings(change: (settings: Settings) => Settings) {
    const next = parse(change(structuredClone(current)));
    apply(next);
    invoke('settings_save', { settings: next }).catch((error) => console.error('settings_save', error));
}

export function resetSettings() {
    updateSettings(() => structuredClone(defaultSettings));
}

const subscribe = (listener: () => void) => {
    listeners.add(listener);
    return () => {
        listeners.delete(listener);
    };
};

export function useSettings(): Settings {
    return useSyncExternalStore(subscribe, getSettings);
}

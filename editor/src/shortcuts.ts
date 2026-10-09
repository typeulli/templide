// 단축키. 명령마다 기본 키가 있고, 설정(settings.shortcuts)에 적힌 명령만 그 키로 바뀐다.
// 키는 "Mod+Shift+Z", "F5", "Delete"처럼 적는다. Mod는 Windows, Linux의 Ctrl이고 macOS의 ⌘이다 (코드에서는 둘 다 받는다).
// 순서는 Mod, Alt, Shift, 키 이름이다. 명령을 하는 곳은 각 화면(Shell.tsx, App.tsx)이고, 여기서는 눌린 키가 어느 명령인지만 정한다
import type * as Monaco from 'monaco-editor/editor/editor.api';
import { t } from './i18n';
import { getSettings } from './settings';

export type CommandId =
    | 'newFile' | 'openFile' | 'save' | 'closeTab' | 'nextTab' | 'previousTab' | 'export' | 'settings'
    | 'undo' | 'redo' | 'copy' | 'cut' | 'paste' | 'duplicate' | 'delete'
    | 'bold' | 'italic' | 'underline'
    | 'newSlide' | 'zoomIn' | 'zoomOut' | 'zoomFit'
    | 'showFromStart' | 'showFromCurrent';

// canvas: 글자를 입력하는 중(코드 편집기, 입력칸)에는 그 입력이 쓰므로 받지 않는다. window: 어디서나 받는다
type Scope = 'window' | 'canvas';

type CommandInfo = { group: string; label: string; defaults: string[]; scope: Scope };

// group과 label은 한국어 문장이다. 보여 줄 때 t()로 번역한다
export const commands: Record<CommandId, CommandInfo> = {
    newFile: { group: '파일', label: '새 파일', defaults: ['Mod+N'], scope: 'window' },
    openFile: { group: '파일', label: '열기', defaults: ['Mod+O'], scope: 'window' },
    save: { group: '파일', label: '저장', defaults: ['Mod+S'], scope: 'window' },
    closeTab: { group: '파일', label: '탭 닫기', defaults: ['Mod+W'], scope: 'window' },
    nextTab: { group: '파일', label: '다음 탭', defaults: ['Mod+Tab'], scope: 'window' },
    previousTab: { group: '파일', label: '이전 탭', defaults: ['Mod+Shift+Tab'], scope: 'window' },
    export: { group: '파일', label: '내보내기', defaults: ['Mod+E'], scope: 'window' },
    settings: { group: '파일', label: '설정', defaults: ['Mod+,'], scope: 'window' },
    undo: { group: '편집', label: '실행 취소', defaults: ['Mod+Z'], scope: 'canvas' },
    redo: { group: '편집', label: '다시 실행', defaults: ['Mod+Y', 'Mod+Shift+Z'], scope: 'canvas' },
    copy: { group: '편집', label: '복사', defaults: ['Mod+C'], scope: 'canvas' },
    cut: { group: '편집', label: '잘라내기', defaults: ['Mod+X'], scope: 'canvas' },
    paste: { group: '편집', label: '붙여넣기', defaults: ['Mod+V'], scope: 'canvas' },
    duplicate: { group: '편집', label: '복제', defaults: ['Mod+D'], scope: 'canvas' },
    delete: { group: '편집', label: '지우기', defaults: ['Delete'], scope: 'canvas' },
    bold: { group: '글자', label: '굵게', defaults: ['Mod+B'], scope: 'canvas' },
    italic: { group: '글자', label: '기울임', defaults: ['Mod+I'], scope: 'canvas' },
    underline: { group: '글자', label: '밑줄', defaults: ['Mod+U'], scope: 'canvas' },
    newSlide: { group: '슬라이드', label: '새 슬라이드', defaults: ['Mod+M'], scope: 'canvas' },
    zoomIn: { group: '보기', label: '확대', defaults: ['Mod+='], scope: 'canvas' },
    zoomOut: { group: '보기', label: '축소', defaults: ['Mod+-'], scope: 'canvas' },
    zoomFit: { group: '보기', label: '화면에 맞춤', defaults: ['Mod+0'], scope: 'canvas' },
    showFromStart: { group: '슬라이드 쇼', label: '처음부터 보기', defaults: ['F5'], scope: 'window' },
    showFromCurrent: { group: '슬라이드 쇼', label: '지금 슬라이드부터 보기', defaults: ['Shift+F5'], scope: 'window' },
};

export const commandIds = Object.keys(commands) as CommandId[];

export const isMac = navigator.userAgent.includes('Mac');

// 지금 명령에 걸린 키들. 설정에 없으면 기본 키
export function bindings(id: CommandId, shortcuts = getSettings().shortcuts): string[] {
    return shortcuts[id] ?? commands[id].defaults;
}

export function isCustomized(id: CommandId, shortcuts = getSettings().shortcuts): boolean {
    const keys = shortcuts[id];
    return keys !== undefined && (keys.length !== commands[id].defaults.length || keys.some((key, i) => key !== commands[id].defaults[i]));
}

const punctuation: Record<string, string> = {
    Minus: '-', Equal: '=', Comma: ',', Period: '.', Slash: '/', Backslash: '\\', Semicolon: ';', Quote: "'",
    BracketLeft: '[', BracketRight: ']', Backquote: '`',
};
const named = new Set([
    'Delete', 'Backspace', 'Enter', 'Tab', 'Escape', 'Insert', 'Home', 'End', 'PageUp', 'PageDown',
    'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
]);

// 눌린 키의 이름. 글자는 한글 입력 상태에서도 Ctrl+S가 S가 되도록 물리 키(code)로 정한다
function keyName(event: KeyboardEvent): string | null {
    const { key, code } = event;
    if (/^[a-zA-Z0-9]$/.test(key)) {
        return key.toUpperCase();
    }
    if (/^Key[A-Z]$/.test(code)) {
        return code.slice(3);
    }
    if (/^Digit[0-9]$/.test(code)) {
        return code.slice(5);
    }
    if (code in punctuation) {
        return punctuation[code];
    }
    if (key === ' ') {
        return 'Space';
    }
    if (/^F([1-9]|1[0-9]|2[0-4])$/.test(key) || named.has(key)) {
        return key;
    }
    return null; // Shift, Ctrl 같은 보조 키만 눌렀거나 알 수 없는 키
}

// 눌린 키를 위의 글자 모양으로. 키 이름이 없으면 null
export function eventBinding(event: KeyboardEvent): string | null {
    const key = keyName(event);
    if (!key) {
        return null;
    }
    return [event.ctrlKey || event.metaKey ? 'Mod' : '', event.altKey ? 'Alt' : '', event.shiftKey ? 'Shift' : '', key].filter(Boolean).join('+');
}

// 이 키가 단축키로 쓸 수 있는지. 아니면 이유(한국어 문장)
export function checkBinding(binding: string): string | null {
    const parts = binding.split('+');
    const key = parts[parts.length - 1];
    const modified = parts.includes('Mod') || parts.includes('Alt');
    if (key === 'Escape') {
        return 'Esc는 취소에 쓰므로 단축키로 쓸 수 없습니다';
    }
    const standalone = /^F([1-9]|1[0-9]|2[0-4])$/.test(key) || ['Delete', 'Backspace', 'Insert', 'Home', 'End', 'PageUp', 'PageDown'].includes(key);
    if (!modified && !standalone) {
        return 'Ctrl 또는 Alt와 함께 눌러 주세요';
    }
    return null;
}

// 눌린 키가 ids 중 어느 명령인지. 여러 명령에 같은 키가 걸려 있으면 앞의 것
export function matchCommand(event: KeyboardEvent, ids: readonly CommandId[], shortcuts = getSettings().shortcuts): CommandId | null {
    const binding = eventBinding(event);
    if (!binding) {
        return null;
    }
    return ids.find((id) => bindings(id, shortcuts).includes(binding)) ?? null;
}

// 글자를 입력하는 중인 곳. canvas 범위의 명령은 여기서 받지 않는다
export const isTextInput = (target: EventTarget | null) =>
    !!(target as HTMLElement | null)?.closest?.('.code-host, input, select, textarea, [contenteditable="true"]');

// 같은 키를 쓰는 다른 명령
export function conflictOf(id: CommandId, binding: string, shortcuts = getSettings().shortcuts): CommandId | null {
    return commandIds.find((other) => other !== id && bindings(other, shortcuts).includes(binding)) ?? null;
}

const macSymbols: Record<string, string> = { Mod: '⌘', Alt: '⌥', Shift: '⇧' };

// 화면에 보일 모양: Ctrl+Shift+Z, macOS는 ⌘⇧Z
export function formatBinding(binding: string): string {
    const parts = binding.split('+');
    const key = parts.pop()!;
    const modifiers = parts;
    const shown = key.startsWith('Arrow') ? key.slice(5) : key;
    if (isMac) {
        return modifiers.map((each) => macSymbols[each] ?? each).join('') + shown;
    }
    return [...modifiers.map((each) => each === 'Mod' ? 'Ctrl' : each), shown].join('+');
}

// 툴팁에 붙일 첫 키. 키가 없으면 빈 글자
export function shortcutText(id: CommandId): string {
    const first = bindings(id)[0];
    return first ? formatBinding(first) : '';
}

// "저장 (Ctrl+S)"
export function withShortcut(text: string, id: CommandId): string {
    const keys = shortcutText(id);
    return keys ? `${text} (${keys})` : text;
}

const monacoNamed: Record<string, string> = {
    Delete: 'Delete', Backspace: 'Backspace', Enter: 'Enter', Tab: 'Tab', Insert: 'Insert', Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown',
    ArrowUp: 'UpArrow', ArrowDown: 'DownArrow', ArrowLeft: 'LeftArrow', ArrowRight: 'RightArrow', Space: 'Space',
    '-': 'Minus', '=': 'Equal', ',': 'Comma', '.': 'Period', '/': 'Slash', ';': 'Semicolon', "'": 'Quote', '[': 'BracketLeft', ']': 'BracketRight',
    '\\': 'Backslash', '`': 'Backquote',
};

// 위의 글자 모양을 Monaco의 키 번호로. 바꿀 수 없으면 null
export function monacoKeybinding(monaco: typeof Monaco, binding: string): number | null {
    const parts = binding.split('+');
    const key = parts.pop()!;
    const codes = monaco.KeyCode as unknown as Record<string, number>;
    const name = /^[A-Z]$/.test(key) ? `Key${key}` : /^[0-9]$/.test(key) ? `Digit${key}` : monacoNamed[key] ?? key;
    const code = codes[name];
    if (code === undefined) {
        return null;
    }
    return (parts.includes('Mod') ? monaco.KeyMod.CtrlCmd : 0) | (parts.includes('Alt') ? monaco.KeyMod.Alt : 0) | (parts.includes('Shift') ? monaco.KeyMod.Shift : 0) | code;
}

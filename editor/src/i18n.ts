// 화면 문자열의 번역. 코드에는 한국어 문장이 그대로 있고 t('한국어 문장')으로 감싼다.
// 다른 언어는 locales/<언어>.ts가 한국어 문장 -> 그 언어 문장으로 짝지어 둔다 (gettext 방식). 짝이 없으면 한국어가 그대로 보인다.
// 문장에 넣을 값은 {0}, {1}로 적고 t('줄 {0}: {1}', line, text)처럼 넘긴다.
// 언어를 더하려면 locales/에 파일을 만들고 languages와 dictionaries에 한 줄씩 더한다
import { createElement, Fragment, useSyncExternalStore, type ReactNode } from 'react';
import { en } from './locales/en';

export type Language = 'ko' | 'en';
export type LanguageSetting = Language | 'system';

// 언어 이름은 번역하지 않고 그 언어로 적는다
export const languages: { id: Language; name: string }[] = [
    { id: 'ko', name: '한국어' },
    { id: 'en', name: 'English' },
];

// 한국어는 코드의 문장이 그대로 쓰이므로 사전이 없다
const dictionaries: Record<Language, Record<string, string> | null> = { ko: null, en };

let current: Language = 'ko';
const listeners = new Set<() => void>();

// 'system'이면 운영체제 언어를 따른다. 지원하지 않는 언어는 영어로 본다
export function resolveLanguage(setting: LanguageSetting): Language {
    if (setting !== 'system') {
        return setting;
    }
    return (navigator.language || 'ko').toLowerCase().startsWith('ko') ? 'ko' : 'en';
}

export function getLanguage(): Language {
    return current;
}

export function setLanguage(next: Language) {
    if (next === current) {
        return;
    }
    current = next;
    document.documentElement.lang = next;
    listeners.forEach((listener) => listener());
}

const missing = new Set<string>();

// 개발 중에 사전에 없는 문장을 한 번씩 알린다
function warnMissing(text: string) {
    if (import.meta.env.DEV && !missing.has(text)) {
        missing.add(text);
        console.warn(`[i18n] no ${current} translation: ${text}`);
    }
}

export function t(text: string, ...args: unknown[]): string {
    const dictionary = dictionaries[current];
    let translated = text;
    if (dictionary) {
        const found = dictionary[text];
        if (found === undefined) {
            warnMissing(text);
        } else {
            translated = found;
        }
    }
    return args.length === 0 ? translated : translated.replace(/\{(\d+)\}/g, (_, index) => String(args[Number(index)] ?? ''));
}

// 문장 가운데에 요소(JSX)를 넣을 때: tn('{0}처럼 추가해 주세요', <code>…</code>). 요소는 번역된 문장의 {0}, {1} 자리에 들어간다
export function tn(text: string, ...nodes: ReactNode[]): ReactNode {
    const parts = t(text).split(/(\{\d+\})/);
    return createElement(Fragment, null, ...parts.map((part, i) => {
        const placeholder = /^\{(\d+)\}$/.exec(part);
        return placeholder ? createElement(Fragment, { key: i }, nodes[Number(placeholder[1])]) : part;
    }));
}

const subscribe = (listener: () => void) => {
    listeners.add(listener);
    return () => {
        listeners.delete(listener);
    };
};

// 언어가 바뀌면 이 컴포넌트를 다시 그린다. t()는 그린 때의 언어를 쓰므로, 부모가 다시 그려도 그려지지 않는 컴포넌트(memo)는 이것을 부른다
export function useLang(): Language {
    return useSyncExternalStore(subscribe, getLanguage);
}

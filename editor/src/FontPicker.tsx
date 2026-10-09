// 폰트 고르기. 설치된 폰트를 검색하고, 이름마다 그 폰트로 그려 보여 준다.
// 코드의 font("...") 옆 목록, 서식 막대의 글꼴 메뉴, 속성 패널의 폰트 칸이 함께 쓴다
import { useEffect, useRef, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { quote } from './Fields';
import { t } from './i18n';

// 설치된 폰트 이름 (한국어 이름이 있으면 그것). 처음 한 번 편집기(src-tauri)에서 읽는다
let fonts: Promise<string[]> | null = null;
function systemFonts(): Promise<string[]> {
    fonts ??= invoke<string[]>('system_fonts').catch(() => []);
    return fonts;
}

// 코드에 적을 값
export const fontCode = (name: string) => `font(${quote(name)})`;

// CSS font-family. 없는 폰트면 편집기 글꼴로 보인다
export const cssFont = (name: string, fallback = 'sans-serif') => `"${name.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}", ${fallback}`;

// 맞는 폰트가 너무 많으면 앞의 것만 그린다 (폰트마다 글꼴을 불러오므로)
const shownLimit = 300;

// 검색 칸과 목록. 맞는 폰트가 없으면 적은 이름을 그대로 쓸 수 있다 (이 컴퓨터에 없는 폰트)
export function FontList({ current, onPick }: { current?: string; onPick(name: string): void }) {
    const [list, setList] = useState<string[] | null>(null);
    const [query, setQuery] = useState('');
    const [active, setActive] = useState(-1);
    const items = useRef<HTMLDivElement>(null);
    useEffect(() => {
        systemFonts().then(setList);
    }, []);
    const lower = query.trim().toLowerCase();
    const matches = (list ?? []).filter((name) => name.toLowerCase().includes(lower));
    const shown = matches.slice(0, shownLimit);
    // 처음에는 지금 폰트에 둔다
    useEffect(() => {
        if (list && current && !query) {
            const at = list.indexOf(current);
            setActive(at);
            items.current?.querySelectorAll('button')[at]?.scrollIntoView({ block: 'center' });
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [list]);
    const pick = () => {
        if (active >= 0 && shown[active]) {
            onPick(shown[active]);
        } else if (query.trim()) {
            onPick(query.trim());
        }
    };
    const move = (step: number) => {
        const next = Math.max(0, Math.min(shown.length - 1, active + step));
        setActive(next);
        items.current?.querySelectorAll('button')[next]?.scrollIntoView({ block: 'nearest' });
    };
    return (
        <div className="font-list">
            <input className="font-search" autoFocus placeholder={t('폰트 검색')} value={query}
                onChange={(event) => { setQuery(event.target.value); setActive(event.target.value.trim() ? 0 : -1); }}
                onKeyDown={(event) => {
                    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
                        event.preventDefault();
                        move(event.key === 'ArrowDown' ? 1 : -1);
                    } else if (event.key === 'Enter') {
                        event.preventDefault();
                        pick();
                    }
                }} />
            <div className="font-items" ref={items}>
                {list === null && <div className="font-empty">{t('폰트 목록을 읽는 중…')}</div>}
                {shown.map((name, i) => (
                    <button key={name} className={'font-item' + (i === active ? ' active' : '') + (name === current ? ' current' : '')} title={name}
                        style={{ fontFamily: cssFont(name) }} onMouseEnter={() => setActive(i)} onClick={() => onPick(name)}>{name}</button>
                ))}
                {list !== null && matches.length > shownLimit && <div className="font-empty">{t('{0}개 더 있습니다. 검색어를 더 적어 주세요', matches.length - shownLimit)}</div>}
                {list !== null && matches.length === 0 && query.trim() && (
                    <button className="font-item active" style={{ fontFamily: cssFont(query.trim()) }} onClick={() => onPick(query.trim())}>
                        {t('"{0}" 쓰기', query.trim())} <span className="muted">{t('(이 컴퓨터에 없는 폰트)')}</span>
                    </button>
                )}
            </div>
        </div>
    );
}

// 화면의 (x, y)에 띄우는 폰트 목록. 밖을 누르거나 Esc를 누르면 닫는다
export function FontPopup({ x, y, current, onPick, onClose }: { x: number; y: number; current?: string; onPick(name: string): void; onClose(): void }) {
    const box = useRef<HTMLDivElement>(null);
    useEffect(() => {
        const down = (event: MouseEvent) => {
            if (!box.current?.contains(event.target as Node)) {
                onClose();
            }
        };
        const key = (event: KeyboardEvent) => event.key === 'Escape' && onClose();
        window.addEventListener('mousedown', down, true);
        window.addEventListener('keydown', key, true);
        return () => {
            window.removeEventListener('mousedown', down, true);
            window.removeEventListener('keydown', key, true);
        };
    }, [onClose]);
    // 화면 밖으로 나가지 않게 한다 (목록은 너비 260px, 높이 360px 정도)
    const left = Math.max(8, Math.min(x, window.innerWidth - 276));
    const top = y + 368 > window.innerHeight ? Math.max(8, y - 392) : y;
    return (
        <div ref={box} className="font-popup" style={{ left, top }}>
            <FontList current={current} onPick={onPick} />
        </div>
    );
}

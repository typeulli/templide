// 캔버스 둘레의 패널: 슬라이드 썸네일, 글자 서식 막대
import { memo, useEffect, useRef, useState, type ReactNode } from 'react';
import {
    AlignCenter, AlignJustify, AlignLeft, AlignRight, ALargeSmall, Baseline, Bold, EyeOff, Highlighter, Italic, RemoveFormatting, Sparkles,
    Strikethrough, Type as TypeIcon, Underline, Ellipsis, type LucideIcon,
} from 'lucide-react';
import type { DeckResult } from './lsp';
import { FontList, fontCode } from './FontPicker';
import { t, useLang } from './i18n';
import { withShortcut } from './shortcuts';

// 아이콘 버튼

export function IconButton({ icon: Icon, title, onClick, disabled, active, keepFocus, children, danger }: {
    icon: LucideIcon; title: string; onClick?(): void; disabled?: boolean; active?: boolean; keepFocus?: boolean; children?: ReactNode; danger?: boolean;
}) {
    return (
        <button className={'icon-button' + (active ? ' active' : '') + (danger ? ' danger' : '') + (children ? ' with-label' : '')} title={title} aria-label={title}
            disabled={disabled} onClick={onClick} onMouseDown={keepFocus ? (event) => event.preventDefault() : undefined}>
            <Icon size={16} strokeWidth={1.8} />
            {children}
        </button>
    );
}

// 썸네일. 숨긴 슬라이드는 흐리게, 전환이나 애니메이션이 있으면 표시한다

export const Thumbnail = memo(function Thumbnail({ deck, index, current, onClick }: { deck: DeckResult; index: number; current: boolean; onClick(): void }) {
    useLang();
    const frame = useRef<HTMLDivElement>(null);
    const width = 148;
    const scale = width / deck.width;
    // 캔버스를 먼저 그리도록 썸네일은 화면이 한가할 때 그린다
    useEffect(() => {
        const handle = requestIdleCallback(() => {
            const { stage, fit } = window.Templide.renderSlide(deck.deck, index);
            frame.current?.replaceChildren(stage);
            fit();
        }, { timeout: 500 });
        return () => cancelIdleCallback(handle);
    }, [deck, index]);
    const info = deck.slides[index];
    const hidden = info?.values.hidden === true;
    const moving = !!info?.transition || (info?.animations.length ?? 0) > 0;
    return (
        <button className={'thumb' + (current ? ' current' : '') + (hidden ? ' hidden-slide' : '')} onClick={onClick} title={t('슬라이드 {0}{1}', index + 1, hidden ? t(' (숨김)') : '')}>
            <span className="thumb-side">
                <span className="thumb-number">{index + 1}</span>
                {hidden && <EyeOff size={11} className="thumb-mark" />}
                {moving && <Sparkles size={11} className="thumb-mark" />}
            </span>
            <span className="thumb-box" style={{ width, height: deck.height * scale }}>
                <span ref={frame} className="thumb-slide" style={{ width: deck.width, height: deck.height, transform: `scale(${scale})` }} />
            </span>
        </button>
    );
});

// 서식 막대. 버튼을 눌러도 고치는 중인 글자에서 포커스가 빠지지 않게 mousedown을 막는다

export type StyleProperty = { name: string; enum?: string; color?: string; number?: number; unit?: string; unset?: boolean; string?: string; code?: string };
// 켜고 끄는 서식. key는 덱 JSON의 run에서 그 서식이 켜졌는지 보는 이름, off는 끌 때 넣는 값
export type Toggle = { key: 'b' | 'i' | 'u' | 'st'; off: StyleProperty };

// 굵게, 기울임, 밑줄. 서식 막대의 버튼과 단축키가 함께 쓴다
export const fontToggles: Record<'bold' | 'italic' | 'underline', { properties: StyleProperty[]; toggle: Toggle }> = {
    bold: { properties: [{ name: 'font-weight', enum: 'bold' }], toggle: { key: 'b', off: { name: 'font-weight', enum: 'normal' } } },
    italic: { properties: [{ name: 'font-style', enum: 'italic' }], toggle: { key: 'i', off: { name: 'font-style', enum: 'normal' } } },
    underline: { properties: [{ name: 'text-decoration', enum: 'underline' }], toggle: { key: 'u', off: { name: 'text-decoration', enum: 'none' } } },
};

const swatches = ['#1F1F1F', '#FFFFFF', '#E74C3C', '#E67E22', '#F1C40F', '#2ECC71', '#1ABC9C', '#3498DB', '#9B59B6', '#7F8C8D'];
const highlights = ['#FFFF00', '#00FF00', '#00FFFF', '#FF00FF', '#FFC000', '#FF0000', '#0000FF', '#D9D9D9'];
const sizes = [10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 60, 72];
const lineHeights = [1, 1.15, 1.5, 2, 2.5, 3];
const spacings = (): [number, string][] => [[-2, t('좁게')], [0, t('보통')], [2, t('넓게')], [5, t('아주 넓게')]];

type Menu = 'color' | 'size' | 'font' | 'align' | 'more' | null;

export function FormatBar({ enabled, onStyle, onClear }: { enabled: boolean; onStyle(properties: StyleProperty[], toggle?: Toggle): void; onClear(): void }) {
    const [menu, setMenu] = useState<Menu>(null);
    const apply = (properties: StyleProperty[], toggle?: Toggle) => {
        setMenu(null);
        onStyle(properties, toggle);
    };
    const toggleMenu = (next: Menu) => setMenu(menu === next ? null : next);
    // 서식 막대 밖(캔버스 등)을 누르거나 버튼이 꺼지면 펼친 메뉴를 닫는다
    const bar = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        if (!menu) {
            return;
        }
        const down = (event: MouseEvent) => {
            if (!bar.current?.contains(event.target as Node)) {
                setMenu(null);
            }
        };
        window.addEventListener('mousedown', down);
        return () => window.removeEventListener('mousedown', down);
    }, [menu]);
    useEffect(() => {
        if (!enabled) {
            setMenu(null);
        }
    }, [enabled]);
    return (
        <span className="tool-group formatbar" ref={bar} onMouseDown={(event) => { if (!(event.target as HTMLElement).closest('input')) event.preventDefault(); }}>
            <IconButton icon={Bold} title={withShortcut(t('굵게'), 'bold')} disabled={!enabled} keepFocus onClick={() => apply(fontToggles.bold.properties, fontToggles.bold.toggle)} />
            <IconButton icon={Italic} title={withShortcut(t('기울임'), 'italic')} disabled={!enabled} keepFocus onClick={() => apply(fontToggles.italic.properties, fontToggles.italic.toggle)} />
            <IconButton icon={Underline} title={withShortcut(t('밑줄'), 'underline')} disabled={!enabled} keepFocus onClick={() => apply(fontToggles.underline.properties, fontToggles.underline.toggle)} />
            <span className="menu-anchor">
                <IconButton icon={Baseline} title={t('글자 색')} disabled={!enabled} keepFocus active={menu === 'color'} onClick={() => toggleMenu('color')} />
                {menu === 'color' && (
                    <span className="menu swatches">
                        {swatches.map((color) => <button key={color} style={{ background: color }} title={color} onClick={() => apply([{ name: 'color', color }])} />)}
                    </span>
                )}
            </span>
            <span className="menu-anchor">
                <IconButton icon={ALargeSmall} title={t('글자 크기')} disabled={!enabled} keepFocus active={menu === 'size'} onClick={() => toggleMenu('size')} />
                {menu === 'size' && (
                    <span className="menu sizes">
                        {sizes.map((size) => <button key={size} onClick={() => apply([{ name: 'font-size', number: size, unit: 'pt' }])}>{size}</button>)}
                    </span>
                )}
            </span>
            <span className="menu-anchor">
                <IconButton icon={TypeIcon} title={t('글꼴')} disabled={!enabled} keepFocus active={menu === 'font'} onClick={() => toggleMenu('font')} />
                {menu === 'font' && (
                    <span className="menu font-menu">
                        <FontList onPick={(name) => apply([{ name: 'font-family', code: fontCode(name) }])} />
                    </span>
                )}
            </span>
            <span className="menu-anchor">
                <IconButton icon={AlignLeft} title={t('문단 정렬')} disabled={!enabled} keepFocus active={menu === 'align'} onClick={() => toggleMenu('align')} />
                {menu === 'align' && (
                    <span className="menu icon-menu">
                        {([['left', AlignLeft, t('왼쪽')], ['center', AlignCenter, t('가운데')], ['right', AlignRight, t('오른쪽')], ['justify', AlignJustify, t('양쪽')]] as [string, LucideIcon, string][]).map(([align, Icon, title]) => (
                            <button key={align} title={title} onClick={() => apply([{ name: 'text-align', enum: align }])}><Icon size={15} /></button>
                        ))}
                    </span>
                )}
            </span>
            <span className="menu-anchor">
                <IconButton icon={Ellipsis} title={t('서식 더 보기 (취소선, 형광펜, 첨자, 대문자, 줄 간격, 자간)')} disabled={!enabled} keepFocus active={menu === 'more'} onClick={() => toggleMenu('more')} />
                {menu === 'more' && (
                    <span className="menu list-menu more-menu">
                        <span className="menu-row">
                            <button title={t('취소선')} onClick={() => apply([{ name: 'text-decoration', enum: 'line_through' }], { key: 'st', off: { name: 'text-decoration', enum: 'none' } })}><Strikethrough size={14} /> {t('취소선')}</button>
                        </span>
                        <span className="menu-label"><Highlighter size={12} /> {t('형광펜')}</span>
                        <span className="menu-row">
                            {highlights.map((color) => <button key={color} className="highlight-chip" style={{ background: color }} title={color} onClick={() => apply([{ name: 'highlight', color }])} />)}
                        </span>
                        <span className="menu-label">{t('첨자')}</span>
                        <span className="menu-row">
                            <button onClick={() => apply([{ name: 'vertical-align', enum: 'super' }])}>{t('위 첨자 x²')}</button>
                            <button onClick={() => apply([{ name: 'vertical-align', enum: 'sub' }])}>{t('아래 첨자 x₂')}</button>
                            <button onClick={() => apply([{ name: 'vertical-align', enum: 'baseline' }])}>{t('보통')}</button>
                        </span>
                        <span className="menu-label">{t('대문자')}</span>
                        <span className="menu-row">
                            <button onClick={() => apply([{ name: 'text-transform', enum: 'uppercase' }])}>ABC</button>
                            <button onClick={() => apply([{ name: 'text-transform', enum: 'small_caps' }])}>{t('작은 대문자')}</button>
                            <button onClick={() => apply([{ name: 'text-transform', enum: 'none' }])}>{t('그대로')}</button>
                        </span>
                        <span className="menu-label">{t('줄 간격')}</span>
                        <span className="menu-row">
                            {lineHeights.map((value) => <button key={value} onClick={() => apply([{ name: 'line-height', number: value }])}>{value}</button>)}
                        </span>
                        <span className="menu-label">{t('자간')}</span>
                        <span className="menu-row">
                            {spacings().map(([value, title]) => <button key={value} onClick={() => apply([{ name: 'letter-spacing', number: value, unit: 'px' }])}>{title}</button>)}
                        </span>
                        <span className="menu-label">{t('밑줄 종류')}</span>
                        <span className="menu-row">
                            <button onClick={() => apply([{ name: 'text-decoration', enum: 'double_underline' }])}>{t('이중 밑줄')}</button>
                            <button onClick={() => apply([{ name: 'text-decoration', enum: 'wavy_underline' }])}>{t('물결 밑줄')}</button>
                            <button onClick={() => apply([{ name: 'text-decoration', enum: 'double_line_through' }])}>{t('이중 취소선')}</button>
                        </span>
                    </span>
                )}
            </span>
            <IconButton icon={RemoveFormatting} title={t('서식 지우기 (이 화면에서 넣은 인라인 style)')} disabled={!enabled} keepFocus onClick={() => { setMenu(null); onClear(); }} />
        </span>
    );
}

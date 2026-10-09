// 애니메이션. 속성 패널의 "애니메이션" 탭(고른 개체의 것)과 애니메이션 창(슬라이드 전체, 끌어서 순서 바꾸기)
import { useEffect, useRef, useState } from 'react';
import { ChevronDown, ChevronUp, GripVertical, Lock, Play, Plus, Square, Trash2, X } from 'lucide-react';
import type { AnimationInfo, DeckResult, Schema, SlideInfo } from './lsp';
import { animationCategories, effectNames, label, objectNames, optionNames, startNames } from './labels';
import { lockOf, findElement } from './SlideView';
import { NumberInput } from './Fields';
import { t } from './i18n';

// 편집 요청: 추가, 고치기, 지우기, 옮기기
export type AnimationOp =
    | { action: 'add'; id: string; spec: Partial<AnimationSpec> }
    | { action: 'update'; index: number; spec: Partial<AnimationSpec> }
    | { action: 'delete'; index: number }
    | { action: 'move'; index: number; to: number };
type AnimationSpec = { category: string; effect: string; option: string; path: string; start: string; duration: number | null; delay: number | null };

const categoryColors: Record<string, string> = { enter: '#30a46c', emphasis: '#e5a000', exit: '#e5484d', move: '#0d99ff', media: '#8e4ec6' };

// 클릭 번호. PowerPoint처럼 클릭할 때 시작하는 효과마다 하나씩 늘고, 처음에 저절로 시작하는 효과는 0이다
function clickNumbers(animations: AnimationInfo[]): (number | null)[] {
    let click = 0;
    return animations.map((animation, i) => {
        if (animation.start === 'on_click') {
            click += 1;
            return click;
        }
        return i === 0 ? 0 : null;
    });
}

// 대상 개체를 부르는 이름: put ... as 이름, 없으면 종류와 글자 앞부분
function targetLabel(deck: DeckResult, page: number, animation: AnimationInfo): string {
    const info = deck.elements[animation.elementId];
    if (info?.name) {
        return info.name;
    }
    const el = findElement(deck.deck.slides[page]?.els ?? [], animation.elementId);
    const text = el?.tx?.ps?.map((paragraph: any) => paragraph.rs.map((run: any) => run.t).join('')).join(' ').trim();
    const kind = info?.fromTemplate || info?.object === 'group' ? t('템플릿') : objectNames[info?.object ?? ''] ?? info?.object ?? animation.target;
    return text ? `${kind}: ${text.slice(0, 16)}${text.length > 16 ? '…' : ''}` : kind;
}

// 고른 개체(또는 그 개체를 만든 template 묶음)에 걸린 애니메이션
export function animationsOf(slide: SlideInfo | undefined, id: string | null): { animation: AnimationInfo; index: number }[] {
    if (!slide || !id) {
        return [];
    }
    return slide.animations.map((animation, index) => ({ animation, index }))
        .filter(({ animation }) => animation.elementId === id || id.startsWith(animation.elementId + '/'));
}

const effectLabel = (animation: { category: string; effect: string; option?: string }) =>
    label(effectNames, animation.effect) + (animation.option ? ` · ${label(optionNames, animation.option)}` : '');

// 효과 고르기

function EffectPicker({ schema, media, onPick, onClose }: { schema: Schema; media: boolean; onPick(category: string, effect: string): void; onClose(): void }) {
    const categories = animationCategories().filter(([category]) => category !== 'media' || media);
    const [category, setCategory] = useState(categories[0][0]);
    const effects = category === 'media' ? ['play', 'pause', 'stop']
        : Object.keys(schema.animations).filter((key) => key.startsWith(category + '.')).map((key) => key.slice(category.length + 1));
    if (category === 'move') {
        effects.unshift('path');
    }
    const ref = useRef<HTMLDivElement>(null);
    useEffect(() => {
        // 여는 버튼과 같은 menu-anchor 안을 누른 것은 버튼이 알아서 닫으므로 넘긴다. 여기서 닫으면 그 버튼이 다시 연다
        const down = (event: MouseEvent) => {
            const anchor = ref.current?.closest('.menu-anchor') ?? ref.current;
            if (anchor && !anchor.contains(event.target as Node)) {
                onClose();
            }
        };
        window.addEventListener('mousedown', down);
        return () => window.removeEventListener('mousedown', down);
    }, [onClose]);
    return (
        <div className="effect-picker" ref={ref}>
            <div className="effect-tabs">
                {categories.map(([key, title]) => (
                    <button key={key} className={key === category ? 'active' : ''} onClick={() => setCategory(key)}>
                        <span className="dot" style={{ background: categoryColors[key] }} />{title}
                    </button>
                ))}
                <button className="close" title={t('닫기')} onClick={onClose}><X size={14} /></button>
            </div>
            <div className="effect-grid">
                {effects.map((effect) => (
                    <button key={effect} title={`${category}.${effect}`} onClick={() => onPick(category, effect)}>
                        <span className="dot" style={{ background: categoryColors[category] }} />{label(effectNames, effect)}
                    </button>
                ))}
            </div>
        </div>
    );
}

// 애니메이션 하나 고치기

function AnimationEditor({ schema, animation, index, count, onOp }: { schema: Schema; animation: AnimationInfo; index: number; count: number; onOp(op: AnimationOp): void }) {
    const lock = lockOf(animation.source);
    const key = `${animation.category}.${animation.effect}`;
    const options = schema.animations[key] ?? [];
    const effects = animation.category === 'media' ? ['play', 'pause', 'stop']
        : Object.keys(schema.animations).filter((each) => each.startsWith(animation.category + '.')).map((each) => each.slice(animation.category.length + 1));
    if (animation.category === 'move' && !effects.includes('path')) {
        effects.unshift('path');
    }
    const endless = schema.endlessAnimations.includes(key) || animation.category === 'media';
    const update = (spec: Partial<AnimationSpec>) => onOp({ action: 'update', index, spec });
    const [path, setPath] = useState(animation.path ?? '');
    useEffect(() => setPath(animation.path ?? ''), [animation.path]);
    return (
        <div className={'animation-editor' + (lock ? ' locked' : '')} title={lock ? t('🔒 {0}. 코드에서 고쳐 주세요', lock) : undefined}>
            {lock && <div className="lock-note"><Lock size={11} /> {t('{0}이라 여기서 바꿀 수 없습니다', lock)}</div>}
            <div className="field-row">
                <span className="field-label">{t('효과')}</span>
                <span className="field-control">
                    <select disabled={!!lock} value={animation.effect} onChange={(event) => update({ effect: event.target.value, ...(event.target.value === 'path' ? { path: 'M 0 0 L 200 0' } : {}) })}>
                        {effects.map((effect) => <option key={effect} value={effect}>{label(effectNames, effect)}</option>)}
                    </select>
                </span>
            </div>
            {animation.effect === 'path' && (
                <div className="field-row">
                    <span className="field-label">{t('경로')}</span>
                    <span className="field-control">
                        <input className="text-input" disabled={!!lock} value={path} title={t('SVG path (M, L, C, Q, A, Z). 좌표는 개체에서 잰 px')} onChange={(event) => setPath(event.target.value)}
                            onBlur={() => path !== animation.path && update({ path })} onKeyDown={(event) => event.key === 'Enter' && update({ path })} />
                    </span>
                </div>
            )}
            {options.length > 0 && (
                <div className="field-row">
                    <span className="field-label">{t('옵션')}</span>
                    <span className="field-control">
                        <select disabled={!!lock} value={animation.option} onChange={(event) => update({ option: event.target.value })}>
                            {options.map((option) => <option key={option} value={option}>{label(optionNames, option)}</option>)}
                        </select>
                    </span>
                </div>
            )}
            <div className="field-row">
                <span className="field-label">{t('시작')}</span>
                <span className="field-control">
                    <select disabled={!!lock} value={animation.start} onChange={(event) => update({ start: event.target.value })}>
                        {Object.entries(startNames).map(([start, title]) => <option key={start} value={start}>{title}</option>)}
                    </select>
                </span>
            </div>
            <div className="pair">
                <div className="field-row compact">
                    <span className="field-label">{t('길이')}</span>
                    <span className="field-control">
                        <NumberInput value={endless ? undefined : animation.duration} measure="seconds" disabled={!!lock || endless} placeholder={endless ? '–' : t('기본')}
                            onCommit={(shown) => update({ duration: Math.max(0, shown * 1000) })} />
                    </span>
                </div>
                <div className="field-row compact">
                    <span className="field-label">{t('지연')}</span>
                    <span className="field-control">
                        <NumberInput value={animation.delay ?? 0} measure="seconds" disabled={!!lock} onCommit={(shown) => update({ delay: Math.max(0, shown * 1000) })} />
                    </span>
                </div>
            </div>
            <div className="animation-actions">
                <span className="muted">{t('재생 순서 {0}{1}', index + 1, animation.order !== undefined ? ` (order ${animation.order})` : '')}</span>
                <span className="spacer" />
                <button className="mini-button" title={t('앞으로 (먼저 재생)')} disabled={index === 0} onClick={() => onOp({ action: 'move', index, to: index - 1 })}><ChevronUp size={14} /></button>
                <button className="mini-button" title={t('뒤로 (나중에 재생)')} disabled={index >= count - 1} onClick={() => onOp({ action: 'move', index, to: index + 1 })}><ChevronDown size={14} /></button>
                <button className="mini-button danger" title={t('애니메이션 지우기')} disabled={!!lock} onClick={() => onOp({ action: 'delete', index })}><Trash2 size={14} /></button>
            </div>
        </div>
    );
}

// 속성 패널의 애니메이션 탭: 고른 개체의 애니메이션

export function ObjectAnimations({ schema, slide, id, media, onOp, onPreview }: {
    schema: Schema | null; slide: SlideInfo | undefined; id: string; media: boolean; onOp(op: AnimationOp): void; onPreview(): void;
}) {
    const [picking, setPicking] = useState(false);
    const [open, setOpen] = useState<number | null>(null);
    if (!schema || !slide) {
        return <div className="props-empty">{t('애니메이션 목록을 불러오는 중입니다')}</div>;
    }
    const list = animationsOf(slide, id);
    const numbers = clickNumbers(slide.animations);
    const add = (category: string, effect: string) => {
        setPicking(false);
        const spec: Partial<AnimationSpec> = { category, effect, ...(effect === 'path' ? { path: 'M 0 0 L 200 0' } : {}) };
        // 이미 애니메이션이 있으면 앞 효과 다음에 이어서 재생하는 것이 흔하지만, PowerPoint처럼 클릭할 때로 넣는다
        onOp({ action: 'add', id, spec });
    };
    return (
        <div className="object-animations">
            <div className="tab-actions">
                <span className="menu-anchor">
                    <button className="primary-button" onClick={() => setPicking(!picking)}><Plus size={14} /> {t('애니메이션 추가')}</button>
                    {picking && <EffectPicker schema={schema} media={media} onPick={add} onClose={() => setPicking(false)} />}
                </span>
                <button className="ghost-button" title={t('이 슬라이드의 애니메이션을 캔버스에서 재생')} onClick={onPreview}><Play size={13} /> {t('미리 보기')}</button>
            </div>
            {list.length === 0 && <div className="props-empty small">{t('이 개체에는 애니메이션이 없습니다.')}<br />{t('추가하면 이 개체의 put 블록에 animate 문장이 들어갑니다')}</div>}
            {list.map(({ animation, index }) => (
                <div key={index} className={'animation-item' + (open === index ? ' open' : '')}>
                    <button className="animation-row" onClick={() => setOpen(open === index ? null : index)}>
                        <span className="click-number">{numbers[index] ?? ''}</span>
                        <span className="dot" style={{ background: categoryColors[animation.category] }} />
                        <span className="animation-name">{effectLabel(animation)}</span>
                        <span className="muted small">{startNames[animation.start]}</span>
                        {lockOf(animation.source) && <Lock size={11} />}
                    </button>
                    {open === index && <AnimationEditor schema={schema} animation={animation} index={index} count={slide.animations.length} onOp={onOp} />}
                </div>
            ))}
        </div>
    );
}

// 애니메이션 창: 슬라이드의 모든 애니메이션. 끌어서 재생 순서를 바꾼다

export function AnimationPane({ deck, schema, page, selected, playing, onSelect, onOp, onPreview, onStop, onClose }: {
    deck: DeckResult; schema: Schema | null; page: number; selected: string | null; playing: boolean;
    onSelect(id: string): void; onOp(op: AnimationOp): void; onPreview(): void; onStop(): void; onClose(): void;
}) {
    const slide = deck.slides[page];
    const [open, setOpen] = useState<number | null>(null);
    const [dragging, setDragging] = useState<number | null>(null);
    const [over, setOver] = useState<number | null>(null);
    const [picking, setPicking] = useState(false);
    useEffect(() => setPicking(false), [selected]);
    const animations = slide?.animations ?? [];
    const numbers = clickNumbers(animations);
    const selectedInfo = selected ? deck.elements[selected] : null;
    const drop = (to: number) => {
        if (dragging !== null && dragging !== to) {
            onOp({ action: 'move', index: dragging, to: to > dragging ? to - 1 : to });
        }
        setDragging(null);
        setOver(null);
    };
    return (
        <aside className="animation-pane">
            <header className="pane-header">
                <span>{t('애니메이션 창')}</span>
                <span className="spacer" />
                {playing
                    ? <button className="ghost-button" onClick={onStop}><Square size={12} /> {t('멈추기')}</button>
                    : <button className="ghost-button" disabled={!animations.length && !slide?.transition} onClick={onPreview}><Play size={13} /> {t('미리 보기')}</button>}
                <button className="mini-button" title={t('닫기')} onClick={onClose}><X size={14} /></button>
            </header>
            <div className="pane-add">
                <span className="menu-anchor">
                    <button className="primary-button" disabled={!selected || !schema} title={selected ? t('고른 개체에 애니메이션 추가') : t('먼저 캔버스에서 개체를 고르세요')} onClick={() => setPicking(!picking)}>
                        <Plus size={14} /> {t('추가')}
                    </button>
                    {picking && schema && selected && (
                        <EffectPicker schema={schema} media={selectedInfo?.object === 'video' || selectedInfo?.object === 'audio'}
                            onPick={(category, effect) => { setPicking(false); onOp({ action: 'add', id: selected, spec: { category, effect, ...(effect === 'path' ? { path: 'M 0 0 L 200 0' } : {}) } }); }}
                            onClose={() => setPicking(false)} />
                    )}
                </span>
                <span className="muted small">{animations.length ? t('끌어서 재생 순서를 바꿉니다') : ''}</span>
            </div>
            <div className="pane-list" onDragOver={(event) => { event.preventDefault(); }} onDrop={() => drop(animations.length)}>
                {animations.length === 0 && <div className="props-empty small">{t('이 슬라이드에는 애니메이션이 없습니다')}</div>}
                {animations.map((animation, index) => {
                    const lock = lockOf(animation.source);
                    const active = selected === animation.elementId || (!!selected && selected.startsWith(animation.elementId + '/'));
                    return (
                        <div key={index} className={'animation-item' + (open === index ? ' open' : '') + (over === index ? ' drop-before' : '') + (dragging === index ? ' dragging' : '')}
                            draggable={!lock} onDragStart={(event) => { setDragging(index); event.dataTransfer.effectAllowed = 'move'; }}
                            onDragEnd={() => { setDragging(null); setOver(null); }}
                            onDragOver={(event) => { event.preventDefault(); event.stopPropagation(); setOver(index); }}
                            onDrop={(event) => { event.stopPropagation(); drop(index); }}>
                            <button className={'animation-row' + (active ? ' active' : '')} onClick={() => { onSelect(animation.elementId); setOpen(open === index ? null : index); }}>
                                <GripVertical size={12} className="grip" />
                                <span className="click-number">{numbers[index] ?? ''}</span>
                                <span className="dot" style={{ background: categoryColors[animation.category] }} />
                                <span className="animation-text">
                                    <span className="animation-target">{targetLabel(deck, page, animation)}</span>
                                    <span className="animation-name muted">{effectLabel(animation)}{animation.start !== 'on_click' ? ` · ${startNames[animation.start]}` : ''}</span>
                                </span>
                                {lock && <Lock size={11} />}
                            </button>
                            {open === index && schema && <AnimationEditor schema={schema} animation={animation} index={index} count={animations.length} onOp={onOp} />}
                        </div>
                    );
                })}
            </div>
        </aside>
    );
}

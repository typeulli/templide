// 속성 패널의 입력 칸. 값은 서버의 편집 요청에 그대로 펼쳐 넣는 객체({length}, {number, unit}, {enum}, {bool}, {color}, {string}, {code})로 보낸다
import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Lock, RotateCcw, FolderOpen, Plus, X } from 'lucide-react';
import type { ActionValue, LinkValue, Origin, PaintValue, Value } from './lsp';
import { enumNames, label } from './labels';
import { lockOf } from './SlideView';

export type SetValue = { length?: number; number?: number; unit?: string; enum?: string; bool?: boolean; color?: string; string?: string; code?: string };

// 칸 하나가 공통으로 받는 것. origin이 없으면(적지 않은 값) 기본값이다
export type FieldProps = {
    name: string;
    label?: string;
    value: Value | undefined;
    origin: Origin | undefined;
    onSet(value: SetValue): void;
    onUnset?(): void;
};

export function lockReason(origin: Origin | undefined): string | null {
    return origin ? lockOf(origin) : '코드에 적힌 값이 없음';
}

// ---- 색과 식

// #RRGGBB(AA)를 코드로. 불투명하면 hex(...), 아니면 rgba(...)
export function colorCode(color: string): string {
    const digits = color.replace('#', '');
    if (digits.length === 6 || digits.slice(6).toUpperCase() === 'FF') {
        return `hex(${digits.slice(0, 6).toUpperCase()})`;
    }
    const channel = (at: number) => parseInt(digits.slice(at, at + 2), 16);
    return `rgba(${channel(0)}, ${channel(2)}, ${channel(4)}, ${Math.round(channel(6) / 2.55) / 100})`;
}

export const quote = (text: string) => '"' + text.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t') + '"';

const withAlpha = (hex: string, alpha: number) => alpha >= 100 ? hex.slice(0, 7) : hex.slice(0, 7) + Math.round(alpha * 2.55).toString(16).padStart(2, '0').toUpperCase();

// 테마 색 이름(theme.accent1)과 기본 테마(Office)의 색
export const themeColors: [string, string][] = [
    ['dark1', '#000000'], ['light1', '#FFFFFF'], ['dark2', '#44546A'], ['light2', '#E7E6E6'], ['accent1', '#4472C4'], ['accent2', '#ED7D31'],
    ['accent3', '#A5A5A5'], ['accent4', '#FFC000'], ['accent5', '#5B9BD5'], ['accent6', '#70AD47'],
];
const swatches = ['#1F1F1F', '#7F8C8D', '#FFFFFF', '#E74C3C', '#E67E22', '#F1C40F', '#2ECC71', '#1ABC9C', '#3498DB', '#9B59B6'];

// ---- 틀

export function Row({ label: text, lock, origin, onUnset, children, wide }: { label: string; lock?: string | null; origin?: Origin; onUnset?(): void; children: ReactNode; wide?: boolean }) {
    const resettable = !!onUnset && !!origin?.statement && !lock;
    return (
        <div className={'field-row' + (wide ? ' wide' : '') + (lock ? ' locked' : '')} title={lock ? `🔒 ${lock}. 코드에서 고쳐 주세요` : undefined}>
            <span className="field-label">{text}{lock && <Lock size={10} />}</span>
            <span className="field-control">{children}</span>
            <button className={'field-reset' + (resettable ? '' : ' hidden')} title="기본값으로 (코드에서 이 값을 지웁니다)" onClick={onUnset} disabled={!resettable}>
                <RotateCcw size={12} />
            </button>
        </div>
    );
}

// ---- 수

// 단위: px(길이), deg(각도), ratio(0~1을 %로 보여 줌), percent(%), seconds(ms를 초로 보여 줌), int, number
export type Measure = 'px' | 'deg' | 'ratio' | 'percent' | 'seconds' | 'int' | 'number';

const suffix: Record<Measure, string> = { px: 'px', deg: '°', ratio: '%', percent: '%', seconds: '초', int: '', number: '' };

export function toDisplay(measure: Measure, value: number): number {
    const shown = measure === 'ratio' ? value * 100 : measure === 'seconds' ? value / 1000 : value;
    return Math.round(shown * 1000) / 1000;
}

export function fromDisplay(measure: Measure, shown: number): SetValue {
    switch (measure) {
        case 'px': return { length: shown };
        case 'deg': return { number: shown };
        case 'ratio': return { number: Math.round(shown * 10) / 1000 };
        case 'percent': return { number: shown, unit: '%' };
        case 'seconds': return { code: `${Math.round(shown * 1000) / 1000}s` };
        case 'int': return { number: Math.round(shown) };
        default: return { number: shown };
    }
}

export function NumberInput({ value, measure, disabled, placeholder, onCommit }: { value: number | undefined; measure: Measure; disabled?: boolean; placeholder?: string; onCommit(shown: number): void }) {
    const shown = value === undefined ? '' : String(toDisplay(measure, value));
    const [draft, setDraft] = useState(shown);
    useEffect(() => setDraft(shown), [shown]);
    const commit = () => {
        const parsed = Number(draft);
        if (draft === shown || draft.trim() === '' || Number.isNaN(parsed)) {
            setDraft(shown);
            return;
        }
        onCommit(parsed);
    };
    return (
        <span className={'number-input' + (disabled ? ' disabled' : '')}>
            <input disabled={disabled} value={draft} placeholder={placeholder ?? '–'} onChange={(event) => setDraft(event.target.value)} onBlur={commit}
                onKeyDown={(event) => { if (event.key === 'Enter') commit(); if (event.key === 'Escape') setDraft(shown); }} />
            {suffix[measure] && <span className="unit">{suffix[measure]}</span>}
        </span>
    );
}

export function NumberField({ name, label: text, value, origin, onSet, onUnset, measure, placeholder }: FieldProps & { measure: Measure; placeholder?: string }) {
    const lock = lockReason(origin);
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <NumberInput value={typeof value === 'number' ? value : undefined} measure={measure} disabled={!!lock} placeholder={placeholder}
                onCommit={(shown) => onSet(fromDisplay(measure, shown))} />
        </Row>
    );
}

// ---- 고르기

export function EnumField({ name, label: text, value, origin, onSet, onUnset, options, names }: FieldProps & { options: string[]; names?: Record<string, string> }) {
    const lock = lockReason(origin);
    const current = typeof value === 'string' ? value : '';
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <select disabled={!!lock} value={current} onChange={(event) => onSet({ enum: event.target.value })}>
                {!current && <option value="">기본값</option>}
                {current && !options.includes(current) && <option value={current}>{current}</option>}
                {options.map((option) => <option key={option} value={option}>{label(names ?? enumNames, option)}</option>)}
            </select>
        </Row>
    );
}

export function BoolField({ name, label: text, value, origin, onSet, onUnset }: FieldProps) {
    const lock = lockReason(origin);
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <label className="switch">
                <input type="checkbox" disabled={!!lock} checked={value === true} onChange={(event) => onSet({ bool: event.target.checked })} />
                <span className="switch-track" />
            </label>
        </Row>
    );
}

export function TextField({ name, label: text, value, origin, onSet, onUnset, multiline }: FieldProps & { multiline?: boolean }) {
    const lock = lockReason(origin);
    const shown = typeof value === 'string' ? value : '';
    const [draft, setDraft] = useState(shown);
    useEffect(() => setDraft(shown), [shown]);
    const commit = () => {
        if (draft !== shown) {
            onSet({ string: draft });
        }
    };
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset} wide={multiline}>
            {multiline
                ? <textarea disabled={!!lock} value={draft} rows={3} onChange={(event) => setDraft(event.target.value)} onBlur={commit} />
                : <input className="text-input" disabled={!!lock} value={draft} onChange={(event) => setDraft(event.target.value)} onBlur={commit}
                    onKeyDown={(event) => { if (event.key === 'Enter') commit(); if (event.key === 'Escape') setDraft(shown); }} />}
        </Row>
    );
}

// 파일 경로. 고르기 버튼은 pick이 문서 폴더로 복사한 뒤 적을 상대 경로를 돌려준다
export function FileField({ name, label: text, value, origin, onSet, onUnset, pick, extra }: FieldProps & { pick(): Promise<string | null>; extra?: ReactNode }) {
    const lock = lockReason(origin);
    const shown = typeof value === 'string' && value ? value : '없음';
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <span className="file-field">
                <span className="file-name" title={shown}>{shown}</span>
                <button className="mini-button" disabled={!!lock} title="파일 고르기" onClick={async () => {
                    const file = await pick();
                    if (file) {
                        onSet({ string: file });
                    }
                }}><FolderOpen size={13} /></button>
                {extra}
            </span>
        </Row>
    );
}

// ---- 색

function useOutside(open: boolean, close: () => void) {
    const ref = useRef<HTMLSpanElement>(null);
    useEffect(() => {
        if (!open) {
            return;
        }
        const down = (event: MouseEvent) => {
            if (ref.current && !ref.current.contains(event.target as Node)) {
                close();
            }
        };
        window.addEventListener('mousedown', down);
        return () => window.removeEventListener('mousedown', down);
    }, [open, close]);
    return ref;
}

// 색 하나를 고르는 칸. 고르면 #RRGGBB(AA) 또는 테마 색의 코드(theme.accent1)를 돌려준다
export function ColorPicker({ value, disabled, onPick, onClear, allowTheme = true }: { value: string | null; disabled?: boolean; onPick(color: { color?: string; code?: string }): void; onClear?(): void; allowTheme?: boolean }) {
    const [open, setOpen] = useState(false);
    const ref = useOutside(open, () => setOpen(false));
    const hex = value ? value.slice(0, 7).toUpperCase() : null;
    const alpha = value && value.length === 9 ? Math.round(parseInt(value.slice(7), 16) / 2.55) : 100;
    return (
        <span className="color-picker" ref={ref}>
            <button className="color-button" disabled={disabled} onClick={() => setOpen(!open)}>
                <span className={'swatch' + (hex ? '' : ' empty')} style={hex ? { background: value! } : undefined} />
                <span className="color-text">{hex ?? '없음'}</span>
                {hex && alpha < 100 && <span className="muted">{alpha}%</span>}
            </button>
            {open && (
                <span className="popover color-popover">
                    <span className="swatch-row">
                        {swatches.map((color) => <button key={color} className="swatch-chip" style={{ background: color }} title={color} onClick={() => onPick({ color: withAlpha(color, alpha) })} />)}
                    </span>
                    {allowTheme && (
                        <>
                            <span className="popover-label">테마 색</span>
                            <span className="swatch-row">
                                {themeColors.map(([name, color]) => <button key={name} className="swatch-chip" style={{ background: color }} title={`theme.${name}`} onClick={() => { setOpen(false); onPick({ code: `theme.${name}` }); }} />)}
                            </span>
                        </>
                    )}
                    <span className="popover-row">
                        <input type="color" value={hex ?? '#FFFFFF'} onChange={(event) => onPick({ color: withAlpha(event.target.value, alpha) })} />
                        <span className="muted">불투명도</span>
                        <input type="range" min={0} max={100} value={alpha} onChange={(event) => onPick({ color: withAlpha(hex ?? '#FFFFFF', Number(event.target.value)) })} />
                        <span className="alpha-text">{alpha}%</span>
                    </span>
                    {onClear && <button className="link-button" onClick={() => { setOpen(false); onClear(); }}>색 없애기 (기본값)</button>}
                </span>
            )}
        </span>
    );
}

export function ColorField({ name, label: text, value, origin, onSet, onUnset }: FieldProps) {
    const lock = lockReason(origin);
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <ColorPicker value={typeof value === 'string' ? value : null} disabled={!!lock}
                onPick={(picked) => onSet(picked.code ? { code: picked.code } : { color: picked.color })}
                onClear={onUnset && origin?.statement ? onUnset : undefined} />
        </Row>
    );
}

// ---- 채우기 (색, 그라데이션, 무늬, 그림)

type Paint = { mode: 'none' | 'solid' | 'gradient' | 'pattern' | 'image'; color: string; radial: boolean; angle: number; colors: string[]; pattern: string; foreground: string; background: string; image: string };

function paintOf(value: Value | undefined): Paint {
    const paint: Paint = { mode: 'none', color: '#FFFFFF', radial: false, angle: 90, colors: ['#FFFFFF', '#000000'], pattern: 'percent_50', foreground: '#000000', background: '#FFFFFF', image: '' };
    if (typeof value === 'string') {
        return { ...paint, mode: 'solid', color: value, colors: [value, '#FFFFFF'] };
    }
    if (value && typeof value === 'object') {
        const object = value as Exclude<PaintValue, string>;
        if ('gradient' in object) {
            return { ...paint, mode: 'gradient', radial: object.gradient.radial, angle: object.gradient.angle, colors: object.gradient.colors };
        }
        if ('pattern' in object) {
            return { ...paint, mode: 'pattern', pattern: object.pattern.kind, foreground: object.pattern.foreground, background: object.pattern.background };
        }
        if ('image' in object) {
            return { ...paint, mode: 'image', image: object.image };
        }
    }
    return paint;
}

export function paintCode(paint: Paint): string | null {
    switch (paint.mode) {
        case 'solid': return colorCode(paint.color);
        case 'gradient': return paint.radial ? `radial(${paint.colors.map(colorCode).join(', ')})` : `linear(${Math.round(paint.angle)}, ${paint.colors.map(colorCode).join(', ')})`;
        case 'pattern': return `pattern(${paint.pattern}, ${colorCode(paint.foreground)}, ${colorCode(paint.background)})`;
        case 'image': return paint.image ? `image(${quote(paint.image)})` : null;
        default: return null;
    }
}

export function PaintField({ name, label: text, value, origin, onSet, onUnset, patterns, pickImage }: FieldProps & { patterns: string[]; pickImage(): Promise<string | null> }) {
    const lock = lockReason(origin);
    const paint = paintOf(value);
    const write = async (next: Paint) => {
        if (next.mode === 'none') {
            onUnset?.();
            return;
        }
        if (next.mode === 'image' && !next.image) {
            const file = await pickImage();
            if (!file) {
                return;
            }
            next = { ...next, image: file };
        }
        const code = paintCode(next);
        if (code) {
            onSet({ code });
        }
    };
    const modes: [Paint['mode'], string][] = [['none', '없음'], ['solid', '단색'], ['gradient', '그라데이션'], ['pattern', '무늬'], ['image', '그림']];
    return (
        <div className={'paint-field' + (lock ? ' locked' : '')} title={lock ? `🔒 ${lock}. 코드에서 고쳐 주세요` : undefined}>
            <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
                <select disabled={!!lock} value={paint.mode} onChange={(event) => write({ ...paint, mode: event.target.value as Paint['mode'] })}>
                    {modes.map(([mode, title]) => <option key={mode} value={mode} disabled={mode === 'none' && !origin?.statement && paint.mode !== 'none'}>{title}</option>)}
                </select>
            </Row>
            {!lock && paint.mode === 'solid' && (
                <Row label="색">
                    <ColorPicker value={paint.color} onPick={(picked) => picked.code ? onSet({ code: picked.code }) : write({ ...paint, color: picked.color! })} />
                </Row>
            )}
            {!lock && paint.mode === 'gradient' && (
                <>
                    <Row label="방식">
                        <span className="segmented-text">
                            <button className={!paint.radial ? 'active' : ''} onClick={() => write({ ...paint, radial: false })}>선형</button>
                            <button className={paint.radial ? 'active' : ''} onClick={() => write({ ...paint, radial: true })}>방사형</button>
                        </span>
                    </Row>
                    {!paint.radial && (
                        <Row label="각도">
                            <NumberInput value={paint.angle} measure="deg" onCommit={(angle) => write({ ...paint, angle })} />
                        </Row>
                    )}
                    {paint.colors.map((color, i) => (
                        <Row key={i} label={`색 ${i + 1}`}>
                            <span className="stop-row">
                                <ColorPicker value={color} allowTheme={false} onPick={(picked) => write({ ...paint, colors: paint.colors.map((each, j) => j === i ? picked.color! : each) })} />
                                {paint.colors.length > 2 && (
                                    <button className="mini-button" title="이 색 빼기" onClick={() => write({ ...paint, colors: paint.colors.filter((_, j) => j !== i) })}><X size={12} /></button>
                                )}
                            </span>
                        </Row>
                    ))}
                    <button className="link-button indent" onClick={() => write({ ...paint, colors: [...paint.colors, paint.colors[paint.colors.length - 1]] })}>
                        <Plus size={12} /> 색 더하기
                    </button>
                </>
            )}
            {!lock && paint.mode === 'pattern' && (
                <>
                    <Row label="무늬">
                        <select value={paint.pattern} onChange={(event) => write({ ...paint, pattern: event.target.value })}>
                            {patterns.map((kind) => <option key={kind} value={kind}>{kind.replace(/_/g, ' ')}</option>)}
                        </select>
                    </Row>
                    <Row label="앞 색"><ColorPicker value={paint.foreground} allowTheme={false} onPick={(picked) => write({ ...paint, foreground: picked.color! })} /></Row>
                    <Row label="뒤 색"><ColorPicker value={paint.background} allowTheme={false} onPick={(picked) => write({ ...paint, background: picked.color! })} /></Row>
                </>
            )}
            {!lock && paint.mode === 'image' && (
                <Row label="그림">
                    <span className="file-field">
                        <span className="file-name" title={paint.image}>{paint.image || '없음'}</span>
                        <button className="mini-button" title="그림 고르기" onClick={async () => {
                            const file = await pickImage();
                            if (file) {
                                write({ ...paint, image: file });
                            }
                        }}><FolderOpen size={13} /></button>
                    </span>
                </Row>
            )}
        </div>
    );
}

// ---- 링크와 실행 설정

const jumps: [string, string][] = [
    ['next_slide', '다음 슬라이드'], ['previous_slide', '이전 슬라이드'], ['first_slide', '첫 슬라이드'], ['last_slide', '마지막 슬라이드'],
    ['last_viewed_slide', '마지막으로 본 슬라이드'], ['end_show', '쇼 마치기'],
];

type ActionDraft = { kind: string; text: string; slide: number; args: string };

function draftOf(value: Value | undefined): ActionDraft {
    const draft: ActionDraft = { kind: 'none', text: '', slide: 1, args: '' };
    const fromLink = (link: LinkValue): ActionDraft => link.url !== undefined ? { ...draft, kind: 'url', text: link.url }
        : link.slide !== undefined ? { ...draft, kind: 'slide', slide: link.slide } : { ...draft, kind: link.jump ?? 'none' };
    if (value && typeof value === 'object' && 'link' in value) {
        return fromLink((value as { link: LinkValue }).link);
    }
    if (value && typeof value === 'object' && 'action' in value) {
        const action = (value as { action: ActionValue }).action;
        if (action.kind === 'link' && action.link) {
            return fromLink(action.link);
        }
        return { ...draft, kind: action.kind, text: action.target, args: action.arguments.map((each) => typeof each === 'string' ? quote(each) : String(each)).join(', ') };
    }
    return draft;
}

function actionCode(draft: ActionDraft): string | null {
    if (jumps.some(([jump]) => jump === draft.kind)) {
        return draft.kind;
    }
    switch (draft.kind) {
        case 'url': return draft.text ? quote(draft.text) : null;
        case 'slide': return `slide(${Math.max(1, Math.round(draft.slide))})`;
        case 'run': return draft.text ? `run(${[quote(draft.text), ...(draft.args.trim() ? [draft.args.trim()] : [])].join(', ')})` : null;
        case 'program':
        case 'macro':
        case 'file': return draft.text ? `${draft.kind}(${quote(draft.text)})` : null;
        default: return null;
    }
}

// link는 링크만, action과 hover_action은 실행 설정(run, program, macro, file)도 받는다
export function ActionField({ name, label: text, value, origin, onSet, onUnset, linkOnly }: FieldProps & { linkOnly?: boolean }) {
    const lock = lockReason(origin);
    const current = draftOf(value);
    const [draft, setDraft] = useState(current);
    const key = JSON.stringify(current);
    useEffect(() => setDraft(current), [key]); // eslint-disable-line react-hooks/exhaustive-deps
    const kinds: [string, string][] = [['none', '없음'], ...jumps, ['slide', '슬라이드 번호'], ['url', '웹 주소'],
        ...(linkOnly ? [] : [['run', 'JS 함수 실행 (html, web)'], ['program', '프로그램 실행 (pptx)'], ['macro', '매크로 실행 (pptx)'], ['file', '파일 열기']] as [string, string][])];
    const commit = (next: ActionDraft) => {
        setDraft(next);
        if (next.kind === 'none') {
            onUnset?.();
            return;
        }
        const code = actionCode(next);
        if (code) {
            onSet({ code });
        }
    };
    const needsText = ['url', 'run', 'program', 'macro', 'file'].includes(draft.kind);
    return (
        <div className={'action-field' + (lock ? ' locked' : '')}>
            <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
                <select disabled={!!lock} value={draft.kind} onChange={(event) => {
                    const kind = event.target.value;
                    const next = { ...draft, kind };
                    if (['url', 'run', 'program', 'macro', 'file'].includes(kind)) {
                        setDraft(next); // 글자를 적은 뒤에 쓴다
                    } else {
                        commit(next);
                    }
                }}>
                    {kinds.map(([kind, title]) => <option key={kind} value={kind}>{title}</option>)}
                </select>
            </Row>
            {!lock && draft.kind === 'slide' && (
                <Row label="번호"><NumberInput value={draft.slide} measure="int" onCommit={(slide) => commit({ ...draft, slide })} /></Row>
            )}
            {!lock && needsText && (
                <Row label={draft.kind === 'url' ? '주소' : draft.kind === 'run' ? '함수 이름' : draft.kind === 'macro' ? '매크로 이름' : '경로'}>
                    <input className="text-input" value={draft.text} placeholder={draft.kind === 'url' ? 'https://' : ''} onChange={(event) => setDraft({ ...draft, text: event.target.value })}
                        onBlur={() => commit(draft)} onKeyDown={(event) => event.key === 'Enter' && commit(draft)} />
                </Row>
            )}
            {!lock && draft.kind === 'run' && (
                <Row label="넘길 값">
                    <input className="text-input" value={draft.args} placeholder='예: "안녕", 3, @slide.number' onChange={(event) => setDraft({ ...draft, args: event.target.value })}
                        onBlur={() => commit(draft)} onKeyDown={(event) => event.key === 'Enter' && commit(draft)} />
                </Row>
            )}
        </div>
    );
}

// ---- 이름 붙은 개체 (연결선의 from, to)

export function RefField({ name, label: text, value, origin, onSet, onUnset, names }: FieldProps & { names: string[] }) {
    const lock = lockReason(origin);
    const current = typeof value === 'string' ? value : '';
    return (
        <Row label={text ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <select disabled={!!lock} value={current} onChange={(event) => onSet({ enum: event.target.value })}>
                {!names.includes(current) && <option value={current}>{current || '고르기'}</option>}
                {names.map((each) => <option key={each} value={each}>{each}</option>)}
            </select>
        </Row>
    );
}

// 접고 펴는 묶음
export function Section({ title, lock, children, defaultOpen = true, actions }: { title: string; lock?: string | null; children: ReactNode; defaultOpen?: boolean; actions?: ReactNode }) {
    const [open, setOpen] = useState(defaultOpen);
    return (
        <section className={'props-section' + (open ? '' : ' closed')}>
            <h4>
                <button className="section-toggle" onClick={() => setOpen(!open)}>
                    <span className="chevron">{open ? '▾' : '▸'}</span>{title}
                    {lock && <span className="section-lock" title={`${lock}. 코드에서 고쳐 주세요`}><Lock size={11} /></span>}
                </button>
                {actions}
            </h4>
            {open && children}
        </section>
    );
}

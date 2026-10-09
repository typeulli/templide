// 오른쪽 패널. 개체를 고르면 그 개체의 모든 속성(속성 탭)과 애니메이션(애니메이션 탭), 아무것도 고르지 않으면 슬라이드와 문서 설정
import { useState, type ReactNode } from 'react';
import { ChevronDown, ChevronUp, ChevronsDown, ChevronsUp, Film, List, ListOrdered, Minus, Pilcrow, Play, Plus, Trash2 } from 'lucide-react';
import type { DeckResult, ElementInfo, Origin, Schema, SchemaVar, SlideInfo, Value } from './lsp';
import {
    ActionField, BoolField, ColorField, EnumField, FileField, FontField, NumberField, PaintField, RefField, Row, Section, TextField, NumberInput,
    lockReason, type Measure, type SetValue,
} from './Fields';
import { label, objectNames, propertyNames, shapeKindNames, transitionNames, transitionOrder, optionNames, pptxOnlyTransitions } from './labels';
import { ObjectAnimations, animationsOf, type AnimationOp } from './Animations';
import { lockOf } from './SlideView';
import { t } from './i18n';

// 파일을 문서 폴더로 가져오는 함수들. 고르지 않으면 null
// 파일을 골라 기본 묶음(.tasset)에 넣고 그 파일을 가리키는 값(이름과 그 선언)을 준다
export type Pickers = {
    image(): Promise<SetValue | null>;
    media(kind: 'video' | 'audio'): Promise<SetValue | null>;
    sound(): Promise<SetValue | null>;
    poster(id: string): Promise<SetValue | null>; // 비디오의 첫 장면을 그림으로 저장한다
};

// 수 속성의 단위. 없으면 px
const measures: Record<string, Measure> = {
    rotation: 'deg', shadow_angle: 'deg', inner_shadow_angle: 'deg', rotation_x: 'deg', rotation_y: 'deg', perspective: 'deg',
    opacity: 'ratio', reflection: 'ratio', reflection_size: 'ratio', crop_left: 'percent', crop_top: 'percent', crop_right: 'percent', crop_bottom: 'percent',
    volume: 'percent', trim_start: 'seconds', trim_end: 'seconds', fade_in: 'seconds', fade_out: 'seconds', advance_after: 'seconds', columns: 'int',
    adj1: 'number', adj2: 'number', adj3: 'number', adj4: 'number', adj5: 'number', adj6: 'number', adj7: 'number', adj8: 'number',
};

// 속성 탭의 묶음. 개체가 가진 속성만 보이고, 어느 묶음에도 없는 속성은 "그 밖의 속성"에 모은다
const sections = (): [string, string[]][] => [
    [t('배치'), ['x', 'y', 'width', 'height', 'x1', 'y1', 'x2', 'y2', 'rotation', 'flip']],
    [t('모양'), ['kind', 'radius', 'adj1', 'adj2', 'adj3', 'adj4', 'adj5', 'adj6', 'adj7', 'adj8']],
    [t('내용'), ['text', 'anchor', 'data', 'path', 'poster', 'fit', 'crop_left', 'crop_top', 'crop_right', 'crop_bottom', 'role', 'blur',
        'from', 'to', 'from_side', 'to_side', 'start_arrow', 'end_arrow']],
    [t('재생'), ['start', 'volume', 'loop', 'rewind', 'fullscreen', 'hide_when_stopped', 'hide_icon', 'across_slides', 'trim_start', 'trim_end', 'fade_in', 'fade_out']],
    [t('글상자'), ['padding', 'padding_left', 'padding_top', 'padding_right', 'padding_bottom', 'autofit', 'wrap', 'text_direction', 'columns', 'column_gap']],
    [t('채우기'), ['fill', 'opacity']],
    [t('선'), ['line_color', 'line_width', 'line_dash', 'line_cap', 'line_join', 'line_compound']],
    [t('그림자'), ['shadow', 'shadow_blur', 'shadow_distance', 'shadow_angle', 'inner_shadow', 'inner_shadow_blur', 'inner_shadow_distance', 'inner_shadow_angle']],
    [t('네온, 가장자리, 반사'), ['glow', 'glow_size', 'soft_edge', 'reflection', 'reflection_size', 'reflection_distance', 'reflection_blur']],
    [t('3차원'), ['bevel', 'bevel_width', 'bevel_height', 'depth', 'depth_color', 'rotation_x', 'rotation_y', 'perspective']],
    [t('링크와 실행 설정'), ['link', 'action', 'action_sound', 'action_highlight', 'hover_action', 'hover_sound', 'hover_highlight']],
];

// 세부값은 기본값(앞의 속성)이 있어야 쓸 수 있다 (컴파일러 규칙)
const needs: Record<string, string> = {
    shadow_blur: 'shadow', shadow_distance: 'shadow', shadow_angle: 'shadow', inner_shadow_blur: 'inner_shadow', inner_shadow_distance: 'inner_shadow',
    inner_shadow_angle: 'inner_shadow', glow_size: 'glow', reflection_size: 'reflection', reflection_distance: 'reflection', reflection_blur: 'reflection',
    bevel_width: 'bevel', bevel_height: 'bevel', depth_color: 'depth',
};
const roundedKinds = ['roundRect', 'round1Rect', 'round2SameRect', 'round2DiagRect', 'snipRoundRect'];

// 그 개체가 쓸 수 없는 공통 속성 (컴파일러 규칙)
function unusable(object: string, kind: string, name: string): boolean {
    if ((object === 'line' || object === 'connector') && (name === 'fill' || name === 'flip')) {
        return true;
    }
    if (object === 'connector' && name === 'rotation') {
        return true;
    }
    if (object === 'backdrop' && name === 'fill') {
        return true;
    }
    return name === 'radius' && !(['shape', 'backdrop', 'image'].includes(object) && roundedKinds.includes(kind));
}

// 자주 쓰는 도형을 앞에
const commonKinds = ['rect', 'roundRect', 'ellipse', 'triangle', 'rtTriangle', 'diamond', 'parallelogram', 'trapezoid', 'pentagon', 'hexagon', 'octagon',
    'star5', 'star4', 'star6', 'rightArrow', 'leftArrow', 'upArrow', 'downArrow', 'leftRightArrow', 'chevron', 'homePlate', 'cloud', 'heart', 'lightningBolt',
    'sun', 'moon', 'donut', 'plus', 'wedgeRectCallout', 'wedgeRoundRectCallout', 'wedgeEllipseCallout', 'cloudCallout'];

type ElementProps = {
    deck: DeckResult;
    schema: Schema | null;
    page: number;
    id: string;
    info: ElementInfo;
    pickers: Pickers;
    readSource(origin: Origin): string | null;
    onSet(name: string, value: SetValue): void;
    onUnset(name: string): void;
    onOrder(direction: string): void;
    onAnimation(op: AnimationOp): void;
    onPreview(): void;
};

// 적지 않은 속성은 그 put 블록에 넣는다. template이 만든 element는 넣을 수 없다
function originOf(info: ElementInfo, name: string): Origin {
    return info.properties[name] ?? (info.fromTemplate ? { kind: 'locked', reason: 'template' } : { ...info.source, name });
}

export function ElementPanel(props: ElementProps) {
    const { info, id, deck, page } = props;
    const [tab, setTab] = useState<'properties' | 'animations'>('properties');
    const count = animationsOf(deck.slides[page], id).length;
    return (
        <aside className="props">
            <header className="props-title">
                <span>{objectNames[info.object] ?? info.object}</span>
                {info.name && <span className="name-badge" title={t('put ... as 이름')}>{info.name}</span>}
                {info.fromTemplate && <span className="badge" title={t('템플릿이 만든 요소입니다. 템플릿에 넘긴 값만 바꿀 수 있습니다')}>{t('템플릿')}</span>}
            </header>
            <div className="props-tabs">
                <button className={tab === 'properties' ? 'active' : ''} onClick={() => setTab('properties')}>{t('속성')}</button>
                <button className={tab === 'animations' ? 'active' : ''} onClick={() => setTab('animations')}>{t('애니메이션')}{count > 0 && <span className="count">{count}</span>}</button>
            </div>
            {tab === 'properties'
                ? <PropertyList {...props} />
                : <ObjectAnimations schema={props.schema} slide={deck.slides[page]} id={id} media={info.object === 'video' || info.object === 'audio'} onOp={props.onAnimation} onPreview={props.onPreview} />}
        </aside>
    );
}

function PropertyList({ deck, schema, page, id, info, pickers, readSource, onSet, onUnset, onOrder }: ElementProps) {
    const sourceLock = lockOf(info.source);
    if (!schema) {
        return <div className="props-empty">{t('속성 목록을 불러오는 중입니다')}</div>;
    }
    const own = schema.objects[info.object] ?? [];
    const hasText = own.some((each) => each.name === 'text');
    const vars: SchemaVar[] = info.object === 'group' ? [] : [...own, ...schema.commonProperties, ...(hasText ? schema.textProperties : [])];
    const byName = new Map(vars.map((each) => [each.name, each]));
    // 모양 조정값은 그 도형 종류에 있는 것만
    const kind = typeof info.values.kind === 'string' ? info.values.kind : info.object === 'image' ? 'rect' : '';
    const adjustments = info.object === 'shape' || info.object === 'backdrop' || info.object === 'image' ? window.Templide.adjustments(kind || 'rect') : [];
    // 조정값이 하나인 도형은 PowerPoint에서 이름이 adj이므로 순서로 맞춘다 (adj1 = 첫 조정값)
    const hidden = (name: string) => (/^adj\d$/.test(name) && Number(name.slice(3)) > adjustments.length) || unusable(info.object, kind, name);
    const used = new Set(sections().flatMap(([, names]) => names));
    const rest = vars.map((each) => each.name).filter((name) => !used.has(name));
    const names = Object.values(deck.elements).map((each) => each.name).filter((name): name is string => !!name);

    const field = (name: string): ReactNode => {
        const v = byName.get(name);
        if (!v || hidden(name)) {
            return null;
        }
        // 세부값은 기본값을 먼저 정해야 하고, 링크와 누를 때 동작은 하나만 쓸 수 있다
        const base = needs[name];
        const blocked = base && info.values[base] === undefined ? t('먼저 {0}을(를) 정하세요', propertyNames[base] ?? base)
            : name === 'link' && info.values.action !== undefined ? t('\'누를 때\'가 있어 링크를 쓸 수 없음')
            : name === 'action' && info.values.link !== undefined ? t('링크가 있어 쓸 수 없음 (링크를 지우세요)') : null;
        const origin: Origin = blocked ? { kind: 'locked', reason: blocked } : originOf(info, name);
        const common = {
            key: name, name, label: propertyNames[name] ?? name, value: info.values[name] as Value | undefined, origin,
            onSet: (value: SetValue) => onSet(name, value), onUnset: () => onUnset(name),
        };
        if (name === 'text') {
            return <TextInfo key={name} origin={common.origin} readSource={readSource} onSet={common.onSet} />;
        }
        if (name === 'kind' && v.type === 'shape_kind') {
            const all = schema.enums.shape_kind ?? [];
            return <EnumField {...common} options={[...commonKinds.filter((each) => all.includes(each)), ...all.filter((each) => !commonKinds.includes(each))]} names={shapeKindNames} />;
        }
        if (/^adj\d$/.test(name)) {
            const fallback = adjustments[Number(name.slice(3)) - 1];
            return <NumberField {...common} label={t('조정 {0}', name.slice(3))} measure="number" placeholder={fallback ? String(fallback.value / 100000) : undefined} />;
        }
        if (name === 'data' && (info.object === 'image' || info.object === 'video' || info.object === 'audio')) {
            return <FileField {...common} pick={info.object === 'image' ? pickers.image : () => pickers.media(info.object as 'video' | 'audio')} />;
        }
        if (name === 'poster') {
            return <FileField {...common} pick={pickers.image} extra={(
                <button className="mini-button" title={t('비디오의 첫 장면을 표지 그림으로')} disabled={!!lockReason(common.origin)} onClick={async () => {
                    const picked = await pickers.poster(id);
                    if (picked) {
                        onSet(name, picked);
                    }
                }}><Film size={13} /></button>
            )} />;
        }
        if (name === 'action_sound' || name === 'hover_sound') {
            return <FileField {...common} pick={pickers.sound} />;
        }
        switch (v.type) {
            case 'int':
            case 'float':
                return <NumberField {...common} measure={measures[name] ?? 'px'} />;
            case 'bool':
                return <BoolField {...common} />;
            case 'string':
                return <TextField {...common} />;
            case 'font':
                return <FontField {...common} />;
            case 'color':
                return <ColorField {...common} />;
            case 'color, gradient, pattern or image':
                return <PaintField {...common} patterns={schema.enums.pattern_kind ?? []} pickImage={pickers.image} />;
            case 'link':
                return <ActionField {...common} linkOnly />;
            case 'action':
                return <ActionField {...common} />;
            case 'object name':
                return <RefField {...common} names={names.filter((each) => each !== info.name)} />;
            default:
                if (schema.enums[v.type]) {
                    return <EnumField {...common} options={schema.enums[v.type]} />;
                }
                return <TextField {...common} />;
        }
    };

    return (
        <div className="property-list">
            {sections().map(([title, list]) => {
                const fields = list.map(field).filter(Boolean);
                return fields.length > 0 && <Section key={title} title={title}>{fields}</Section>;
            })}
            {rest.length > 0 && <Section title={t('그 밖의 속성')}>{rest.map(field)}</Section>}
            <Section title={t('순서')} lock={sourceLock}>
                <div className="order">
                    <button disabled={!!sourceLock} onClick={() => onOrder('back')} title={t('맨 뒤로')}><ChevronsDown size={15} /> {t('맨 뒤')}</button>
                    <button disabled={!!sourceLock} onClick={() => onOrder('backward')} title={t('뒤로')}><ChevronDown size={15} /> {t('뒤로')}</button>
                    <button disabled={!!sourceLock} onClick={() => onOrder('forward')} title={t('앞으로')}><ChevronUp size={15} /> {t('앞으로')}</button>
                    <button disabled={!!sourceLock} onClick={() => onOrder('front')} title={t('맨 앞으로')}><ChevronsUp size={15} /> {t('맨 앞')}</button>
                </div>
            </Section>
        </div>
    );
}

// text 속성: 글자는 캔버스에서 고치고, 여기서는 목록 종류(글머리 기호, 번호, 대시, 없음)를 바꾼다
function TextInfo({ origin, readSource, onSet }: { origin: Origin; readSource(origin: Origin): string | null; onSet(value: SetValue): void }) {
    const lock = lockReason(origin);
    const source = origin.kind === 'literal' ? readSource(origin) : null;
    const current = source ? (/^\s*(bullets|numbers|dashes|paragraphs)\s*\[/.exec(source)?.[1] ?? 'none') : null;
    // 문자열 하나는 줄마다 목록 항목으로 나누고, 목록은 앞의 종류 낱말만 바꾼다
    const change = (kind: string) => {
        if (!source || kind === current) {
            return;
        }
        if (current === 'none') {
            const text = /^"((?:[^"\\]|\\.)*)"$/.exec(source.trim());
            if (!text) {
                return;
            }
            const lines = text[1].split('\\n');
            onSet({ code: `${kind}[${lines.map((line) => `"${line}"`).join(', ')}]` });
            return;
        }
        const replaced = source.replace(/^(\s*)(bullets|numbers|dashes|paragraphs)/, `$1${kind === 'none' ? 'paragraphs' : kind}`);
        onSet({ code: replaced });
    };
    const kinds: [string, ReactNode, string][] = [['none', <Pilcrow size={14} />, t('목록 아님')], ['bullets', <List size={14} />, t('글머리 기호')],
        ['numbers', <ListOrdered size={14} />, t('번호 매기기')], ['dashes', <Minus size={14} />, t('대시')]];
    return (
        <>
            <Row label={t('글자')} lock={null}>
                <span className="muted small">{t('캔버스에서 두 번 눌러 고칩니다')}</span>
            </Row>
            <Row label={t('목록')} lock={current === null ? lock ?? t('글자가 하나의 문자열이나 목록이 아님') : null}>
                <span className="segmented">
                    {kinds.map(([kind, icon, title]) => (
                        <button key={kind} className={current === kind || (kind === 'none' && current === 'paragraphs') ? 'active' : ''} title={title} disabled={current === null} onClick={() => change(kind)}>{icon}</button>
                    ))}
                </span>
            </Row>
        </>
    );
}

// 슬라이드와 문서

type SlideProps = {
    deck: DeckResult;
    schema: Schema | null;
    page: number;
    pickers: Pickers;
    readSource(origin: Origin): string | null;
    onSlide(op: object): void;     // slide_set, slide_unset, layout, transition, notes, review (page는 붙여 준다)
    onDocument(op: object): void;  // document_set, document_unset
    onPreview(transition: boolean): void;
    onReviewSelect?(index: number): void;
};

export function SlidePanel({ deck, schema, page, pickers, readSource, onSlide, onDocument, onPreview }: SlideProps) {
    const slide: SlideInfo | undefined = deck.slides[page];
    if (!slide) {
        return <aside className="props"><div className="props-empty">{t('슬라이드가 없습니다')}</div></aside>;
    }
    const slideLock = lockOf(slide.source);
    const blockOrigin = (name: string): Origin => slide.properties[name] ?? { ...slide.source, name };
    const set = (name: string) => (value: SetValue) => onSlide({ op: 'slide_set', name, ...value });
    const unset = (name: string) => () => onSlide({ op: 'slide_unset', name });
    const layoutOrigin = slide.properties.layout;
    const layoutSource = layoutOrigin && layoutOrigin.kind === 'literal' ? readSource(layoutOrigin) : null;
    const layoutMaster = layoutSource ? /^\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(layoutSource)?.[1] : null;
    const layoutCase = layoutSource ? layoutSource.slice(layoutSource.lastIndexOf('.') + 1).trim() : null;
    const cases = layoutMaster && schema ? schema.masters[layoutMaster]?.cases ?? [] : [];
    const doc = deck.document;
    const docOrigin = (name: string): Origin | undefined => doc.properties?.[name] ?? (doc.source ? { ...doc.source, name } : undefined);
    return (
        <aside className="props">
            <header className="props-title">
                <span>{t('슬라이드 {0}', page + 1)}</span>
                {slide.section && <span className="name-badge" title={t('구역')}>{slide.section}</span>}
            </header>
            <div className="property-list">
                {layoutOrigin && (
                    <Section title={t('레이아웃')}>
                        <Row label={t('레이아웃')} lock={lockOf(layoutOrigin)}>
                            <select disabled={!!lockOf(layoutOrigin) || cases.length === 0} value={layoutCase ?? ''} onChange={(event) => onSlide({ op: 'layout', case: event.target.value })}>
                                {layoutCase && !cases.includes(layoutCase) && <option value={layoutCase}>{layoutCase}</option>}
                                {cases.map((each) => <option key={each} value={each}>{each}</option>)}
                            </select>
                        </Row>
                    </Section>
                )}
                <Section title={t('배경')} lock={slideLock}>
                    <PaintField name="background" label={t('배경')} value={slide.values.background} origin={blockOrigin('background')}
                        onSet={set('background')} onUnset={unset('background')} patterns={schema?.enums.pattern_kind ?? []} pickImage={pickers.image} />
                </Section>
                <TransitionSection deck={deck} schema={schema} slide={slide} pickers={pickers} onSlide={onSlide} onPreview={() => onPreview(true)} />
                <Section title={t('슬라이드 쇼')} lock={slideLock}>
                    <NumberField name="advance_after" label={t('자동으로 넘기기')} value={slide.values.advance_after} origin={blockOrigin('advance_after')} measure="seconds"
                        placeholder={t('클릭할 때')} onSet={set('advance_after')} onUnset={unset('advance_after')} />
                    <BoolField name="hidden" label={t('슬라이드 숨기기')} value={slide.values.hidden} origin={blockOrigin('hidden')} onSet={set('hidden')} onUnset={unset('hidden')} />
                    <div className="tab-actions">
                        <button className="ghost-button" onClick={() => onPreview(true)}><Play size={13} /> {t('이 슬라이드 미리 보기')}</button>
                    </div>
                </Section>
                <NotesSection slide={slide} onSlide={onSlide} />
                <ReviewSection slide={slide} onSlide={onSlide} />
                <Section title={t('문서 (고른 target)')} lock={doc.source ? lockOf(doc.source) : t('target이 없음')}>
                    <TextField name="title" label={t('제목')} value={doc.title} origin={docOrigin('title')}
                        onSet={(value) => onDocument({ op: 'document_set', name: 'title', ...value })} onUnset={() => onDocument({ op: 'document_unset', name: 'title' })} />
                    <TextField name="author" label={t('작성자')} value={doc.author} origin={docOrigin('author')}
                        onSet={(value) => onDocument({ op: 'document_set', name: 'author', ...value })} onUnset={() => onDocument({ op: 'document_unset', name: 'author' })} />
                    <BoolField name="loop" label={t('끝나면 처음부터 반복')} value={doc.loop} origin={docOrigin('loop')}
                        onSet={(value) => onDocument({ op: 'document_set', name: 'loop', ...value })} onUnset={() => onDocument({ op: 'document_unset', name: 'loop' })} />
                </Section>
            </div>
        </aside>
    );
}

function TransitionSection({ deck, schema, slide, pickers, onSlide, onPreview }: { deck: DeckResult; schema: Schema | null; slide: SlideInfo; pickers: Pickers; onSlide(op: object): void; onPreview(): void }) {
    const transition = slide.transition;
    const lock = transition ? lockOf(transition.source) : lockOf(slide.source);
    const all = schema ? Object.keys(schema.transitions) : [];
    const kinds = [...transitionOrder.filter((kind) => all.includes(kind)), ...all.filter((kind) => !transitionOrder.includes(kind))];
    const options = transition && schema ? schema.transitions[transition.kind] ?? [] : [];
    const write = (kind: string, option: string, duration?: number) => onSlide({ op: 'transition', kind, option, ...(duration !== undefined ? { duration } : {}) });
    const html = deck.targets.find((each) => each.name === deck.target)?.type !== 'pptx';
    const soundOrigin: Origin = slide.properties.transition_sound ?? { ...slide.source, name: 'transition_sound' };
    return (
        <Section title={t('화면 전환')} lock={lock}>
            <div className="transition-grid">
                <button className={!transition ? 'active' : ''} disabled={!!lock} onClick={() => onSlide({ op: 'transition', remove: true })}>{t('없음')}</button>
                {kinds.map((kind) => (
                    <button key={kind} className={transition?.kind === kind ? 'active' : ''} disabled={!!lock} title={kind + (pptxOnlyTransitions.has(kind) ? t(' (pptx 전용)') : '')}
                        onClick={() => write(kind, '', transition?.duration)}>
                        {label(transitionNames, kind)}{html && pptxOnlyTransitions.has(kind) ? ' *' : ''}
                    </button>
                ))}
            </div>
            {html && <div className="muted small note">{t('* 표시는 PowerPoint(pptx)에서만 됩니다')}</div>}
            {transition && options.length > 0 && (
                <Row label={t('효과 옵션')} lock={lock}>
                    <select disabled={!!lock} value={transition.option} onChange={(event) => write(transition.kind, event.target.value, transition.duration)}>
                        {options.map((option) => <option key={option} value={option}>{label(optionNames, option)}</option>)}
                    </select>
                </Row>
            )}
            {transition && (
                <Row label={t('기간')} lock={lock}>
                    <NumberInput value={transition.duration} measure="seconds" disabled={!!lock} placeholder={t('기본')} onCommit={(shown) => write(transition.kind, transition.option, Math.max(0, shown * 1000))} />
                </Row>
            )}
            <FileField name="transition_sound" label={t('소리 (wav)')} value={slide.values.transition_sound} origin={soundOrigin} pick={pickers.sound}
                onSet={(value) => onSlide({ op: 'slide_set', name: 'transition_sound', ...value })} onUnset={() => onSlide({ op: 'slide_unset', name: 'transition_sound' })} />
            {transition && (
                <div className="tab-actions">
                    <button className="ghost-button" onClick={onPreview}><Play size={13} /> {t('전환 미리 보기')}</button>
                </div>
            )}
        </Section>
    );
}

function NotesSection({ slide, onSlide }: { slide: SlideInfo; onSlide(op: object): void }) {
    const lock = slide.noteSources.map(lockOf).find(Boolean) ?? lockOf(slide.source);
    const [draft, setDraft] = useState(slide.notes);
    const [base, setBase] = useState(slide.notes);
    if (base !== slide.notes) {
        setBase(slide.notes);
        setDraft(slide.notes);
    }
    return (
        <Section title={t('발표자 메모')} lock={lock}>
            <textarea className="notes-input" disabled={!!lock} rows={4} placeholder={t('발표할 때 볼 메모 (comment 문장)')} value={draft}
                onChange={(event) => setDraft(event.target.value)} onBlur={() => draft !== slide.notes && onSlide({ op: 'notes', text: draft })} />
        </Section>
    );
}

function ReviewSection({ slide, onSlide }: { slide: SlideInfo; onSlide(op: object): void }) {
    const lock = lockOf(slide.source);
    const [adding, setAdding] = useState(false);
    const [text, setText] = useState('');
    const [author, setAuthor] = useState(() => localStorage.getItem('templide.author') ?? '');
    const save = () => {
        if (!text.trim()) {
            return;
        }
        localStorage.setItem('templide.author', author);
        onSlide({ op: 'review', action: 'add', text, author: author || 'templide', x: 20, y: 20 });
        setText('');
        setAdding(false);
    };
    return (
        <Section title={t('검토 메모{0}', slide.reviews.length ? ` (${slide.reviews.length})` : '')} lock={lock}
            actions={<button className="mini-button" title={t('검토 메모 추가')} disabled={!!lock} onClick={() => setAdding(!adding)}><Plus size={13} /></button>}>
            {slide.reviews.map((review, index) => (
                <ReviewItem key={index} review={review} onSave={(next) => onSlide({ op: 'review', action: 'update', index, text: next.text, author: next.author, x: review.x, y: review.y })}
                    onDelete={() => onSlide({ op: 'review', action: 'delete', index })} />
            ))}
            {slide.reviews.length === 0 && !adding && <div className="muted small">{t('검토 메모가 없습니다')}</div>}
            {adding && (
                <div className="review-form">
                    <input className="text-input" placeholder={t('작성자')} value={author} onChange={(event) => setAuthor(event.target.value)} />
                    <textarea rows={3} placeholder={t('메모')} value={text} onChange={(event) => setText(event.target.value)} />
                    <div className="tab-actions">
                        <button className="primary-button" onClick={save}>{t('추가')}</button>
                        <button className="ghost-button" onClick={() => setAdding(false)}>{t('취소')}</button>
                    </div>
                </div>
            )}
        </Section>
    );
}

function ReviewItem({ review, onSave, onDelete }: { review: SlideInfo['reviews'][number]; onSave(next: { text: string; author: string }): void; onDelete(): void }) {
    const lock = lockOf(review.source);
    const [text, setText] = useState(review.text);
    return (
        <div className="review-item">
            <div className="review-head">
                <span className="review-author">{review.author}</span>
                <span className="spacer" />
                <button className="mini-button danger" title={t('지우기')} disabled={!!lock} onClick={onDelete}><Trash2 size={12} /></button>
            </div>
            <textarea rows={2} disabled={!!lock} value={text} onChange={(event) => setText(event.target.value)}
                onBlur={() => text !== review.text && onSave({ text, author: review.author })} />
        </div>
    );
}


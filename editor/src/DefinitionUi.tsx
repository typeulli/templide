// template과 master를 Unity의 프리팹 편집 화면처럼 따로 열어 고치는 화면의 조각들: 위의 띠, 왼쪽 목록, 오른쪽 패널(매개변수와 master의 배경)
import { useState, type ReactNode } from 'react';
import { ArrowLeft, Layers, LayoutTemplate, Lock, Plus, X } from 'lucide-react';
import type { DeckResult, DefinitionInfo, DefinitionRef, DefinitionVar, Origin, Schema, Value } from './lsp';
import {
    ActionField, BoolField, ColorField, EnumField, FontField, NumberField, PaintField, Row, Section, TextField, lockReason, type SetValue,
} from './Fields';
import type { Pickers } from './Properties';
import { lockOf } from './SlideView';
import { t } from './i18n';

const sameRef = (a: DefinitionRef | null, b: DefinitionRef | null) => !!a && !!b && a.kind === b.kind && a.name === b.name;

// 정의를 열고 있다는 것을 알리는 띠. 돌아가는 단추와, master면 고치는 case를 고르는 탭이 있다
export function DefinitionBar({ definition, info, error, onExit, onCase }: {
    definition: DefinitionRef; info: DefinitionInfo | undefined; error: string | null; onExit(): void; onCase(name: string): void;
}) {
    const isMaster = definition.kind === 'master';
    return (
        <div className="definition-bar">
            <button className="definition-back" title={t('슬라이드로 돌아가기 (Esc)')} onClick={onExit}>
                <ArrowLeft size={14} /> {t('슬라이드')}
            </button>
            <span className="definition-path">
                <span className="definition-sep">›</span>
                {isMaster ? <Layers size={14} /> : <LayoutTemplate size={14} />}
                <span className="definition-kind">{isMaster ? t('마스터') : t('템플릿')}</span>
                <strong>{definition.name}</strong>
                {isMaster && info && info.cases.length > 0 && (
                    <>
                        <span className="definition-sep">›</span>
                        <select className="definition-case-select" value={info.case} title={t('레이아웃 (case)')} onChange={(event) => onCase(event.target.value)}>
                            {info.cases.map((each) => <option key={each} value={each}>{each}</option>)}
                        </select>
                    </>
                )}
            </span>
            {info?.package && <span className="definition-badge" title={t('표준 라이브러리 파일이라 고칠 수 없습니다')}><Lock size={11} /> {t('읽기 전용')}</span>}
            {error && <span className="definition-error" title={error}>{error}</span>}
            <span className="definition-hint">{isMaster ? t('이 case의 배경과 요소를 고칩니다') : t('이 템플릿이 만드는 요소를 고칩니다. 미리 보기는 매개변수의 기본값으로 그립니다')}</span>
        </div>
    );
}

// 왼쪽 목록: 문서의 template과 master. 눌러서 편집 화면으로 들어간다
export function DefinitionList({ schema, current, onOpen, collapsible }: {
    schema: Schema | null; current: DefinitionRef | null; onOpen(next: DefinitionRef): void; collapsible?: boolean;
}) {
    const [open, setOpen] = useState(true);
    const [folded, setFolded] = useState<string[]>([]); // 접어 둔 master (case가 보이지 않는다)
    const entries = (schema?.definitions ?? []).slice().sort((a, b) => Number(a.package) - Number(b.package) || (a.uri === b.uri ? a.range.start.line - b.range.start.line : a.uri < b.uri ? -1 : 1));
    const own = entries.filter((each) => !each.package);
    const packages = entries.filter((each) => each.package);
    const [showPackages, setShowPackages] = useState(false);
    const item = (each: (typeof entries)[number]): ReactNode => {
        const master = each.kind === 'master';
        const key = each.kind + each.name;
        const fold = master && each.cases.length > 0 && folded.includes(key);
        return (
            <div key={key}>
                <div className={'definition-item' + (sameRef(current, each) && (!master || !current?.case) ? ' current' : '')}
                    title={master ? t('마스터 {0} 편집', each.name) : t('템플릿 {0} 편집', each.name)} onClick={() => onOpen({ kind: each.kind, name: each.name })}>
                    <span className="chevron" onClick={(event) => {
                        if (master) {
                            event.stopPropagation();
                            setFolded(fold ? folded.filter((name) => name !== key) : [...folded, key]);
                        }
                    }}>{master && each.cases.length > 0 ? (fold ? '▸' : '▾') : ''}</span>
                    {master ? <Layers size={13} /> : <LayoutTemplate size={13} />}
                    <span className="definition-name">{each.name}</span>
                </div>
                {master && !fold && each.cases.map((name) => (
                    <div key={name} className={'definition-item case' + (sameRef(current, each) && current?.case === name ? ' current' : '')}
                        onClick={() => onOpen({ kind: 'master', name: each.name, case: name })}>
                        <span className="chevron" />
                        <span className="definition-name">{name}</span>
                    </div>
                ))}
            </div>
        );
    };
    if (entries.length === 0) {
        return null;
    }
    return (
        <div className={'definition-list' + (collapsible ? ' collapsible' : '') + (open ? ' open' : '')}>
            <button className="definition-title" onClick={() => setOpen(!open)}>
                <span className="chevron">{open ? '▾' : '▸'}</span>{t('개요')}
            </button>
            {open && (
                <div className="definition-items">
                    {own.length === 0 && <div className="muted small definition-empty">{t('이 문서에 정의한 템플릿과 마스터가 없습니다')}</div>}
                    {own.map(item)}
                    {packages.length > 0 && (
                        <>
                            <div className="definition-item group" onClick={() => setShowPackages(!showPackages)}>
                                <span className="chevron">{showPackages ? '▾' : '▸'}</span>
                                <span className="definition-name">{t('표준 라이브러리 ({0})', packages.length)}</span>
                            </div>
                            {showPackages && <div className="definition-children">{packages.map(item)}</div>}
                        </>
                    )}
                </div>
            )}
        </div>
    );
}

// 매개변수 하나에 맞는 입력 칸. 기본값이 없으면 타입의 기본값을 흐리게 보이고, 값을 넣으면 기본값이 된다
function ParameterField({ v, schema, pickers, disabled, onSet, onClear, onRemove }: {
    v: DefinitionVar; schema: Schema | null; pickers: Pickers; disabled: boolean;
    onSet(name: string, value: SetValue): void; onClear(name: string): void; onRemove(name: string): void;
}) {
    const origin: Origin = disabled
        ? { kind: 'locked', reason: 'package' }
        : v.hasDefault ? { ...(v.origin ?? { kind: 'locked', reason: 'computed' }), statement: v.origin?.kind === 'literal' } as Origin : { kind: 'block' };
    const common = {
        name: v.name, label: v.name, value: v.value as Value | undefined, origin,
        onSet: (value: SetValue) => onSet(v.name, value), onUnset: () => onClear(v.name),
    };
    const field = (): ReactNode => {
        switch (v.type) {
            case 'int': return <NumberField {...common} measure="int" />;
            case 'float': return <NumberField {...common} measure="number" />;
            case 'bool': return <BoolField {...common} />;
            case 'string': case 'text': return <TextField {...common} />;
            case 'font': return <FontField {...common} />;
            case 'color': return <ColorField {...common} />;
            case 'color, gradient, pattern or image':
                return <PaintField {...common} patterns={schema?.enums.pattern_kind ?? []} pickImage={pickers.image} />;
            case 'link': return <ActionField {...common} linkOnly />;
            case 'action': return <ActionField {...common} />;
            default:
                if (schema?.enums[v.type]) {
                    return <EnumField {...common} options={schema.enums[v.type]} />;
                }
                return <CodeField {...common} type={v.type} text={v.defaultText} />;
        }
    };
    return (
        <div className={'param' + (v.hasDefault ? '' : ' unset')}
            title={v.hasDefault ? undefined : t('기본값이 없는 매개변수입니다. put에서 꼭 넣어야 하고, 흐린 값은 미리 보기에 쓴 {0}의 기본값입니다', v.type)}>
            {field()}
            {!disabled && <button className="param-remove" title={t('매개변수 지우기')} onClick={() => onRemove(v.name)}><X size={11} /></button>}
        </div>
    );
}

// 기본값을 코드로 적는 칸 (목록, 그림 등 전용 입력이 없는 타입)
function CodeField({ name, label, origin, onSet, onUnset, type, text }: {
    name: string; label?: string; origin: Origin; onSet(value: SetValue): void; onUnset?(): void; type: string; text: string;
}) {
    const lock = lockReason(origin);
    const [draft, setDraft] = useState(text);
    const [base, setBase] = useState(text);
    if (base !== text) {
        setBase(text);
        setDraft(text);
    }
    const commit = () => {
        if (draft.trim() && draft !== text) {
            onSet({ code: draft.trim() });
        }
    };
    return (
        <Row label={label ?? name} lock={lock} origin={origin} onUnset={onUnset}>
            <input className="text-input" disabled={!!lock} value={draft} placeholder={type} onChange={(event) => setDraft(event.target.value)} onBlur={commit}
                onKeyDown={(event) => { if (event.key === 'Enter') commit(); if (event.key === 'Escape') setDraft(text); }} />
        </Row>
    );
}

// 새 매개변수의 기본값 코드. 기본값을 함께 넣어 이미 이 template을 쓰는 put이 오류가 되지 않게 한다
function starterCode(type: string, schema: Schema | null): string {
    switch (type) {
        case 'int': case 'float': return '0';
        case 'string': case 'text': return '""';
        case 'bool': return 'false';
        case 'color': return 'hex(000000)';
        case 'font': return 'font("Arial")';
        case 'fill': return 'hex(000000)';
        default: return schema?.enums[type]?.[0] ?? '';
    }
}

const baseTypes = ['int', 'float', 'string', 'text', 'bool', 'color', 'fill', 'font'];

function AddParameter({ schema, taken, onAdd }: { schema: Schema | null; taken: string[]; onAdd(type: string, name: string, code: string): void }) {
    const [name, setName] = useState('');
    const [type, setType] = useState('string');
    const types = [...baseTypes, ...Object.keys(schema?.enums ?? {}).filter((each) => !(schema?.definitions ?? []).some((d) => d.name === each))];
    const valid = /^[A-Za-z_][A-Za-z0-9_]*$/.test(name) && !taken.includes(name);
    const submit = () => {
        if (valid) {
            onAdd(type, name, starterCode(type, schema));
            setName('');
        }
    };
    return (
        <div className="param-add">
            <input className="text-input" placeholder={t('새 매개변수 이름')} value={name} onChange={(event) => setName(event.target.value)} onKeyDown={(event) => event.key === 'Enter' && submit()} />
            <select value={type} onChange={(event) => setType(event.target.value)}>
                {types.map((each) => <option key={each} value={each}>{each}</option>)}
            </select>
            <button className="mini-button" disabled={!valid} title={t('매개변수 추가')} onClick={submit}><Plus size={13} /></button>
        </div>
    );
}

type PanelProps = {
    deck: DeckResult; schema: Schema | null; pickers: Pickers; definition: DefinitionRef;
    onEdit(ops: object[]): void; onOpen(next: DefinitionRef): void;
};

// 아무것도 고르지 않았을 때의 오른쪽 패널. template은 매개변수, master는 case와 배경
export function DefinitionPanel(props: PanelProps) {
    const info = props.deck.definition;
    if (!info || !props.deck.slides[0]) {
        return <aside className="props"><div className="props-empty">{t('정의를 불러오는 중입니다')}</div></aside>;
    }
    return (
        <aside className="props definition-props">
            <header className="props-title">
                <span>{info.kind === 'master' ? t('마스터 {0}', info.name) : t('템플릿 {0}', info.name)}</span>
                {info.kind === 'master' && <span className="name-badge">{info.case}</span>}
            </header>
            <DefinitionSections {...props} />
        </aside>
    );
}

// 정의의 매개변수(template)나 case와 배경(master). 요소를 고른 패널의 아래에도 접어서 둔다
export function DefinitionSections({ deck, schema, pickers, definition, onEdit, onOpen, collapsed }: PanelProps & { collapsed?: boolean }) {
    const info = deck.definition;
    const slide = deck.slides[0];
    if (!info || !slide) {
        return null;
    }
    const slideLock = lockOf(slide.source);
    const set = (name: string, value: SetValue) => {
        if (value.code !== undefined && !value.code.trim()) {
            return;
        }
        onEdit([{ op: 'var_default', name, ...value }]);
    };
    const backgroundOrigin: Origin = slide.properties.background ?? { ...slide.source, name: 'background' };
    return (
        <div className="property-list definition-sections">
            {collapsed && <div className="definition-sections-title">{info.kind === 'master' ? t('마스터 {0}', info.name) : t('템플릿 {0}', info.name)}</div>}
            <>
                {(
                    <Section title={t('매개변수 ({0})', info.vars.length)} lock={info.package ? t('패키지 파일에 있는 값') : null} defaultOpen={!collapsed}>
                        {info.vars.length === 0 && <div className="muted small">{info.kind === 'master' ? t('매개변수가 없습니다. 매개변수는 layout에서 값을 받고, 기본값이 있으면 생략할 수 있습니다') : t('매개변수가 없습니다. 매개변수는 이 템플릿을 쓰는 put에서 값을 받습니다')}</div>}
                        {info.vars.map((v) => (
                            <ParameterField key={v.name} v={v} schema={schema} pickers={pickers} disabled={info.package}
                                onSet={set} onClear={(name) => onEdit([{ op: 'var_default_clear', name }])} onRemove={(name) => onEdit([{ op: 'var_remove', name }])} />
                        ))}
                        {!info.package && <AddParameter schema={schema} taken={info.vars.map((v) => v.name)} onAdd={(type, name, code) => onEdit([{ op: 'var_add', type, name, default: code }])} />}
                        {info.vars.some((v) => !v.hasDefault) && <div className="muted small param-note">{t('흐린 값은 기본값이 없는 매개변수의 미리 보기 값입니다 (int는 0, 목록은 빈 목록 ...)')}</div>}
                    </Section>
                )}
                {info.kind === 'master' && (
                    <>
                        <Section title={t('레이아웃 (case)')} defaultOpen={!collapsed}>
                            <select className="case-select" value={info.case} onChange={(event) => onOpen({ kind: 'master', name: definition.name, case: event.target.value })}>
                                {info.cases.map((each) => <option key={each} value={each}>{each}</option>)}
                            </select>
                        </Section>
                        <Section title={t('배경')} lock={slideLock} defaultOpen={!collapsed}>
                            <PaintField name="background" label={t('배경')} value={slide.values.background} origin={backgroundOrigin}
                                onSet={(value) => onEdit([{ op: 'slide_set', page: 1, name: 'background', ...value }])}
                                onUnset={() => onEdit([{ op: 'slide_unset', page: 1, name: 'background' }])}
                                patterns={schema?.enums.pattern_kind ?? []} pickImage={pickers.image} />
                        </Section>
                    </>
                )}
                {info.diagnostics.length > 0 && (
                    <Section title={t('미리 보기 오류 ({0})', info.diagnostics.length)}>
                        {info.diagnostics.map((each, i) => <div key={i} className="definition-diagnostic">{each.message}</div>)}
                    </Section>
                )}
            </>
        </div>
    );
}

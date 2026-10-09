// 설정 창의 단축키 쪽. 명령마다 키를 보여 주고, 눌러서 바꾸고, 더하고, 뺀다. 이미 다른 명령이 쓰는 키면 묻는다
import { useEffect, useState } from 'react';
import { Plus, RotateCcw, Search, X } from 'lucide-react';
import { t, useLang } from './i18n';
import { getSettings, updateSettings, useSettings, type Settings } from './settings';
import { bindings, checkBinding, commandIds, commands, conflictOf, eventBinding, formatBinding, isCustomized, type CommandId } from './shortcuts';

// index가 null이면 새 키를 더하는 중
type Recording = { id: CommandId; index: number | null };
type Conflict = Recording & { binding: string; other: CommandId };

// 기본 키와 같아지면 설정에서 지운다
function setBindings(settings: Settings, id: CommandId, keys: string[]) {
    const defaults = commands[id].defaults;
    if (keys.length === defaults.length && keys.every((key, i) => key === defaults[i])) {
        delete settings.shortcuts[id];
    } else {
        settings.shortcuts[id] = keys;
    }
}

export function ShortcutSettings() {
    useLang();
    const settings = useSettings();
    const [query, setQuery] = useState('');
    const [recording, setRecording] = useState<Recording | null>(null);
    const [conflict, setConflict] = useState<Conflict | null>(null);
    const [problem, setProblem] = useState<string | null>(null);

    const stop = () => {
        setRecording(null);
        setProblem(null);
    };

    // takeFrom이 있으면 그 명령에서는 이 키를 뺀다
    const assign = ({ id, index }: Recording, binding: string, takeFrom?: CommandId) => {
        updateSettings((next) => {
            const current = bindings(id, next.shortcuts);
            const keys = index === null ? [...current, binding] : current.map((each, i) => i === index ? binding : each);
            setBindings(next, id, [...new Set(keys)]);
            if (takeFrom) {
                setBindings(next, takeFrom, bindings(takeFrom, next.shortcuts).filter((each) => each !== binding));
            }
            return next;
        });
        setConflict(null);
        stop();
    };

    // 키를 받는 동안에는 창 전체의 키를 가로챈다
    useEffect(() => {
        if (!recording) {
            return;
        }
        const keydown = (event: KeyboardEvent) => {
            event.preventDefault();
            event.stopPropagation();
            if (event.key === 'Escape') {
                stop();
                return;
            }
            const binding = eventBinding(event);
            if (!binding) {
                return; // Ctrl, Shift 같은 보조 키만 눌렀다
            }
            const reason = checkBinding(binding);
            if (reason) {
                setProblem(t(reason));
                return;
            }
            const other = conflictOf(recording.id, binding, getSettings().shortcuts);
            if (other) {
                setConflict({ ...recording, binding, other });
                stop();
                return;
            }
            assign(recording, binding);
        };
        const mousedown = (event: MouseEvent) => {
            if (!(event.target as HTMLElement).closest('.key-chip.recording')) {
                stop();
            }
        };
        window.addEventListener('keydown', keydown, true);
        window.addEventListener('mousedown', mousedown, true);
        return () => {
            window.removeEventListener('keydown', keydown, true);
            window.removeEventListener('mousedown', mousedown, true);
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [recording]);

    const words = query.trim().toLowerCase();
    const shown = commandIds.filter((id) =>
        !words || t(commands[id].label).toLowerCase().includes(words) || bindings(id, settings.shortcuts).some((binding) => formatBinding(binding).toLowerCase().includes(words)));
    const groups = [...new Set(shown.map((id) => commands[id].group))];

    const begin = (next: Recording) => {
        setConflict(null);
        setProblem(null);
        setRecording(next);
    };
    const recordingChip = <span className="key-chip recording">{t('키를 누르세요…')}</span>;

    return (
        <div className="shortcuts">
            <div className="shortcuts-bar">
                <label className="search-box">
                    <Search size={14} />
                    <input value={query} placeholder={t('명령이나 키로 찾기')} onChange={(event) => setQuery(event.target.value)} />
                </label>
                <button className="plain-button" disabled={Object.keys(settings.shortcuts).length === 0}
                    onClick={() => { setConflict(null); stop(); updateSettings((next) => ({ ...next, shortcuts: {} })); }}>
                    <RotateCcw size={13} /> {t('모두 기본 키로')}
                </button>
            </div>
            <p className="setting-hint">{t('키를 누르면 다른 키로 바꿀 수 있고, +로 키를 더합니다. Esc를 누르면 취소합니다.')}</p>
            {problem && <p className="setting-problem">{problem}</p>}
            {groups.map((group) => (
                <section key={group} className="shortcut-group">
                    <h3>{t(group)}</h3>
                    {shown.filter((id) => commands[id].group === group).map((id) => {
                        const keys = bindings(id, settings.shortcuts);
                        const adding = recording?.id === id && recording.index === null;
                        return (
                            <div key={id} className="shortcut-item">
                                <div className="shortcut-row">
                                    <span className="shortcut-name">{t(commands[id].label)}</span>
                                    <span className="shortcut-keys">
                                        {keys.map((binding, i) => recording?.id === id && recording.index === i ? <span key={binding}>{recordingChip}</span> : (
                                            <span key={binding} className="key-chip">
                                                <button className="key-text" title={t('눌러서 다른 키로 바꿉니다')} onClick={() => begin({ id, index: i })}>{formatBinding(binding)}</button>
                                                <button className="key-remove" title={t('이 키 빼기')}
                                                    onClick={() => updateSettings((next) => { setBindings(next, id, keys.filter((_, at) => at !== i)); return next; })}><X size={11} /></button>
                                            </span>
                                        ))}
                                        {adding ? recordingChip : (
                                            <button className="key-add" title={t('키 더하기')} onClick={() => begin({ id, index: null })}><Plus size={13} /></button>
                                        )}
                                        {keys.length === 0 && !adding && <span className="muted">{t('없음')}</span>}
                                    </span>
                                    <button className="icon-only" disabled={!isCustomized(id, settings.shortcuts)} title={t('기본 키로 되돌리기')}
                                        onClick={() => updateSettings((next) => { delete next.shortcuts[id]; return next; })}><RotateCcw size={14} /></button>
                                </div>
                                {conflict?.id === id && (
                                    <div className="shortcut-conflict">
                                        <span>{t('{0}은(는) "{1}"에서 이미 쓰는 키입니다.', formatBinding(conflict.binding), t(commands[conflict.other].label))}</span>
                                        <button className="plain-button" onClick={() => assign(conflict, conflict.binding, conflict.other)}>{t('그 명령에서 빼고 쓰기')}</button>
                                        <button className="plain-button" onClick={() => setConflict(null)}>{t('취소')}</button>
                                    </div>
                                )}
                            </div>
                        );
                    })}
                </section>
            ))}
            {shown.length === 0 && <div className="settings-empty">{t('찾는 항목이 없습니다')}</div>}
        </div>
    );
}

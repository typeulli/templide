// 변경 기록. 이 탭에서 문서가 바뀔 때마다 글 전체를 기록해 두고(프로그램을 시작한 뒤로 가능한 한 많이), 패널에는 최신 몇 개만 보인다.
// Monaco는 실행 취소 목록을 내주지 않으므로 따로 기록한다. 항목을 누르면 그 시점의 글로 되돌리고, 되돌린 것도 한 번의 실행 취소로 취소할 수 있다.
// 에이전트(AI)가 MCP로 고친 것은 'AI'로 표시한다. 탭을 닫으면 기록도 사라진다
import { useState, useSyncExternalStore } from 'react';
import { History as HistoryIcon, RotateCcw } from 'lucide-react';
import { t, useLang } from './i18n';

export type HistoryOrigin = 'user' | 'ai' | 'restore';

export type HistoryEntry = { id: number; time: number; label: string; origin: HistoryOrigin; text: string };

// 한 탭의 기록이 가질 수 있는 글자 수. 넘으면 첫 항목을 남기고 오래된 것부터 버린다
const budget = 64 * 1024 * 1024;

export class DocumentHistory {
    entries: HistoryEntry[] = [];
    version = 0;
    private nextId = 1;
    private size = 0;
    private listeners = new Set<() => void>();

    subscribe = (listener: () => void) => {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    };

    getVersion = () => this.version;

    // 마지막 항목과 글이 같으면 기록하지 않는다. 기록했으면 true
    record(label: string, origin: HistoryOrigin, text: string): boolean {
        const last = this.entries[this.entries.length - 1];
        if (last && last.text === text) {
            return false;
        }
        this.entries.push({ id: this.nextId++, time: Date.now(), label, origin, text });
        this.size += text.length;
        while (this.size > budget && this.entries.length > 2) {
            this.size -= this.entries[1].text.length;
            this.entries.splice(1, 1);
        }
        this.version += 1;
        this.listeners.forEach((listener) => listener());
        return true;
    }
}

const clock = (time: number) => new Date(time).toLocaleTimeString([], { hour12: false });

const pageSize = 30;

export function HistoryPanel({ history, onRestore }: { history: DocumentHistory; onRestore(entry: HistoryEntry): void }) {
    useLang();
    useSyncExternalStore(history.subscribe, history.getVersion);
    const [shown, setShown] = useState(pageSize);
    const entries = history.entries;
    const list = entries.slice(-shown).reverse();
    const hidden = entries.length - list.length;
    return (
        <div className="history">
            <div className="history-note"><HistoryIcon size={13} /> {t('이 탭을 연 뒤의 변경 {0}개. 항목을 누르면 그 시점으로 되돌립니다 (실행 취소로 다시 취소할 수 있습니다)', entries.length)}</div>
            {list.map((entry, i) => (
                <button key={entry.id} className={'history-item' + (i === 0 ? ' current' : '')} disabled={i === 0}
                    title={i === 0 ? t('지금 상태') : t('{0} 시점으로 되돌리기', clock(entry.time))} onClick={() => onRestore(entry)}>
                    <span className="history-time">{clock(entry.time)}</span>
                    {entry.origin === 'ai' && <span className="history-ai" title={t('AI 에이전트가 고친 것')}>AI</span>}
                    {entry.origin === 'restore' && <RotateCcw size={12} className="history-mark" />}
                    <span className="history-label">{entry.label}</span>
                    {i === 0 && <span className="history-now">{t('지금')}</span>}
                </button>
            ))}
            {hidden > 0 && <button className="history-more" onClick={() => setShown(shown + pageSize)}>{t('이전 항목 {0}개 더 보기', hidden)}</button>}
        </div>
    );
}

// 캔버스에서 고친 것의 이름 (기록에 적는다). ops는 templide/edit에 보내는 편집들이다
export function editLabel(ops: { op?: string; action?: string; object?: string; name?: string }[]): string {
    const op = ops.find((each) => each.op !== 'declare') ?? ops[0];
    if (!op) {
        return t('캔버스 편집');
    }
    switch (op.op) {
        case 'move': return t('개체 이동');
        case 'set': case 'unset': case 'percent': case 'size': return t('속성 변경: {0}', op.name ?? '');
        case 'text': case 'style': case 'unstyle': return t('글 수정');
        case 'insert': return t('{0} 넣기', op.object ?? '');
        case 'delete': return t('개체 지우기');
        case 'copy': case 'paste': return t('개체 붙여넣기');
        case 'order': return t('그리는 순서 변경');
        case 'name': return t('개체 이름 변경');
        case 'animation': return t('애니메이션 편집');
        case 'transition': return t('전환 효과 편집');
        case 'notes': return t('발표자 메모 수정');
        case 'review': return t('검토 메모 편집');
        case 'layout': return t('슬라이드 배치 변경');
        case 'table': return t('표 편집');
        case 'slide': {
            const names: Record<string, string> = {
                add: t('슬라이드 추가'), delete: t('슬라이드 지우기'), duplicate: t('슬라이드 복제'), move: t('슬라이드 이동'), up: t('슬라이드 이동'), down: t('슬라이드 이동'),
            };
            return names[op.action ?? ''] ?? t('슬라이드 편집');
        }
        case 'slide_set': case 'slide_unset': return t('슬라이드 속성 변경');
        case 'document_set': case 'document_unset': return t('문서 속성 변경');
        case 'var_add': case 'var_remove': case 'var_default': case 'var_default_clear': return t('정의의 변수 편집');
        default: return t('캔버스 편집');
    }
}

// 접근성 패널. 컴파일러의 접근성 검사(templide tool access와 같은 검사)를 열린 문서에 돌려 결과를 보여 준다.
// 오류는 대체 텍스트가 없는 것처럼 꼭 고쳐야 하는 것, 경고는 색 대비 같은 고치면 좋은 것, 정보는 참고다
import { useEffect, useState } from 'react';
import { CircleAlert, CircleCheck, Info, LoaderCircle, TriangleAlert } from 'lucide-react';
import { lsp, type AccessFinding, type DeckResult, type Range } from './lsp';
import { t, useLang } from './i18n';

export type AccessCounts = { errors: number; warnings: number; infos: number };

export function AccessPanel({ uri, deck, onCounts, onReveal }: {
    uri: string | null; deck: DeckResult | null; onCounts(counts: AccessCounts): void; onReveal(range: Range): void;
}) {
    useLang(); // 화면 언어가 바뀌면 패널의 글을 다시 그린다
    const [findings, setFindings] = useState<AccessFinding[] | null>(null);
    const [problem, setProblem] = useState<string | null>(null);

    // 문서를 다시 그릴 때(deck이 바뀔 때) 다시 검사한다. 입력이 이어지는 동안은 잠깐 기다린다
    useEffect(() => {
        if (!uri || !deck) {
            return;
        }
        let stale = false;
        const timer = window.setTimeout(() => {
            lsp.request<{ findings?: AccessFinding[]; error?: string }>('templide/accessibility', { uri }).then((result) => {
                if (stale) {
                    return;
                }
                if (result.findings) {
                    setFindings(result.findings);
                    setProblem(null);
                } else {
                    setProblem(result.error ?? t('검사하지 못했습니다'));
                }
            }).catch(() => undefined);
        }, 400);
        return () => {
            stale = true;
            window.clearTimeout(timer);
        };
    }, [uri, deck]);

    useEffect(() => {
        const list = findings ?? [];
        onCounts({ errors: list.filter((each) => each.level === 'error').length, warnings: list.filter((each) => each.level === 'warning').length, infos: list.filter((each) => each.level === 'info').length });
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [findings]);

    const icon = (level: AccessFinding['level']) => level === 'error' ? <CircleAlert size={13} /> : level === 'warning' ? <TriangleAlert size={13} /> : <Info size={13} />;
    const list = findings ?? [];
    return (
        <div className="problems access">
            {problem && <div className="problems-item static warning"><TriangleAlert size={13} /><span className="problems-message">{t('코드에 오류가 있어 접근성을 검사할 수 없습니다. 문제 탭을 확인하세요')}</span></div>}
            {!problem && findings === null && <div className="problems-empty"><LoaderCircle size={13} className="spin" /> {t('검사하는 중')}</div>}
            {!problem && findings !== null && list.length === 0 && <div className="problems-empty"><CircleCheck size={13} /> {t('접근성 문제 없음')}</div>}
            {list.map((finding, i) => (
                <button key={i} className={'problems-item ' + (finding.level === 'error' ? 'error' : finding.level === 'warning' ? 'warning' : 'info')} onClick={() => onReveal(finding.range)}
                    title={finding.name ?? finding.code}>
                    {icon(finding.level)}
                    <span className="problems-message">{finding.message}</span>
                    <span className="problems-code">{finding.code}</span>
                    <span className="problems-line">{t('줄 {0}', finding.range.start.line + 1)}</span>
                </button>
            ))}
        </div>
    );
}

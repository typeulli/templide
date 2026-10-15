// 상태 표시줄의 인코딩을 누르면 뜨는 창. VS Code처럼 먼저 '인코딩으로 다시 열기'와 '인코딩으로 저장' 중 하나를 고르고,
// 이어서 인코딩 목록에서 고른다. 밖을 누르거나 Esc를 누르면 닫는다
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { encodingHint, encodingLabel, encodingNames } from './encodings';
import { t } from './i18n';

type Choice = { name: string; bom: boolean };

type Props = {
    x: number;
    y: number;                                // 누른 단추 아래. 여기서 아래로 연다
    above: number;                            // 누른 단추 위. 아래에 자리가 모자라면 여기서 위로 연다
    current: Choice;                         // 지금 이 파일의 인코딩
    onReopen(name: string): void;             // 고른 인코딩으로 다시 읽는다
    onSave(name: string, bom: boolean): void; // 고른 인코딩으로 저장한다
    onClose(): void;
};

function EncodingList({ current, withBom, onPick }: { current: Choice; withBom: boolean; onPick(choice: Choice): void }) {
    const [query, setQuery] = useState('');
    const [active, setActive] = useState(0);
    const items = useRef<HTMLDivElement>(null);
    // UTF-8은 저장할 때만 BOM이 있는 것도 고를 수 있다. UTF-16은 항상 BOM을 쓴다
    const choices: Choice[] = encodingNames.flatMap((name) => name === 'UTF-8' && withBom ? [{ name, bom: false }, { name, bom: true }] : [{ name, bom: name.startsWith('UTF-16') }]);
    const lower = query.trim().toLowerCase();
    const shown = choices.filter((choice) => `${encodingLabel(choice.name, choice.bom)} ${choice.name} ${encodingHint(choice.name)}`.toLowerCase().includes(lower));
    const same = (choice: Choice) => choice.name === current.name && (choice.name !== 'UTF-8' || !withBom || choice.bom === current.bom);
    // 처음에는 지금 인코딩에 둔다
    useEffect(() => {
        const at = shown.findIndex(same);
        setActive(Math.max(0, at));
        items.current?.querySelectorAll('button')[at]?.scrollIntoView({ block: 'center' });
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);
    const move = (step: number) => {
        const next = Math.max(0, Math.min(shown.length - 1, active + step));
        setActive(next);
        items.current?.querySelectorAll('button')[next]?.scrollIntoView({ block: 'nearest' });
    };
    return (
        <div className="font-list encoding-list">
            <input className="font-search" autoFocus placeholder={t('인코딩 검색')} value={query}
                onChange={(event) => { setQuery(event.target.value); setActive(0); }}
                onKeyDown={(event) => {
                    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
                        event.preventDefault();
                        move(event.key === 'ArrowDown' ? 1 : -1);
                    } else if (event.key === 'Enter') {
                        event.preventDefault();
                        if (shown[active]) {
                            onPick(shown[active]);
                        }
                    }
                }} />
            <div className="font-items" ref={items}>
                {shown.map((choice, i) => (
                    <button key={choice.name + choice.bom} className={'font-item' + (i === active ? ' active' : '') + (same(choice) ? ' current' : '')}
                        onMouseEnter={() => setActive(i)} onClick={() => onPick(choice)}>
                        {encodingLabel(choice.name, choice.bom)} <span className="muted">{encodingHint(choice.name)}</span>
                    </button>
                ))}
                {shown.length === 0 && <div className="font-empty">{t('맞는 인코딩이 없습니다')}</div>}
            </div>
        </div>
    );
}

export function EncodingPopup({ x, y, above, current, onReopen, onSave, onClose }: Props) {
    const box = useRef<HTMLDivElement>(null);
    const [step, setStep] = useState<'menu' | 'reopen' | 'save'>('menu');
    const [place, setPlace] = useState({ left: x, top: y });
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
    // 화면 밖으로 나가지 않게 한다. 메뉴와 목록은 높이가 다르므로 그려진 크기로 정하고, 단계가 바뀔 때마다 다시 정한다
    useLayoutEffect(() => {
        const width = box.current?.offsetWidth ?? 0;
        const height = box.current?.offsetHeight ?? 0;
        setPlace({
            left: Math.max(8, Math.min(x, window.innerWidth - width - 8)),
            top: y + height + 8 > window.innerHeight ? Math.max(8, above - height) : y,
        });
    }, [x, y, above, step]);
    return (
        <div ref={box} className="font-popup encoding-popup" style={place}>
            {step === 'menu' && (
                <div className="encoding-menu">
                    <button onClick={() => setStep('reopen')}>{t('인코딩으로 다시 열기')}</button>
                    <button onClick={() => setStep('save')}>{t('인코딩으로 저장')}</button>
                </div>
            )}
            {step === 'reopen' && <EncodingList current={current} withBom={false} onPick={(choice) => onReopen(choice.name)} />}
            {step === 'save' && <EncodingList current={current} withBom onPick={(choice) => onSave(choice.name, choice.bom)} />}
        </div>
    );
}

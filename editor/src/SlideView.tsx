// 캔버스. 슬라이드 하나를 templide.js로 그리고, 요소를 고르고 끌고 크기를 바꾸고 글자를 고친다.
// 바꾼 것은 화면에서 미리 보여 주기만 하고, 놓을 때 onMove, onResize, onText로 원문 수정을 요청한다
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import type { DeckResult, ElementInfo, Origin } from './lsp';
import { t } from './i18n';

type Box = { x: number; y: number; w: number; h: number };
export type Resize = { x?: number; y?: number; width?: number; height?: number };

// 글자를 고치는 중인 run. 서식 버튼이 그 안의 선택 범위에 서식을 넣는다
export type EditingRun = { id: string; paragraph: number; run: number; node: HTMLElement; original: string; cancel(): void };

// node 안의 선택 범위를 글자 위치(UTF-16, 줄바꿈 <br>은 한 글자)로. 선택이 없거나 비어 있으면 null
export function selectionIn(node: HTMLElement): { start: number; end: number } | null {
    const selection = window.getSelection();
    if (!selection || selection.rangeCount === 0 || selection.isCollapsed) {
        return null;
    }
    const range = selection.getRangeAt(0);
    if (!node.contains(range.startContainer) || !node.contains(range.endContainer)) {
        return null;
    }
    const offset = (container: Node, at: number) => {
        const before = document.createRange();
        before.setStart(node, 0);
        before.setEnd(container, at);
        let length = 0;
        const walk = (current: Node) => {
            if (current.nodeType === Node.TEXT_NODE) {
                length += current.textContent!.length;
            } else if (current.nodeName === 'BR') {
                length += 1;
            }
            current.childNodes.forEach(walk);
        };
        walk(before.cloneContents());
        return length;
    };
    return { start: offset(range.startContainer, range.startOffset), end: offset(range.endContainer, range.endOffset) };
}

const reasons = (): Record<string, string> => ({
    template: t('템플릿 정의 안에 적힌 값'),
    computed: t('계산식으로 만든 값'),
    for: t('for 반복으로 만든 값'),
    package: t('패키지 파일에 있는 값'),
    layout: t('마스터 레이아웃의 값'),
});

// 고칠 수 없으면 그 이유
export function lockOf(origin: Origin | undefined): string | null {
    if (!origin) {
        return t('코드에 적힌 값이 없음');
    }
    return origin.kind === 'locked' ? reasons()[origin.reason ?? ''] ?? origin.reason ?? t('고칠 수 없는 값') : null;
}

// 끌어서 옮길 수 없는 이유. template이 만든 요소는 그 template을 넣은 put의 x, y를 바꾼다
function moveLock(info: ElementInfo): string | null {
    const properties = info.fromTemplate ? info.instance ?? {} : info.properties;
    if (info.fromTemplate && (!properties.x || !properties.y)) {
        return t('템플릿을 넣은 put에 x, y가 없음');
    }
    return lockOf(properties.x) ?? lockOf(properties.y);
}

export function findElement(elements: any[], src: string): any {
    for (const el of elements) {
        if (el.eid === src) {
            return el;
        }
        if (el.c) {
            const found = findElement(el.c, src);
            if (found) {
                return found;
            }
        }
    }
    return null;
}

function nearestRun(node: HTMLElement, x: number, y: number): HTMLElement | null {
    let best: HTMLElement | null = null;
    let bestDistance = Infinity;
    for (const run of node.querySelectorAll<HTMLElement>('.tl-run')) {
        for (const box of run.getClientRects()) {
            const dx = Math.max(box.left - x, 0, x - box.right);
            const dy = Math.max(box.top - y, 0, y - box.bottom);
            const distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = run;
            }
        }
    }
    return best;
}

const sameRange = (a: Origin, b: Origin) =>
    a.uri === b.uri && a.range?.start.line === b.range?.start.line && a.range?.start.character === b.range?.start.character;

// 손잡이 이름의 글자가 움직이는 변. n, s는 위, 아래, w, e는 왼쪽, 오른쪽
const handles = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w'];

type Drag =
    | { kind: 'move'; id: string; startX: number; startY: number; dx: number; dy: number; nodes: HTMLElement[] }
    | { kind: 'resize'; id: string; handle: string; startX: number; startY: number; box: Box; current: Box };

export type Point = { x: number; y: number };

export function SlideView({ deck, index, selected, editable, editing, zoom, onZoom, onSelect, onPaint, onMove, onResize, onText, onNotice, stale, preview, onPreviewEnd, drawing, onDraw }: {
    deck: DeckResult;
    index: number;
    selected: string | null;
    editable: boolean;
    editing: React.MutableRefObject<EditingRun | null>;
    zoom: number | null; // null이면 화면에 맞춘다
    onZoom(zoom: number | null, fit: number): void;
    onSelect(id: string | null): void;
    onPaint(): void;
    onMove(id: string, dx: number, dy: number): Promise<boolean>;
    onResize(id: string, change: Resize, instance: boolean): Promise<boolean>;
    onText(id: string, paragraph: number, run: number, text: string): Promise<boolean>;
    onNotice(message: string): void;
    stale: boolean;
    // 미리 보기 중이면 이 슬라이드의 전환과 애니메이션을 재생한다 (key가 바뀌면 처음부터)
    preview: { transition: boolean; key: number } | null;
    onPreviewEnd(): void;
    // 자유형 그리기. 누를 때마다 점을 찍고, 두 번 누르거나 Enter로 끝낸다. Esc는 취소(null)
    drawing: boolean;
    onDraw(points: Point[] | null): void;
}) {
    const host = useRef<HTMLDivElement>(null);
    const frame = useRef<HTMLDivElement>(null);
    const drag = useRef<Drag | null>(null);
    const [fit, setFit] = useState(1); // 화면에 맞추는 배율
    const scale = zoom ?? fit;
    const [outline, setOutline] = useState<Box | null>(null);
    const [dragBox, setDragBox] = useState<Box | null>(null);
    const [offset, setOffset] = useState<{ dx: number; dy: number } | null>(null);
    const [points, setPoints] = useState<Point[]>([]);
    const pointsRef = useRef<Point[]>([]);
    pointsRef.current = points;
    const [cursor, setCursor] = useState<Point | null>(null);

    useLayoutEffect(() => {
        const resize = () => {
            const box = host.current!.getBoundingClientRect();
            setFit(Math.max(0.05, Math.min((box.width - 64) / deck.width, (box.height - 64) / deck.height)));
        };
        resize();
        const observer = new ResizeObserver(resize);

        observer.observe(host.current!);
        return () => observer.disconnect();
    }, [deck.width, deck.height]);

    useLayoutEffect(() => {
        const container = frame.current!;
        container.replaceChildren();
        if (!deck.deck.slides[index]) {
            return; // slide를 지우거나 넣은 직전처럼 덱과 번호가 잠깐 어긋날 때
        }
        if (preview) {
            return window.Templide.preview(container, deck.deck, index, { transition: preview.transition, onEnd: onPreviewEnd });
        }
        const { stage, fit } = window.Templide.renderSlide(deck.deck, index);
        container.appendChild(stage);
        fit();
        setDragBox(null);
        setOffset(null);
        requestAnimationFrame(() => requestAnimationFrame(onPaint));
    }, [deck, index, onPaint, preview, onPreviewEnd]);

    // 그리기를 끝내거나 그만두면 점을 비운다
    useEffect(() => {
        setPoints([]);
        setCursor(null);
        if (!drawing) {
            return;
        }
        const key = (event: KeyboardEvent) => {
            if (event.key === 'Escape') {
                onDraw(null);
            } else if (event.key === 'Enter') {
                const current = pointsRef.current;
                setPoints([]);
                onDraw(current.length >= 2 ? current : null);
            }
        };
        window.addEventListener('keydown', key);
        return () => window.removeEventListener('keydown', key);
    }, [drawing, onDraw]);

    const slidePoint = (event: { clientX: number; clientY: number }): Point => {
        const box = frame.current!.getBoundingClientRect();
        return { x: Math.round((event.clientX - box.left) / scale), y: Math.round((event.clientY - box.top) / scale) };
    };

    // 선택한 요소의 테두리. 화면에 그려진 상자에서 잰다. template이 만든 요소는 같은 put에서 나온 요소들을 모두 감싼다
    useLayoutEffect(() => {
        const nodes = selected && deck.elements[selected] ? nodesToMove(selected) : [];
        if (nodes.length === 0) {
            setOutline(null);
            return;
        }
        const outer = frame.current!.getBoundingClientRect();
        const boxes = nodes.map((node) => node.getBoundingClientRect());
        const left = Math.min(...boxes.map((box) => box.left));
        const top = Math.min(...boxes.map((box) => box.top));
        const right = Math.max(...boxes.map((box) => box.right));
        const bottom = Math.max(...boxes.map((box) => box.bottom));
        setOutline({ x: (left - outer.left) / scale, y: (top - outer.top) / scale, w: (right - left) / scale, h: (bottom - top) / scale });
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [selected, deck, index, scale]);

    const info = selected ? deck.elements[selected] : undefined;
    const lock = info ? moveLock(info) : null;
    const el = selected ? findElement(deck.deck.slides[index]?.els ?? [], selected) : null;
    // 손잡이마다 바꿔야 하는 속성이 모두 고칠 수 있어야 한다
    const handleAllowed = (handle: string) => {
        if (!info || !el || (el.t === 'grp' && !info.fromTemplate)) {
            return false;
        }
        const needs = [
            ...(handle.includes('w') ? ['x', 'width'] : handle.includes('e') ? ['width'] : []),
            ...(handle.includes('n') ? ['y', 'height'] : handle.includes('s') ? ['height'] : []),
        ];
        const properties = info.fromTemplate ? info.instance ?? {} : info.properties;
        return needs.every((name) => !lockOf(properties[name]));
    };

    // template이 만든 요소는 같은 put에서 나온 요소를 함께 옮긴다
    const nodesToMove = (id: string): HTMLElement[] => {
        const target = deck.elements[id];
        const ids = target.fromTemplate ? Object.keys(deck.elements).filter((other) => deck.elements[other].fromTemplate && sameRange(deck.elements[other].source, target.source)) : [id];
        return ids.map((other) => frame.current!.querySelector<HTMLElement>(`[data-src="${CSS.escape(other)}"]`)).filter((node): node is HTMLElement => !!node)
            // 그룹 안의 요소는 그룹을 옮기면 같이 움직인다
            .filter((node, _, all) => !all.some((parent) => parent !== node && parent.contains(node)));
    };

    const onPointerDown = (event: React.PointerEvent) => {
        const target = event.target as HTMLElement;
        if (target.isContentEditable || event.button !== 0) {
            return;
        }
        if (drawing) {
            const point = slidePoint(event);
            setPoints((current) => [...current, point]);
            return;
        }
        if (preview) {
            return;
        }
        // 캔버스를 누르면 코드 패널에서 포커스를 빼서 키 입력이 코드로 가지 않게 한다
        (document.activeElement as HTMLElement | null)?.blur();
        const handle = target.dataset.handle;
        if (handle && selected && el) {
            // template은 화면의 테두리(만든 요소 전체)를 기준으로 늘리고 줄인다
            const box = deck.elements[selected]?.fromTemplate && outline ? outline : { x: el.x, y: el.y, w: el.w, h: el.h };
            drag.current = { kind: 'resize', id: selected, handle, startX: event.clientX, startY: event.clientY, box, current: { ...box } };
            return;
        }
        const node = target.closest<HTMLElement>('[data-src]');
        const id = node?.dataset.src ?? null;
        onSelect(id);
        if (!id || !editable) {
            return;
        }
        const reason = moveLock(deck.elements[id]);
        if (reason) {
            return;
        }
        // preventDefault를 하면 dblclick이 오지 않으므로, 끄는 동안 글자가 선택되는 것은 CSS(user-select)로 막는다
        drag.current = { kind: 'move', id, startX: event.clientX, startY: event.clientY, dx: 0, dy: 0, nodes: nodesToMove(id) };
    };

    useEffect(() => {
        const move = (event: PointerEvent) => {
            const current = drag.current;
            if (!current) {
                return;
            }
            const dx = (event.clientX - current.startX) / scale;
            const dy = (event.clientY - current.startY) / scale;
            if (current.kind === 'move') {
                current.dx = dx;
                current.dy = dy;
                for (const node of current.nodes) {
                    node.style.translate = `${dx}px ${dy}px`;
                }
                setOffset({ dx, dy });
                return;
            }
            // 크기는 0 아래로 내려가지 않는다. 왼쪽, 위쪽 변을 끌면 반대쪽 변은 그 자리에 있다
            const { box, handle } = current;
            let { x, y, w, h } = box;
            if (handle.includes('e')) {
                w = Math.max(1, box.w + dx);
            }
            if (handle.includes('s')) {
                h = Math.max(1, box.h + dy);
            }
            if (handle.includes('w')) {
                w = Math.max(1, box.w - dx);
                x = box.x + box.w - w;
            }
            if (handle.includes('n')) {
                h = Math.max(1, box.h - dy);
                y = box.y + box.h - h;
            }
            current.current = { x, y, w, h };
            setDragBox({ x, y, w, h });
        };
        const up = async () => {
            const current = drag.current;
            drag.current = null;
            if (!current) {
                return;
            }
            if (current.kind === 'move') {
                const dx = Math.round(current.dx);
                const dy = Math.round(current.dy);
                if ((dx !== 0 || dy !== 0) && await onMove(current.id, dx, dy)) {
                    return; // 다시 컴파일한 덱이 오면 새로 그린다
                }
                for (const node of current.nodes) {
                    node.style.translate = '';
                }
                setOffset(null);
                return;
            }
            const { box } = current;
            const next = current.current;
            const target = deck.elements[current.id];
            const instance = !!target?.fromTemplate;
            // template은 put에 적은 값에 바뀐 만큼을 더한다
            const base = (name: keyof Resize, fallback: number) => instance ? (target.instanceValues?.[name] ?? fallback) : fallback;
            const change: Resize = {};
            const deltas: [keyof Resize, number, number][] = [['x', next.x, box.x], ['y', next.y, box.y], ['width', next.w, box.w], ['height', next.h, box.h]];
            for (const [name, after, before] of deltas) {
                if (Math.round(after) !== Math.round(before)) {
                    change[name] = Math.max(name === 'width' || name === 'height' ? 1 : -Infinity, Math.round(base(name, before) + after - before));
                }
            }
            if (Object.keys(change).length === 0 || !await onResize(current.id, change, instance)) {
                setDragBox(null);
            }
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        return () => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
        };
    }, [scale, onMove, onResize, deck]);

    useEffect(() => {
        const element = host.current!;
        const wheel = (event: WheelEvent) => {
            if (!event.ctrlKey) {
                return;
            }
            event.preventDefault();
            const next = Math.min(4, Math.max(0.1, scale * (event.deltaY < 0 ? 1.1 : 1 / 1.1)));
            onZoom(next, fit);
        };
        element.addEventListener('wheel', wheel, { passive: false });
        return () => element.removeEventListener('wheel', wheel);
    }, [scale, fit, onZoom]);

    // 글자를 두 번 누르면 그 run을 그 자리에서 고친다. Enter로 끝내고 Shift+Enter는 줄바꿈, Esc는 취소
    const onDoubleClick = (event: React.MouseEvent) => {
        if (drawing) {
            // 두 번 누른 것의 두 번째 점은 이미 들어가 있다
            const finished = points.length >= 2 ? points : null;
            setPoints([]);
            onDraw(finished);
            return;
        }
        const node = (event.target as HTMLElement).closest<HTMLElement>('[data-src]');
        // 글자 밖(글상자의 빈 곳)을 눌렀으면 가장 가까운 run을 고친다
        const run = (event.target as HTMLElement).closest<HTMLElement>('.tl-run') ?? (node ? nearestRun(node, event.clientX, event.clientY) : null);
        if (!run || !node || !editable) {
            return;
        }
        const id = node.dataset.src!;
        const paragraph = run.closest('.tl-p')!;
        const paragraphIndex = Array.from(node.querySelectorAll('.tl-p')).indexOf(paragraph);
        const runIndex = Array.from(paragraph.querySelectorAll('.tl-run')).indexOf(run);
        const reason = lockOf(deck.elements[id]?.text?.[paragraphIndex]?.[runIndex]);
        if (reason) {
            onNotice(t('이 글자는 고칠 수 없습니다: {0}. 코드에서 고쳐 주세요', reason));
            return;
        }
        const original = run.innerText;
        run.contentEditable = 'true';
        run.classList.add('editing');
        run.focus();
        const range = document.createRange();
        range.selectNodeContents(run);
        const selection = window.getSelection()!;
        selection.removeAllRanges();
        selection.addRange(range);
        let done = false;
        const finish = async (commit: boolean) => {
            if (done) {
                return;
            }
            done = true;
            editing.current = null;
            run.removeEventListener('keydown', keydown);
            run.removeEventListener('blur', blur);
            run.contentEditable = 'false';
            run.classList.remove('editing');
            const text = run.innerText.replace(/\r/g, '');
            if (!commit || text === original || !await onText(id, paragraphIndex, runIndex, text)) {
                run.innerText = original;
            }
        };
        const keydown = (key: KeyboardEvent) => {
            if (key.isComposing) {
                return; // 한글을 조합하는 중의 Enter는 글자를 확정할 뿐이다
            }
            if (key.key === 'Enter' && !key.shiftKey) {
                key.preventDefault();
                finish(true);
            } else if (key.key === 'Escape') {
                finish(false);
            }
        };
        const blur = () => finish(true);
        run.addEventListener('keydown', keydown);
        run.addEventListener('blur', blur);
        editing.current = { id, paragraph: paragraphIndex, run: runIndex, node: run, original, cancel: () => finish(false) };
    };

    const shown = dragBox ?? outline;
    const resizing = drag.current?.kind === 'resize';
    return (
        <div ref={host} className={'slide-host' + (zoom !== null && zoom > fit ? ' zoomed' : '') + (drawing ? ' drawing' : '')} onPointerDown={onPointerDown} onDoubleClick={onDoubleClick}
            onPointerMove={drawing ? (event) => setCursor(slidePoint(event)) : undefined}>
            <div className="slide-stage">
            <div className="slide-box" style={{ width: deck.width * scale, height: deck.height * scale }}>
            <div className={'slide-frame' + (stale ? ' stale' : '') + (editable ? ' editable' : '')} style={{ width: deck.width, height: deck.height, transform: `scale(${scale})` }}>
                <div ref={frame} className="slide-content" />
                {drawing && (
                    <svg className="draw-layer" width={deck.width} height={deck.height}>
                        <polyline points={[...points, ...(cursor ? [cursor] : [])].map((point) => `${point.x},${point.y}`).join(' ')} fill="none" stroke="#0d99ff" strokeWidth={2 / scale} />
                        {points.map((point, i) => <circle key={i} cx={point.x} cy={point.y} r={4 / scale} fill="#fff" stroke="#0d99ff" strokeWidth={1.5 / scale} />)}
                    </svg>
                )}
                {shown && !preview && !drawing && (
                    <div className={'selection' + (lock ? ' locked' : '') + (resizing ? ' resizing' : '')} style={{ left: shown.x, top: shown.y, width: shown.w, height: shown.h, borderWidth: 2 / scale, translate: offset ? `${offset.dx}px ${offset.dy}px` : undefined }}>
                        {editable && !dragBox && !offset && handles.filter(handleAllowed).map((handle) => (
                            <div key={handle} data-handle={handle} className={`handle handle-${handle}`} style={{ width: 9 / scale, height: 9 / scale, borderWidth: 1 / scale }} />
                        ))}
                        {lock && <div className="lock" style={{ fontSize: 12 / scale, padding: `${2 / scale}px ${6 / scale}px`, top: -22 / scale }}>{t('🔒 옮길 수 없음: {0}', lock)}</div>}
                    </div>
                )}
            </div>
            </div>
            </div>
        </div>
    );
}

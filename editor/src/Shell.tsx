// 편집기 창. 위의 탭 줄에 연 파일들이 있고, 탭마다 .tlide 편집 화면(App)이나 .tasset 묶음 화면(AssetView)을 띄운다.
// 보이지 않는 탭은 숨겨 두어 편집 상태(실행 취소, 선택, 슬라이드 위치 등)를 그대로 둔다.
// 컴파일러 연결, MCP, 창 닫기 확인, 다른 프로세스가 보낸 파일(탐색기에서 연 파일)을 탭으로 여는 일은 여기서 한 번만 한다
import { memo, useCallback, useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { ask, message, open as openDialog } from '@tauri-apps/plugin-dialog';
import { FileText, Package, X } from 'lucide-react';
import { App, AppMenu, WindowControls, isMac, type DocumentHandle } from './App';
import { AssetView, type OpenDocument } from './AssetView';
import { lsp, type Range } from './lsp';
import { serveMcp, type EditorAccess } from './mcp';
import './tabs.css';

type Tab = {
    id: string;
    kind: 'tlide' | 'asset';
    file: string | null; // 탭을 열 때 준 파일. 없으면 빈 탭
    path: string | null; // 지금 연 파일
    dirty: boolean;
};

let nextId = 1;
const createTab = (file: string | null): Tab => ({ id: `tab${nextId++}`, kind: file && /\.tasset$/i.test(file) ? 'asset' : 'tlide', file, path: file, dirty: false });

// Windows 경로는 대소문자와 구분자를 가리지 않는다
const samePath = (a: string, b: string) => a.replace(/\//g, '\\').toLowerCase() === b.replace(/\//g, '\\').toLowerCase();
const nameOf = (path: string) => path.split(/[\\/]/).pop()!;

// 문서가 없는 탭(.tasset, 닫은 탭)에 온 MCP 도구 호출
const noDocument: EditorAccess = {
    path: null, dirty: false, text: null, deck: null, stale: false, page: 0, selected: null, diagnostics: [], warnings: [],
    edit: async () => ({ error: 'No document is open in the editor' }),
    goto: () => {},
    select: () => {},
    resolve: () => null,
    build: async () => ({ error: 'No document is open in the editor' }),
};

// 다른 탭이 바뀌어도 숨은 탭의 화면은 다시 그리지 않는다
const TabApp = memo(App);
const TabAssets = memo(AssetView);

type TabCallbacks = {
    register(handle: DocumentHandle | null): void;
    onInfo(info: { path: string | null; dirty: boolean }): void;
};

export function Shell() {
    const [ready, setReady] = useState(false);
    const [tabs, setTabs] = useState<Tab[]>([]);
    const [activeId, setActiveId] = useState<string | null>(null);
    const tabsRef = useRef<Tab[]>([]);
    const activeRef = useRef<string | null>(null);
    const handles = useRef(new Map<string, DocumentHandle>());   // .tlide 탭 id -> 손잡이
    const accesses = useRef(new Map<string, EditorAccess>());    // .tlide 탭 id -> MCP 도구가 보는 상태
    const callbacks = useRef(new Map<string, TabCallbacks>());

    // 탭 목록과 보이는 탭은 연달아 바꿀 수 있게(파일 여러 개 열기) ref를 먼저 바꾼다
    const update = useCallback((next: Tab[], active: string | null = activeRef.current) => {
        tabsRef.current = next;
        activeRef.current = active;
        setTabs(next);
        setActiveId(active);
    }, []);

    const activate = useCallback((id: string) => update(tabsRef.current, id), [update]);

    // 파일을 탭으로 연다. 이미 열려 있으면 그 탭을 보이고, 보이는 탭이 아무것도 열지 않은 빈 탭이면 그 자리에 연다
    const open = useCallback((file: string) => {
        const current = tabsRef.current;
        const existing = current.find((tab) => tab.path && samePath(tab.path, file));
        if (existing) {
            update(current, existing.id);
            return;
        }
        const tab = createTab(file);
        const index = current.findIndex((each) => each.id === activeRef.current);
        const active = current[index];
        if (active && active.kind === 'tlide' && !active.path && !active.dirty) {
            update(current.map((each) => each.id === active.id ? tab : each), tab.id);
        } else {
            update([...current.slice(0, index + 1), tab, ...current.slice(index + 1)], tab.id);
        }
    }, [update]);

    const pick = useCallback(async () => {
        const files = await openDialog({ multiple: true, filters: [{ name: 'templide (.tlide, .tasset)', extensions: ['tlide', 'tasset'] }] });
        for (const each of Array.isArray(files) ? files : typeof files === 'string' ? [files] : []) {
            open(each);
        }
    }, [open]);

    // 탭을 닫는다. 저장하지 않은 변경이 있으면 묻는다. 마지막 탭을 닫으면 빈 탭을 하나 남긴다
    const close = useCallback(async (id: string) => {
        const tab = tabsRef.current.find((each) => each.id === id);
        const handle = handles.current.get(id);
        if (!tab) {
            return;
        }
        if (tab.kind === 'tlide' && handle?.dirty()) {
            if (!handle.path()) {
                if (!await ask('저장하지 않은 내용이 있습니다. 탭을 닫을까요?', { title: 'templide', kind: 'warning', okLabel: '닫기', cancelLabel: '취소' })) {
                    return;
                }
            } else {
                activate(id);
                const choice = await message(`${nameOf(handle.path()!)}에 저장하지 않은 변경이 있습니다. 저장할까요?`, {
                    title: 'templide', kind: 'warning', buttons: { yes: '저장', no: '저장 안 함', cancel: '취소' },
                });
                if (choice === 'Yes' || choice === '저장') {
                    if (!await handle.save()) {
                        return;
                    }
                } else if (choice !== 'No' && choice !== '저장 안 함') {
                    return;
                }
            }
        }
        const current = tabsRef.current;
        const index = current.findIndex((each) => each.id === id);
        let next = current.filter((each) => each.id !== id);
        if (next.length === 0) {
            next = [createTab(null)];
        }
        const active = activeRef.current === id ? next[Math.min(index, next.length - 1)].id : activeRef.current;
        callbacks.current.delete(id);
        update(next, active);
    }, [activate, update]);

    // 탭을 끌어 놓은 자리로 옮긴다. Tauri 창은 HTML 드래그 앤 드롭을 파일 놓기로 가로채므로 포인터 이벤트로 직접 끈다.
    // over는 놓을 자리: i번째 탭 앞이 i, 맨 끝이 탭 수
    const [drag, setDrag] = useState<{ id: string; over: number } | null>(null);
    const tabsBox = useRef<HTMLDivElement>(null);
    const pointerDown = (event: ReactPointerEvent, id: string) => {
        if (event.button !== 0) {
            return;
        }
        activate(id);
        const startX = event.clientX;
        let dragging = false;
        let over: number | null = null;
        const move = (moved: PointerEvent) => {
            if (!dragging && Math.abs(moved.clientX - startX) < 5) {
                return;
            }
            dragging = true;
            const slots = [...(tabsBox.current?.querySelectorAll<HTMLElement>('.tab') ?? [])];
            const at = slots.findIndex((slot) => {
                const box = slot.getBoundingClientRect();
                return moved.clientX < box.left + box.width / 2;
            });
            over = at < 0 ? slots.length : at;
            setDrag({ id, over });
        };
        const up = () => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            window.removeEventListener('pointercancel', cancel);
            setDrag(null);
            const current = tabsRef.current;
            const from = current.findIndex((tab) => tab.id === id);
            if (!dragging || over === null || from < 0) {
                return;
            }
            const to = over > from ? over - 1 : over;
            if (to !== from) {
                const next = [...current];
                next.splice(to, 0, ...next.splice(from, 1));
                update(next);
            }
        };
        const cancel = () => {
            over = null;
            up();
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        window.addEventListener('pointercancel', cancel);
    };

    // 탭마다 한 번 만든 함수를 계속 넘겨 숨은 탭을 다시 그리지 않게 한다
    const callbacksFor = (id: string): TabCallbacks => {
        let found = callbacks.current.get(id);
        if (!found) {
            found = {
                register: (handle) => {
                    if (handle) {
                        handles.current.set(id, handle);
                    } else {
                        handles.current.delete(id);
                    }
                },
                onInfo: ({ path, dirty }) => {
                    const current = tabsRef.current;
                    if (current.some((tab) => tab.id === id && (tab.path !== path || tab.dirty !== dirty))) {
                        update(current.map((tab) => tab.id === id ? { ...tab, path, dirty } : tab));
                    }
                },
            };
            callbacks.current.set(id, found);
        }
        return found;
    };

    // 묶음 탭이 쓰는 것: 열린 .tlide 탭들, 코드의 한 자리로 가기, 묶음이 바뀌었을 때 다시 컴파일
    const documents = useCallback((): OpenDocument[] => tabsRef.current.flatMap((tab) => {
        const handle = handles.current.get(tab.id);
        const path = handle?.path();
        return tab.kind === 'tlide' && handle && path ? [{ tab: tab.id, name: nameOf(path), handle }] : [];
    }), []);
    const reveal = useCallback((id: string, range: Range) => {
        activate(id);
        // 숨어 있던 편집기가 보이고 크기를 잰 뒤에 옮긴다
        window.setTimeout(() => handles.current.get(id)?.reveal(range), 50);
    }, [activate]);
    const bundleChanged = useCallback(() => {
        lsp.notify('templide/reload', {});
        for (const handle of handles.current.values()) {
            handle.refresh();
        }
    }, []);

    // 시작: 컴파일러와 연결하고, MCP를 열고, 명령줄의 파일(없으면 빈 탭)을 연다
    useEffect(() => {
        (async () => {
            await lsp.start();
            await lsp.request('initialize', { processId: null, rootUri: null, capabilities: {} });
            lsp.notify('initialized', {});
            // 에이전트는 자기를 실행한 탭(session)의 문서를 고친다. session이 없으면 보이는 탭
            serveMcp((session) => accesses.current.get(session || activeRef.current || '') ?? noDocument).catch((error) => console.error('mcp', error));
            // 저장하지 않은 탭이 있으면 창을 닫기 전에 묻는다
            getCurrentWindow().onCloseRequested(async (event) => {
                const unsaved = tabsRef.current.filter((tab) => handles.current.get(tab.id)?.dirty());
                if (unsaved.length === 0) {
                    return;
                }
                const names = unsaved.map((tab) => tab.path ? nameOf(tab.path) : '새 탭').join(', ');
                if (!await ask(`저장하지 않은 변경이 있습니다 (${names}). 정말 종료할까요?`, { title: 'templide', kind: 'warning', okLabel: '종료', cancelLabel: '취소' })) {
                    event.preventDefault();
                }
            });
            // 탐색기에서 연 파일은 이 창(먼저 뜬 프로세스)으로 온다
            await listen('open-files', async () => {
                for (const file of await invoke<string[]>('open_files')) {
                    open(file);
                }
            });
            const files = await invoke<string[]>('open_files');
            if (files.length === 0) {
                const tab = createTab(null);
                update([tab], tab.id);
            }
            for (const file of files) {
                open(file);
            }
            setReady(true);
        })().catch((error) => console.error('start', error));
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    return (
        <div className={'shell' + (isMac ? ' mac' : '')}>
            {/* 창 제목 줄을 겸한다. 빈 곳을 끌면 창이 움직이고, 두 번 누르면 최대화한다 */}
            <header className="tabbar" data-tauri-drag-region>
                <span className="brand" data-tauri-drag-region><AppMenu /></span>
                <div className={'tabs' + (drag ? ' sorting' : '')} ref={tabsBox} data-tauri-drag-region>
                    {tabs.map((tab, i) => {
                        const Icon = tab.kind === 'asset' ? Package : FileText;
                        const place = !drag ? '' : drag.over === i ? ' drop-before' : drag.over === tabs.length && i === tabs.length - 1 ? ' drop-after' : '';
                        return (
                            <div key={tab.id} className={'tab' + (tab.id === activeId ? ' active' : '') + (drag?.id === tab.id ? ' dragging' : '') + place} title={tab.path ?? undefined}
                                onPointerDown={(event) => pointerDown(event, tab.id)}
                                onAuxClick={(event) => event.button === 1 && close(tab.id)}>
                                <Icon size={14} className="tab-icon" />
                                <span className="tab-name">{tab.path ? nameOf(tab.path) : '새 탭'}</span>
                                {tab.dirty && <span className="dirty" title="저장하지 않은 변경" />}
                                <button className="tab-close" title="닫기" onPointerDown={(event) => event.stopPropagation()} onClick={() => close(tab.id)}>
                                    <X size={13} />
                                </button>
                            </div>
                        );
                    })}
                </div>
                <span className="spacer" data-tauri-drag-region />
                {!isMac && <WindowControls />}
            </header>
            <div className="shell-body">
                {ready && tabs.map((tab) => {
                    const own = callbacksFor(tab.id);
                    return (
                        <div key={tab.id} className={'tab-page' + (tab.id === activeId ? ' active' : '')}>
                            {tab.kind === 'tlide'
                                ? <TabApp tab={tab.id} file={tab.file} active={tab.id === activeId} accesses={accesses.current}
                                    register={own.register} onInfo={own.onInfo} onOpen={open} />
                                : <TabAssets path={tab.file!} active={tab.id === activeId} documents={documents} onReveal={reveal} onChanged={bundleChanged} onPick={pick} />}
                        </div>
                    );
                })}
            </div>
        </div>
    );
}

// templide --serve와 주고받는 JSON-RPC. 메시지는 Tauri(src-tauri)가 컴파일러의 stdin/stdout으로 옮긴다
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';

type Message = { id?: number; method?: string; params?: any; result?: any; error?: { code: number; message: string } };

export class Lsp {
    private nextId = 1;
    private pending = new Map<number, { resolve: (value: any) => void; reject: (error: Error) => void }>();
    private handlers = new Map<string, Set<(params: any) => void>>();

    async start() {
        await listen<string>('lsp', (event) => this.receive(JSON.parse(event.payload)));
    }

    // 컴파일러를 다시 띄웠을 때: 이전 컴파일러에 보낸 요청은 답을 받을 수 없으므로 모두 실패시킨다
    reset() {
        const pending = [...this.pending.values()];
        this.pending.clear();
        for (const { reject } of pending) {
            reject(new Error('The compiler was restarted'));
        }
    }

    // 탭마다 받으므로 여럿 둘 수 있다. 돌려준 함수를 부르면 그만 받는다
    onNotification(method: string, handler: (params: any) => void): () => void {
        const set = this.handlers.get(method) ?? new Set();
        set.add(handler);
        this.handlers.set(method, set);
        return () => set.delete(handler);
    }

    request<T = any>(method: string, params: unknown): Promise<T> {
        const id = this.nextId++;
        return new Promise<T>((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.send({ jsonrpc: '2.0', id, method, params });
        });
    }

    notify(method: string, params: unknown) {
        this.send({ jsonrpc: '2.0', method, params });
    }

    private send(message: object) {
        invoke('lsp_send', { body: JSON.stringify(message) }).catch((error) => console.error('lsp_send', error));
    }

    private receive(message: Message) {
        if (message.id !== undefined && this.pending.has(message.id)) {
            const { resolve, reject } = this.pending.get(message.id)!;
            this.pending.delete(message.id);
            if (message.error) {
                reject(new Error(message.error.message));
            } else {
                resolve(message.result);
            }
        } else if (message.method) {
            this.handlers.get(message.method)?.forEach((handler) => handler(message.params));
        }
    }
}

// 모든 탭이 함께 쓰는 컴파일러 연결. 창(Shell)이 한 번 시작한다
export const lsp = new Lsp();

// Windows 경로 C:\a b\x.tlide -> file:///C:/a%20b/x.tlide
export function pathToUri(path: string): string {
    const slashed = path.replace(/\\/g, '/');
    return 'file://' + (slashed.startsWith('/') ? '' : '/') + slashed.split('/').map((part) => encodeURIComponent(part).replace(/%3A/gi, ':')).join('/');
}

// file:///C:/a%20b/x.tlide -> C:\a b\x.tlide
export function uriToPath(uri: string): string {
    const path = decodeURIComponent(uri.replace(/^file:\/\//, ''));
    return (/^\/[A-Za-z]:/.test(path) ? path.slice(1) : path).replace(/\//g, '\\');
}

export type Range = { start: { line: number; character: number }; end: { line: number; character: number } };

// 값의 출처. server/server.h 참고. statement면 'name = 값;'을 지워 기본값으로 되돌릴 수 있다
export type Origin = {
    kind: 'literal' | 'block' | 'locked'; uri?: string; range?: Range; unit?: string; name?: string; reason?: string;
    integer?: boolean; text?: boolean; styled?: boolean; statement?: boolean;
};

// 그라데이션, 무늬, 그림, 링크, 실행 설정 값
export type LinkValue = { url?: string; slide?: number; jump?: string };
export type ActionValue = { kind: string; target: string; arguments: (string | number | boolean)[]; link?: LinkValue };
export type PaintValue = string
    | { gradient: { radial: boolean; angle: number; colors: string[]; positions: (number | null)[] } }
    | { pattern: { kind: string; foreground: string; background: string } }
    | { image: string };
export type Value = number | string | boolean | PaintValue | { link: LinkValue } | { action: ActionValue };

// 표의 칸 하나. 병합과 서식을 풀어 낸 지금 값이다. border는 왼쪽, 위, 오른쪽, 아래 순서이고 선이 없으면 null
export type TableCellInfo = {
    text?: string;
    runs?: Origin[][];
    origin?: Origin;
    covered: boolean;
    rowSpan: number;
    columnSpan: number;
    header: boolean;
    fill?: PaintValue;
    anchor: string;
    align: string;
    padding: number[];
    border: ({ color: string; width: number; dash: string } | null)[];
};

// 표 편집에 필요한 것 (server의 table_info). 행과 열 번호는 0부터이고, editable이 false면 칸 목록을 고칠 수 없다
export type TableInfo = {
    rows?: number;
    columns?: number;
    editable?: boolean;
    cells?: TableCellInfo[][];
    cellStyles?: { row: number; column: number; source: Origin; properties: Record<string, Origin>; values: Record<string, Value> }[];
};

export type ElementInfo = {
    object: string;
    name?: string; // put ... as 이름
    source: Origin;
    fromTemplate: boolean;
    properties: Record<string, Origin>;
    table?: TableInfo;
    text?: Origin[][];
    instance?: Record<string, Origin>;
    instanceValues?: Record<string, number>; // template을 넣은 put의 x, y, width, height (px)
    values: Record<string, Value>; // 지금 값. 길이는 px, 시간은 ms, 색은 #RRGGBB(AA)
};

// 재생 차례대로인 애니메이션 하나. 시간은 ms
export type AnimationInfo = {
    target: string;
    elementId: string;
    category: string;
    effect: string;
    option: string;
    path?: string;
    duration?: number;
    start: 'on_click' | 'with_previous' | 'after_previous';
    delay?: number;
    order?: number;
    source: Origin;
    inside: boolean;   // put, group 블록 안에 적었다
    implicit: boolean; // video, audio의 start가 만든 재생
};

type ReviewInfo = { text: string; author: string; x: number; y: number; source: Origin };

export type SlideInfo = {
    page: number;
    source: Origin;
    animations: AnimationInfo[];
    properties: Record<string, Origin>; // slide 블록에 적은 속성(background, hidden, layout 등)
    values: Record<string, Value>;
    transition?: { kind: string; option: string; duration?: number; source: Origin };
    notes: string;
    noteSources: Origin[];
    reviews: ReviewInfo[];
    section?: string;
};

// 고른 target의 문서 속성
type DocumentInfo = { title?: string; author?: string; loop?: boolean; type?: string; path?: string; source?: Origin; properties?: Record<string, Origin> };

// 편집 화면으로 열 정의. template, 또는 master의 case 하나
export type DefinitionRef = { kind: 'template' | 'master'; name: string; case?: string };

// template, master의 var(매개변수) 하나. 기본값이 없으면 value는 그 타입의 기본값(미리 보기에 쓴 값)이다
export type DefinitionVar = {
    name: string;
    type: string;
    hasDefault: boolean;
    defaultText: string;
    value?: Value | unknown[];
    origin?: Origin; // 기본값을 적은 곳
    declaration: Range;
};

// 정의를 편집할 때 deck에 붙어 오는 정보. 덱은 그 정의 하나를 슬라이드 한 장으로 펼친 것이다
export type DefinitionInfo = {
    kind: 'template' | 'master';
    name: string;
    case: string;
    cases: string[];
    package: boolean;     // 표준 라이브러리에 있어 고칠 수 없다
    vars: DefinitionVar[];
    diagnostics: { message: string }[]; // 펼치다 난 오류
    uri?: string;
    range?: Range;
};

export type DefinitionEntry = { kind: 'template' | 'master'; name: string; cases: string[]; package: boolean; uri: string; range: Range };

export type DeckResult = {
    error?: string;
    definition?: DefinitionInfo;
    deck: any;
    targets: { name: string; type: string }[];
    target: string | null;
    width: number;
    height: number;
    warnings: PreviewWarning[];      // html 미리보기가 그릴 수 없는 것
    targetWarnings: TargetWarning[]; // 고른 target이 빼고 만드는 것 (pptx의 run(...) 등)
    script: string | null;           // 슬라이드 쇼가 run(...)에 쓰는 script 파일의 경로
    slides: SlideInfo[];
    elements: Record<string, ElementInfo>;
    document: DocumentInfo;
};

export type SchemaVar = { name: string; type: string; default: string; required: boolean };

// templide/schema. 마지막으로 분석에 성공한 문서의 이름들
export type Schema = {
    definitions: DefinitionEntry[]; // template과 master (document 안의 위치와 함께)
    // asset 문들. bundle은 묶음 파일의 경로, written은 asset 문에 적은 경로다
    assets: { bundle: string; written: string; default: boolean; hasBy: boolean; namespaces: string[]; aliases: Record<string, string>; entries: string[] }[];
    constants: Record<string, string>; // image, video, audio로 이름 붙인 값 -> 그 타입
    styles: Record<string, string[]>;  // style 이름 -> 매개변수 타입
    objects: Record<string, SchemaVar[]>;
    templates: Record<string, SchemaVar[]>;
    enums: Record<string, string[]>;
    masters: Record<string, { parameters: SchemaVar[]; cases: string[] }>;
    themes: string[];
    commonProperties: SchemaVar[];
    textProperties: SchemaVar[];
    styleProperties: SchemaVar[];
    slideProperties: SchemaVar[];
    cellProperties: SchemaVar[];       // put table 안 cell(행, 열) 블록의 속성
    files: { path: string; package: boolean }[]; // 분석기가 읽은 파일(문서, #include한 파일). package는 표준 라이브러리
    transitions: Record<string, string[]>;
    animations: Record<string, string[]>; // "종류.효과" -> 옵션 (첫 옵션이 기본값)
    endlessAnimations: string[];
    themeColors: string[];
};

// code는 컴파일러 진단 코드(E2029, W2001, A0001 ...)다. 없는 진단도 있다
export type Diagnostic = { range: Range; message: string; severity: number; code?: string };

// templide/accessibility의 결과 하나
export type AccessFinding = { uri: string; range: Range; severity: number; level: 'error' | 'warning' | 'info'; code: string; name?: string; message: string };

// templide/assets: 문서 폴더의 그림, 비디오, 오디오와 .tasset 묶음 안의 파일. used는 지금 문서가 쓰는지
export type AssetKind = 'image' | 'video' | 'audio';
export type AssetListing = {
    folder: string;
    files: { path: string; kind: AssetKind; size: number; used: boolean }[];
    bundles: { path: string; entries: { name: string; kind: AssetKind; size: number; used: boolean }[]; error: string }[];
    truncated: boolean;
};

// templide/format. formatted면 text가 새 내용이고, noSettings면 #fmt가 없어 포맷하지 않은 것이다 (defaults는 넣을 기본 설정 줄들)
export type FormatReply = {
    status: 'formatted' | 'unchanged' | 'noSettings' | 'error';
    text?: string;
    defaults?: string;
    message?: string;
    diagnostics?: { level: string; code: string; message: string }[];
};

// templide/fmtOptions. options는 #fmt로 정할 수 있는 모든 옵션이고, directives는 이 문서에 적힌 #fmt 줄이다 (줄과 열은 1부터)
export type FmtOption = {
    name: string;
    kind: 'integer' | 'boolean' | 'choice' | 'keepOrInteger';
    choices: string[]; // choice의 선택지 (boolean은 true, false)
    min: number;
    max: number;
    default: string;
    description: string;
};
export type FmtDirective = { name: string; value: string; line: number; column: number; valueLine: number; valueColumn: number; valueLength: number };
export type FmtOptionsReply = { options: FmtOption[]; directives: FmtDirective[]; defaults: string; error?: string };

export type TargetWarning = Diagnostic & { uri: string };

// 원문의 위치를 알면 uri와 range가 있다 (슬라이드 전환 등)
export type PreviewWarning = { message: string; uri?: string; range?: Range };

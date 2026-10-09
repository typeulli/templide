// 화면에 보이는 개체, 속성, 효과 이름. 이름 표는 언어마다 있고(labels.ko.ts, labels.en.ts) 여기서 지금 언어의 표를 내보낸다.
// 쓰는 쪽은 objectNames[이름]처럼 그대로 읽는다. 읽을 때마다 지금 언어의 표를 보므로, 언어를 바꾸면 다시 그릴 때 바뀐다
import { getLanguage } from './i18n';
import * as ko from './labels.ko';
import * as en from './labels.en';

const locales = { ko, en };
type Table = Record<string, string>;
type TableName = { [K in keyof typeof ko]: (typeof ko)[K] extends Table ? K : never }[keyof typeof ko];

function table(name: TableName): Table {
    const pick = () => locales[getLanguage()][name];
    return new Proxy({}, {
        get: (_, key) => typeof key === 'string' ? pick()[key] : undefined,
        has: (_, key) => typeof key === 'string' && key in pick(),
        ownKeys: () => Reflect.ownKeys(pick()),
        getOwnPropertyDescriptor: (_, key) => typeof key === 'string' && key in pick()
            ? { value: pick()[key], enumerable: true, configurable: true, writable: true }
            : undefined,
    });
}

export const objectNames = table('objectNames');
export const propertyNames = table('propertyNames');
export const enumNames = table('enumNames');
export const shapeKindNames = table('shapeKindNames');
export const effectNames = table('effectNames');
export const optionNames = table('optionNames');
export const transitionNames = table('transitionNames');
export const startNames = table('startNames');

// 애니메이션 종류 [id, 이름]
export const animationCategories = (): [string, string][] => locales[getLanguage()].animationCategories;

// PowerPoint의 전환 갤러리 순서 (은은한 효과, 화려한 효과, 동적 콘텐츠)
export const transitionOrder = ['morph', 'fade', 'push', 'wipe', 'split', 'reveal', 'cut', 'randomBars', 'circle', 'diamond', 'plus', 'uncover', 'cover',
    'flashbulb', 'fallOver', 'drape', 'curtains', 'wind', 'prestige', 'fracture', 'crush', 'peelOff', 'pageCurlSingle', 'pageCurlDouble', 'airplane', 'origami',
    'dissolve', 'checkerboard', 'blinds', 'wheel', 'wheelReverse', 'wedge', 'ripple', 'honeycomb', 'glitter', 'vortex', 'shred', 'switch', 'flip', 'gallery',
    'cube', 'doors', 'box', 'comb', 'warp', 'newsflash', 'strips', 'random', 'pan', 'ferrisWheel', 'conveyor', 'rotate', 'window', 'orbit', 'flyThrough'];

// html, web에서 쓸 수 없는 전환 (pptx에서만 된다)
export const pptxOnlyTransitions = new Set(['vortex', 'ripple', 'glitter', 'honeycomb', 'shred', 'warp', 'drape', 'curtains', 'wind', 'prestige',
    'fracture', 'crush', 'peelOff', 'pageCurlSingle', 'pageCurlDouble', 'airplane', 'origami', 'morph', 'fallOver', 'randomBars']);

// 이름이 없으면 camelCase, snake_case를 띄어 쓴다
export function label(table: Record<string, string>, name: string): string {
    return table[name] ?? name.replace(/_/g, ' ').replace(/([a-z])([A-Z0-9])/g, '$1 $2');
}

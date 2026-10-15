// 텍스트 파일의 인코딩 목록과 이름. 이름은 웹 표준(encoding_rs)의 이름이고, 읽고 쓰는 일은 Rust(src-tauri/src/encoding.rs)가 한다.
// 컴파일러가 직접 읽는 것은 UTF-8과 UTF-16뿐이다 (middleend/source_text.h). 나머지는 편집기에서만 열고 저장할 수 있다
import { t } from './i18n';

// 이름 -> 어느 글을 쓰는 인코딩인지 (선택 창에서 보인다). 글은 언어에 따라 바뀌므로 함수다
const hints: Record<string, () => string> = {
    'UTF-8': () => t('유니코드'),
    'UTF-16LE': () => t('유니코드'),
    'UTF-16BE': () => t('유니코드'),
    'EUC-KR': () => t('한국어'),
    'Shift_JIS': () => t('일본어'),
    'EUC-JP': () => t('일본어'),
    'GBK': () => t('중국어 간체'),
    'gb18030': () => t('중국어 간체'),
    'Big5': () => t('중국어 번체'),
    'windows-1252': () => t('서유럽'),
    'ISO-8859-15': () => t('서유럽 (유로)'),
    'windows-1250': () => t('중부 유럽'),
    'ISO-8859-2': () => t('중부 유럽'),
    'ISO-8859-3': () => t('남유럽'),
    'ISO-8859-4': () => t('발트어'),
    'windows-1257': () => t('발트어'),
    'ISO-8859-13': () => t('발트어'),
    'ISO-8859-10': () => t('북유럽'),
    'ISO-8859-14': () => t('켈트어'),
    'ISO-8859-16': () => t('남동 유럽'),
    'windows-1251': () => t('키릴 문자'),
    'ISO-8859-5': () => t('키릴 문자'),
    'KOI8-R': () => t('키릴 문자 (러시아어)'),
    'KOI8-U': () => t('키릴 문자 (우크라이나어)'),
    'IBM866': () => t('키릴 문자 (DOS)'),
    'x-mac-cyrillic': () => t('키릴 문자 (맥)'),
    'windows-1253': () => t('그리스어'),
    'ISO-8859-7': () => t('그리스어'),
    'windows-1254': () => t('터키어'),
    'windows-1255': () => t('히브리어'),
    'ISO-8859-8': () => t('히브리어'),
    'ISO-8859-8-I': () => t('히브리어 (논리 순서)'),
    'windows-1256': () => t('아랍어'),
    'ISO-8859-6': () => t('아랍어'),
    'windows-1258': () => t('베트남어'),
    'windows-874': () => t('태국어'),
    'macintosh': () => t('맥 로마 문자'),
};

// 선택 창의 차례. 자주 쓰는 것을 앞에 둔다
export const encodingNames = Object.keys(hints);

export const isEncodingName = (name: unknown): name is string => typeof name === 'string' && name in hints;

export const encodingHint = (name: string) => hints[name]?.() ?? '';

// 상태 표시줄과 선택 창에 보일 이름. UTF-8만 BOM이 있는지로 나뉜다 (UTF-16은 항상 BOM을 쓴다)
export function encodingLabel(name: string, bom = false): string {
    switch (name) {
        case 'UTF-8': return bom ? 'UTF-8 with BOM' : 'UTF-8';
        case 'UTF-16LE': return 'UTF-16 LE';
        case 'UTF-16BE': return 'UTF-16 BE';
        default: return name;
    }
}

// 컴파일러가 이 인코딩의 파일을 직접 읽을 수 있나 (include, 명령줄). 아니면 편집기에서만 열린다
export const compilerReads = (name: string) => name === 'UTF-8' || name === 'UTF-16LE' || name === 'UTF-16BE';

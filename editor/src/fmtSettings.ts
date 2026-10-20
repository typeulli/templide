// 설정 창의 '포맷' 페이지. 열려 있는 문서에 적힌 #fmt 줄을 읽고 고치는 계산과 옵션의 이름표를 모았다.
// 옵션의 이름, 종류, 값의 범위와 기본값은 컴파일러(templide/fmtOptions)가 알려 준다. 여기에는 화면에 보일 한국어 이름표만 둔다
// (이름표가 없는 새 옵션은 컴파일러의 영어 설명으로 보인다). 설정 창과 편집기 창은 별도의 창이라 FmtState와 FmtChange를 이벤트로 주고받는다
import type { FmtDirective, FmtOption, FmtOptionsReply } from './lsp';

// 편집기 창이 설정 창에 보내는 것
export type FmtState = {
    document: string | null;          // 열린 .tlide 문서의 파일 이름. 없으면 null
    options: FmtOption[];
    written: Record<string, string>;  // 문서에 적힌 옵션과 값 (같은 옵션을 여러 번 적었으면 나중 것). 모르는 옵션은 뺀다
    count: number;                    // 문서의 #fmt 줄 수. 0이면 이 문서는 포맷하지 않는다
    defaults: string;                 // 모든 옵션을 기본값으로 쓴 #fmt 줄들
};

// 설정 창이 편집기 창에 부탁하는 것. value가 null이면 문서에서 그 옵션의 줄을 지워 기본값으로 돌린다
export type FmtChange = { name: string; value: string | null } | { defaults: true };

// Monaco의 편집 (줄과 열은 1부터)
export type FmtEdit = { startLine: number; startColumn: number; endLine: number; endColumn: number; text: string };

export const noFmtDocument: FmtState = { document: null, options: [], written: {}, count: 0, defaults: '' };

export function fmtState(document: string, reply: FmtOptionsReply): FmtState {
    const written: Record<string, string> = {};
    for (const directive of reply.directives) {
        if (reply.options.some((option) => option.name === directive.name)) {
            written[directive.name] = directive.value;
        }
    }
    return { document, options: reply.options, written, count: reply.directives.length, defaults: reply.defaults };
}

// 값이 옵션이 받는 값인지. 컴파일러(valid_value)와 같은 규칙이다
export function validFmtValue(option: FmtOption, value: string): boolean {
    const number = /^\d{1,6}$/.test(value) ? Number(value) : -1;
    switch (option.kind) {
        case 'integer': return number >= option.min && number <= option.max;
        case 'boolean':
        case 'choice': return option.choices.includes(value);
        case 'keepOrInteger': return value === 'keep' || (number >= option.min && number <= option.max);
    }
}

// 지금 포맷에 쓰이는 값: 문서에 맞는 값이 적혀 있으면 그 값, 아니면 기본값
export function effectiveFmtValue(state: FmtState, option: FmtOption): string {
    const value = state.written[option.name];
    return value !== undefined && validFmtValue(option, value) ? value : option.default;
}

const escapeRegExp = (text: string) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// 옵션 하나를 바꾸는 편집. lines는 문서의 줄들이다
export function fmtEdits(reply: FmtOptionsReply, change: FmtChange, lines: string[]): FmtEdit[] {
    if ('defaults' in change) {
        return reply.directives.length === 0 ? [{ startLine: 1, startColumn: 1, endLine: 1, endColumn: 1, text: reply.defaults + '\n' }] : [];
    }
    const { name, value } = change;
    const option = reply.options.find((each) => each.name === name);
    if (!option) {
        return [];
    }
    const mine = reply.directives.filter((directive) => directive.name === name);
    const replaceValue = (directive: FmtDirective, text: string): FmtEdit[] => directive.value === text ? [] : [{
        startLine: directive.valueLine, startColumn: directive.valueColumn, endLine: directive.valueLine, endColumn: directive.valueColumn + directive.valueLength, text,
    }];

    if (value !== null) {
        if (mine.length > 0) {
            return replaceValue(mine[mine.length - 1], value);
        }
        const line = `#fmt ${name} ${value}`;
        if (reply.directives.length === 0) {
            return [{ startLine: 1, startColumn: 1, endLine: 1, endColumn: 1, text: line + '\n' }];
        }
        // 마지막 #fmt 줄 바로 아래에 넣는다
        const last = Math.max(...reply.directives.map((directive) => directive.line));
        return last < lines.length
            ? [{ startLine: last + 1, startColumn: 1, endLine: last + 1, endColumn: 1, text: line + '\n' }]
            : [{ startLine: last, startColumn: lines[last - 1].length + 1, endLine: last, endColumn: lines[last - 1].length + 1, text: '\n' + line }];
    }

    // 기본값으로 되돌린다: 그 옵션의 줄을 지운다. 지우면 #fmt가 하나도 없게 되거나(포맷을 끄게 된다),
    // 줄에 다른 내용이 같이 있으면 지우지 않고 기본값을 적는다
    if (mine.length === 0) {
        return [];
    }
    const wholeLine = new RegExp(`^\\s*#fmt\\s+${escapeRegExp(name)}\\s+\\S+\\s*$`);
    const removable = mine.every((directive) => directive.line === directive.valueLine && wholeLine.test(lines[directive.line - 1] ?? ''));
    if (reply.directives.length - mine.length === 0 || !removable) {
        return replaceValue(mine[mine.length - 1], option.default);
    }
    return mine.map((directive) => directive.line < lines.length
        ? { startLine: directive.line, startColumn: 1, endLine: directive.line + 1, endColumn: 1, text: '' }
        : directive.line > 1
            ? { startLine: directive.line - 1, startColumn: lines[directive.line - 2].length + 1, endLine: directive.line, endColumn: lines[directive.line - 1].length + 1, text: '' }
            : { startLine: 1, startColumn: 1, endLine: 1, endColumn: lines[0].length + 1, text: '' });
}

// 화면에 보일 이름표. title과 hint는 한국어 원문이고 화면에 보일 때 t()로 번역한다. values는 선택지의 이름표다
export type FmtLabel = { title: string; hint?: string; values?: Record<string, string> };

export const fmtLabels: Record<string, FmtLabel> = {
    indent_style: { title: '들여쓰기 방식', hint: '공백과 탭 중 무엇으로 들여 쓸지 정합니다.', values: { space: '공백', tab: '탭' } },
    indent_size: { title: '들여쓰기 칸 수', hint: '한 단계 들여쓸 칸 수입니다. 탭으로 들여 쓸 때는 줄 길이를 재는 데만 쓰입니다.' },
    brace_style: { title: '여는 중괄호 자리', hint: '여러 줄 블록의 {를 줄 끝에 둘지 다음 줄에 둘지 정합니다.', values: { same_line: '같은 줄 끝', next_line: '다음 줄' } },
    empty_blocks: { title: '빈 블록', hint: '내용이 없는 블록을 { }로 쓸지 {}로 쓸지 정합니다.', values: { space: '{ }', tight: '{}' } },
    short_blocks: { title: '한 줄로 쓴 블록', hint: '한 줄로 쓴 짧은 블록을 그대로 둘지, 항상 여러 줄로 펼칠지 정합니다.', values: { keep: '그대로 둠', expand: '항상 펼침' } },
    inline_max_statements: { title: '한 줄 블록의 문장 수 상한', hint: '한 줄 블록에 둘 수 있는 문장 수입니다. 0이면 제한이 없습니다.' },
    inline_brace_space: { title: '한 줄 블록의 중괄호 안 공백', hint: '{ a; }처럼 중괄호 안쪽에 공백을 둡니다. 끄면 {a;}가 됩니다.' },
    max_line_length: { title: '한 줄 블록의 최대 길이', hint: '한 줄 블록이 이 길이를 넘으면 여러 줄로 펼칩니다. 0이면 제한이 없습니다.' },
    space_around_assign: { title: '= 앞뒤 공백', hint: 'x = 1처럼 = 앞뒤에 공백을 둡니다.' },
    space_around_operators: { title: '연산자 앞뒤 공백', hint: '+ - * / == < > 앞뒤에 공백을 둡니다.' },
    space_after_comma: { title: '쉼표 뒤 공백', hint: 'a, b처럼 쉼표 뒤에 공백을 둡니다.' },
    space_inside_parens: { title: '괄호 안쪽 공백', hint: '( a, b )처럼 괄호 안쪽에 공백을 둡니다.' },
    comment_gap: { title: '줄 끝 주석 앞 공백 수', hint: '코드 뒤에 같은 줄로 쓴 주석 앞의 공백 수입니다.' },
    max_blank_lines: { title: '이어지는 빈 줄의 최대 수', hint: '빈 줄이 이보다 많이 이어지면 이 수만큼으로 줄입니다.' },
    blank_lines_before_slide: { title: 'slide 앞 빈 줄', hint: '파일 맨 위에 쓴 slide 앞에 둘 빈 줄 수입니다.', values: { keep: '그대로' } },
    blank_lines_after_includes: { title: '#include 뒤 빈 줄', hint: '#include 줄들 바로 뒤에 둘 빈 줄 수입니다.', values: { keep: '그대로' } },
    sort_properties: { title: '속성 정렬', hint: 'NAME = VALUE;만 있는 블록의 속성을 이름순으로 정렬합니다.', values: { keep: '그대로 둠', alphabetical: '이름순' } },
    sort_includes: { title: '#include 정렬', hint: '이어진 #include 줄을 이름순으로 정렬합니다 (<...> 패키지가 먼저).', values: { keep: '그대로 둠', alphabetical: '이름순' } },
    hex_case: { title: 'hex 색의 글자', hint: 'hex(2e86ab)의 A~F 글자를 대문자나 소문자로 맞춥니다.', values: { keep: '그대로 둠', upper: '대문자', lower: '소문자' } },
    final_newline: { title: '파일 끝 줄바꿈', hint: '파일을 줄바꿈으로 끝냅니다.' },
    line_ending: { title: '줄 끝 모양', hint: '그대로 두면 파일에서 가장 많이 쓰인 모양을 따릅니다.', values: { keep: '그대로 둠', lf: 'LF', crlf: 'CRLF' } },
};

// 페이지에서 옵션을 묶어 보이는 순서. 여기에 없는 옵션은 마지막의 '기타'에 보인다
export const fmtGroups: { title: string; names: string[] }[] = [
    { title: '들여쓰기와 중괄호', names: ['indent_style', 'indent_size', 'brace_style', 'empty_blocks'] },
    { title: '한 줄 블록', names: ['short_blocks', 'inline_max_statements', 'inline_brace_space', 'max_line_length'] },
    { title: '띄어쓰기', names: ['space_around_assign', 'space_around_operators', 'space_after_comma', 'space_inside_parens', 'comment_gap'] },
    { title: '빈 줄', names: ['max_blank_lines', 'blank_lines_before_slide', 'blank_lines_after_includes'] },
    { title: '정리', names: ['sort_properties', 'sort_includes', 'hex_case'] },
    { title: '파일', names: ['final_newline', 'line_ending'] },
];

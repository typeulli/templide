// Monaco는 무거우므로 첫 슬라이드를 그린 뒤에 이 모듈을 불러온다 (import('./monaco'))
import * as monaco from 'monaco-editor/editor/editor.api';
import 'monaco-editor/features/register.all';
import EditorWorker from 'monaco-editor/editor/editor.worker.start?worker';

self.MonacoEnvironment = { getWorker: () => new EditorWorker() };

const keywords = [
    'slide', 'put', 'template', 'style', 'object', 'var', 'master', 'case', 'target', 'if', 'else', 'for', 'in', 'enum', 'transition',
    'animate', 'group', 'as', 'theme', 'section', 'review', 'comment', 'bullets', 'numbers', 'dashes', 'paragraphs', 'true', 'false',
    'asset', 'by', 'default',
];
const types = ['int', 'float', 'string', 'text', 'color', 'bool', 'ref'];

monaco.languages.register({ id: 'tlide', extensions: ['.tlide'] });
monaco.languages.setLanguageConfiguration('tlide', {
    comments: { blockComment: ['/*', '*/'] },
    brackets: [['{', '}'], ['[', ']'], ['(', ')']],
    autoClosingPairs: [{ open: '{', close: '}' }, { open: '[', close: ']' }, { open: '(', close: ')' }, { open: '"', close: '"', notIn: ['string'] }],
});
monaco.languages.setMonarchTokensProvider('tlide', {
    keywords,
    types,
    tokenizer: {
        root: [
            [/\/\*/, 'comment', '@comment'],
            [/#include/, 'keyword'],
            [/@[a-zA-Z_]\w*/, 'variable.predefined'],
            // image, video, audio, font는 타입 자리(image 이름 = ...;, var font 이름;, (image 이름))에서만 타입이다.
            // put image { }의 개체 이름, image("a.png"), font("Arial")의 함수 이름은 그대로 둔다
            [/(var)(\s+)(image|video|audio|font)\b/, ['keyword', '', 'type']],
            [/(image|video|audio|font)(?=\s+[a-zA-Z_]\w*\s*[=,;)])/, 'type'],
            // hex(73B99D)의 16진수는 글자가 섞여도 한 값이다
            [/(hex)(\s*\(\s*)([0-9a-fA-F]+)/, ['function', '', 'number']],
            // 함수 이름 (rgb, oklch, font, file, linear 등). if (...) 같은 키워드는 그대로 둔다
            [/[a-zA-Z_]\w*(?=\s*\()/, { cases: { '@keywords': 'keyword', '@default': 'function' } }],
            [/[a-zA-Z_][\w]*(-[a-zA-Z_]\w*)*/, { cases: { '@keywords': 'keyword', '@types': 'type', '@default': 'identifier' } }],
            [/-?\d+(\.\d+)?(px|pt|%|ms|s|deg|in|cm|mm)?/, 'number'],
            [/f?"/, 'string', '@string'],
        ],
        comment: [[/[^*]+/, 'comment'], [/\*\//, 'comment', '@pop'], [/\*/, 'comment']],
        string: [[/[^\\"{]+/, 'string'], [/\\./, 'string.escape'], [/\{/, 'string'], [/"/, 'string', '@pop']],
    },
});

// 기본 밝은 테마에 함수 이름의 색(VS Code 밝은 테마와 같은 갈색)을 더한다
monaco.editor.defineTheme('templide', {
    base: 'vs',
    inherit: true,
    rules: [{ token: 'function', foreground: '795E26' }],
    colors: {},
});

export { monaco };

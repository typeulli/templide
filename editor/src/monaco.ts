// Monaco는 무거우므로 첫 슬라이드를 그린 뒤에 이 모듈을 불러온다 (import('./monaco'))
import * as monaco from 'monaco-editor/editor/editor.api';
import 'monaco-editor/features/register.all';
import EditorWorker from 'monaco-editor/editor/editor.worker.start?worker';

self.MonacoEnvironment = { getWorker: () => new EditorWorker() };

const keywords = [
    'slide', 'put', 'template', 'style', 'object', 'var', 'master', 'case', 'target', 'if', 'else', 'for', 'in', 'enum', 'transition',
    'animate', 'group', 'as', 'theme', 'section', 'review', 'comment', 'bullets', 'numbers', 'dashes', 'paragraphs', 'true', 'false',
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
            [/[a-zA-Z_][\w]*(-[a-zA-Z_]\w*)*/, { cases: { '@keywords': 'keyword', '@types': 'type', '@default': 'identifier' } }],
            [/-?\d+(\.\d+)?(px|pt|%|ms|s|deg|in|cm|mm)?/, 'number'],
            [/f?"/, 'string', '@string'],
        ],
        comment: [[/[^*]+/, 'comment'], [/\*\//, 'comment', '@pop'], [/\*/, 'comment']],
        string: [[/[^\\"{]+/, 'string'], [/\\./, 'string.escape'], [/\{/, 'string'], [/"/, 'string', '@pop']],
    },
});

export { monaco };

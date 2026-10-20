"""templide 컴파일러의 스냅샷 테스트와 오류/경고 테스트. 표준 라이브러리만 쓴다 (Python 3.10 이상).

사용법

    python test/run.py                       # 모든 케이스
    python test/run.py text units            # 이름(kind/이름)에 text나 units가 든 케이스만 (대소문자 무시)
    python test/run.py pptx/                 # 한 종류만
    python test/run.py --update [이름 ...]   # 기대 파일을 지금 출력으로 다시 쓰고 바뀐 것을 알린다
    python test/run.py -v                    # 통과한 케이스의 출력과 잘리지 않은 diff도 보인다
    python test/run.py --list                # 케이스 목록만 보인다
    python test/run.py --exe 경로            # 컴파일러 지정

컴파일러는 --exe, 환경 변수 TEMPLIDE_EXE, cmake-build-release/templide, cmake-build-debug/templide 순서로 찾는다.
packages/와 libs/는 컴파일러 옆에 있어야 한다. 종료 코드는 0(모두 통과), 1(실패가 있다), 2(컴파일러나 케이스를 못 찾았다)다.

종류(kind)마다 test/cases/<kind>.txtar 파일 하나에 그 종류의 케이스가 모두 들어 있다. `-- 경로 --` 줄마다 파일이 하나 시작되고
다음 구분선 앞까지가 그 내용이다 (첫 구분선 앞의 글은 설명이다). 경로는 그 종류 폴더를 기준으로 한다.

    -- 이름.tlide --            시험할 .tlide. 이름이 _로 시작하지 않고 경로에 폴더가 없거나 하나뿐인 .tlide마다 케이스 하나다
    -- 이름.expected --         기대. .tlide와 이름이 같고 확장자만 다른 구분선이다 (확장자는 아래 표). .steps, .parts, .edits도 이렇게 짝을 짓는다
    -- 폴더/main.tlide --       폴더 안에 있으면 그 폴더 전체가 한 케이스다 (-- 폴더/_도우미.tlide --, 하위 폴더, 그림도 같은 폴더 아래에 둔다)

러너는 케이스의 파일을 기대만 빼고 임시 폴더에 풀어 놓고 컴파일러를 돌린다. 이름이 _로 시작하는 .tlide는 #include용 도우미라서 시험하지 않는다.
UTF-8 글이 아니거나 줄바꿈으로 끝나지 않는 내용(그림, UTF-16 케이스)은 `-- 경로 (base64) --`로 적는다.

    kind    기대        하는 일
    ir      .ir         `templide x.tlide --ir`의 stdout. 종료 코드 0이어야 한다. stderr(경고)가 있으면 맨 앞에 `# stderr:` 줄로 함께 적는다
    error   .expected   `templide x.tlide --check`의 종료 코드, stdout, stderr. 파일을 쓰면 실패다. 케이스 하나는 한 가지만 시험한다
    pptx    .xml        빌드한 .pptx 안의 XML 부분을 예쁘게 찍은 것. 기본은 ppt/slides/slide*.xml이고, 같은 이름의 .parts가 있으면
                        그 안의 glob(한 줄에 하나, # 주석)으로 바꾼다. 예: ppt/slideLayouts/*.xml, ppt/slides/_rels/*.rels
    html    .json       빌드한 html(또는 web의 index.html)에 들어 있는 덱 JSON을 예쁘게 찍은 것
    build   .expected   같은 이름의 .steps에 적은 명령줄을 차례로 실행한 기록 (종료 코드, stdout, stderr, 새로 생긴 파일). .steps가
                        없으면 `x.tlide` 하나만 실행한다. .steps는 한 줄에 명령줄 인자 하나 묶음이고, 그 밖에 이런 줄이 있다
                        `# 주석`, `@requires browser` (Chrome/Edge가 없으면 건너뛴다), `@env 이름=값` (뒤 명령의 환경 변수,
                        $DIR은 작업 폴더, 값이 비면 지운다). 단계마다 새로 생긴 파일은 목록에 적고 지우므로 단계끼리 섞이지 않는다
    edit    .expected   편집기가 보내는 편집(`templide --serve`의 templide/edit)을 같은 이름의 .edits에 적은 대로 한 줄씩 적용한다.
                        .edits는 한 줄에 JSON 편집 하나이고(# 주석), "id": "@table"은 문서의 첫 table, "@table:2"는 둘째를 가리킨다.
                        편집에 "definition": {"kind": "template", "name": "card"} (master는 "case"도)를 적으면 그 정의를 편집 화면(슬라이드 한 장)으로 열어 고친다.
                        기대 파일에는 단계마다 편집과 원문이 바뀐 줄(diff), 또는 편집이 거절된 이유가 적힌다. 편집한 뒤에도 문서는 컴파일돼야 한다
    fmt     .expected   `templide fmt --stdout x.tlide`의 종료 코드, stdout, stderr. 결과를 다시 포맷해도 같은지(멱등성)도 본다. `#fmt`가 없는
                        파일은 건너뛴다는 안내만 나온다. 문법 오류가 있으면 고치지 않고 오류를 알린다
    lsp     .expected   편집기가 보내는 LSP 요청에 서버(`templide --serve`)가 어떻게 답하는지. 문서를 열 때 나오는 진단(오류와 경고)과, .tlide 안의
                        표식이 가리키는 자리의 답을 적는다. ‸는 자동 완성(바꿀 범위와 목록), ⌖는 hover, 정의, 강조를 물을 자리다.
                        표식은 지우고 문서를 열며, 자리는 줄:칸(둘 다 1부터)으로 적는다. 표식이 없으면 진단만 적는다

catalog/consistency는 케이스 파일이 아니라 진단 카탈로그와 소스의 호출이 맞는지 보는 검사다 (test/catalog_check.py).

케이스를 더하는 법
    1. test/cases/<kind>.txtar 맨 아래에 `-- <이름>.tlide --` 구분선과 .tlide를 더한다.
       그림, #include할 파일처럼 다른 파일이 필요하면 `-- <이름>/main.tlide --`처럼 폴더를 쓰고 같은 폴더 아래에 다른 구분선을 더한다.
    2. python test/run.py --update 이름   으로 기대 구분선을 만든다. .tlide 바로 뒤에 생긴다.
    3. 만들어진 기대를 직접 읽어 언어가 해야 하는 대로인지 확인한 뒤 함께 커밋한다. 버그 같은 출력을 그대로 굳히지 않는다.

기대 고치는 법
    의도한 변경이면 python test/run.py --update [이름]을 돌리고 git diff로 바뀐 줄이 의도한 것뿐인지 본다.
    --update는 컴파일러가 비정상으로 끝났거나(ir은 종료 코드가 0이 아닐 때) 기대한 파일을 만들지 못한 케이스는 고치지 않고 실패로 둔다.

새 종류(kind) 더하는 법
    KINDS에 (기대 구분선 확장자, 실행 함수)를 한 줄 더하고 test/cases/<kind>.txtar를 만든다. 실행 함수는 Sandbox로 컴파일러를 돌려
    비교할 글을 Outcome으로 돌려준다 (run_ir, run_pptx를 보면 된다).

test/.gitattributes는 cases/의 줄바꿈 변환을 막는다 (CRLF, BOM 케이스와 기대 구분선이 바이트 그대로 있어야 한다).
각 케이스는 임시 폴더에 풀어 그 안에서 컴파일러를 실행하므로 저장소에는 아무것도 생기지 않는다.
출력의 임시 폴더 경로는 $DIR, 컴파일러 폴더 경로는 $EXE_DIR로, 날짜와 시각은 $DATE, $TIMESTAMP로 바꾸고
(ir은 이름 없는 개체에 붙는 원문 위치 숫자도 $ID로 바꾸어) Windows, Linux, macOS에서 같은 기대 파일을 쓴다.
"""
from __future__ import annotations

import argparse
import base64
import difflib
import fnmatch
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unicodedata
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable
from xml.parsers import expat
from xml.sax.saxutils import escape as xml_escape, quoteattr

import catalog_check

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CASES = HERE / 'cases'
TIMEOUT = 60
PDF_TIMEOUT = 180


# ---------------------------------------------------------------- 컴파일러와 브라우저 찾기

def find_exe(chosen: str | None) -> Path:
    """--exe, TEMPLIDE_EXE, 빌드 폴더 순서로 컴파일러를 찾는다. 없으면 SystemExit."""
    suffix = '.exe' if os.name == 'nt' else ''
    if chosen:
        candidates = [Path(chosen)]
    elif os.environ.get('TEMPLIDE_EXE'):
        candidates = [Path(os.environ['TEMPLIDE_EXE'])]
    else:
        candidates = [ROOT / folder / f'templide{suffix}' for folder in ('cmake-build-release', 'cmake-build-debug')]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    where = ', '.join(str(candidate) for candidate in candidates)
    print(f'error: 컴파일러를 찾지 못했다 ({where}). --exe 경로나 환경 변수 TEMPLIDE_EXE로 알려 주거나 먼저 빌드한다', file=sys.stderr)
    raise SystemExit(2)


def find_browser() -> Path | None:
    """PDF 출력에 쓰는 브라우저. 컴파일러(backend/pdf.cpp)가 찾는 순서와 같다."""
    chosen = os.environ.get('TEMPLIDE_BROWSER')
    if chosen:
        return Path(chosen) if Path(chosen).is_file() else None
    candidates: list[Path] = []
    if sys.platform == 'win32':
        for relative in ('Microsoft/Edge/Application/msedge.exe', 'Google/Chrome/Application/chrome.exe'):
            for variable in ('ProgramFiles(x86)', 'ProgramFiles', 'LocalAppData'):
                base = os.environ.get(variable)
                if base:
                    candidates.append(Path(base) / relative)
    elif sys.platform == 'darwin':
        candidates = [Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
                      Path('/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge'),
                      Path('/Applications/Chromium.app/Contents/MacOS/Chromium')]
    else:
        for name in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser', 'microsoft-edge', 'microsoft-edge-stable'):
            found = shutil.which(name)
            if found:
                candidates.append(Path(found))
    return next((candidate for candidate in candidates if candidate.is_file()), None)


# ---------------------------------------------------------------- 출력 정리

def path_spellings(path: Path) -> list[str]:
    """같은 폴더를 가리키는 여러 표기 (별칭 해석 전후, 구분자 두 가지). 긴 것부터."""
    found = {str(path), str(path.resolve()), os.path.realpath(path)}
    found |= {text.replace('\\', '/') for text in found} | {text.replace('/', '\\') for text in found}
    return sorted((text for text in found if text), key=len, reverse=True)


class Normalizer:
    """컴파일러 출력에서 실행마다, 운영체제마다 달라지는 부분을 지운다."""

    def __init__(self, work: Path, exe_dir: Path):
        flags = re.IGNORECASE if os.name == 'nt' else 0
        # 한 폴더가 다른 폴더 안에 있어도 긴 경로부터 바꾼다
        roots = sorted(((work, '$DIR'), (exe_dir, '$EXE_DIR')), key=lambda root: len(str(root[0].resolve())), reverse=True)
        self.roots = [(re.compile('|'.join(re.escape(text) for text in path_spellings(path)), flags), token) for path, token in roots]
        self.tail = re.compile(r'\$(?:DIR|EXE_DIR)(?:[\\/][^\\/\s"\'<>|*?:()]+)*')
        self.source_path = re.compile(r'[^\s"\'<>|*?:()]+\.tlide')

    def __call__(self, text: str) -> str:
        text = unicodedata.normalize('NFC', text.replace('\r\n', '\n'))
        for pattern, token in self.roots:
            text = pattern.sub(lambda _: token, text)
        # 경로 뒤쪽의 구분자는 /로 맞춘다 (글 안의 \는 건드리지 않는다). 하위 폴더의 .tlide 이름도 같다
        text = self.tail.sub(lambda match: match.group(0).replace('\\', '/'), text)
        text = self.source_path.sub(lambda match: match.group(0).replace('\\', '/'), text)
        text = re.sub(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z?', '$TIMESTAMP', text)
        # @date 필드: IR의 Field datetime1 "날짜", pptx의 <a:fld type="datetime1">...<a:t>날짜</a:t>
        text = re.sub(r'(Field datetime1 ")\d{4}-\d{2}-\d{2}(")', r'\1$DATE\2', text)
        text = re.sub(r'(type="datetime1">(?:(?!</a:fld>).)*?<a:t>)\d{4}-\d{2}-\d{2}(</a:t>)', r'\1$DATE\2', text, flags=re.DOTALL)
        return text


def pretty_xml(data: bytes) -> str:
    """XML을 한 요소 한 줄로 들여써서 찍는다. 속성 순서와 접두사(a:, p:)는 원문 그대로 둔다."""
    root: list = []                  # 최상위 요소들. 요소는 [이름, [(속성, 값)...], 자식들(요소나 글)]
    stack: list[list] = [root]       # 지금 열려 있는 요소들의 자식 목록

    def start(name, attributes):
        node = [name, list(zip(attributes[::2], attributes[1::2])), []]
        stack[-1].append(node)
        stack.append(node[2])

    def end(_):
        stack.pop()

    def text(content):
        # 요소 안의 글은 문자열로 남긴다 (<a:t> 등)
        if stack[-1] is not root:
            stack[-1].append(content)

    parser = expat.ParserCreate()
    parser.ordered_attributes = True
    parser.buffer_text = True
    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = text
    parser.Parse(data, True)

    def flat(node) -> str:
        """요소 전체를 한 줄로."""
        name, attributes, children = node
        head = '<' + name + ''.join(f' {key}={quoteattr(value)}' for key, value in attributes)
        body = ''.join(flat(child) if isinstance(child, list) else xml_escape(child) for child in children
                       if isinstance(child, list) or child.strip() or len(children) == 1)
        return head + '/>' if not body else f'{head}>{body}</{name}>'

    def render(node, depth: int) -> list[str]:
        name, attributes, children = node
        pad = '  ' * depth
        # 한 줄에 들어가는 요소는 한 줄로 찍는다
        line = pad + flat(node)
        if len(line) <= 100 and '\n' not in line:
            return [line]
        rendered = [f'{key}={quoteattr(value)}' for key, value in attributes]
        head = pad + '<' + name + ''.join(' ' + item for item in rendered)
        if len(head) > 110 and len(rendered) > 1:
            head = pad + '<' + name + ''.join('\n' + pad + '    ' + item for item in rendered)
        elements = [child for child in children if isinstance(child, list)]
        texts = [child for child in children if isinstance(child, str)]
        if not elements and not texts:
            return [head + '/>']
        if not elements:
            return [head + '>' + xml_escape(''.join(texts)) + '</' + name + '>']
        lines = [head + '>']
        for child in children:
            if isinstance(child, list):
                lines += render(child, depth + 1)
            elif child.strip():
                lines.append(pad + '  ' + xml_escape(child.strip()))
        return lines + [pad + '</' + name + '>']

    return '\n'.join(line for node in root for line in render(node, 0)) + '\n'


def pretty_json(value, indent: int = 0) -> str:
    """JSON을 찍는다. 한 줄에 들어가는 작은 값은 한 줄로, 큰 값만 펼친다."""
    flat = json.dumps(value, ensure_ascii=False, separators=(', ', ': '))
    pad = '  ' * indent
    if not isinstance(value, (dict, list)) or len(pad) + len(flat) <= 100 or not value:
        return flat
    inner = pad + '  '
    if isinstance(value, list):
        return '[\n' + ',\n'.join(inner + pretty_json(item, indent + 1) for item in value) + '\n' + pad + ']'
    return '{\n' + ',\n'.join(f'{inner}{json.dumps(key, ensure_ascii=False)}: {pretty_json(item, indent + 1)}'
                              for key, item in value.items()) + '\n' + pad + '}'


# ---------------------------------------------------------------- 실행 환경

@dataclass
class Proc:
    code: int
    out: str
    err: str


class Sandbox:
    """케이스 하나를 돌릴 임시 폴더. 케이스의 파일을 하나씩 만들어 둔다 (기대 파일은 빼고)."""

    def __init__(self, case: 'Case', exe: Path):
        self.case = case
        self.exe = exe
        self.work = Path(tempfile.mkdtemp(prefix='templide-test-'))
        for name, data in case.files.items():
            if name != case.expected_name:
                (self.work / name).parent.mkdir(parents=True, exist_ok=True)
                (self.work / name).write_bytes(data)
        self.source = case.main
        self.normalize = Normalizer(self.work, exe.parent)
        self.baseline = self.tree()
        self.baseline_dirs = self.folders()

    def close(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def tree(self) -> set[str]:
        return {unicodedata.normalize('NFC', path.relative_to(self.work).as_posix()) for path in self.work.rglob('*') if path.is_file()}

    def folders(self) -> set[str]:
        return {unicodedata.normalize('NFC', path.relative_to(self.work).as_posix()) for path in self.work.rglob('*') if path.is_dir()}

    def run(self, args: list[str], env: dict[str, str] | None = None, timeout: int = TIMEOUT) -> Proc:
        environment = dict(os.environ)
        for name, value in (env or {}).items():
            if value:
                environment[name] = value.replace('$DIR', str(self.work))
            else:
                environment.pop(name, None)
        try:
            done = subprocess.run([str(self.exe), *args], cwd=self.work, env=environment, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise CaseError(f'{timeout}초 안에 끝나지 않았다: templide {" ".join(args)}')
        return Proc(done.returncode, self.normalize(done.stdout.decode('utf-8', 'replace')),
                    self.normalize(done.stderr.decode('utf-8', 'replace')))

    def sidecar(self, suffix: str) -> Path:
        """시험할 .tlide와 이름이 같고 확장자만 다른 파일 (.steps, .parts, .edits). 없을 수 있다."""
        return self.work / Path(self.source).with_suffix(suffix)

    def new_files(self) -> list[str]:
        """처음 복사한 뒤에 생긴 파일들 (작업 폴더 기준 상대 경로, 정렬)."""
        return sorted(self.tree() - self.baseline)

    def discard_new(self):
        """새로 생긴 파일과 폴더를 지워 처음 상태로 되돌린다."""
        for name in self.new_files():
            (self.work / name).unlink()
        for name in sorted(self.folders() - self.baseline_dirs, key=len, reverse=True):
            (self.work / name).rmdir()


class CaseError(Exception):
    """케이스가 기대한 모양으로 실행되지 않았다 (기대 파일과 비교하기 전의 실패). --update로도 고칠 수 없다."""


def kind_of_bytes(path: Path) -> str:
    head = path.read_bytes()[:26]
    if head.startswith(b'PK\x03\x04'):
        return 'zip'
    if head.startswith(b'\x89PNG\r\n\x1a\n'):
        # IHDR의 크기와 색 종류 (6은 투명도가 있는 RGBA, 2는 RGB)
        width, height = struct.unpack('>II', head[16:24])
        return f'png {width}x{height} ' + {6: 'rgba', 2: 'rgb'}.get(head[25], f'color-type-{head[25]}')
    if head.startswith(b'%PDF-'):
        return 'pdf'
    if head.lower().startswith(b'<!doctype html'):
        return 'html'
    return 'file'


# ---------------------------------------------------------------- 종류별 실행

@dataclass
class Outcome:
    text: str = ''
    skip: str = ''


def transcript(args: list[str], proc: Proc, files: list[str] | None = None) -> str:
    """명령 하나의 실행 기록. 비어 있는 부분은 적지 않는다."""
    shown = ' '.join(f'"{arg}"' if re.search(r'\s', arg) else arg for arg in args)
    lines = [f'$ templide {shown}', f'[exit] {proc.code}']
    if proc.out.strip():
        lines += ['[stdout]', proc.out.rstrip('\n')]
    if proc.err.strip():
        lines += ['[stderr]', proc.err.rstrip('\n')]
    if files:
        lines += ['[files]'] + files
    return '\n'.join(lines) + '\n'


def run_ir(box: Sandbox) -> Outcome:
    proc = box.run([box.source, '--ir'])
    if proc.code != 0:
        raise CaseError(f'종료 코드 {proc.code}\n{proc.err}{proc.out}')
    header = ''
    if proc.err.strip():
        header = ''.join(f'# stderr: {line}\n' for line in proc.err.rstrip('\n').split('\n'))
    # 이름 없이 animate를 건 개체의 이름(label 1/44)에 든 숫자는 원문의 바이트 위치라 줄바꿈이 바뀌면 달라진다
    out = re.sub(r'(Element \w+ as \w+) \d+(?:[/#]\d+)+', r'\1 $ID', proc.out)
    out = re.sub(r'(animate \w+) \d+(?:[/#]\d+)+', r'\1 $ID', out)
    return Outcome(header + out)


def run_error(box: Sandbox) -> Outcome:
    args = [box.source, '--check']
    proc = box.run(args)
    if proc.code not in (0, 1, 2):  # 0 성공, 1 검사 실패, 2 파일을 읽을 수 없음
        raise CaseError(f'컴파일러가 비정상으로 끝났다 (종료 코드 {proc.code})\n{proc.err}{proc.out}')
    if box.new_files():
        raise CaseError('--check가 파일을 썼다: ' + ', '.join(box.new_files()))
    return Outcome(transcript(args, proc))


def run_fmt(box: Sandbox) -> Outcome:
    args = ['fmt', '--stdout', box.source]
    proc = box.run(args)
    if proc.code not in (0, 1, 2):
        raise CaseError(f'컴파일러가 비정상으로 끝났다 (종료 코드 {proc.code})\n{proc.err}{proc.out}')
    if proc.code == 0 and proc.out.strip() and not proc.out.startswith('skipped '):
        # 포맷한 결과를 다시 포맷하면 그대로여야 한다
        formatted = box.work / box.source
        formatted.write_bytes(proc.out.encode('utf-8'))
        again = box.run(args)
        if again.code != 0 or again.out != proc.out:
            raise CaseError('포맷한 결과를 다시 포맷하면 결과가 달라진다 (멱등성)\n' + '\n'.join(
                difflib.unified_diff(proc.out.split('\n'), again.out.split('\n'), 'first', 'second', lineterm='', n=2)))
    return Outcome(transcript(args, proc))


def built_file(box: Sandbox, suffix: str, proc: Proc) -> Path:
    """빌드가 새로 만든 파일 중 suffix인 것 하나 (폴더 안이어도 된다)."""
    if proc.code != 0 or proc.err.strip():
        raise CaseError(f'빌드가 깨끗하게 끝나지 않았다 (종료 코드 {proc.code})\n{proc.err}{proc.out}')
    found = [name for name in box.new_files() if name.endswith(suffix)]
    if len(found) != 1:
        raise CaseError(f'새로 생긴 {suffix} 파일이 하나여야 하는데 {len(found)}개다: {found}')
    return box.work / found[0]


def natural_key(text: str):
    return [int(part) if part.isdigit() else part for part in re.split(r'(\d+)', text)]


def run_pptx(box: Sandbox) -> Outcome:
    proc = box.run([box.source])
    output = built_file(box, '.pptx', proc)
    parts_file = box.sidecar('.parts')
    patterns = ['ppt/slides/slide*.xml']
    if parts_file.is_file():
        lines = parts_file.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n')
        patterns = [line.strip() for line in lines if line.strip() and not line.strip().startswith('#')]
    sections = []
    with zipfile.ZipFile(output) as archive:
        names = archive.namelist()
        for pattern in patterns:
            # [Content_Types].xml처럼 glob 기호가 든 이름 그대로도 맞는다
            matched = sorted((name for name in names if name == pattern or fnmatch.fnmatchcase(name, pattern)), key=natural_key)
            if not matched:
                raise CaseError(f'.pptx에 {pattern}에 맞는 부분이 없다. 있는 부분: {names}')
            for name in matched:
                data = archive.read(name)
                if name.endswith(('.xml', '.rels')):
                    body = pretty_xml(data)
                else:
                    body = f'<binary {len(data)} bytes sha1 {hashlib.sha1(data).hexdigest()}>\n'
                sections.append(f'=== {name} ===\n{box.normalize(body)}')
    return Outcome('\n'.join(sections))


DECK = re.compile(r'<script type="application/json" id="templide-deck">(.*?)</script>', re.DOTALL)


def run_html(box: Sandbox) -> Outcome:
    proc = box.run([box.source])
    output = built_file(box, '.html', proc)
    page = output.read_text(encoding='utf-8')
    found = DECK.findall(page)
    if len(found) != 1:
        raise CaseError(f'덱 JSON이 든 script가 하나여야 하는데 {len(found)}개다')
    if output.name != 'index.html' and re.search(r'<script src=|<link ', page):
        raise CaseError('type = html은 파일 하나에 다 들어 있어야 하는데 바깥 파일을 가리키는 태그가 있다')

    def mask_date(value):
        """@date 필드의 글자(오늘 날짜)를 $DATE로 바꾼다."""
        if isinstance(value, dict):
            if value.get('fld') == 'datetime1':
                value = {**value, 't': '$DATE'}
            return {key: mask_date(item) for key, item in value.items()}
        if isinstance(value, list):
            return [mask_date(item) for item in value]
        return value

    return Outcome(box.normalize(pretty_json(mask_date(json.loads(found[0])))) + '\n')


def parse_steps(box: Sandbox) -> tuple[list[tuple[list[str], dict[str, str]]], bool]:
    """.steps를 (명령줄 인자, 환경 변수) 목록과 브라우저가 필요한지로 읽는다. 없으면 `x.tlide` 하나."""
    steps_file = box.sidecar('.steps')
    if not steps_file.is_file():
        return [([box.source], {})], False
    steps: list[tuple[list[str], dict[str, str]]] = []
    env: dict[str, str] = {}
    needs_browser = False
    for number, raw in enumerate(steps_file.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n'), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line == '@requires browser':
            needs_browser = True
        elif line.startswith('@env '):
            name, _, value = line[5:].partition('=')
            env[name.strip()] = value.strip()
        elif line.startswith('@'):
            raise CaseError(f'{steps_file.name}:{number}: 알 수 없는 지시 {line}')
        else:
            steps.append((shlex.split(line), dict(env)))
    return steps, needs_browser


def run_build(box: Sandbox) -> Outcome:
    steps, needs_browser = parse_steps(box)
    if needs_browser and find_browser() is None:
        return Outcome(skip='Chrome/Edge를 찾지 못했다 (TEMPLIDE_BROWSER로 알릴 수 있다)')
    records = []
    for args, env in steps:
        proc = box.run(args, env, PDF_TIMEOUT if needs_browser else TIMEOUT)
        files = [f'{name} ({kind_of_bytes(box.work / name)})' for name in box.new_files()]
        records.append(transcript(args, proc, files))
        box.discard_new()
    return Outcome('\n'.join(records))


class Server:
    """`templide --serve`와 LSP 형식(Content-Length 머리글 + JSON)으로 대화한다."""

    def __init__(self, exe: Path, cwd: Path):
        self.proc = subprocess.Popen([str(exe), '--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=cwd)
        self.next_id = 1
        self.versions: dict[str, int] = {}
        self.notifications: list[dict] = []     # 대답을 기다리는 동안 서버가 먼저 보낸 알림 (publishDiagnostics 등)

    def send(self, message: dict):
        body = json.dumps(message, ensure_ascii=False).encode('utf-8')
        self.proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
        self.proc.stdin.flush()

    def read(self) -> dict:
        length = 0
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise CaseError('서버가 대답 없이 끝났다')
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b'content-length:'):
                length = int(line.split(b':')[1])
        return json.loads(self.proc.stdout.read(length).decode('utf-8'))

    def request(self, method: str, params):
        ident = self.next_id
        self.next_id += 1
        self.send({'jsonrpc': '2.0', 'id': ident, 'method': method, 'params': params})
        while True:
            message = self.read()
            if message.get('id') == ident:
                if 'error' in message:
                    raise CaseError(f'{method}: {message["error"]}')
                return message['result']
            if 'method' in message and 'id' not in message:
                self.notifications.append(message)

    def notify(self, method: str, params):
        self.send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def open(self, uri: str, text: str):
        self.versions[uri] = 1
        self.notify('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'tlide', 'version': 1, 'text': text}})

    def change(self, uri: str, text: str):
        self.versions[uri] += 1
        self.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': self.versions[uri]}, 'contentChanges': [{'text': text}]})

    def close(self):
        try:
            self.request('shutdown', None)
            self.notify('exit', None)
        except Exception:
            pass
        self.proc.kill()
        self.proc.wait()


def offset_of(text: str, line: int, character: int) -> int:
    """LSP의 (줄, UTF-16 칸)을 파이썬 문자열 위치로."""
    position = 0
    for _ in range(line):
        position = text.index('\n', position) + 1
    units = 0
    while units < character and position < len(text):
        units += 2 if ord(text[position]) > 0xFFFF else 1
        position += 1
    return position


def apply_edits(text: str, edits: list[dict]) -> str:
    spans = []
    for edit in edits:
        start, end = edit['range']['start'], edit['range']['end']
        spans.append((offset_of(text, start['line'], start['character']), offset_of(text, end['line'], end['character']), edit['newText']))
    for begin, end, new in sorted(spans, key=lambda span: (span[0], span[1]), reverse=True):
        text = text[:begin] + new + text[end:]
    return text


def run_edit(box: Sandbox) -> Outcome:
    """편집기가 보내는 편집(templide/edit)을 한 줄씩 적용하고, 단계마다 원문이 어떻게 바뀌었는지 diff로 적는다.
    .edits는 한 줄에 JSON 편집 하나이고(# 주석), 'id'가 "@table"이면 문서의 첫 table(@table:2는 둘째)을 가리킨다"""
    steps = []
    for raw in box.sidecar('.edits').read_text(encoding='utf-8').splitlines():
        line = raw.strip()
        if line and not line.startswith('#'):
            steps.append(json.loads(line))
    path = box.work / box.source
    uri = path.resolve().as_uri()
    text = path.read_bytes().decode('utf-8').replace('\r\n', '\n')
    server = Server(box.exe, box.work)
    records = []
    try:
        server.request('initialize', {'processId': None, 'rootUri': None, 'capabilities': {}})
        server.notify('initialized', {})
        server.open(uri, text)
        for step in steps:
            operation = dict(step)
            # "definition"이 있으면 template, master case를 슬라이드 한 장으로 펼친 편집 화면에서 편집한다
            scope = {'uri': uri, **({'definition': operation['definition']} if 'definition' in operation else {})}
            operation.pop('definition', None)
            deck = server.request('templide/deck', scope)
            if 'error' in deck:
                raise CaseError(f'편집하기 전에 문서에 오류가 있다: {deck["error"]}')
            reference = str(operation.get('id', ''))
            if reference.startswith('@'):
                name, _, number = reference[1:].partition(':')
                ids = [eid for eid, entry in deck['elements'].items() if entry['object'] == name]
                index = int(number) - 1 if number else 0
                if index >= len(ids):
                    raise CaseError(f'{reference}에 맞는 개체가 없다')
                operation['id'] = ids[index]
            result = server.request('templide/edit', {**scope, 'edits': [operation]})
            header = '$ ' + json.dumps(step, ensure_ascii=False)
            if 'error' in result:
                records.append(f'{header}\n[error] {result["error"]}\n')
                continue
            updated = apply_edits(text, result['edits'])
            server.change(uri, updated)
            check = server.request('templide/deck', scope)
            diff = [line for line in difflib.unified_diff(text.split('\n'), updated.split('\n'), lineterm='', n=0) if not line.startswith(('---', '+++', '@@'))]
            body = '\n'.join(diff) if diff else '(바뀐 것 없음)'
            status = f'[깨짐] {check["error"]}\n' if 'error' in check else ''
            records.append(f'{header}\n[diff]\n{body}\n{status}')
            text = updated
    finally:
        server.close()
    return Outcome('\n'.join(records))


COMPLETE_MARK = '‸'      # 자동 완성을 물을 자리
HOVER_MARK = '⌖'         # hover, 정의, 강조를 물을 자리


def split_marks(text: str) -> tuple[str, list[tuple[str, int, int]]]:
    """표식을 지운 글과, 표식의 (종류, 줄, UTF-16 칸) 목록 (줄과 칸은 0부터)."""
    clean, marks, line, column = [], [], 0, 0
    for char in text:
        if char in (COMPLETE_MARK, HOVER_MARK):
            marks.append((char, line, column))
            continue
        clean.append(char)
        if char == '\n':
            line, column = line + 1, 0
        else:
            column += 2 if ord(char) > 0xFFFF else 1
    return ''.join(clean), marks


def show_range(range_: dict) -> str:
    """LSP 범위를 줄:칸-줄:칸 (1부터)으로."""
    start, end = range_['start'], range_['end']
    return f'{start["line"] + 1}:{start["character"] + 1}-{end["line"] + 1}:{end["character"] + 1}'


def normalize_strings(value, normalize):
    """JSON 값 안의 글마다 normalize를 적용한다 (이스케이프되기 전의 경로를 알아보도록)."""
    if isinstance(value, str):
        return normalize(value)
    if isinstance(value, list):
        return [normalize_strings(item, normalize) for item in value]
    if isinstance(value, dict):
        return {key: normalize_strings(item, normalize) for key, item in value.items()}
    return value


def run_lsp(box: Sandbox) -> Outcome:
    """문서를 열어 진단을 받고, 표식(‸ 자동 완성, ⌖ hover·정의·강조) 자리마다 서버의 답을 적는다."""
    path = box.work / box.source
    uri = path.resolve().as_uri()
    text, marks = split_marks(path.read_bytes().decode('utf-8').replace('\r\n', '\n'))

    def place(target: str) -> str:
        """uri를 작업 폴더 기준 이름으로 (컴파일러 폴더의 파일은 $EXE_DIR/...)."""
        name = box.normalize(str(Path(urllib.request.url2pathname(urllib.parse.urlparse(target).path))))
        return name[len('$DIR/'):] if name.startswith('$DIR/') else name

    server = Server(box.exe, box.work)
    lines = []
    try:
        server.request('initialize', {'processId': None, 'rootUri': None, 'capabilities': {}})
        server.notify('initialized', {})
        server.open(uri, text)
        # 서버는 한 줄씩 처리하므로 이 대답이 오기 전에 문서를 컴파일한 진단이 먼저 온다
        server.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
        latest = {}
        for note in server.notifications:
            if note['method'] == 'textDocument/publishDiagnostics':
                latest[note['params']['uri']] = note['params']['diagnostics']
        shown = [(place(target), diagnostics) for target, diagnostics in latest.items() if diagnostics]
        lines.append('[diagnostics]')
        for name, diagnostics in sorted(shown, key=lambda item: item[0]):
            for item in sorted(diagnostics, key=lambda d: (d['range']['start']['line'], d['range']['start']['character'])):
                severity = {1: 'error', 2: 'warning'}.get(item.get('severity'), 'info')
                code = f'[{item["code"]}]' if item.get('code') else ''
                message = box.normalize(item['message']).replace('\n', '\n    ')
                lines.append(f'{name} {show_range(item["range"])} {severity}{code}: {message}')
        if not shown:
            lines.append('(없음)')
        for kind, line, column in marks:
            position = {'line': line, 'character': column}
            here = {'textDocument': {'uri': uri}, 'position': position}
            at = f'{line + 1}:{column + 1}'
            if kind == COMPLETE_MARK:
                result = server.request('textDocument/completion', here)
                items = sorted(result['items'], key=lambda item: item.get('sortText', item['label']))
                labels = [item['label'] for item in items]
                lines.append(f'\n[completion {at}]')
                if not items:
                    lines.append('(없음)')
                    continue
                ranges = {show_range(item['textEdit']['range']) for item in items}
                lines.append('replace ' + ', '.join(sorted(ranges)))
                lines.append(f'{len(items)} items: ' + ' '.join(labels[:14]) + (' ...' if len(labels) > 14 else ''))
                inserts = [item['textEdit']['newText'] for item in items[:3]]
                lines.append('insert ' + ' | '.join(json.dumps(insert, ensure_ascii=False) for insert in inserts))
            else:
                lines.append(f'\n[hover/definition {at}]')
                hover = server.request('textDocument/hover', here)
                if hover:
                    contents = hover['contents']['value'] if isinstance(hover['contents'], dict) else str(hover['contents'])
                    lines.append(f'hover {show_range(hover["range"])}: ' + ' / '.join(part for part in contents.split('\n') if part and part != '```'))
                else:
                    lines.append('hover: (없음)')
                definition = server.request('textDocument/definition', here)
                found = definition if isinstance(definition, list) else [definition] if definition else []
                lines.append('definition: ' + (', '.join(f'{place(item["uri"])} {show_range(item["range"])}' for item in found) or '(없음)'))
                highlight = server.request('textDocument/documentHighlight', here) or []
                lines.append('highlight: ' + (', '.join(show_range(item['range']) for item in highlight) or '(없음)'))
        # 같은 이름의 .requests: 한 줄에 요청 하나({"method": ..., "params": ...}, $URI는 이 문서의 URI, # 주석)를 보내고 답을 적는다
        requests_file = box.sidecar('.requests')
        if requests_file.is_file():
            for raw in requests_file.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n'):
                if not raw.strip() or raw.startswith('#'):
                    continue
                item = json.loads(raw.replace('$URI', uri))
                reply = server.request(item['method'], item.get('params', {}))
                lines.append(f'\n[request {item["method"]}]')
                lines.append(pretty_json(normalize_strings(reply, box.normalize)))
    finally:
        server.close()
    return Outcome('\n'.join(lines) + '\n')


@dataclass
class Kind:
    suffix: str                      # 기대 파일의 확장자
    run: Callable[[Sandbox], Outcome]


# 보이는 순서대로 돈다. 새 종류는 여기에 한 줄을 더하면 된다
KINDS: dict[str, Kind] = {
    'ir': Kind('.ir', run_ir),
    'error': Kind('.expected', run_error),
    'pptx': Kind('.xml', run_pptx),
    'html': Kind('.json', run_html),
    'build': Kind('.expected', run_build),
    'edit': Kind('.expected', run_edit),
    'lsp': Kind('.expected', run_lsp),
    'fmt': Kind('.expected', run_fmt),
}


# ---------------------------------------------------------------- 케이스 찾기와 실행

# 종류마다 케이스를 모은 파일(.txtar): `-- 경로 --` 줄마다 파일 하나가 시작되고, 다음 구분선 앞까지가 그 내용이다. 첫 구분선 앞의 글은 설명이다.
# 글로 읽을 수 없는 내용(UTF-8이 아니거나, 줄바꿈으로 끝나지 않거나, 구분선처럼 보이는 줄이 있는 것)은 `-- 경로 (base64) --`로 적는다
MARK = re.compile(rb'^-- (.+?)( \(base64\))? --$', re.M)


def read_container(path: Path) -> tuple[bytes, dict[str, bytes]]:
    data = path.read_bytes()
    marks = list(MARK.finditer(data))
    files = {}
    for i, mark in enumerate(marks):
        body = data[mark.end() + 1:marks[i + 1].start() if i + 1 < len(marks) else len(data)]
        files[mark.group(1).decode('utf-8')] = base64.b64decode(body) if mark.group(2) else body
    return data[:marks[0].start()] if marks else data, files


def write_container(path: Path, comment: bytes, files: dict[str, bytes]):
    parts = [comment]
    for name, data in files.items():
        try:
            data.decode('utf-8')
            raw = data.endswith(b'\n') and b'\x00' not in data and not MARK.search(data)
        except UnicodeDecodeError:
            raw = False
        parts += [f'-- {name}{"" if raw else " (base64)"} --\n'.encode('utf-8'), data if raw else base64.encodebytes(data)]
    path.write_bytes(b''.join(parts))


@dataclass
class Archive:
    """한 종류의 케이스를 모은 .txtar 파일."""
    path: Path
    comment: bytes
    files: dict[str, bytes]  # 구분선 경로 -> 내용

    def put(self, name: str, data: bytes, after: str):
        """name 구분선의 내용을 바꿔 파일에 쓴다. 없던 구분선이면 after 구분선 바로 뒤에 넣는다."""
        if name in self.files:
            self.files[name] = data
        else:
            items = []
            for key, value in self.files.items():
                items.append((key, value))
                if key == after:
                    items.append((name, data))
            self.files = dict(items)
        write_container(self.path, self.comment, self.files)


@dataclass
class Case:
    kind: str
    name: str                # 종류 안에서 겹치지 않는 이름
    prefix: str              # 이 케이스의 구분선 경로 앞에 붙는 폴더 ('' 또는 '폴더/')
    files: dict[str, bytes]  # 작업 폴더에 풀 파일들 (이름은 접두어를 뗀 경로)
    archive: Archive

    @property
    def id(self) -> str:
        return f'{self.kind}/{self.name}'

    @property
    def main(self) -> str:
        """시험할 .tlide. 이름에 /가 없고 _로 시작하지 않는 첫 것 (나머지는 #include용 도우미)."""
        return next(name for name in self.files if name.endswith('.tlide') and '/' not in name and not name.startswith('_'))

    @property
    def expected_name(self) -> str:
        return Path(self.main).with_suffix(KINDS[self.kind].suffix).as_posix()


def discover() -> list[Case]:
    """시험할 .tlide(_로 시작하지 않고, 종류 바로 아래나 폴더 하나 안에 있는 것)마다 케이스 하나다.
    종류 바로 아래면 같은 이름의 파일들(기대, .steps 등)이, 폴더 안이면 그 폴더 전체가 그 케이스의 파일이다."""
    cases = []
    for kind in KINDS:
        path = CASES / f'{kind}.txtar'
        if not path.is_file():
            continue
        archive = Archive(path, *read_container(path))
        for main in sorted(name for name in archive.files if name.endswith('.tlide') and name.count('/') <= 1 and not Path(name).name.startswith('_')):
            folder = main.rpartition('/')[0]
            prefix = folder + '/' if folder else ''
            if folder:
                own = {name[len(prefix):]: data for name, data in archive.files.items() if name.startswith(prefix)}
            else:
                own = {name: data for name, data in archive.files.items() if '/' not in name and Path(name).stem == Path(main).stem}
            cases.append(Case(kind, folder or Path(main).stem, prefix, own, archive))
    return cases


@dataclass
class Result:
    case: Case
    status: str                      # pass, fail, skip
    detail: str = ''                 # 실패 이유(diff 등) 또는 건너뛴 이유
    text: str = ''
    change: str = ''                 # --update에서 created, updated, 또는 빈 문자열


def run_case(case: Case, exe: Path, update: bool, verbose: bool) -> Result:
    box = Sandbox(case, exe)
    try:
        outcome = KINDS[case.kind].run(box)
    except CaseError as error:
        return Result(case, 'fail', str(error))
    except Exception as error:  # 깨진 zip 같은 예상 밖의 문제도 이 케이스만 실패로 하고 다음으로 넘어간다
        return Result(case, 'fail', f'{type(error).__name__}: {error}')
    finally:
        box.close()
    if outcome.skip:
        return Result(case, 'skip', outcome.skip)
    actual = outcome.text if outcome.text.endswith('\n') or not outcome.text else outcome.text + '\n'
    saved = case.files.get(case.expected_name)
    expected = None if saved is None else saved.decode('utf-8').replace('\r\n', '\n')
    if update:
        if expected == actual:
            return Result(case, 'pass', text=actual)
        case.archive.put(case.prefix + case.expected_name, actual.encode('utf-8'), case.prefix + case.main)
        return Result(case, 'pass', text=actual, change='created' if expected is None else 'updated')
    if expected is None:
        return Result(case, 'fail', f'{case.archive.path.relative_to(ROOT).as_posix()}에 `-- {case.prefix}{case.expected_name} --` 구분선이 없다. 내용을 확인한 뒤 --update로 만든다', actual)
    if expected != actual:
        diff = list(difflib.unified_diff(expected.split('\n'), actual.split('\n'), 'expected', 'actual', lineterm='', n=3))
        limit = None if verbose else 120
        shown = diff if limit is None else diff[:limit]
        if limit is not None and len(diff) > limit:
            shown.append(f'... (diff {len(diff) - limit}줄 더. -v로 전부 본다)')
        return Result(case, 'fail', '\n'.join(shown), actual)
    return Result(case, 'pass', text=actual)


CATALOG_CASE = 'catalog/consistency'


def indent(text: str, prefix: str = '    ') -> str:
    return '\n'.join(prefix + line for line in text.rstrip('\n').split('\n'))


def main(argv: list[str] | None = None) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description='templide 컴파일러 테스트 (자세한 설명은 이 파일 맨 위)')
    parser.add_argument('filters', nargs='*', help='kind/이름에 들어 있어야 하는 글. 여러 개면 하나라도 맞는 케이스')
    parser.add_argument('--exe', help='컴파일러 경로')
    parser.add_argument('--update', action='store_true', help='기대 파일을 지금 출력으로 다시 쓴다')
    parser.add_argument('-v', '--verbose', action='store_true', help='통과한 케이스의 출력과 전체 diff도 보인다')
    parser.add_argument('--list', action='store_true', help='케이스 목록만 보인다')
    args = parser.parse_args(argv)

    cases = discover()
    check_catalog = True
    if args.filters:
        words = [word.lower() for word in args.filters]
        cases = [case for case in cases if any(word in case.id.lower() for word in words)]
        check_catalog = any(word in CATALOG_CASE for word in words)
    if args.list:
        for case in cases:
            print(case.id)
        if check_catalog:
            print(CATALOG_CASE)
        return 0
    if not cases and not check_catalog:
        print('error: 맞는 케이스가 없다', file=sys.stderr)
        return 2
    exe = find_exe(args.exe)
    print(f'compiler: {exe}')

    started = time.time()
    results = []
    for case in cases:
        result = run_case(case, exe, args.update, args.verbose)
        results.append(result)
        label = {'pass': 'ok  ', 'fail': 'FAIL', 'skip': 'skip'}[result.status]
        note = {'created': ' (created)', 'updated': ' (updated)'}.get(result.change, '')
        print(f'{label} {case.id}{note}' + (f' - {result.detail}' if result.status == 'skip' else ''), flush=True)
        if result.status == 'fail':
            print(indent(result.detail), flush=True)
        elif args.verbose and result.status == 'pass':
            print(indent(result.text), flush=True)

    passed = sum(result.status == 'pass' for result in results)
    skipped = sum(result.status == 'skip' for result in results)
    failed = sum(result.status == 'fail' for result in results)
    if check_catalog:
        problems = catalog_check.check()
        print(f"{'FAIL' if problems else 'ok  '} {CATALOG_CASE}", flush=True)
        if problems:
            print(indent('\n'.join(problems)), flush=True)
            failed += 1
        else:
            passed += 1
    print(f'\n{passed} passed, {skipped} skipped, {failed} failed ({time.time() - started:.1f}s)')
    if args.update:
        for change in ('created', 'updated'):
            names = [result.case.id for result in results if result.change == change]
            if names:
                print(f'{change}: ' + ', '.join(names))
        if not any(result.change for result in results):
            print('기대 파일이 바뀐 것은 없다')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

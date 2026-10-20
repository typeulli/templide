"""진단 카탈로그(middleend/diagnostic_catalog.cpp)가 소스의 진단 호출과 맞는지 검사한다. 표준 라이브러리만 쓴다.

test/run.py가 `catalog/consistency` 케이스로 부른다. 직접 돌리면 문제를 한 줄씩 적고 문제가 있으면 종료 코드 1이다.

검사하는 것
  1. 소스에서 "E2001"처럼 코드를 넘기는 호출의 코드가 모두 카탈로그에 있다
  2. 카탈로그의 코드가 모두 쓰인다
  3. 메시지가 문자열을 +로 이은 식이면, 그 모양(바뀌는 부분은 {})이 카탈로그의 english와 같다. 3항 연산자가 섞였거나
     통째로 변수인 메시지는 모양을 알 수 없으므로 건너뛴다
  4. 코드가 중복되지 않고 정렬되어 있다
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'middleend' / 'diagnostic_catalog.cpp'
# 코드를 넘기는 호출이 있는 소스
SOURCES = ['middleend/analyzer.cpp', 'frontend/parser.h', 'backend/backend.cpp', 'main.cpp', 'server/server.cpp', 'middleend/accessibility.cpp', 'format/options.cpp', 'format/formatter.cpp']
CALL = re.compile(r'(?<![\w:])(error|warning|include_error|fail|add|add_error|forbid)\(\s*"([EWA]\d{4})"')
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')


def read(path: Path) -> str:
    return path.read_text(encoding='utf-8').replace('\r\n', '\n')


def catalog_entries() -> list[tuple[str, str, str]]:
    """(code, name, english). 문자열은 소스에 적힌 그대로(이스케이프 포함)다."""
    text = read(CATALOG)
    pattern = re.compile(r'\{"([EWA]\d{4})",\s*Level::\w+,\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}')
    return [(m.group(1), m.group(2), m.group(3)) for m in pattern.finditer(text)]


def scan_call(text: str, start: int) -> tuple[list[str], int]:
    """text[start]는 '(' 다음 위치. 짝이 맞는 ')'까지의 인자들과 그 끝 위치."""
    depth = 1
    i = start
    cur = start
    args: list[str] = []
    while i < len(text):
        c = text[i]
        if c in '"\'':
            i += 1
            while text[i] != c:
                if text[i] == '\\':
                    i += 1
                i += 1
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append(text[cur:i])
                return args, i
        elif c == ',' and depth == 1:
            args.append(text[cur:i])
            cur = i + 1
        i += 1
    raise ValueError('괄호가 맞지 않는다')


def shape_of(expression: str) -> tuple[str, bool]:
    """+로 이은 식을 고정 문장과 {}로 줄인다. 두 번째 값은 모양을 알 수 없는지(3항 연산자 등)."""
    text = expression.strip()
    unknown = '?' in STRING.sub('', text)
    items: list[str] = []
    depth = 0
    cur = ''
    i = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                if text[j] == '\\':
                    j += 1
                j += 1
            cur += text[i:j + 1]
            i = j + 1
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == '+' and depth == 0:
            items.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    items.append(cur)
    out = ''
    for item in items:
        match = re.fullmatch(r'(?:std::string\()?\s*"((?:[^"\\]|\\.)*)"\s*\)?', item.strip())
        out += match.group(1) if match else '{}'
    while '{}{}' in out:
        out = out.replace('{}{}', '{}')
    return out, unknown


def used_codes() -> tuple[dict[str, list[tuple[str, str, bool]]], list[str]]:
    """코드 -> [(위치, 메시지 모양, 모양을 모르는지)]와 읽지 못한 소스 목록."""
    result: dict[str, list[tuple[str, str, bool]]] = {}
    problems: list[str] = []
    for name in SOURCES:
        path = ROOT / name
        if not path.is_file():
            continue
        text = read(path)
        for match in CALL.finditer(text):
            line = text.count('\n', 0, match.start()) + 1
            try:
                args, _ = scan_call(text, match.end() - len(match.group(0)) + len(match.group(1)) + 1)
            except ValueError as error:
                problems.append(f'{name}:{line}: {error}')
                continue
            shape, unknown = shape_of(args[-1])
            result.setdefault(match.group(2), []).append((f'{name}:{line}', shape, unknown))
    return result, problems


def check() -> list[str]:
    problems: list[str] = []
    entries = catalog_entries()
    if not entries:
        return [f'{CATALOG.name}에서 항목을 읽지 못했다']
    seen: set[str] = set()
    previous = ''
    for code, name, english in entries:
        if code in seen:
            problems.append(f'{code}: 코드가 두 번 나온다')
        seen.add(code)
        if code < previous:
            problems.append(f'{code}: {previous} 뒤에 있어 정렬이 깨졌다')
        previous = code
    used, scan_problems = used_codes()
    problems += scan_problems
    english_of = {code: english for code, name, english in entries}
    for code, sites in sorted(used.items()):
        if code not in english_of:
            problems.append(f'{code}: 카탈로그에 없다 ({sites[0][0]})')
            continue
        for place, shape, unknown in sites:
            if unknown or not re.search(r'[A-Za-z]', shape.replace('{}', '')):
                continue
            if shape != english_of[code]:
                problems.append(f'{code}: {place}의 메시지 모양이 카탈로그와 다르다\n        소스: {shape}\n        카탈로그: {english_of[code]}')
    mentioned = set(used)
    for name in SOURCES:
        path = ROOT / name
        if path.is_file():
            mentioned.update(re.findall(r'"([EWA]\d{4})"', read(path)))
    for code, name, english in entries:
        if code not in mentioned:
            problems.append(f'{code}: 소스에서 쓰이지 않는다')
    return problems


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='replace')
    problems = check()
    for problem in problems:
        print(problem)
    if not problems:
        print('catalog ok')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

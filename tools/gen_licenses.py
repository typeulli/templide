"""편집기에 들어가는 오픈소스의 라이선스 목록(editor/src/generated/licenses.json)을 만든다.
편집기의 '오픈소스 라이선스' 창이 이 파일을 보여 준다. build.py가 편집기를 빌드하기 전에 실행한다.

    python tools/gen_licenses.py

- 편집기 화면: editor의 npm 패키지 중 실행에 쓰는 것 (devDependencies와 타입 정의는 뺀다)
- 편집기 프로그램: src-tauri가 Windows에서 링크하는 Rust 크레이트 (빌드에만 쓰는 크레이트와 proc-macro는 뺀다)
- 컴파일러: templide.exe에 들어가는 C++ 라이브러리와 MinGW-w64 런타임
- 함께 넣은 파일: libs의 reveal.js, AI 탭의 ConPTY, monaco-editor에 든 Codicons 아이콘 글꼴

패키지에 라이선스 파일이 없는 것은 원본에서 받아 tools/licenses에 둔 본문을 쓴다
- webview2-rs.txt: webview2-com, webview2-com-sys (https://github.com/wravery/webview2-rs 의 LICENSE)
- webview2-sdk.txt: webview2-com-sys가 링크하는 WebView2LoaderStatic.lib (NuGet Microsoft.Web.WebView2 의 LICENSE.txt)
- codicons.txt: monaco-editor에 든 codicon.ttf (https://github.com/microsoft/vscode-codicons 의 LICENSE, CC BY 4.0).
  monaco-editor의 MIT 고지에는 들어 있지 않아 따로 고지한다

npm, cargo가 있어야 하고, 컴파일러는 cmake-build-release로 한 번 빌드되어 있어야 한다 (FetchContent로 받은 소스를 읽는다).
"""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITOR = ROOT / 'editor'
TAURI = EDITOR / 'src-tauri'
COMPILER_BUILD = ROOT / 'cmake-build-release'
OUT = EDITOR / 'src' / 'generated' / 'licenses.json'
SAVED = ROOT / 'tools' / 'licenses'
# 패키지 안에 라이선스 파일이 없는 크레이트 -> tools/licenses의 본문
SAVED_TEXTS = {'webview2-com': ['webview2-rs.txt'], 'webview2-com-sys': ['webview2-rs.txt', 'webview2-sdk.txt']}
TARGET = 'x86_64-pc-windows-msvc'

LICENSE_FILE = re.compile(r'^(licen[cs]e|copying|notice|unlicense|thirdpartynotices)', re.IGNORECASE)


def run(command, cwd):
    program = shutil.which(command[0])
    if program is None:
        sys.exit(f'{command[0]}을(를) 찾을 수 없습니다')
    return subprocess.run([program, *command[1:]], cwd=cwd, capture_output=True, text=True, encoding='utf-8', check=True).stdout


# 저장소 주소를 브라우저로 열 수 있는 주소로 (git+https://...git, git://, github:사용자/저장소)
def web_url(url):
    url = url.strip()
    if url.startswith('github:'):
        url = 'https://github.com/' + url[len('github:'):]
    url = re.sub(r'^git\+', '', url)
    url = re.sub(r'^(git|ssh)://(git@)?', 'https://', url)
    url = re.sub(r'^git@([^:]+):', r'https://\1/', url)
    return re.sub(r'\.git$', '', url)


class Collector:
    def __init__(self):
        self.texts = {}
        self.sections = []

    def text(self, content):
        content = content.replace('\r\n', '\n').strip()
        key = hashlib.sha1(content.encode('utf-8')).hexdigest()[:12]
        self.texts.setdefault(key, content)
        return key

    def files(self, folder):
        found = []
        for path in sorted(folder.iterdir()):
            if path.is_file() and LICENSE_FILE.match(path.name):
                found.append(self.text(path.read_text(encoding='utf-8', errors='replace')))
        return found

    def section(self, title, packages):
        packages.sort(key=lambda package: package['name'].lower())
        self.sections.append({'title': title, 'packages': packages})


def npm_packages(collector):
    lines = run(['npm', 'ls', '--omit=dev', '--all', '--parseable'], EDITOR).splitlines()
    packages = []
    seen = set()
    for line in lines:
        folder = pathlib.Path(line.strip())
        if folder == EDITOR or 'node_modules' not in folder.parts or folder in seen:
            continue
        seen.add(folder)
        info = json.loads((folder / 'package.json').read_text(encoding='utf-8'))
        name = info['name']
        # 타입 정의만 있는 패키지는 실행 파일에 들어가지 않는다
        if name.startswith('@types/') or name == 'csstype':
            continue
        repository = info.get('repository')
        url = info.get('homepage') or (repository.get('url') if isinstance(repository, dict) else repository) or f'https://www.npmjs.com/package/{name}'
        packages.append({'name': name, 'version': info.get('version', ''), 'license': info.get('license', ''), 'url': web_url(url), 'texts': collector.files(folder)})
    collector.section('편집기 화면 (npm)', packages)


def cargo_packages(collector):
    # 실행 파일에 링크되는 크레이트: 일반 의존성만, proc-macro는 컴파일할 때만 쓰인다
    tree = run(['cargo', 'tree', '-e', 'normal,no-proc-macro', '--target', TARGET, '--prefix', 'none', '-f', '{p}'], TAURI)
    linked = set()
    for line in tree.splitlines():
        parts = line.replace(' (*)', '').split()
        if len(parts) >= 2:
            linked.add((parts[0], parts[1].lstrip('v')))
    metadata = json.loads(run(['cargo', 'metadata', '--format-version', '1', '--filter-platform', TARGET], TAURI))
    root = metadata['resolve']['root']
    # 크레이트 안에 라이선스 파일이 없으면 같은 저장소의 다른 크레이트에 든 것을 쓴다 (예: webview2-com-sys)
    by_repository = {}
    for package in metadata['packages']:
        folder = pathlib.Path(package['manifest_path']).parent
        if package.get('repository') and any(LICENSE_FILE.match(path.name) for path in folder.iterdir() if path.is_file()):
            by_repository.setdefault(package['repository'].rstrip('/'), folder)
    packages = []
    for package in metadata['packages']:
        if package['id'] == root or (package['name'], package['version']) not in linked:
            continue
        folder = pathlib.Path(package['manifest_path']).parent
        license = package.get('license') or ''
        url = package.get('repository') or package.get('homepage') or f'https://crates.io/crates/{package["name"]}'
        texts = collector.files(folder) or [collector.text((SAVED / name).read_text(encoding='utf-8')) for name in SAVED_TEXTS.get(package['name'], [])]
        if not texts and package.get('repository') and package['repository'].rstrip('/') in by_repository:
            texts = collector.files(by_repository[package['repository'].rstrip('/')])
        packages.append({'name': package['name'], 'version': package['version'], 'license': license, 'url': web_url(url), 'texts': texts})
    collector.section('편집기 프로그램 (Rust)', packages)


def mit_text(copyright):
    return f'''MIT License

{copyright}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.'''


def mingw_licenses():
    # 컴파일러를 빌드한 MinGW의 licenses 폴더 (CMAKE_CXX_COMPILER가 <mingw>/bin/c++.exe)
    cache = (COMPILER_BUILD / 'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
    match = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', cache, re.MULTILINE)
    if match is None:
        sys.exit('cmake-build-release/CMakeCache.txt에서 컴파일러를 찾을 수 없습니다')
    folder = pathlib.Path(match.group(1).strip()).parent.parent / 'licenses'
    if not folder.exists():
        sys.exit(f'MinGW 라이선스 폴더가 없습니다: {folder}')
    return folder


def compiler_packages(collector):
    deps = COMPILER_BUILD / '_deps'
    if not deps.exists():
        sys.exit('cmake-build-release를 먼저 빌드해 주세요')
    stb = (deps / 'stb-src' / 'stb_image.h').read_text(encoding='utf-8', errors='replace')
    stb_license = stb[stb.rindex('This software is available under 2 licenses'):].rstrip().rstrip('*/').strip()
    json_header = (deps / 'nlohmann_json-src' / 'json.hpp').read_text(encoding='utf-8', errors='replace')
    json_copyright = re.search(r'SPDX-FileCopyrightText: (.+)', json_header).group(1).strip()
    mingw = mingw_licenses()
    packages = [
        {'name': 'miniz', 'version': '3.0.2', 'license': 'MIT', 'url': 'https://github.com/richgel999/miniz', 'texts': collector.files(deps / 'miniz-src')},
        {'name': 'stb_image', 'version': '', 'license': 'MIT OR Unlicense', 'url': 'https://github.com/nothings/stb', 'texts': [collector.text(stb_license)]},
        {'name': 'nlohmann/json', 'version': '3.11.3', 'license': 'MIT', 'url': 'https://github.com/nlohmann/json', 'texts': [collector.text(mit_text(f'Copyright (c) {json_copyright}'))]},
        {'name': 'MinGW-w64 winpthreads', 'version': '', 'license': 'MIT', 'url': 'https://www.mingw-w64.org', 'texts': collector.files(mingw / 'winpthreads')},
        {'name': 'MinGW-w64 runtime', 'version': '', 'license': 'ZPL-2.1, public domain', 'url': 'https://www.mingw-w64.org', 'texts': collector.files(mingw / 'crt')},
        {'name': 'GCC runtime (libstdc++, libgcc)', 'version': '', 'license': 'GPL-3.0 WITH GCC-exception-3.1', 'url': 'https://gcc.gnu.org',
         'texts': [collector.text((mingw / 'gcc' / 'COPYING.RUNTIME').read_text(encoding='utf-8', errors='replace'))]},
    ]
    collector.section('컴파일러 (C++)', packages)


def bundled_packages(collector):
    packages = [
        {'name': 'reveal.js', 'version': '', 'license': 'MIT', 'url': 'https://revealjs.com', 'texts': [collector.text((ROOT / 'libs' / 'reveal' / 'LICENSE').read_text(encoding='utf-8'))]},
        {'name': 'ConPTY, OpenConsole (Windows Terminal)', 'version': '1.25.260930003', 'license': 'MIT', 'url': 'https://github.com/microsoft/terminal',
         'texts': [collector.text((TAURI / 'conpty' / 'LICENSE').read_text(encoding='utf-8'))]},
        # CC BY 4.0은 저작자, 라이선스, 수정 여부를 밝히도록 한다
        {'name': 'Codicons (monaco-editor의 아이콘 글꼴)', 'version': '', 'license': 'CC-BY-4.0', 'url': 'https://github.com/microsoft/vscode-codicons',
         'texts': [collector.text('Codicons\n'
                                  'Copyright (c) Microsoft Corporation\n'
                                  'Licensed under the Creative Commons Attribution 4.0 International Public License (https://creativecommons.org/licenses/by/4.0/).\n'
                                  'The font (codicon.ttf) is included unmodified as part of monaco-editor.'),
                   collector.text((SAVED / 'codicons.txt').read_text(encoding='utf-8'))]},
    ]
    collector.section('함께 넣은 파일', packages)


def main():
    collector = Collector()
    npm_packages(collector)
    cargo_packages(collector)
    compiler_packages(collector)
    bundled_packages(collector)
    missing = [f'{section["title"]}: {package["name"]}' for section in collector.sections for package in section['packages'] if not package['texts']]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({'sections': collector.sections, 'texts': collector.texts}, ensure_ascii=False, indent=1), encoding='utf-8')
    count = sum(len(section['packages']) for section in collector.sections)
    print(f'{OUT.relative_to(ROOT)}: 패키지 {count}개, 라이선스 본문 {len(collector.texts)}개')
    for name in missing:
        print(f'  라이선스 파일 없음: {name}')


if __name__ == '__main__':
    main()

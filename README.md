<p align="center">
  <img src="assets/icon/templide.svg" width="128" alt="templide">
</p>

<h1 align="center">templide</h1>

<p align="center">A markup-based presentation language</p>

templide는 텍스트로 발표 자료를 작성하고 다양한 형식으로 내보낼 수 있는 마크업 기반 프레젠테이션 도구입니다. `.tlide` 파일 하나에 슬라이드의 구조와 스타일을 정의하면 PowerPoint(`.pptx`), HTML, PDF, PNG 등의 결과물을 만들 수 있습니다.

반복되는 슬라이드 작업을 템플릿과 코드로 재사용하고 싶은 개발자와 디자이너를 위한 도구입니다. 프레젠테이션을 코드로 관리하면서도 편집기에서 결과물을 시각적으로 확인하고 수정할 수 있습니다.

주요 기능:

- 변수, 조건문, 반복문, f-string을 지원하는 프레젠테이션 문법
- 템플릿과 마스터를 활용한 슬라이드 구성 요소 재사용
- 코드와 캔버스를 함께 사용하는 양방향 편집기
- 도형, 표, 차트, 수식, 이미지, 비디오, 오디오, 하이퍼링크, 발표자 메모 지원
- PowerPoint 파일 가져오기 및 `.tlide` 변환(Windows)
- HTML, PDF, PNG 등 여러 출력 형식 지원
- 코드 포맷터, 접근성 검사, 히스토리, 에셋 라이브러리 제공
- `#include <std/...>`로 사용할 수 있는 표준 스타일 및 위젯 라이브러리

자세한 문법과 사용법은 아래 예제와 문서를 참고하세요.

## 예제

```tlide
#include <std/stddef>

style title {
    font-size = 40pt;
    font-weight = bold;
}

slide {
    put text_box {
        text = title "안녕하세요, templide";
        x = 80px; y = 300px; width = 1120px; height = 80px;
    }
}

slide {
    put text_box {
        text = bullets ["코드로 쓰는 발표 자료", "PowerPoint와 HTML로 내보내기"];
        x = 80px; y = 120px; width = 1120px; height = 400px;
    }
}

target deck { path = "hello.pptx"; type = pptx; }
target web { path = "hello.html"; type = html; }
```

표는 `cells`에 행마다 칸의 글을 늘어놓습니다. 머리글과 줄무늬, 열 너비, 선과 여백은 표 속성이고, 칸을 병합하거나 한 칸만 꾸밀 때는 `cell(행, 열) { ... }`을 씁니다 (행과 열 번호는 1부터입니다. 병합에 가려지는 칸은 `""`로 비워 둡니다).

```tlide
#include <std/stddef>
#include <std/styles>

slide {
    put table {
        x = 80px; y = 120px; width = 1120px; height = 240px;
        cells = [
            ["이름", "1분기", "2분기"],
            ["서울", "10", bold "20"],
            ["합계", "", ""]
        ];
        header_rows = 1;
        banded_rows = true;
        cell(3, 2) { col_span = 2; fill = hex(FFE699); }
    }
}
```

차트는 데이터를 코드에 적습니다. `kind`는 `column`(세로 막대), `bar`(가로 막대), `line`, `area`, `pie`, `scatter`이고, `values`는 계열마다 한 줄입니다. PowerPoint에서는 데이터를 고칠 수 있는 차트가 되고, HTML, PDF, PNG에서는 같은 모양으로 직접 그립니다. 산점도는 `categories` 대신 `xs`에 점의 x 값을 적습니다.

```tlide
#include <std/stddef>

slide {
    put chart {
        kind = column;
        x = 80px; y = 120px; width = 700px; height = 420px;
        title = "분기별 매출"; x_title = "분기"; y_title = "억 원";
        categories = ["1분기", "2분기", "3분기"];
        names = ["서울", "부산"];
        values = [[10, 20, 15], [8, 12, 9]];
        data_labels = true;
        alt = "분기별 매출 막대 차트";
    }
}
```

수식은 LaTeX로 작성합니다(문자열 안에서는 `\`를 `\\`로 적습니다). PowerPoint에서는 편집 가능한 PowerPoint 수식으로 변환되고, HTML, PDF, PNG에서는 KaTeX로 렌더링됩니다. LaTeX 문법이 틀리면 항상 오류이며, PowerPoint 수식으로 변환할 수 없는 명령(`\xrightarrow` 등)은 `pptx`를 만드는 경우에만 오류로 처리됩니다.

```tlide
#include <std/stddef>

slide {
    put math {
        latex = "\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}";
        x = 80px; y = 200px; width = 800px; height = 160px;
        font_size = 48px;
    }
}
```

더 많은 예제는 [`examples/`](examples)에서 확인할 수 있습니다.

## 설치

[Releases](https://github.com/typeulli/templide/releases)에서 운영체제에 맞는 설치 파일을 내려받습니다. 직접 빌드하려면 [소스에서 빌드](#소스에서-빌드)를 참고하세요.

| 운영체제 | 파일 |
|---|---|
| Windows 10/11 (x64) | `templide_<버전>_x64-setup.exe` |
| Windows 10/11 (32비트) | `templide_<버전>_x86-setup.exe` |
| Windows 11 (ARM) | `templide_<버전>_arm64-setup.exe` |
| Linux (Debian, Ubuntu 22.04 이상) | `templide_<버전>_amd64.deb` |
| macOS (Apple Silicon, Intel) | `templide_<버전>_universal.dmg` |

- **Windows**: 설치할 때 `나만` 또는 `모든 사용자`를 선택합니다. `모든 사용자`를 선택하면 `C:\Program Files\templide\`에 설치되므로 관리자 권한이 필요합니다.
- **Linux**: 아래 명령을 실행해 설치합니다.
  ```bash
  sudo apt install ./templide_<버전>_amd64.deb
  ```
- **macOS**: 앱에 서명이 없어서 처음 실행할 때 막힐 수 있습니다. 응용 프로그램 폴더로 옮긴 뒤 아래 명령을 실행하거나, 시스템 설정 → 개인정보 보호 및 보안에서 `그래도 열기`를 누르세요.
  ```bash
  xattr -dr com.apple.quarantine /Applications/templide.app
  ```
- 설치가 끝나면 `.tlide` 파일이 templide 편집기와 자동으로 연결됩니다.

## 명령줄 컴파일러

편집기를 열지 않고 설치된 컴파일러로 `.tlide` 파일을 바로 내보낼 수 있습니다. 컴파일러는 다음 경로에 설치됩니다.

- Windows: 설치 폴더의 `templide.exe`
- Linux: `/usr/lib/templide/templide`
- macOS: `templide.app/Contents/Resources/templide`

```text
templide hello.tlide                 # 모든 target을 만든다
templide hello.tlide --target deck   # 지정한 target만 만든다
templide hello.tlide --check         # 파일을 쓰지 않고 오류와 경고만 검사한다
templide hello.tlide --watch         # 문서나 문서가 읽는 파일이 바뀔 때마다 다시 만든다
templide hello.tlide --ir            # 분석 결과(IR)를 출력한다
templide hello.tlide --ast           # 구문 트리(AST)를 출력한다
```

`--target`은 여러 번 쓸 수 있습니다. `--watch`는 Ctrl+C로 끝내며 `--check`와 함께 쓸 수도 있습니다.

종료 코드는 컴파일러와 `templide fmt`, `templide tool access`가 모두 같습니다.

| 코드 | 뜻 |
|---|---|
| 0 | 성공. 경고만 있어도 0입니다 (`--strict`가 없을 때) |
| 1 | 오류가 있습니다. `--strict`면 경고만 있어도 1입니다 |
| 2 | 명령을 잘못 썼거나 파일을 읽거나 쓰지 못했습니다 |

오류와 경고는 기본적으로 `파일:줄:열: error[E2117]: 메시지` 형식(gcc와 동일)으로 출력되며, 도움말이 있으면 다음 줄에 `help:`로 표시됩니다. `--format`으로 출력 형식을 선택할 수 있습니다. 메시지는 영어로만 표시됩니다.

```text
--format text      # 기본값. gcc 같은 한 줄 + help
--format json      # 진단 목록을 JSON으로 (도구가 읽기 좋습니다)
--format github    # GitHub Actions의 ::error file=...:: 주석 (PR 화면에 줄마다 표시됩니다)
--strict           # 경고도 실패로 칩니다 (종료 코드 1)
```

`--check`에는 두 가지 검사를 추가할 수 있습니다. `--fmt`는 `#fmt`가 있는 파일이 해당 설정에 맞게 포맷되었는지 확인하고(그렇지 않으면 경고 `W4001`), `--access`는 접근성 문제(`A0001` ~ `A0008`)도 함께 검사합니다. CI에서는 보통 다음과 같이 사용합니다.

```text
templide deck.tlide --check --fmt --access --strict --format github
```

`type = pdf`인 target은 Chrome이나 Edge(헤드리스 모드)로 슬라이드를 인쇄해 PDF를 만듭니다. 슬라이드 하나가 PDF 한 페이지가 되며, 전환과 애니메이션 없이 모든 개체가 표시되고 숨긴 슬라이드는 제외됩니다. Chrome 또는 Edge가 설치되어 있어야 하며, 다른 위치에 설치되어 있다면 `TEMPLIDE_BROWSER` 환경 변수에 실행 파일 경로를 지정합니다.

`type = png`인 target은 같은 방식으로 슬라이드를 렌더링해 슬라이드마다 PNG 파일 하나를 만듭니다. 렌더링에는 Chrome이나 Edge가 사용되며, 전환과 애니메이션 없이 모든 개체가 표시되고 숨긴 슬라이드는 제외됩니다. `path = "slides.png"`이면 `slides-1.png`, `slides-2.png`, ...가 생성되고 번호는 슬라이드 순서를 따릅니다. 이미지 크기는 슬라이드 크기를 따르며, `scale = 2;`로 배율을 지정할 수 있습니다(0.1 ~ 10). `transparent = true;`로 설정하면 슬라이드 배경을 제외해 투명한 이미지를 만듭니다.

```tlide
target shots { path = "out/slides.png"; type = png; scale = 2; transparent = true; }
```

`#include <std/...>`는 컴파일러 실행 파일과 같은 위치의 `packages` 폴더에서 파일을 찾습니다. `#include "..."`는 현재 `.tlide` 파일이 있는 폴더를 기준으로 찾습니다.

컴파일러는 UTF-8(BOM 포함 가능)과 BOM이 있는 UTF-16으로 저장된 `.tlide`를 읽습니다. 다른 인코딩은 오류로 처리됩니다. 편집기는 EUC-KR 같은 인코딩의 파일도 열고 저장할 수 있습니다. 코드 패널 오른쪽 위의 인코딩 표시에서 `인코딩으로 다시 열기` 또는 `인코딩으로 저장`을 선택할 수 있으며, 컴파일러가 읽지 못하는 파일은 `UTF-8`로 저장하면 됩니다.

### 코드 포맷터

```text
templide fmt deck.tlide              # 파일을 #fmt 설정대로 포맷합니다
templide fmt --check deck.tlide      # 파일을 쓰지 않고 포맷이 필요한지만 확인합니다 (필요하면 종료 코드 1)
templide fmt --stdout deck.tlide     # 결과를 화면에 출력합니다
```

설정은 `.tlide` 안의 `#fmt` 줄에 적고, 줄 하나에 옵션 하나입니다. 위치는 아무 데나 되고 (블록 안도 됩니다), **`#fmt`가 하나도 없는 파일은 고치지 않습니다.** 모르는 옵션이나 잘못된 값은 경고입니다. 포맷터는 코드를 다시 읽어 토큰이 그대로인지 확인하고, 다르면 파일을 건드리지 않고 오류(`E4001`, `E4002`)로 끝냅니다. BOM과 UTF-16 같은 인코딩은 그대로 유지합니다.

```tlide
#fmt indent_style space
#fmt indent_size 4
#fmt brace_style same_line
#fmt hex_case upper
```

| 옵션 | 값 (선택지는 첫 번째가 기본값) |
|---|---|
| `indent_style` | `space`, `tab` |
| `indent_size` | 1 ~ 16 (4) |
| `brace_style` | `same_line`, `next_line` (여러 줄 블록의 `{` 자리) |
| `short_blocks` | `keep`, `expand` (한 줄로 작성한 블록을 유지하거나 모두 펼침) |
| `inline_max_statements` | 한 줄 블록에 둘 수 있는 문장 수 (0 ~ 100, 기본 0은 제한 없음) |
| `empty_blocks` | `space`(`{ }`), `tight`(`{}`) |
| `inline_brace_space` | `true`(`{ a; }`), `false`(`{a;}`) (기본 `true`) |
| `max_line_length` | 한 줄 블록이 이보다 길면 펼침 (0 ~ 500, 기본 120, 0은 제한 없음) |
| `space_around_assign`, `space_around_operators`, `space_after_comma` | `true`, `false` (기본 `true`) |
| `space_inside_parens` | `true`, `false` (기본 `false`) |
| `max_blank_lines` | 연속해서 허용할 빈 줄 수 (0 ~ 10, 기본 1) |
| `blank_lines_before_slide`, `blank_lines_after_includes` | `keep`(현재 상태 유지) 또는 빈 줄 수 (0 ~ 10) |
| `sort_properties`, `sort_includes` | `keep`, `alphabetical` |
| `hex_case` | `keep`, `upper`, `lower` |
| `final_newline` | `true`, `false` (기본 `true`) |
| `line_ending` | `keep`, `lf`, `crlf` |
| `comment_gap` | 코드와 뒤따르는 주석 사이의 공백 수 (1 ~ 8, 기본 1) |

편집기에서 `#fmt `를 입력하면 옵션 이름과 값이 자동 완성됩니다. 줄을 직접 작성하지 않고 설정하려면 설정 창(로고를 누르면 열립니다)의 `포맷` 페이지를 사용하세요. 이 페이지에는 현재 문서의 설정이 표시되며, 값을 바꾸면 해당 문서의 `#fmt` 줄이 추가되거나 수정됩니다. 되돌리기 단추는 해당 옵션의 줄을 삭제해 기본값으로 되돌립니다(`#fmt`가 그 줄 하나뿐이면 기본값을 다시 적습니다). 모든 변경은 실행 취소로 되돌릴 수 있습니다. 문서에 `#fmt`가 없으면 `기본 설정 넣기` 단추로 모든 옵션의 기본값을 한 번에 추가할 수 있습니다. 슬라이드 순서는 코드에 적힌 순서를 따르므로 포맷터는 슬라이드 순서를 바꾸지 않습니다.

### 접근성 검사

```text
templide tool access deck.tlide                  # 접근성 문제만 검사합니다
templide tool access deck.tlide --strict         # 경고도 실패로
```

다음 항목을 검사합니다. 편집기의 `접근성` 탭에서도 같은 검사 결과를 확인할 수 있습니다.

| 코드 | 수준 | 내용 |
|---|---|---|
| `A0001` | 오류 | 이미지, 비디오, 오디오, 차트에 `alt`가 없습니다. 장식용 콘텐츠라면 `alt = ""`로 표시합니다 |
| `A0002` | 경고 | 글자와 배경의 명암비가 낮습니다 |
| `A0003` | 경고 | 12pt보다 작은 글자입니다 |
| `A0004` | 경고 | 제목이 없는 슬라이드입니다 (숨긴 슬라이드는 제외) |
| `A0005` | 경고 | 읽는 순서(코드 순서)가 화면의 위아래 순서와 다릅니다 |
| `A0006` | 정보 | 한 슬라이드에 애니메이션이 많습니다 |
| `A0007` | 경고 | 표에 머리글 행이나 열이 없습니다 |
| `A0008` | 정보 | 글이 없는 도형, 선, 연결선, 자유형, 배경 흐림에 `alt`가 없습니다 |

## 소스에서 빌드

### 빌드 요구 사항

- CMake 4.3 이상
- Ninja
- C++20 컴파일러
  - Windows에서는 MinGW-w64를 사용할 수 있습니다. CLion에 포함된 컴파일러도 사용할 수 있습니다.
- Node.js와 npm
- Rust (stable)
  - Windows: Visual Studio C++ 빌드 도구
  - Linux: [Tauri에서 요구하는 패키지](https://v2.tauri.app/start/prerequisites/)
- Python 3
- 인터넷 연결: 컴파일러를 처음 빌드할 때 수식을 그리는 KaTeX를 npm 레지스트리에서 받아 `libs/katex`에 둡니다(`tools/fetch_katex.cmake`). 이미 받은 뒤에는 필요하지 않습니다.

### 설치 파일 만들기

```bash
python build.py                                   # 현재 환경용으로 빌드
python build.py --target i686-pc-windows-msvc     # 다른 타깃용으로 빌드
python build.py --target universal-apple-darwin   # Intel + Apple Silicon용으로 빌드
python build.py --fast                            # 마지막 단계가 빠른 thin LTO 빌드 (설치 파일이 조금 크고 이름 뒤에 -fast가 붙습니다)
```

빌드 스크립트는 컴파일러(`cmake-build-release`), 오픈소스 라이선스 목록, 편집기를 차례로 빌드한 뒤 설치 파일을 `installer/`에 만듭니다. Windows에서는 NSIS 설치 파일, Linux에서는 `.deb`, macOS에서는 `.dmg`가 생성됩니다.

### 릴리스

`v1.2.0`처럼 `v`로 시작하는 태그를 GitHub에 올리면 GitHub Actions(`.github/workflows/release.yml`)가 설치 파일 다섯 개를 빌드해 해당 태그의 Release에 업로드합니다. 릴리스 노트는 `CHANGELOG.md`에서 해당 태그의 항목(`v1.2.0`이면 `## [1.2.0]`)을 가져옵니다. 따라서 태그를 올리기 전에 CHANGELOG에 항목을 작성하고 커밋해야 합니다. 항목이 없으면 GitHub가 이전 태그 이후의 변경 사항을 바탕으로 노트를 생성합니다. 태그 이름에 `-`가 포함되면(`v1.2.0-beta.1` 등) 시험판으로 표시됩니다.

```bash
git tag v1.2.0
git push origin v1.2.0
```

### 개발

```bash
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
python tools/gen_licenses.py

cd editor
npm install
npm run tauri dev        # 화면을 수정하면 변경 사항이 바로 반영된다
npm run build:fast       # 빠른 실행 파일 (target/fast)
npm run build            # release 실행 파일
```

컴파일러를 수정한 뒤에는 테스트를 실행해 기존 동작이 유지되는지 확인합니다. Python 3.10 이상이 필요합니다.

```bash
python test/run.py                 # 모든 케이스
python test/run.py table png       # 이름에 table이나 png가 든 케이스만
python test/run.py --update 이름    # 의도한 변경이면 기대 결과 파일을 다시 쓴다
```

컴파일러는 `cmake-build-release`에서 찾습니다. 다른 컴파일러를 사용하려면 `--exe 경로`(또는 `TEMPLIDE_EXE`)로 경로를 지정하세요. 테스트 케이스를 추가하고 기대 결과 파일을 수정하는 방법은 `test/run.py` 맨 위의 설명을 참고하세요.

개발 버전 편집기는 저장소의 `cmake-build-release/templide(.exe)`를 컴파일러로 사용합니다. 다른 컴파일러를 사용하려면 `TEMPLIDE_EXE` 환경 변수에 경로를 지정하세요. 편집기의 설정 창(로고를 누르면 열립니다)에서도 컴파일러 경로를 지정할 수 있으며, 설정에 지정한 경로가 `TEMPLIDE_EXE`보다 우선합니다. 설정 창에서는 언어(한국어, English), 단축키, 코드 편집기 테마도 변경할 수 있습니다.

## 구조

| 폴더 | 내용 |
|---|---|
| `frontend/` | 렉서와 파서 (`.tlide` → AST) |
| `middleend/` | 의미 분석, 템플릿 펼치기, 단위 변환, 진단(오류 코드), 접근성 검사, LaTeX 수식 (AST → IR) |
| `format/` | 코드 포맷터 (`templide fmt`) |
| `backend/` | PPTX(차트와 수식 포함), HTML, PDF, PNG 출력 |
| `importer/` | PowerPoint 파일(`.pptx`) 가져오기 (→ `.tlide`, `.tasset`) |
| `server/` | 편집기와 통신하는 언어 서버 (`templide --serve`) |
| `packages/std/` | 표준 라이브러리 (`stddef`, `styles`, 위젯) |
| `libs/` | HTML, PDF, PNG 출력에 사용되는 `templide.js`, reveal.js, KaTeX(빌드할 때 받아 옴) |
| `editor/` | 편집기 (Tauri + React + Monaco) |
| `examples/` | 예제 발표 자료 |
| `test/` | 컴파일러의 스냅샷 테스트와 오류/경고 테스트 (`python test/run.py`) |
| `tools/` | 버전 갱신, 라이선스 목록과 `templide.js` 데이터 생성, 전환·무늬 보정 도구 |
| `assets/` | 아이콘과 설치 화면 이미지 |

## 라이선스

[MIT License](LICENSE)를 따릅니다. 함께 사용하는 오픈소스 라이브러리의 라이선스는 편집기의 설정 창(로고를 누르면 열립니다) → `정보` → `오픈소스 라이선스`에서 확인할 수 있습니다.

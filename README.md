<p align="center">
  <img src="assets/icon/templide.svg" width="128" alt="templide">
</p>

<h1 align="center">templide</h1>

<p align="center">A markup-based presentation language</p>

templide는 `.tlide` 파일에 발표 자료를 작성하고, 이를 PowerPoint(`.pptx`)나 웹 페이지(`.html`)로 내보낼 수 있는 발표 자료 제작 도구입니다.

스타일이나 템플릿을 한 번 정의해 두면 여러 슬라이드에서 재사용할 수 있어 비슷한 내용을 일일이 반복해서 작성할 필요가 없습니다. 편집기에서는 캔버스에서 요소를 직접 옮기거나 크기를 조절할 수도 있으며, 이렇게 바꾼 내용은 코드에 바로 반영됩니다.

## 특징

- **코드로 작성하는 슬라이드**: 스타일, 템플릿, 변수, `if`/`for`, f-string 등을 사용해 반복되는 내용을 한 곳에서 관리할 수 있습니다.
- **PowerPoint와 HTML로 내보내기**: 하나의 `.tlide` 파일에서 PowerPoint 파일이나 웹 페이지를 만들 수 있습니다. HTML은 하나의 파일로 만들거나 폴더 형태로 내보낼 수 있습니다.
- **다양한 PowerPoint 기능**: 화면 전환과 애니메이션, 도형, 이미지, 비디오와 오디오, 하이퍼링크, 실행 설정, 발표자 메모, 숨긴 슬라이드 등을 지원합니다.
- **코드와 캔버스를 함께 사용하는 편집기**: 캔버스에서 요소를 끌어 옮기거나 크기를 바꾸면 코드가 자동으로 수정되고, 반대로 코드를 수정하면 캔버스에도 바로 반영됩니다. 오류와 경고는 줄 번호와 함께 `문제` 탭에서 확인할 수 있습니다.
- **표준 라이브러리**: 자주 사용하는 레이아웃과 위젯 스타일을 `#include`로 가져와 사용할 수 있습니다. flat, material, glassmorphism, neumorphism 등의 스타일을 제공합니다.
- **AI 탭**: 편집기 안의 터미널에서 AI 에이전트를 사용할 수 있습니다. AI 에이전트는 MCP를 통해 현재 열려 있는 문서를 읽고 수정할 수 있습니다.

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

더 큰 예제는 [`examples/`](examples)에서 볼 수 있습니다.

## 설치

[Releases](https://github.com/typeulli/templide/releases)에서 운영체제에 맞는 설치 파일을 받을 수 있습니다. 직접 빌드하려면 아래 [소스에서 빌드](#소스에서-빌드)를 참고하세요.

| 운영체제 | 파일 |
|---|---|
| Windows 10/11 (x64) | `templide_<버전>_x64-setup.exe` |
| Windows 10/11 (32비트) | `templide_<버전>_x86-setup.exe` |
| Windows 11 (ARM) | `templide_<버전>_arm64-setup.exe` |
| Linux (Debian, Ubuntu 22.04 이상) | `templide_<버전>_amd64.deb` |
| macOS (Apple Silicon, Intel) | `templide_<버전>_universal.dmg` |

- **Windows**: 설치 과정에서 `나만` 또는 `모든 사용자`를 선택할 수 있습니다. `모든 사용자`로 설치하면 `C:\Program Files\templide\`에 설치되며 관리자 권한이 필요합니다.
- **Linux**: 다음 명령으로 설치할 수 있습니다.
  ```bash
  sudo apt install ./templide_<버전>_amd64.deb
  ```
- **macOS**: 현재 배포되는 앱은 서명되지 않았기 때문에 처음 실행할 때 macOS가 실행을 막을 수 있습니다. 이 경우 응용 프로그램 폴더로 옮긴 뒤 다음 명령을 실행하거나, 시스템 설정 → 개인정보 보호 및 보안에서 `그래도 열기`를 선택하면 됩니다.
  ```bash
  xattr -dr com.apple.quarantine /Applications/templide.app
  ```
- 설치가 끝나면 `.tlide` 파일이 templide 편집기와 자동으로 연결됩니다.

## 명령줄 컴파일러

설치된 컴파일러를 이용하면 편집기를 열지 않고도 `.tlide` 파일을 바로 내보낼 수 있습니다.

컴파일러의 위치는 다음과 같습니다.

- Windows: 설치 폴더의 `templide.exe`
- Linux: `/usr/lib/templide/templide`
- macOS: `templide.app/Contents/Resources/templide`

```text
templide hello.tlide                 # 모든 target을 만든다
templide hello.tlide --target deck   # 지정한 target만 만든다
templide hello.tlide --ir            # 분석 결과(IR)를 출력한다
templide hello.tlide --ast           # 구문 트리(AST)를 출력한다
```

`--target`은 여러 번 지정할 수도 있습니다.

`#include <std/...>`는 컴파일러 옆의 `packages` 폴더에서 파일을 찾고, `#include "..."`는 현재 `.tlide` 파일이 있는 폴더를 기준으로 파일을 찾습니다.

## 소스에서 빌드

### 필요한 것

- CMake 4.3 이상
- Ninja
- C++20 컴파일러
  - Windows에서는 MinGW-w64를 사용할 수 있습니다. CLion에 포함된 컴파일러도 사용할 수 있습니다.
- Node.js와 npm
- Rust (stable)
  - Windows: Visual Studio C++ 빌드 도구
  - Linux: [Tauri에서 요구하는 패키지](https://v2.tauri.app/start/prerequisites/)
- Python 3

### 설치 파일 만들기

```bash
python build.py                                   # 현재 컴퓨터용으로 빌드
python build.py --target i686-pc-windows-msvc     # 다른 타깃으로 빌드
python build.py --target universal-apple-darwin   # Intel + Apple Silicon
```

빌드 스크립트는 컴파일러(`cmake-build-release`), 오픈소스 라이선스 목록, 편집기를 차례로 빌드한 뒤 설치 파일을 `installer/`에 만듭니다.

Windows에서는 NSIS 설치 파일, Linux에서는 `.deb`, macOS에서는 `.dmg` 파일이 생성됩니다.

### 릴리스

`v1.2.0`처럼 `v`로 시작하는 태그를 GitHub에 올리면 GitHub Actions(`.github/workflows/release.yml`)가 자동으로 다섯 가지 설치 파일을 빌드해 해당 태그의 Release에 추가합니다.

릴리스 노트는 GitHub가 이전 태그 이후의 변경 사항을 바탕으로 자동으로 생성합니다. 태그 이름에 `-`가 들어가면(`v1.2.0-beta.1` 등) 해당 Release는 시험판으로 표시됩니다.

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
npm run tauri dev        # 화면을 수정하면 바로 반영된다
npm run build:fast       # 빠른 실행 파일 (target/fast)
npm run build            # release 실행 파일
```

개발 중인 편집기는 저장소의 `cmake-build-release/templide(.exe)`를 컴파일러로 사용합니다.

다른 컴파일러를 사용하려면 `TEMPLIDE_EXE` 환경 변수에 컴파일러의 경로를 지정하면 됩니다.

## 구조

| 폴더 | 내용 |
|---|---|
| `frontend/` | 렉서와 파서 (`.tlide` → AST) |
| `middleend/` | 의미 분석, 템플릿 펼치기, 단위 변환 (AST → IR) |
| `backend/` | PPTX, HTML 출력 |
| `server/` | 편집기와 통신하는 언어 서버 (`templide --serve`) |
| `packages/std/` | 표준 라이브러리 (`stddef`, 레이아웃, 위젯) |
| `libs/` | HTML 출력에 사용되는 `templide.js`와 reveal.js |
| `editor/` | 편집기 (Tauri + React + Monaco) |
| `tools/` | 라이선스 목록 생성, 전환 효과 보정 도구 |
| `assets/` | 아이콘과 설치 화면 이미지 |

## 라이선스

templide는 [MIT License](LICENSE)로 배포됩니다.

함께 사용하는 오픈소스 라이브러리의 라이선스는 편집기의 로고 메뉴 → `오픈소스 라이선스`에서 확인할 수 있습니다.
```
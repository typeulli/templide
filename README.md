<p align="center">
  <img src="assets/icon/templide.svg" width="128" alt="templide">
</p>

<h1 align="center">templide</h1>

<p align="center">A markup-based presentation language</p>

templide는 `.tlide` 파일에 발표 자료를 쓰고 PowerPoint(`.pptx`)나 웹 페이지(`.html`)로 내보내는 도구입니다.

스타일과 템플릿을 한 번 정의해 두면 여러 슬라이드에서 다시 쓸 수 있어서, 비슷한 슬라이드를 계속 복사해 붙일 필요가 없습니다. 변수, `if`/`for`, f-string도 쓸 수 있습니다. HTML은 파일 하나로도, 폴더로도 내보낼 수 있습니다. PowerPoint 쪽은 화면 전환과 애니메이션, 도형, 이미지, 비디오와 오디오, 하이퍼링크, 발표자 메모, 숨긴 슬라이드 등을 지원합니다.

편집기는 코드와 캔버스를 나란히 보여 줍니다. 캔버스에서 요소를 옮기거나 크기를 바꾸면 코드가 고쳐지고, 코드를 고치면 캔버스에 바로 반영됩니다. 오류와 경고는 `문제` 탭에 줄 번호와 함께 나옵니다. 편집기 안의 터미널에서는 AI 에이전트를 실행할 수 있고, 에이전트는 MCP로 지금 열려 있는 문서를 읽고 고칩니다.

`#include <std/...>`로 불러 쓰는 표준 라이브러리에는 레이아웃과 flat, material, glassmorphism, neumorphism 같은 위젯 스타일이 들어 있습니다.

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

[Releases](https://github.com/typeulli/templide/releases)에서 운영체제에 맞는 설치 파일을 받습니다. 직접 빌드하려면 [소스에서 빌드](#소스에서-빌드)를 보세요.

| 운영체제 | 파일 |
|---|---|
| Windows 10/11 (x64) | `templide_<버전>_x64-setup.exe` |
| Windows 10/11 (32비트) | `templide_<버전>_x86-setup.exe` |
| Windows 11 (ARM) | `templide_<버전>_arm64-setup.exe` |
| Linux (Debian, Ubuntu 22.04 이상) | `templide_<버전>_amd64.deb` |
| macOS (Apple Silicon, Intel) | `templide_<버전>_universal.dmg` |

- **Windows**: 설치할 때 `나만`과 `모든 사용자` 중에서 고릅니다. `모든 사용자`는 `C:\Program Files\templide\`에 설치하므로 관리자 권한이 필요합니다.
- **Linux**: 아래 명령으로 설치합니다.
  ```bash
  sudo apt install ./templide_<버전>_amd64.deb
  ```
- **macOS**: 앱에 서명이 없어서 처음 실행할 때 막힐 수 있습니다. 응용 프로그램 폴더로 옮긴 뒤 아래 명령을 실행하거나, 시스템 설정 → 개인정보 보호 및 보안에서 `그래도 열기`를 누르세요.
  ```bash
  xattr -dr com.apple.quarantine /Applications/templide.app
  ```
- 설치가 끝나면 `.tlide` 파일이 templide 편집기와 자동으로 연결됩니다.

## 명령줄 컴파일러

편집기를 열지 않고 설치된 컴파일러로 `.tlide` 파일을 바로 내보낼 수 있습니다. 컴파일러는 다음 위치에 있습니다.

- Windows: 설치 폴더의 `templide.exe`
- Linux: `/usr/lib/templide/templide`
- macOS: `templide.app/Contents/Resources/templide`

```text
templide hello.tlide                 # 모든 target을 만든다
templide hello.tlide --target deck   # 지정한 target만 만든다
templide hello.tlide --ir            # 분석 결과(IR)를 출력한다
templide hello.tlide --ast           # 구문 트리(AST)를 출력한다
```

`--target`은 여러 번 쓸 수 있습니다.

`#include <std/...>`는 컴파일러 옆의 `packages` 폴더에서, `#include "..."`는 `.tlide` 파일이 있는 폴더에서 파일을 찾습니다.

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

빌드 스크립트는 컴파일러(`cmake-build-release`), 오픈소스 라이선스 목록, 편집기를 차례로 빌드하고 설치 파일을 `installer/`에 만듭니다. Windows는 NSIS 설치 파일, Linux는 `.deb`, macOS는 `.dmg`가 나옵니다.

### 릴리스

`v1.2.0`처럼 `v`로 시작하는 태그를 GitHub에 올리면 GitHub Actions(`.github/workflows/release.yml`)가 설치 파일 다섯 개를 빌드해 해당 태그의 Release에 올립니다. 릴리스 노트는 이전 태그 이후의 변경 사항으로 GitHub가 만들고, 태그 이름에 `-`가 있으면(`v1.2.0-beta.1` 등) 시험판으로 표시됩니다.

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

개발 중인 편집기는 저장소의 `cmake-build-release/templide(.exe)`를 컴파일러로 씁니다. 다른 컴파일러를 쓰려면 `TEMPLIDE_EXE` 환경 변수에 경로를 지정하세요.

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

[MIT License](LICENSE)입니다. 함께 쓰는 오픈소스 라이브러리의 라이선스는 편집기의 로고 메뉴 → `오픈소스 라이선스`에서 볼 수 있습니다.
<p align="center">
  <img src="assets/icon/templide.svg" width="128" alt="templide">
</p>

<h1 align="center">templide</h1>

<p align="center">A markup-based presentation language</p>

templide는 `.tlide` 언어로 발표 자료를 쓰고 PowerPoint(`.pptx`)나 웹 페이지(`.html`)로 내보내는 도구입니다.
스타일과 템플릿을 한 번 정의해 두면 슬라이드마다 같은 모양을 반복해 쓸 수 있고, 편집기에서는 화면을 직접 끌고 고친 내용이 코드에 그대로 반영됩니다.

## 특징

- **코드로 쓰는 슬라이드**: 스타일, 템플릿, 변수, `if`/`for`, f-string으로 반복되는 모양을 한 곳에서 관리합니다
- **여러 형식으로 내보내기**: 하나의 원본에서 PowerPoint 파일과 웹 페이지(한 파일 또는 폴더)를 만듭니다
- **PowerPoint 기능**: 화면 전환, 애니메이션, 도형, 그림, 비디오와 오디오, 하이퍼링크와 실행 설정, 발표자 메모, 숨긴 슬라이드
- **편집기**: 캔버스에서 끌어 옮기거나 크기를 바꾸면 코드가 바뀌고, 코드를 고치면 캔버스가 바로 바뀝니다. 오류와 경고는 줄 번호와 함께 '문제' 탭에 모입니다
- **표준 라이브러리**: 레이아웃과 위젯 스타일(flat, material, glassmorphism, neumorphism 등)을 `#include`로 가져다 씁니다
- **AI 탭**: 편집기 안의 터미널에서 AI 에이전트가 MCP로 열린 문서를 읽고 고칩니다

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

더 큰 예제는 [`examples/`](examples)에 있습니다.

## 설치

[Releases](https://github.com/typeulli/templide/releases)에서 운영체제에 맞는 파일을 받습니다. 직접 만들려면 아래 [소스에서 빌드](#소스에서-빌드)를 보세요.

| 운영체제 | 파일 |
|---|---|
| Windows 10/11 (x64) | `templide_<버전>_x64-setup.exe` |
| Windows 10/11 (32비트) | `templide_<버전>_x86-setup.exe` |
| Windows 11 (ARM) | `templide_<버전>_arm64-setup.exe` |
| Linux (Debian, Ubuntu 22.04 이상) | `templide_<버전>_amd64.deb` |
| macOS (Apple Silicon, Intel) | `templide_<버전>_universal.dmg` |

- Windows: 설치할 때 '나만' 또는 '모든 사용자'를 고를 수 있습니다. '모든 사용자'는 `C:\Program Files\templide`에 설치되며 관리자 권한이 필요합니다
- Linux: `sudo apt install ./templide_<버전>_amd64.deb`로 설치합니다
- macOS: 서명하지 않은 앱이라 처음 열 때 막힐 수 있습니다. 응용 프로그램 폴더로 옮긴 뒤 `xattr -dr com.apple.quarantine /Applications/templide.app`을 실행하거나, 시스템 설정 → 개인정보 보호 및 보안에서 '그래도 열기'를 누릅니다
- 설치하면 `.tlide` 파일이 편집기와 연결됩니다

## 명령줄 컴파일러

설치된 컴파일러로 편집기 없이 바로 내보낼 수 있습니다. 위치는 Windows가 설치 폴더의 `templide.exe`, Linux가 `/usr/lib/templide/templide`, macOS가 `templide.app/Contents/Resources/templide`입니다.

```
templide hello.tlide                 # 모든 target을 만든다
templide hello.tlide --target deck   # 고른 target만 만든다 (여러 번 쓸 수 있다)
templide hello.tlide --ir            # 분석 결과(IR)를 출력한다
templide hello.tlide --ast           # 구문 트리(AST)를 출력한다
```

`#include <std/...>`는 컴파일러 옆의 `packages` 폴더에서, `#include "..."`는 그 `.tlide` 파일이 있는 폴더에서 찾습니다.

## 소스에서 빌드

### 필요한 것

- CMake 4.3 이상, Ninja, C++20 컴파일러 (Windows는 MinGW-w64. CLion에 들어 있는 것을 써도 됩니다)
- Node.js와 npm
- Rust (stable). Windows는 Visual Studio C++ 빌드 도구, Linux는 [Tauri가 요구하는 패키지](https://v2.tauri.app/start/prerequisites/)
- Python 3

### 설치 파일 만들기

```
python build.py                                   # 이 컴퓨터용
python build.py --target i686-pc-windows-msvc     # 다른 타깃 (Rust 타깃 이름)
python build.py --target universal-apple-darwin   # macOS Apple Silicon + Intel
```

컴파일러(`cmake-build-release`), 오픈소스 라이선스 목록, 편집기를 차례로 빌드하고 설치 파일(Windows는 NSIS, Linux는 .deb, macOS는 .dmg)을 `installer/`에 만듭니다.

### 릴리스

`v1.2.0`처럼 `v`로 시작하는 태그를 올리면 GitHub Actions(`.github/workflows/release.yml`)가 다섯 가지 설치 파일을 빌드해 그 태그의 릴리스에 붙입니다. 릴리스 노트는 GitHub가 이전 태그 이후의 변경으로 만들고, 태그에 `-`가 있으면(`v1.2.0-beta.1`) 시험판으로 올립니다.

```
git tag v1.2.0
git push origin v1.2.0
```

### 개발

```
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
python tools/gen_licenses.py

cd editor
npm install
npm run tauri dev        # 화면을 고치면 바로 반영된다
npm run build:fast       # 빠른 실행 파일 (target/fast)
npm run build            # release 실행 파일
```

개발 중의 편집기는 저장소의 `cmake-build-release/templide(.exe)`를 컴파일러로 씁니다. 다른 컴파일러를 쓰려면 `TEMPLIDE_EXE` 환경 변수에 경로를 넣습니다.

## 구조

| 폴더 | 내용 |
|---|---|
| `frontend/` | 렉서와 파서 (`.tlide` → AST) |
| `middleend/` | 의미 분석, 템플릿 펼치기, 단위 변환 (AST → IR) |
| `backend/` | pptx, html 출력 |
| `server/` | 편집기와 대화하는 언어 서버 (`templide --serve`) |
| `packages/std/` | 표준 라이브러리 (`stddef`, 레이아웃, 위젯) |
| `libs/` | html 출력이 쓰는 `templide.js`와 reveal.js |
| `editor/` | 편집기 (Tauri + React + Monaco) |
| `tools/` | 라이선스 목록 생성, 전환 효과 보정 도구 |
| `assets/` | 아이콘과 설치 화면 이미지 |

## 라이선스

[MIT](LICENSE). 함께 쓰는 오픈소스의 라이선스는 편집기의 로고 메뉴 → '오픈소스 라이선스'에서 볼 수 있습니다.

"""컴파일러와 편집기를 release로 빌드하고 설치 파일을 만든다. 저장소 맨 위에서 실행한다.

    python build.py [--target <Rust 타깃>] [--skip-compiler]

--target은 편집기의 Rust 타깃이고, 없으면 이 컴퓨터의 타깃이다. 만드는 설치 파일은 타깃의 운영체제로 정해진다.
- Windows (x86_64-, i686-, aarch64-pc-windows-msvc): NSIS 설치 파일 (*-setup.exe)
- Linux (x86_64-unknown-linux-gnu): .deb
- macOS (universal-apple-darwin): .dmg. 컴파일러도 arm64와 x86_64를 합쳐 빌드한다

1. 컴파일러: cmake-build-release를 빌드한다. 폴더가 없으면 Ninja, Release로 먼저 구성한다.
   --skip-compiler면 이미 빌드된 것을 쓴다 (CI의 Windows는 MSYS2에서 따로 빌드한다)
2. tools/gen_licenses.py로 그 타깃의 오픈소스 라이선스 목록을 만든다
3. 편집기: src-tauri/tauri.bundle.conf.json에 타깃의 설치 파일 형식과 함께 넣을 파일을 더해 tauri build를 실행한다.
   컴파일러, packages, libs는 편집기의 리소스 폴더에 함께 넣는다 (컴파일러가 자기 옆의 packages, libs를 쓴다)

만든 설치 파일은 저장소 맨 위의 installer 로 복사한다.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
COMPILER_BUILD = ROOT / 'cmake-build-release'
EDITOR = ROOT / 'editor'
TAURI = EDITOR / 'src-tauri'
INSTALLER = ROOT / 'installer'

# 운영체제마다 tauri가 만드는 설치 파일 형식과 그 파일
BUNDLES = {'windows': ('nsis', '*-setup.exe'), 'linux': ('deb', '*.deb'), 'macos': ('dmg', '*.dmg')}
# Rust 타깃의 아키텍처 -> conpty 폴더 (editor/src-tauri/conpty/README.md)
CONPTY = {'x86_64': 'x64', 'i686': 'x86', 'aarch64': 'arm64'}


def run(step, command, cwd=ROOT):
    print(f'\n==> {step}: {" ".join(command)}', flush=True)
    # npm, npx는 Windows에서 .cmd라 경로를 찾아 실행한다
    program = shutil.which(command[0])
    if program is None:
        sys.exit(f'{command[0]}을(를) 찾을 수 없습니다. PATH를 확인해 주세요')
    if subprocess.run([program, *command[1:]], cwd=cwd).returncode != 0:
        sys.exit(f'{step} 실패')


def host_target():
    output = subprocess.run(['rustc', '-vV'], capture_output=True, text=True, check=True).stdout
    return next(line.split(':', 1)[1].strip() for line in output.splitlines() if line.startswith('host:'))


def target_os(target):
    if 'windows' in target:
        return 'windows'
    if 'apple' in target:
        return 'macos'
    if 'linux' in target:
        return 'linux'
    sys.exit(f'지원하지 않는 타깃입니다: {target}')


def build_compiler(target):
    if not (COMPILER_BUILD / 'CMakeCache.txt').exists():
        configure = ['cmake', '-S', str(ROOT), '-B', str(COMPILER_BUILD), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release']
        if target == 'universal-apple-darwin':
            configure.append('-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64')
        run('컴파일러 구성', configure)
    run('컴파일러 빌드', ['cmake', '--build', str(COMPILER_BUILD)])


# tauri.bundle.conf.json에 이 타깃의 설치 파일 형식과 리소스를 더한 설정. 경로는 src-tauri 기준이다
def bundle_config(target, system):
    config = json.loads((TAURI / 'tauri.bundle.conf.json').read_text(encoding='utf-8'))
    compiler = 'templide.exe' if system == 'windows' else 'templide'
    if not (COMPILER_BUILD / compiler).exists():
        sys.exit(f'컴파일러가 없습니다: {COMPILER_BUILD / compiler}')
    resources = {
        f'../../cmake-build-release/{compiler}': compiler,
        '../../packages/': 'packages/',
        '../../libs/': 'libs/',
    }
    if system == 'windows':
        # 정적으로 링크하지 않은 MinGW 빌드(CLion)는 winpthread DLL이 컴파일러 옆에 있어야 한다
        if (COMPILER_BUILD / 'libwinpthread-1.dll').exists():
            resources['../../cmake-build-release/libwinpthread-1.dll'] = 'libwinpthread-1.dll'
        arch = CONPTY[target.split('-')[0]]
        resources[f'conpty/{arch}/conpty.dll'] = 'conpty.dll'
        resources[f'conpty/{arch}/OpenConsole.exe'] = 'OpenConsole.exe'
        # 탐색기의 '새로 만들기'가 복사하는 파일 (installer-hooks.nsh)
        resources['templates/new.tlide'] = 'templates/new.tlide'
    config['bundle']['targets'] = [BUNDLES[system][0]]
    config['bundle']['resources'] = resources
    path = TAURI / 'target' / f'bundle.{target}.conf.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding='utf-8')
    return path


def main():
    parser = argparse.ArgumentParser(description='컴파일러와 편집기를 빌드하고 설치 파일을 만든다')
    parser.add_argument('--target', help='편집기의 Rust 타깃 (기본: 이 컴퓨터)')
    parser.add_argument('--skip-compiler', action='store_true', help='이미 빌드된 cmake-build-release의 컴파일러를 쓴다')
    args = parser.parse_args()
    host = host_target()
    target = args.target or host
    system = target_os(target)

    if not args.skip_compiler:
        build_compiler(target)

    if not (EDITOR / 'node_modules').exists():
        run('편집기 패키지 설치', ['npm', 'ci'], EDITOR)
    run('라이선스 목록', [sys.executable, str(ROOT / 'tools' / 'gen_licenses.py'), '--target', target])

    config = bundle_config(target, system)
    command = ['npx', 'tauri', 'build', '--config', str(config)]
    # 이 컴퓨터의 타깃이면 --target을 빼서 target/release를 그대로 쓴다 (평소 빌드와 캐시를 같이 쓴다)
    if target != host:
        command += ['--target', target]
    run('편집기와 설치 파일', command, EDITOR)

    release = TAURI / 'target' / ('release' if target == host else f'{target}/release')
    kind, pattern = BUNDLES[system]
    built = sorted((release / 'bundle' / kind).glob(pattern), key=lambda path: path.stat().st_mtime)
    if not built:
        sys.exit(f'{release / "bundle" / kind}에 설치 파일이 없습니다')
    INSTALLER.mkdir(exist_ok=True)
    installer = INSTALLER / built[-1].name
    shutil.copy2(built[-1], installer)
    print(f'\n설치 파일: {installer}')


if __name__ == '__main__':
    main()

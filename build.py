"""컴파일러와 편집기를 release로 빌드하고 NSIS 설치 파일을 만든다. Windows에서 저장소 맨 위에서 실행한다.

    python build.py

1. 컴파일러: cmake-build-release를 빌드한다. 폴더가 없으면 Ninja, Release로 먼저 구성한다
2. 편집기: tools/gen_licenses.py로 오픈소스 라이선스 목록을 새로 만든 뒤 editor에서 npm run build (화면 빌드 후 release 실행 파일)
3. 설치 파일: tauri bundle에 src-tauri/tauri.nsis.conf.json을 더해 NSIS 설치 파일을 만든다.
   1의 templide.exe, packages, libs를 함께 넣는다

tauri가 editor/src-tauri/target/release/bundle/nsis 에 만든 설치 파일을 저장소 맨 위의 installer 로 복사한다.
"""
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
COMPILER_BUILD = ROOT / 'cmake-build-release'
EDITOR = ROOT / 'editor'
NSIS_OUT = EDITOR / 'src-tauri' / 'target' / 'release' / 'bundle' / 'nsis'
INSTALLER = ROOT / 'installer'


def run(step, command, cwd=ROOT):
    print(f'\n==> {step}: {" ".join(command)}', flush=True)
    # npm, npx는 Windows에서 .cmd라 경로를 찾아 실행한다
    program = shutil.which(command[0])
    if program is None:
        sys.exit(f'{command[0]}을(를) 찾을 수 없습니다. PATH를 확인해 주세요')
    if subprocess.run([program, *command[1:]], cwd=cwd).returncode != 0:
        sys.exit(f'{step} 실패')


def main():
    if not (COMPILER_BUILD / 'CMakeCache.txt').exists():
        run('컴파일러 구성', ['cmake', '-S', str(ROOT), '-B', str(COMPILER_BUILD), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'])
    run('컴파일러 빌드', ['cmake', '--build', str(COMPILER_BUILD)])

    if not (EDITOR / 'node_modules').exists():
        run('편집기 패키지 설치', ['npm', 'ci'], EDITOR)
    run('라이선스 목록', [sys.executable, str(ROOT / 'tools' / 'gen_licenses.py')])
    run('편집기 빌드', ['npm', 'run', 'build'], EDITOR)

    run('설치 파일', ['npx', 'tauri', 'bundle', '--config', 'src-tauri/tauri.nsis.conf.json'], EDITOR)

    installers = sorted(NSIS_OUT.glob('*-setup.exe'), key=lambda path: path.stat().st_mtime)
    if not installers:
        sys.exit(f'{NSIS_OUT}에 설치 파일이 없습니다')
    INSTALLER.mkdir(exist_ok=True)
    installer = INSTALLER / installers[-1].name
    shutil.copy2(installers[-1], installer)
    print(f'\n설치 파일: {installer}')


if __name__ == '__main__':
    main()

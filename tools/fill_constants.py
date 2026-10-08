"""여러 파일에 흩어져 있는 상수(지금은 버전)를 한 번에 바꾼다.

    python tools/fill_constants.py --version 1.2.0

버전은 편집기의 npm 패키지(package.json, package-lock.json), Rust 크레이트(Cargo.toml, Cargo.lock),
tauri 설정(tauri.conf.json)에 있다. 설치 파일 이름과 프로그램 정보는 tauri.conf.json의 버전을 쓴다.
파일의 나머지 부분과 줄 맞춤은 그대로 두고 값만 바꾼다. click이 있어야 한다 (pip install click).

상수를 더하려면 PLACES에 이름과 그 값이 있는 곳들을 적고, main에 같은 이름의 옵션을 더한다.
"""
import pathlib
import re

import click

ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITOR = ROOT / 'editor'
TAURI = EDITOR / 'src-tauri'

# 상수 이름 -> 값이 있는 곳들 (파일, 정규식). 정규식의 첫 그룹과 둘째 그룹 사이가 값이고, 파일마다 정확히 한 번 맞아야 한다
PLACES = {
    'version': [
        (EDITOR / 'package.json', r'(?m)^(  "version": ")[^"]*(")'),
        (EDITOR / 'package-lock.json', r'(?m)^(  "version": ")[^"]*(")'),
        (EDITOR / 'package-lock.json', r'(?m)^(    "": \{\s*"name": "templide-editor",\s*"version": ")[^"]*(")'),
        (TAURI / 'Cargo.toml', r'(?ms)^(\[package\]\s*name = "templide-editor"\s*version = ")[^"]*(")'),
        (TAURI / 'Cargo.lock', r'(?m)^(name = "templide-editor"\s*version = ")[^"]*(")'),
        (TAURI / 'tauri.conf.json', r'(?m)^(  "version": ")[^"]*(")'),
    ],
}

# 1.2.0, 1.2.0-beta.1 (npm, Cargo, tauri가 모두 받는 semver)
VERSION = re.compile(r'\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?')


def fill(name, value):
    """name 상수가 있는 곳을 모두 value로 바꾸고 바뀐 파일들을 돌려준다. 한 곳이라도 찾지 못하면 아무 파일도 쓰지 않는다"""
    texts = {}
    for path, pattern in PLACES[name]:
        # 줄바꿈(LF, CRLF)을 그대로 두려고 newline=''로 읽고 쓴다
        text = texts.get(path)
        if text is None:
            text = path.open(encoding='utf-8', newline='').read()
        text, count = re.subn(pattern, lambda match: match.group(1) + value + match.group(2), text)
        if count != 1:
            raise click.ClickException(f'{path.relative_to(ROOT)}에서 {name}의 자리를 {count}개 찾았습니다 (1개여야 합니다)')
        texts[path] = text
    changed = []
    for path, text in texts.items():
        if path.open(encoding='utf-8', newline='').read() != text:
            path.open('w', encoding='utf-8', newline='').write(text)
            changed.append(path)
    return changed


@click.command(help='여러 파일에 흩어져 있는 상수를 한 번에 바꾼다')
@click.option('--version', 'version', metavar='X.Y.Z', help='편집기와 설치 파일의 버전 (예: 1.2.0)')
def main(version):
    values = {'version': version}
    if all(value is None for value in values.values()):
        raise click.UsageError('바꿀 상수를 하나 이상 주세요 (예: --version 1.2.0)')
    if version is not None and VERSION.fullmatch(version) is None:
        raise click.BadParameter(f'{version}은 1.2.0 같은 버전이 아닙니다', param_hint='--version')
    for name, value in values.items():
        if value is None:
            continue
        changed = fill(name, value)
        if changed:
            click.echo(f'{name} = {value}: ' + ', '.join(str(path.relative_to(ROOT)) for path in changed))
        else:
            click.echo(f'{name}: 이미 {value}입니다')


if __name__ == '__main__':
    main()

"""tools/patterns.json을 다시 만든다. Windows와 PowerPoint, Pillow가 있어야 한다.

    python tools/probe_patterns.py <templide.exe>

pattern_kind마다 검은 앞색, 흰 뒷색의 사각형을 한 장에 놓고 PowerPoint로 1280px(슬라이드 1px = 그림 1px)
PNG를 내보낸 뒤, 각 사각형에서 8x8 칸 하나를 읽는다. 행마다 한 byte이고 가장 높은 bit가 왼쪽이다.
사각형의 위치는 모두 8의 배수라 무늬의 시작점이 사각형의 왼쪽 위와 같다.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image

KINDS = """percent_5 percent_10 percent_20 percent_25 percent_30 percent_40 percent_50 percent_60 percent_70 percent_75 percent_80 percent_90
horizontal vertical light_horizontal light_vertical dark_horizontal dark_vertical narrow_horizontal narrow_vertical
dashed_horizontal dashed_vertical cross downward_diagonal upward_diagonal light_downward_diagonal light_upward_diagonal
dark_downward_diagonal dark_upward_diagonal wide_downward_diagonal wide_upward_diagonal dashed_downward_diagonal dashed_upward_diagonal
diagonal_cross small_checker large_checker small_grid large_grid dotted_grid small_confetti large_confetti
horizontal_brick diagonal_brick solid_diamond outlined_diamond dotted_diamond plaid sphere weave divot
shingle wave trellis zigzag""".split()

EXPORT = r'''
$app = New-Object -ComObject PowerPoint.Application
try {
    $presentation = $app.Presentations.Open("{pptx}", $true, $false, $false)
    $presentation.Slides(1).Export("{png}", "PNG", 1280, 720)
    $presentation.Close()
} finally {
    $app.Quit()
}
'''


def position(index):
    return 16 + index % 9 * 136, 16 + index // 9 * 112


def main():
    exe = sys.argv[1]
    work = pathlib.Path(tempfile.mkdtemp())
    lines = ['#include <std/stddef>', 'slide {', '    background = hex(FFFFFF);']
    for i, kind in enumerate(KINDS):
        x, y = position(i)
        lines.append(f'    put shape {{ kind = rect; x = {x}px; y = {y}px; width = 128px; height = 104px; line_color = rgba(0, 0, 0, 0); '
                     f'fill = pattern({kind}, hex(000000), hex(FFFFFF)); }}')
    lines += ['}', 'target out { path = "patterns.pptx"; type = pptx; }']
    (work / 'patterns.tlide').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    subprocess.run([exe, 'patterns.tlide'], cwd=work, check=True, stdout=subprocess.DEVNULL)
    script = EXPORT.replace('{pptx}', str(work / 'patterns.pptx')).replace('{png}', str(work / 'patterns.png'))
    subprocess.run(['powershell', '-NoProfile', '-Command', script], check=True)

    image = Image.open(work / 'patterns.png').convert('L')
    result = {}
    for i, kind in enumerate(KINDS):
        x0, y0 = position(i)
        x0, y0 = x0 + 16, y0 + 16  # 가장자리의 안티에일리어싱을 피한다
        rows = []
        for y in range(8):
            row = 0
            for x in range(8):
                row = row * 2 + (1 if image.getpixel((x0 + x, y0 + y)) < 128 else 0)
            rows.append(row)
        result[kind] = rows
    lines = ['{'] + [f'  "{kind}": {json.dumps(rows)}' + (',' if i < len(result) - 1 else '') for i, (kind, rows) in enumerate(result.items())] + ['}']
    (pathlib.Path(__file__).resolve().parent / 'patterns.json').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

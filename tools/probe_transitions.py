"""PowerPoint과 libs/templide.js의 전환을 같은 진행률에서 비교한다. Windows, PowerPoint, Chrome(또는 Edge)과
numpy, opencv-python, websocket-client가 있어야 한다.

    python tools/probe_transitions.py <templide.exe> 전환 [전환 ...] [--at 0.1,0.5,0.9] [--match] [--out 폴더]

전환은 .tlide에 쓰는 이름(push.left, newsflash 등)이다. 전환마다 옛 슬라이드(빨강)와 새 슬라이드(파랑) 한 쌍을 만들어
PowerPoint로 60fps 동영상을 내보내고, 진행률 u마다 동영상 프레임과 Chrome에서 그 진행률에 멈춰 그린 html 화면의
평균 픽셀 차이(0~255)를 출력한다. 움직임이 없는 화면끼리도 동영상 압축 때문에 1~2 차이가 난다.
둘을 나란히 놓은 그림은 --out 폴더에 쓴다.

--match는 u마다 PowerPoint 프레임과 가장 비슷하게 그려지는 html 시점 p를 찾고 잘 알려진 시간 곡선과 맞춰 본다.
templide.js에 시간 곡선(ease)이 없고 모양이 맞는 전환이라면 이 p(u)가 PowerPoint의 시간 곡선이고,
ease를 넣은 뒤에는 p가 u와 같아야 한다.

html은 컴파일러가 넣은 templide.js 대신 저장소의 libs/templide.js를 쓰므로 고친 뒤 다시 빌드하지 않아도 된다.
"""
import argparse
import atexit
import base64
import json
import pathlib
import shutil
import subprocess
import tempfile
import time
import urllib.request

import cv2
import numpy as np
import websocket

ROOT = pathlib.Path(__file__).resolve().parent.parent
BROWSERS = [r'C:\Program Files\Google\Chrome\Application\chrome.exe', r'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
            r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe']
W, H = 1280, 720
FPS = 60
# 슬라이드 쇼에서 옛 슬라이드 1초, 전환 2초, 새 슬라이드 1초인 쌍이 동영상에서는 조금 늘어난다.
# 전환은 쌍이 시작하고 1.051초 뒤에 시작해 2.083초 동안 이어진다 (push로 잰 값)
START = 1.051
LENGTH = 2.083
# 동영상 하나에 넣는 쌍의 수. 25쌍이 넘는 동영상에서 PowerPoint이 중간 슬라이드를 짧게 내보낸 적이 있다
CHUNK = 10

NO_LINE = 'line_color = rgba(0, 0, 0, 0);'


def shape(kind, x, y, w, h, color):
    return f'    put shape {{ kind = {kind}; x = {x}px; y = {y}px; width = {w}px; height = {h}px; fill = hex({color}); {NO_LINE} }}\n'


# 옛 슬라이드와 새 슬라이드. 표식(80px 사각형)은 슬라이드 안쪽에 두어 움직여도 화면 가장자리에 걸리지 않게 한다
def old_slide(advance):
    return ('slide {\n    background = hex(C83C3C);\n' + advance
            + shape('rect', 160, 100, 80, 80, 'FFFF00') + shape('rect', 1040, 100, 80, 80, '00FF00')
            + shape('rect', 160, 540, 80, 80, '00FFFF') + shape('rect', 1040, 540, 80, 80, 'FF00FF')
            + shape('rightArrow', 440, 260, 400, 200, 'FFFFFF') + shape('rect', 0, 356, 1280, 8, '400000') + shape('rect', 636, 0, 8, 720, '400000') + '}\n')


def new_slide(transition, advance):
    return ('slide {\n    background = hex(3C50C8);\n' + advance + f'    transition {transition};\n'
            + shape('rect', 160, 100, 80, 80, 'FF8000') + shape('rect', 1040, 100, 80, 80, '0080FF')
            + shape('rect', 160, 540, 80, 80, 'FF0080') + shape('rect', 1040, 540, 80, 80, '8000FF')
            + shape('upArrow', 540, 160, 200, 400, 'FFFFFF') + shape('rect', 0, 236, 1280, 8, '000040') + shape('rect', 0, 476, 1280, 8, '000040') + '}\n')


def compile_deck(exe, work, name, transitions, target):
    advance = '    advance_after = 1s;\n' if target == 'pptx' else ''
    source = '#include <std/stddef>\n' + ''.join(old_slide(advance) + new_slide(t + ' 2s', advance) for t in transitions)
    source += f'target out {{ path = "{name}.{target}"; type = {target}; }}\n'
    (work / f'{name}.tlide').write_text(source, encoding='utf-8')
    subprocess.run([exe, f'{name}.tlide'], cwd=work, check=True, stdout=subprocess.DEVNULL)
    return work / f'{name}.{target}'


# ---- PowerPoint

EXPORT = r'''
$app = New-Object -ComObject PowerPoint.Application
try {
    $presentation = $app.Presentations.Open("{pptx}", $true, $false, $false)
    $presentation.CreateVideo("{mp4}", $true, 1, 720, 60, 100)
    # 3은 끝남, 4는 실패다. 시작한 직후에는 0이 나올 수 있다
    $wait = 0
    while ($presentation.CreateVideoStatus -ne 3 -and $presentation.CreateVideoStatus -ne 4 -and $wait -lt 2000) { Start-Sleep -Milliseconds 300; $wait++ }
    $presentation.Close()
} finally {
    $app.Quit()
}
'''


class Video:
    """PowerPoint이 내보낸 동영상. 쌍마다 옛 슬라이드가 다시 나타나는 프레임이 그 쌍의 시작이다"""

    def __init__(self, path):
        self.capture = cv2.VideoCapture(str(path))
        self.starts = []
        first = None
        was_old = False
        index = 0
        while True:
            ok, frame = self.capture.read()
            if not ok:
                break
            small = cv2.resize(frame, (160, 90), interpolation=cv2.INTER_AREA).astype(np.int16)
            if first is None:
                first = small
            is_old = np.abs(small - first).mean() < 1.0
            # 전환 중에 잠깐 옛 슬라이드와 같아 보이는 프레임은 쌍의 시작이 아니다. 쌍 하나는 약 252프레임이다
            if is_old and not was_old and (not self.starts or index - self.starts[-1] > 190):
                self.starts.append(index)
            was_old = is_old
            index += 1

    def frame(self, pair, u):
        index = int(round((self.starts[pair] / FPS + START + u * LENGTH) * FPS))
        self.capture.set(cv2.CAP_PROP_POS_FRAMES, index)
        ok, frame = self.capture.read()
        return frame


def export_video(exe, work, name, transitions):
    pptx = compile_deck(exe, work, name, transitions, 'pptx')
    mp4 = pptx.with_suffix('.mp4')
    script = EXPORT.replace('{pptx}', str(pptx)).replace('{mp4}', str(mp4))
    subprocess.run(['powershell', '-NoProfile', '-Command', script], check=True)
    video = Video(mp4)
    if len(video.starts) != len(transitions):
        raise SystemExit(f'{mp4}: found {len(video.starts)} slide pairs, expected {len(transitions)}')
    return video


# ---- Chrome

# 시간을 멈추고 requestAnimationFrame을 모아 두었다가 원하는 시각으로 부른다
HOOK = '''<script>
(function () {
    let now = 0;
    const realNow = performance.now.bind(performance);
    const realFrame = window.requestAnimationFrame.bind(window);
    window.__fake = false;
    window.__frames = [];
    performance.now = () => window.__fake ? now : realNow();
    window.requestAnimationFrame = (callback) => {
        if (window.__fake) {
            window.__frames.push(callback);
            return 1;
        }
        return realFrame(callback);
    };
    window.__at = (t) => {
        now = t;
        window.__frames.splice(0).forEach((callback) => callback(t));
    };
})();
</script>
'''

DRAW = '''(() => {
    const sections = document.querySelectorAll('.reveal .slides > section');
    const section = sections[1];
    const stage = section.querySelector(':scope > .tl-slide');
    for (const node of section.querySelectorAll('.tl-transition, .tl-black')) {
        node.remove();
    }
    for (const property of ['opacity', 'transform', 'clipPath', 'visibility', 'zIndex']) {
        stage.style[property] = '';
    }
    // 앞에서 그린 전환이 남긴 콜백은 버린다
    window.__fake = true;
    window.__frames = [];
    window.__at(0);
    Templide.runTransition(%s, sections[0].querySelector(':scope > .tl-slide'), section, stage, %d, %d, () => {});
    window.__at(%f);
    window.__fake = false;
})()'''


class Chrome:
    def __init__(self, page, port=9335):
        browser = next((path for path in BROWSERS if pathlib.Path(path).exists()), None)
        if browser is None:
            raise SystemExit('Chrome or Edge not found')
        self.profile = tempfile.mkdtemp(prefix='templide-chrome-')
        self.process = subprocess.Popen([browser, '--headless=new', f'--remote-debugging-port={port}', f'--user-data-dir={self.profile}',
                                         '--hide-scrollbars', '--force-device-scale-factor=1', f'--window-size={W},{H}', 'about:blank'],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        atexit.register(self.close)
        for _ in range(100):
            try:
                targets = json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json'))
                target = next(t for t in targets if t['type'] == 'page')
                break
            except Exception:
                time.sleep(0.1)
        else:
            raise SystemExit('cannot connect to the browser')
        self.socket = websocket.create_connection(target['webSocketDebuggerUrl'], suppress_origin=True)
        self.id = 0
        self.call('Emulation.setDeviceMetricsOverride', width=W, height=H, deviceScaleFactor=1, mobile=False)
        self.call('Page.navigate', url=page.as_uri() + '?templide-static#2')
        for _ in range(200):
            if self.evaluate('document.readyState') == 'complete' and self.evaluate('typeof Templide') == 'object':
                break
            time.sleep(0.05)
        time.sleep(0.3)

    def call(self, method, **params):
        self.id += 1
        self.socket.send(json.dumps({'id': self.id, 'method': method, 'params': params}))
        while True:
            message = json.loads(self.socket.recv())
            if message.get('id') == self.id:
                if 'error' in message:
                    raise RuntimeError(message['error'])
                return message.get('result', {})

    def evaluate(self, expression):
        result = self.call('Runtime.evaluate', expression=expression, returnByValue=True)
        if 'exceptionDetails' in result:
            raise RuntimeError(result['exceptionDetails'])
        return result['result'].get('value')

    def draw(self, transition, p):
        """transition: 'push.left'. 진행률 p에 멈춘 화면"""
        kind, _, option = transition.partition('.')
        spec = {'k': kind, 'd': 2000}
        if option:
            spec['o'] = option
        self.evaluate(DRAW % (json.dumps(spec), W, H, min(p, 0.9999) * 2000))
        data = self.call('Page.captureScreenshot', format='png', clip={'x': 0, 'y': 0, 'width': W, 'height': H, 'scale': 1})['data']
        return cv2.imdecode(np.frombuffer(base64.b64decode(data), np.uint8), cv2.IMREAD_COLOR)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        shutil.rmtree(self.profile, ignore_errors=True)


def html_page(exe, work):
    """옛 슬라이드와 새 슬라이드 한 쌍인 html. templide.js는 저장소의 것으로 바꾸고 runTransition을 밖에 보인다"""
    text = compile_deck(exe, work, 'pair', ['fade'], 'html').read_text(encoding='utf-8')
    start = text.index('<script>/*!\n * templide.js')
    end = text.index('</script>', start)
    code = (ROOT / 'libs' / 'templide.js').read_text(encoding='utf-8').replace('</script', '<\\/script')
    code = code.replace('global.Templide = {mount, version: 1};', 'global.Templide = {mount, version: 1, runTransition};', 1)
    text = text[:start] + '<script>' + code + text[end:]
    text = text.replace('<head>\n', '<head>\n' + HOOK, 1)
    page = work / 'pair_probe.html'
    page.write_text(text, encoding='utf-8')
    return page


# ---- 비교

def difference(a, b):
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def sheet(images, labels, columns=4, scale=0.25):
    w, h = int(W * scale), int(H * scale)
    rows = (len(images) + columns - 1) // columns
    out = np.full((rows * (h + 4), columns * (w + 4), 3), 40, np.uint8)
    for i, (image, label) in enumerate(zip(images, labels)):
        r, c = divmod(i, columns)
        small = cv2.resize(image, (w, h), interpolation=cv2.INTER_AREA)
        cv2.putText(small, label, (4, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
        out[r * (h + 4):r * (h + 4) + h, c * (w + 4):c * (w + 4) + w] = small
    return out


def compare(chrome, video, pair, transition, us, out):
    ppt = [video.frame(pair, u) for u in us]
    mine = [chrome.draw(transition, u) for u in us]
    diffs = [difference(a, b) for a, b in zip(ppt, mine)]
    images, labels = [], []
    for u, a, b, d in zip(us, ppt, mine, diffs):
        images += [a, b]
        labels += [f'ppt u={u:g}', f'html u={u:g} d={d:.1f}']
    cv2.imwrite(str(out / f'{transition}.png'), sheet(images, labels))
    print(f'{transition:24s} ' + ' '.join(f'{d:5.1f}' for d in diffs) + f'   mean {np.mean(diffs):.2f}')


# 시간 곡선 후보. ease는 templide.js의 ease(p, accel, decel)와 같다
def ease(p, accel, decel):
    p = np.asarray(p, float)
    speed = 1 / (1 - accel / 2 - decel / 2)
    q = 1 - p
    return np.where(p < accel, speed * p * p / (2 * max(accel, 1e-9)), np.where(p <= 1 - decel, speed * (p - accel / 2), 1 - speed * q * q / (2 * max(decel, 1e-9))))


CURVES = {
    'linear': lambda u: np.asarray(u, float),
    'ease(0.5, 0.5)': lambda u: ease(u, 0.5, 0.5),
    'sine': lambda u: (1 - np.cos(np.pi * np.asarray(u, float))) / 2,
    'smoothstep': lambda u: np.asarray(u, float) ** 2 * (3 - 2 * np.asarray(u, float)),
    'cubic in-out': lambda u: np.where(np.asarray(u, float) < 0.5, 4 * np.asarray(u, float) ** 3, 1 - 4 * (1 - np.asarray(u, float)) ** 3),
    'quartic in-out': lambda u: np.where(np.asarray(u, float) < 0.5, 8 * np.asarray(u, float) ** 4, 1 - 8 * (1 - np.asarray(u, float)) ** 4),
    'ease-in u^2': lambda u: np.asarray(u, float) ** 2,
    'ease-out 1-(1-u)^2': lambda u: 1 - (1 - np.asarray(u, float)) ** 2,
}


def match(chrome, video, pair, transition, us):
    """u마다 PowerPoint 프레임과 가장 비슷한 html 진행률 p. 0.02 간격으로 찾은 뒤 0.002 간격으로 다듬는다"""
    grid = np.round(np.arange(0, 1.0001, 0.02), 4)
    drawn = {float(p): cv2.resize(chrome.draw(transition, p), (W // 4, H // 4), interpolation=cv2.INTER_AREA) for p in grid}
    found = []
    for u in us:
        target = cv2.resize(video.frame(pair, u), (W // 4, H // 4), interpolation=cv2.INTER_AREA)
        best = min(drawn, key=lambda p: difference(drawn[p], target))
        fine = np.round(np.arange(max(0, best - 0.02), min(1, best + 0.02) + 1e-9, 0.002), 4)
        scores = {float(p): difference(cv2.resize(chrome.draw(transition, p), (W // 4, H // 4), interpolation=cv2.INTER_AREA), target) for p in fine}
        p = min(scores, key=scores.get)
        found.append((u, p, scores[p]))
    print(f'{transition}: u -> p (difference)')
    print('  ' + ' '.join(f'{u:g}->{p:.3f}({d:.1f})' for u, p, d in found))
    u = np.array([f[0] for f in found]); p = np.array([f[1] for f in found])
    fits = sorted((float(np.sqrt(np.mean((curve(u) - p) ** 2))), name) for name, curve in CURVES.items())
    print('  curve rms: ' + ', '.join(f'{name} {rms:.3f}' for rms, name in fits))
    return found


def main():
    parser = argparse.ArgumentParser(description='PowerPoint과 templide.js의 전환 비교')
    parser.add_argument('exe')
    parser.add_argument('transitions', nargs='+')
    parser.add_argument('--at', default='0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9', help='비교할 진행률 u')
    parser.add_argument('--match', action='store_true', help='u마다 가장 비슷한 html 진행률을 찾는다')
    parser.add_argument('--out', help='비교 그림을 쓸 폴더 (없으면 임시 폴더)')
    args = parser.parse_args()
    exe = str(pathlib.Path(args.exe).resolve())
    us = [float(x) for x in args.at.split(',')]
    work = pathlib.Path(tempfile.mkdtemp(prefix='templide-probe-'))
    out = pathlib.Path(args.out) if args.out else work
    out.mkdir(parents=True, exist_ok=True)
    chrome = Chrome(html_page(exe, work))
    for start in range(0, len(args.transitions), CHUNK):
        chunk = args.transitions[start:start + CHUNK]
        video = export_video(exe, work, f'video{start // CHUNK}', chunk)
        for pair, transition in enumerate(chunk):
            if args.match:
                match(chrome, video, pair, transition, us)
            else:
                compare(chrome, video, pair, transition, us, out)
    print(f'pictures: {out}')


if __name__ == '__main__':
    main()

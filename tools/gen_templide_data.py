"""libs/templide.js의 DATA(도형, 애니메이션, 무늬)를 만든다.

    python tools/gen_templide_data.py

- 도형: ECMA-376 Part 1의 presetShapeDefinitions.xml. LibreOffice가 가진 사본을 받아 쓴다
- 애니메이션: backend/pptx_animations.cpp의 효과 노드(PowerPoint가 저장한 XML)를 JSON 나무로 바꾼다
- 무늬: tools/patterns.json (PowerPoint이 1280px로 내보낸 그림에서 뽑은 8x8 비트맵. tools/probe_patterns.py)

templide.js에서 /*@generated-begin*/ 과 /*@generated-end*/ 사이만 바꾼다.
"""
import json
import pathlib
import re
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = ROOT / 'tools'
SHAPES_URL = 'https://raw.githubusercontent.com/LibreOffice/core/master/oox/source/drawingml/customshapes/presetShapeDefinitions.xml'
SHAPES_CACHE = pathlib.Path(tempfile.gettempdir()) / 'templide' / 'presetShapeDefinitions.xml'
A = '{http://schemas.openxmlformats.org/drawingml/2006/main}'
P = '{http://schemas.openxmlformats.org/presentationml/2006/main}'


def number_or_name(text):
    try:
        return int(text)
    except ValueError:
        return text


def formula(text):
    parts = text.split()
    return [parts[0]] + [number_or_name(p) for p in parts[1:]]


# ---- 도형

def shapes():
    if not SHAPES_CACHE.exists():
        SHAPES_CACHE.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(SHAPES_URL, SHAPES_CACHE)
    root = ET.parse(SHAPES_CACHE).getroot()
    result = {}
    for shape in root:
        entry = {}
        av = shape.find(A + 'avLst')
        if av is not None and len(av):
            entry['av'] = [[gd.get('name'), formula(gd.get('fmla'))] for gd in av]
        gd = shape.find(A + 'gdLst')
        if gd is not None and len(gd):
            entry['gd'] = [[g.get('name'), formula(g.get('fmla'))] for g in gd]
        rect = shape.find(A + 'rect')
        if rect is not None:
            entry['rect'] = [number_or_name(rect.get(k)) for k in ('l', 't', 'r', 'b')]
        paths = []
        for path in shape.find(A + 'pathLst'):
            item = {}
            for key in ('w', 'h'):
                if path.get(key) is not None:
                    item[key] = int(path.get(key))
            if path.get('fill') not in (None, 'norm'):
                item['fill'] = path.get('fill')
            if path.get('stroke') == 'false':
                item['stroke'] = False
            commands = []
            for command in path:
                tag = command.tag[len(A):]
                points = [number_or_name(v) for pt in command for v in (pt.get('x'), pt.get('y'))]
                if tag == 'moveTo':
                    commands.append(['M'] + points)
                elif tag == 'lnTo':
                    commands.append(['L'] + points)
                elif tag == 'arcTo':
                    commands.append(['A'] + [number_or_name(command.get(k)) for k in ('wR', 'hR', 'stAng', 'swAng')])
                elif tag == 'quadBezTo':
                    commands.append(['Q'] + points)
                elif tag == 'cubicBezTo':
                    commands.append(['C'] + points)
                elif tag == 'close':
                    commands.append(['Z'])
                else:
                    raise ValueError(tag)
            item['d'] = commands
            paths.append(item)
        entry['paths'] = paths
        result[shape.tag] = entry
    return result


# ---- 애니메이션

def timing(ctn, node):
    """cTn의 시간 속성을 node에 넣는다. 시간은 ms, 비율은 0 ~ 1"""
    dur = ctn.get('dur')
    if dur is not None:
        node['dur'] = None if dur == 'indefinite' else int(dur)
    cond = ctn.find(P + 'stCondLst/' + P + 'cond')
    if cond is not None and cond.get('delay') not in (None, '0'):
        node['delay'] = int(cond.get('delay'))
    for key, name in (('accel', 'accel'), ('decel', 'decel')):
        if ctn.get(key):
            node[name] = int(ctn.get(key)) / 100000
    if ctn.get('autoRev') == '1':
        node['autoRev'] = True
    if ctn.get('repeatCount'):
        node['repeat'] = None if ctn.get('repeatCount') == 'indefinite' else int(ctn.get('repeatCount')) / 1000
    if ctn.get('fill'):
        node['fill'] = ctn.get('fill')
    if ctn.get('tmFilter'):
        node['tmFilter'] = [[float(v) for v in pair.split(',')] for pair in ctn.get('tmFilter').split(';') if pair.strip()]
    iterate = ctn.find(P + 'iterate')
    if iterate is not None:
        pct = iterate.find(P + 'tmPct')
        absolute = iterate.find(P + 'tmAbs')
        node['iterate'] = {'type': iterate.get('type', 'el')}
        if pct is not None:
            node['iterate']['pct'] = int(pct.get('val')) / 100000
        if absolute is not None:
            node['iterate']['ms'] = int(absolute.get('val'))


def value(element):
    """p:val, p:to 안의 값"""
    child = element[0]
    tag = child.tag.split('}')[1]
    if tag == 'strVal':
        return child.get('val')
    if tag == 'fltVal':
        return float(child.get('val'))
    if tag == 'boolVal':
        return child.get('val') == '1'
    if tag == 'intVal':
        return int(child.get('val'))
    if tag == 'clrVal':
        return color(child)
    raise ValueError(tag)


def color(element):
    child = element[0]
    tag = child.tag.split('}')[1]
    if tag == 'schemeClr':
        return {'scheme': child.get('val')}
    if tag == 'srgbClr':
        return '#' + child.get('val')
    if tag == 'prstClr':
        return {'preset': child.get('val')}
    raise ValueError(tag)


def behavior(element, node):
    cbhvr = element.find(P + 'cBhvr')
    timing(cbhvr.find(P + 'cTn'), node)
    names = cbhvr.find(P + 'attrNameLst')
    if names is not None:
        node['attr'] = [name.text for name in names]
    for key in ('additive', 'override'):
        if cbhvr.get(key):
            node[key] = cbhvr.get(key)


def convert(element):
    tag = element.tag[len(P):]
    node = {}
    if tag in ('par', 'seq'):
        node['t'] = tag
        ctn = element.find(P + 'cTn')
        timing(ctn, node)
        children = ctn.find(P + 'childTnLst')
        node['c'] = [convert(child) for child in children] if children is not None else []
        return node
    behavior(element, node)
    if tag == 'set':
        node['t'] = 'set'
        node['to'] = value(element.find(P + 'to'))
    elif tag == 'anim':
        node['t'] = 'anim'
        node['calc'] = element.get('calcmode', 'lin')
        node['vt'] = element.get('valueType', 'num')
        for key in ('by', 'from', 'to'):
            if element.get(key) is not None:
                node[key] = element.get(key)
        to = element.find(P + 'to')
        if to is not None:
            node['to'] = value(to)
        tavs = element.find(P + 'tavLst')
        if tavs is not None:
            node['tav'] = []
            for tav in tavs:
                item = [None if tav.get('tm') == 'indefinite' else int(tav.get('tm', '0')) / 100000]
                val = tav.find(P + 'val')
                item.append(value(val) if val is not None else None)
                if tav.get('fmla'):
                    item.append(tav.get('fmla'))
                node['tav'].append(item)
    elif tag == 'animEffect':
        node['t'] = 'effect'
        node['tr'] = element.get('transition', 'in')
        node['filter'] = element.get('filter', '')
        if element.get('prLst'):
            node['pr'] = element.get('prLst')
    elif tag == 'animScale':
        node['t'] = 'scale'
        for key in ('by', 'from', 'to'):
            child = element.find(P + key)
            if child is not None:
                node[key] = [int(child.get('x')) / 100000, int(child.get('y')) / 100000]
        if element.get('zoomContents') == '1':
            node['zoom'] = True
    elif tag == 'animRot':
        node['t'] = 'rot'
        for key in ('by', 'from', 'to'):
            if element.get(key) is not None:
                node[key] = int(element.get(key)) / 60000
    elif tag == 'animMotion':
        node['t'] = 'motion'
        node['path'] = element.get('path')
        node['origin'] = element.get('origin', 'parent')
    elif tag == 'animClr':
        node['t'] = 'clr'
        node['space'] = element.get('clrSpc', 'rgb')
        node['dir'] = element.get('dir', 'cw')
        by = element.find(P + 'by')
        if by is not None:
            hsl = by.find(P + 'hsl')
            if hsl is not None:
                node['by'] = {'h': int(hsl.get('h')) / 60000, 's': int(hsl.get('s')) / 1000, 'l': int(hsl.get('l')) / 1000}
            else:
                node['by'] = color(by)
        for key in ('from', 'to'):
            child = element.find(P + key)
            if child is not None:
                node[key] = color(child)
    else:
        raise ValueError(tag)
    return node


def animations():
    source = (ROOT / 'backend' / 'pptx_animations.cpp').read_text(encoding='utf-8')
    result = {}
    pattern = re.compile(r'\{"([^"]+)", \{(-?\d+), (true|false), (true|false), R"x\((.*?)\)x"\}\}', re.S)
    for key, duration, builds, background, xml in pattern.findall(source):
        root = ET.fromstring('<r xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
                             'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">' + xml + '</r>')
        effect = convert(root[0])
        entry = {'dur': int(duration), 'c': effect['c']}
        # 글자나 단어마다 차례로 시작하는 효과. bg면 도형 바탕도 함께 움직인다 (bldP의 animBg)
        if 'iterate' in effect:
            entry['it'] = effect['iterate']
        if background == 'true':
            entry['bg'] = 1
        result[key] = entry
    assert len(result) == 286, len(result)
    return result


def main():
    data = {
        'shapes': shapes(),
        'animations': animations(),
        'patterns': json.loads((TOOLS / 'patterns.json').read_text(encoding='utf-8')),
    }
    text = json.dumps(data, ensure_ascii=False, separators=(',', ':'))
    target = ROOT / 'libs' / 'templide.js'
    js = target.read_text(encoding='utf-8')
    begin, end = '/*@generated-begin*/', '/*@generated-end*/'
    start = js.index(begin) + len(begin)
    stop = js.index(end)
    target.write_text(js[:start] + text + js[stop:], encoding='utf-8', newline='\n')
    print(f'shapes {len(data["shapes"])}, animations {len(data["animations"])}, patterns {len(data["patterns"])}, {len(text)} bytes')


if __name__ == '__main__':
    main()

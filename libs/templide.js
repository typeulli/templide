/*!
 * templide.js
 * templide 컴파일러가 만든 덱(JSON)을 브라우저에서 그린다. 화면 맞춤, 넘기기, 발표자 메모 창은 reveal.js가 맡는다.
 * 모듈이 아닌 일반 스크립트라 file://로 연 html에서도 쓸 수 있고, 전역 Templide를 만든다.
 *
 *   Templide.mount(document.querySelector('.reveal'), deck);
 *
 * 도형 정의는 ECMA-376 presetShapeDefinitions.xml, 애니메이션은 PowerPoint가 저장한 효과 노드,
 * 무늬는 PowerPoint 그림에서 뽑은 8x8 비트맵이다. 맨 아래의 DATA는 tools/gen_templide_data.py가 만든다.
 */
(function (global) {
    'use strict';

    const SVG_NS = 'http://www.w3.org/2000/svg';
    const DEG = Math.PI / 180;
    const ANGLE = DEG / 60000; // DrawingML 각도(60000분의 1도) -> 라디안

    let counter = 0;
    const uid = (name) => `tl-${name}-${++counter}`;
    const round = (value) => Math.round(value * 1000) / 1000;
    const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

    function html(tag, className, style) {
        const node = document.createElement(tag);
        if (className) {
            node.className = className;
        }
        if (style) {
            Object.assign(node.style, style);
        }
        return node;
    }

    function svg(tag, attributes, parent) {
        const node = document.createElementNS(SVG_NS, tag);
        for (const [key, value] of Object.entries(attributes || {})) {
            if (value !== undefined && value !== null) {
                node.setAttribute(key, typeof value === 'number' ? round(value) : value);
            }
        }
        if (parent) {
            parent.appendChild(node);
        }
        return node;
    }

    // ---- 도형 모양 (DrawingML의 guide 수식)

    const BUILTIN_GUIDES = {cd2: 10800000, cd4: 5400000, cd8: 2700000, '3cd4': 16200000, '3cd8': 8100000, '5cd8': 13500000, '7cd8': 18900000};

    function guide(name, vars) {
        if (typeof name === 'number') {
            return name;
        }
        if (name in vars) {
            return vars[name];
        }
        if (name in BUILTIN_GUIDES) {
            return BUILTIN_GUIDES[name];
        }
        const match = /^(wd|hd|ssd)(\d+)$/.exec(name);
        if (match) {
            return (match[1] === 'wd' ? vars.w : match[1] === 'hd' ? vars.h : vars.ss) / Number(match[2]);
        }
        const value = Number(name);
        return Number.isNaN(value) ? 0 : value;
    }

    function formula(parts, vars) {
        const a = (index) => guide(parts[index + 1], vars);
        switch (parts[0]) {
            case 'val': return a(0);
            case '*/': return a(2) === 0 ? 0 : a(0) * a(1) / a(2);
            case '+-': return a(0) + a(1) - a(2);
            case '+/': return a(2) === 0 ? 0 : (a(0) + a(1)) / a(2);
            case '?:': return a(0) > 0 ? a(1) : a(2);
            case 'abs': return Math.abs(a(0));
            case 'at2': return Math.atan2(a(1), a(0)) / ANGLE;
            case 'cat2': return a(0) * Math.cos(Math.atan2(a(2), a(1)));
            case 'sat2': return a(0) * Math.sin(Math.atan2(a(2), a(1)));
            case 'cos': return a(0) * Math.cos(a(1) * ANGLE);
            case 'sin': return a(0) * Math.sin(a(1) * ANGLE);
            case 'tan': return a(0) * Math.tan(a(1) * ANGLE);
            case 'max': return Math.max(a(0), a(1));
            case 'min': return Math.min(a(0), a(1));
            case 'mod': return Math.hypot(a(0), a(1), a(2));
            case 'pin': return a(1) < a(0) ? a(0) : a(1) > a(2) ? a(2) : a(1);
            case 'sqrt': return Math.sqrt(Math.max(0, a(0)));
            default: return 0;
        }
    }

    // 현재 점에서 시작하는 타원 호. 각도는 타원 위 점의 실제 방향이라 매개변수 각으로 바꾸고, 90도 이하의 3차 베지어로 나눈다
    function arcTo(pen, wR, hR, start, sweep) {
        if (wR <= 0 || hR <= 0) {
            return;
        }
        const parameter = (angle) => Math.atan2(wR * Math.sin(angle), hR * Math.cos(angle));
        const t1 = parameter(start);
        let dt = parameter(start + sweep) - t1;
        if (Math.abs(sweep) >= 2 * Math.PI - 1e-9) {
            dt = Math.sign(sweep) * 2 * Math.PI;
        } else {
            while (sweep > 0 && dt < 0) {
                dt += 2 * Math.PI;
            }
            while (sweep < 0 && dt > 0) {
                dt -= 2 * Math.PI;
            }
        }
        const cx = pen.x - wR * Math.cos(t1);
        const cy = pen.y - hR * Math.sin(t1);
        const count = Math.max(1, Math.ceil(Math.abs(dt) / (Math.PI / 2) - 1e-9));
        const step = dt / count;
        const k = 4 / 3 * Math.tan(step / 4);
        let t = t1;
        for (let i = 0; i < count; i++) {
            const t2 = t + step;
            pen.d += `C${round(cx + wR * (Math.cos(t) - k * Math.sin(t)))} ${round(cy + hR * (Math.sin(t) + k * Math.cos(t)))} `
                + `${round(cx + wR * (Math.cos(t2) + k * Math.sin(t2)))} ${round(cy + hR * (Math.sin(t2) - k * Math.cos(t2)))} `
                + `${round(cx + wR * Math.cos(t2))} ${round(cy + hR * Math.sin(t2))}`;
            t = t2;
        }
        pen.x = cx + wR * Math.cos(t1 + dt);
        pen.y = cy + hR * Math.sin(t1 + dt);
    }

    const geometryCache = new Map();

    // 도형 종류의 path들과 글자 자리(l, t, r, b). 좌표는 w x h 상자 안의 px
    function presetGeometry(kind, w, h, adjust) {
        const key = `${kind}|${round(w)}|${round(h)}|${adjust ? JSON.stringify(adjust) : ''}`;
        const cached = geometryCache.get(key);
        if (cached) {
            return cached;
        }
        const definition = DATA.shapes[kind] || DATA.shapes.rect;
        const vars = {w, h, l: 0, t: 0, r: w, b: h, hc: w / 2, vc: h / 2, ls: Math.max(w, h), ss: Math.min(w, h)};
        for (const [name, parts] of definition.av || []) {
            vars[name] = adjust && name in adjust ? adjust[name] : formula(parts, vars);
        }
        for (const [name, parts] of definition.gd || []) {
            vars[name] = formula(parts, vars);
        }
        const paths = definition.paths.map((path) => {
            const sx = path.w ? w / path.w : 1;
            const sy = path.h ? h / path.h : 1;
            const x = (value) => guide(value, vars) * sx;
            const y = (value) => guide(value, vars) * sy;
            const pen = {x: 0, y: 0, d: ''};
            for (const command of path.d) {
                switch (command[0]) {
                    case 'M':
                    case 'L':
                        pen.x = x(command[1]);
                        pen.y = y(command[2]);
                        pen.d += `${command[0]}${round(pen.x)} ${round(pen.y)}`;
                        break;
                    case 'Q':
                        pen.d += `Q${round(x(command[1]))} ${round(y(command[2]))} ${round(x(command[3]))} ${round(y(command[4]))}`;
                        pen.x = x(command[3]);
                        pen.y = y(command[4]);
                        break;
                    case 'C':
                        pen.d += `C${round(x(command[1]))} ${round(y(command[2]))} ${round(x(command[3]))} ${round(y(command[4]))} ${round(x(command[5]))} ${round(y(command[6]))}`;
                        pen.x = x(command[5]);
                        pen.y = y(command[6]);
                        break;
                    case 'A':
                        arcTo(pen, x(command[1]), y(command[2]), guide(command[3], vars) * ANGLE, guide(command[4], vars) * ANGLE);
                        break;
                    case 'Z':
                        pen.d += 'Z';
                        break;
                }
            }
            return {d: pen.d, fill: path.fill || 'norm', stroke: path.stroke !== false};
        });
        const rect = definition.rect ? definition.rect.map((value) => guide(value, vars)) : [0, 0, w, h];
        const result = {paths, rect};
        geometryCache.set(key, result);
        return result;
    }

    // element의 모양. freeform은 path가 그대로 있다
    function elementGeometry(el, w, h) {
        if (el.g && el.g.d !== undefined) {
            return {paths: [{d: el.g.d, fill: 'norm', stroke: true}], rect: [0, 0, w, h]};
        }
        return presetGeometry(el.g ? el.g.p : 'rect', w, h, el.g ? el.g.a : null);
    }

    // ---- 색

    function parseColor(text) {
        if (!text) {
            return {r: 0, g: 0, b: 0, a: 0};
        }
        if (typeof text === 'object') {
            return text;
        }
        if (text[0] === '#') {
            return {r: parseInt(text.slice(1, 3), 16), g: parseInt(text.slice(3, 5), 16), b: parseInt(text.slice(5, 7), 16), a: 1};
        }
        const parts = text.slice(text.indexOf('(') + 1, text.indexOf(')')).split(',').map(Number);
        return {r: parts[0], g: parts[1], b: parts[2], a: parts.length > 3 ? parts[3] : 1};
    }

    function formatColor(color) {
        const r = Math.round(clamp(color.r, 0, 255));
        const g = Math.round(clamp(color.g, 0, 255));
        const b = Math.round(clamp(color.b, 0, 255));
        return color.a >= 1 ? `rgb(${r},${g},${b})` : `rgba(${r},${g},${b},${round(clamp(color.a, 0, 1))})`;
    }

    function toHsl(color) {
        const r = color.r / 255;
        const g = color.g / 255;
        const b = color.b / 255;
        const max = Math.max(r, g, b);
        const min = Math.min(r, g, b);
        const l = (max + min) / 2;
        if (max === min) {
            return {h: 0, s: 0, l, a: color.a};
        }
        const d = max - min;
        const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        let h;
        if (max === r) {
            h = (g - b) / d + (g < b ? 6 : 0);
        } else if (max === g) {
            h = (b - r) / d + 2;
        } else {
            h = (r - g) / d + 4;
        }
        return {h: h * 60, s, l, a: color.a};
    }

    function fromHsl(hsl) {
        const h = ((hsl.h % 360) + 360) % 360 / 360;
        const s = clamp(hsl.s, 0, 1);
        const l = clamp(hsl.l, 0, 1);
        if (s === 0) {
            return {r: l * 255, g: l * 255, b: l * 255, a: hsl.a};
        }
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        const channel = (t) => {
            t = (t + 1) % 1;
            if (t < 1 / 6) {
                return p + (q - p) * 6 * t;
            }
            if (t < 1 / 2) {
                return q;
            }
            if (t < 2 / 3) {
                return p + (q - p) * (2 / 3 - t) * 6;
            }
            return p;
        };
        return {r: channel(h + 1 / 3) * 255, g: channel(h) * 255, b: channel(h - 1 / 3) * 255, a: hsl.a};
    }

    const mix = (a, b, t) => ({r: a.r + (b.r - a.r) * t, g: a.g + (b.g - a.g) * t, b: a.b + (b.b - a.b) * t, a: a.a + (b.a - a.a) * t});

    // ---- 채우기

    // Abramowitz-Stegun 근사
    function erf(x) {
        const sign = Math.sign(x);
        x = Math.abs(x);
        const t = 1 / (1 + 0.3275911 * x);
        const y = 1 - ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
        return sign * y;
    }

    const toLinear = (channel) => (channel /= 255) <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    const toSrgb = (value) => 255 * (value <= 0.0031308 ? value * 12.92 : 1.055 * Math.pow(value, 1 / 2.4) - 0.055);

    // PowerPoint은 두 색 그라데이션(컴파일러가 sm을 붙인다)을 선형광(linear-light) 공간에서 정규분포 누적 곡선(σ = 0.255)으로
    // 섞고, 나머지는 브라우저처럼 sRGB에서 곧게 섞는다. 곡선은 사이에 정지점을 더 넣어 따라간다
    const GRADIENT_SIGMA = 0.255 * Math.SQRT2;
    const GRADIENT_EDGE = erf(0.5 / GRADIENT_SIGMA);
    const gradientCurve = (t) => (erf((t - 0.5) / GRADIENT_SIGMA) + GRADIENT_EDGE) / (2 * GRADIENT_EDGE);

    function powerpointStops(paint) {
        const stops = paint.st;
        if (!paint.sm) {
            return stops;
        }
        const result = [];
        stops.forEach(([position, color], index) => {
            if (index === 0) {
                result.push([position, color]);
                return;
            }
            const [start, from] = stops[index - 1];
            const a = parseColor(from);
            const b = parseColor(color);
            for (let k = 1; k <= 12; k++) {
                const e = gradientCurve(k / 12);
                const mixed = {
                    r: toSrgb(toLinear(a.r) + (toLinear(b.r) - toLinear(a.r)) * e),
                    g: toSrgb(toLinear(a.g) + (toLinear(b.g) - toLinear(a.g)) * e),
                    b: toSrgb(toLinear(a.b) + (toLinear(b.b) - toLinear(a.b)) * e),
                    a: a.a + (b.a - a.a) * e,
                };
                result.push([start + (position - start) * k / 12, formatColor(mixed)]);
            }
        });
        return result;
    }

    function addStops(gradient, paint) {
        for (const [position, color] of powerpointStops(paint)) {
            const parsed = parseColor(color);
            svg('stop', {offset: position, 'stop-color': formatColor({...parsed, a: 1}), 'stop-opacity': parsed.a}, gradient);
        }
    }

    // 8x8 무늬 한 칸을 그린 path. 행마다 한 byte이고 높은 bit가 왼쪽이다
    function patternPath(kind) {
        const rows = DATA.patterns[kind] || DATA.patterns.percent_50;
        let d = '';
        rows.forEach((row, y) => {
            for (let x = 0; x < 8; x++) {
                if (row & (128 >> x)) {
                    d += `M${x} ${y}h1v1h-1z`;
                }
            }
        });
        return d;
    }

    // SVG의 fill 값. 그라데이션, 무늬, 그림은 defs에 넣는다. 좌표는 w x h 상자다
    function svgPaint(paint, w, h, defs) {
        if (!paint) {
            return 'none';
        }
        const id = uid('paint');
        switch (paint.k) {
            case 's':
                return paint.c;
            case 'lin': {
                // CSS linear-gradient와 같다. DrawingML의 lin(scaled=0)도 상자 끝에서 끝까지 퍼진다
                const angle = paint.a * DEG;
                const dx = Math.sin(angle);
                const dy = -Math.cos(angle);
                const half = (Math.abs(w * dx) + Math.abs(h * dy)) / 2;
                const gradient = svg('linearGradient', {id, gradientUnits: 'userSpaceOnUse', x1: w / 2 - dx * half, y1: h / 2 - dy * half, x2: w / 2 + dx * half, y2: h / 2 + dy * half}, defs);
                addStops(gradient, paint);
                return `url(#${id})`;
            }
            case 'rad': {
                // 가운데에서 가장 먼 모서리까지
                const gradient = svg('radialGradient', {id, gradientUnits: 'userSpaceOnUse', cx: w / 2, cy: h / 2, r: Math.max(Math.hypot(w, h) / 2, 0.001)}, defs);
                addStops(gradient, paint);
                return `url(#${id})`;
            }
            case 'pat': {
                const pattern = svg('pattern', {id, patternUnits: 'userSpaceOnUse', width: 8, height: 8}, defs);
                svg('rect', {width: 8, height: 8, fill: paint.bg}, pattern);
                svg('path', {d: patternPath(paint.p), fill: paint.fg, 'shape-rendering': 'crispEdges'}, pattern);
                return `url(#${id})`;
            }
            case 'img': {
                const pattern = svg('pattern', {id, patternUnits: 'userSpaceOnUse', width: Math.max(w, 0.001), height: Math.max(h, 0.001)}, defs);
                svg('image', {href: paint.src, width: w, height: h, preserveAspectRatio: 'none', opacity: paint.o}, pattern);
                return `url(#${id})`;
            }
            default:
                return 'none';
        }
    }

    // 슬라이드 배경의 CSS background. 그림은 비율을 지키며 빈틈없이 채우고 가운데를 기준으로 자른다
    function cssPaint(paint) {
        const stops = () => powerpointStops(paint).map(([position, color]) => `${color} ${round(position * 100)}%`).join(', ');
        switch (paint && paint.k) {
            case 's':
                return paint.c;
            case 'lin':
                return `linear-gradient(${paint.a}deg, ${stops()})`;
            case 'rad':
                return `radial-gradient(circle farthest-corner at 50% 50%, ${stops()})`;
            case 'pat': {
                const image = `<svg xmlns="${SVG_NS}" width="8" height="8" shape-rendering="crispEdges"><rect width="8" height="8" fill="${paint.bg}"/><path d="${patternPath(paint.p)}" fill="${paint.fg}"/></svg>`;
                return `url("data:image/svg+xml,${encodeURIComponent(image)}") 0 0 / 8px 8px repeat`;
            }
            case 'img':
                return `url("${paint.src}") center / cover no-repeat`;
            default:
                return '#fff';
        }
    }

    // ---- 효과 (SVG filter)

    // 그림자, 네온, 안쪽 그림자, 부드러운 가장자리. CSS filter로 element 전체(도형과 글자)에 건다.
    // outside면 도형 바깥의 그림자와 네온만 남긴다 (backdrop은 filter를 건 조상이 있으면 뒤를 흐리게 할 수 없다)
    function effectFilter(fx, w, h, defs, outside) {
        if (!fx || !(fx.sh || fx.gl || (!outside && (fx.ish || fx.se)))) {
            return null;
        }
        const id = uid('fx');
        const reach = Math.max(fx.sh ? fx.sh.b * 2 + Math.hypot(fx.sh.dx, fx.sh.dy) : 0, fx.ish ? fx.ish.b * 2 + Math.hypot(fx.ish.dx, fx.ish.dy) : 0, fx.gl ? fx.gl.r * 2 : 0) + 4;
        const filter = svg('filter', {id, filterUnits: 'userSpaceOnUse', x: -reach, y: -reach, width: w + 2 * reach, height: h + 2 * reach, 'color-interpolation-filters': 'sRGB'}, defs);
        const flood = (color, result) => {
            const parsed = parseColor(color);
            svg('feFlood', {'flood-color': formatColor({...parsed, a: 1}), 'flood-opacity': parsed.a, result}, filter);
        };
        let source = 'SourceGraphic';
        let alpha = 'SourceAlpha';
        // 반경과 흐림의 비율은 PowerPoint 그림에서 잰 값이다
        if (fx.se && !outside) {
            svg('feMorphology', {in: 'SourceAlpha', operator: 'erode', radius: fx.se * 0.875, result: 'seA'}, filter);
            svg('feGaussianBlur', {in: 'seA', stdDeviation: fx.se / 3, result: 'seB'}, filter);
            svg('feComposite', {in: 'SourceGraphic', in2: 'seB', operator: 'in', result: 'soft'}, filter);
            source = 'soft';
            alpha = 'seB';
        }
        const layers = [];
        if (fx.gl) {
            // PowerPoint의 네온은 약 0.46r + 0.4px 넓어진다. feMorphology는 반경을 정수로 쓰고 가장자리의 반투명 픽셀만큼
            // 0.5px 더 넓어지므로 그만큼 빼서 반올림한다
            svg('feMorphology', {in: alpha, operator: 'dilate', radius: Math.max(0, Math.round(0.46 * fx.gl.r - 0.1)), result: 'glA'}, filter);
            svg('feGaussianBlur', {in: 'glA', stdDeviation: fx.gl.r / 6, result: 'glB'}, filter);
            flood(fx.gl.c, 'glC');
            svg('feComposite', {in: 'glC', in2: 'glB', operator: 'in', result: 'glow'}, filter);
            layers.push('glow');
        }
        if (fx.sh) {
            svg('feGaussianBlur', {in: alpha, stdDeviation: fx.sh.b / 3, result: 'shB'}, filter);
            svg('feOffset', {in: 'shB', dx: fx.sh.dx, dy: fx.sh.dy, result: 'shO'}, filter);
            flood(fx.sh.c, 'shC');
            svg('feComposite', {in: 'shC', in2: 'shO', operator: 'in', result: 'shadow'}, filter);
            layers.push('shadow');
        }
        if (outside) {
            const merge = svg('feMerge', {result: 'effects'}, filter);
            for (const layer of layers) {
                svg('feMergeNode', {in: layer}, merge);
            }
            svg('feComposite', {in: 'effects', in2: 'SourceAlpha', operator: 'out'}, filter);
            return id;
        }
        layers.push(source);
        if (fx.ish) {
            const inverse = svg('feComponentTransfer', {in: alpha, result: 'ishI'}, filter);
            svg('feFuncA', {type: 'table', tableValues: '1 0'}, inverse);
            svg('feGaussianBlur', {in: 'ishI', stdDeviation: fx.ish.b / 3, result: 'ishB'}, filter);
            // PowerPoint의 안쪽 그림자는 방향 쪽 가장자리에 생기므로 바깥 영역을 반대로 옮긴다
            svg('feOffset', {in: 'ishB', dx: -fx.ish.dx, dy: -fx.ish.dy, result: 'ishO'}, filter);
            flood(fx.ish.c, 'ishC');
            svg('feComposite', {in: 'ishC', in2: 'ishO', operator: 'in', result: 'ishS'}, filter);
            svg('feComposite', {in: 'ishS', in2: alpha, operator: 'in', result: 'inner'}, filter);
            layers.push('inner');
        }
        const merge = svg('feMerge', {}, filter);
        for (const layer of layers) {
            svg('feMergeNode', {in: layer}, merge);
        }
        return id;
    }

    // ---- 선

    const DASHES = {
        sysDot: [1, 1], sysDash: [3, 1], sysDashDot: [3, 1, 1, 1], sysDashDotDot: [3, 1, 1, 1, 1, 1],
        dot: [1, 3], dash: [4, 3], lgDash: [8, 3], dashDot: [4, 3, 1, 3], lgDashDot: [8, 3, 1, 3], lgDashDotDot: [8, 3, 1, 3, 1, 3],
    };
    const CAPS = {flat: 'butt', round: 'round', square: 'square'};

    // 선 끝 모양. 크기는 선 두께의 3배(PowerPoint의 중간 크기)이고 가는 선도 알아볼 수 있게 두께를 2px 이상으로 본다
    function arrowMarker(type, color, width, defs, start) {
        const id = uid('arrow');
        const base = Math.max(width, 2);
        const length = base * 3;
        const breadth = base * 3;
        const marker = svg('marker', {
            id, markerUnits: 'userSpaceOnUse', markerWidth: length + width, markerHeight: breadth + width,
            viewBox: `${-width / 2} ${-width / 2} ${length + width} ${breadth + width}`,
            refX: type === 'diamond' || type === 'oval' ? length / 2 : length, refY: breadth / 2,
            orient: start ? 'auto-start-reverse' : 'auto', overflow: 'visible',
        }, defs);
        const half = breadth / 2;
        switch (type) {
            case 'stealth':
                svg('path', {d: `M0 0L${length} ${half}L0 ${breadth}L${length * 0.3} ${half}Z`, fill: color}, marker);
                break;
            case 'diamond':
                svg('path', {d: `M0 ${half}L${length / 2} 0L${length} ${half}L${length / 2} ${breadth}Z`, fill: color}, marker);
                break;
            case 'oval':
                svg('ellipse', {cx: length / 2, cy: half, rx: length / 2, ry: half, fill: color}, marker);
                break;
            case 'arrow':
                svg('path', {d: `M0 0L${length} ${half}L0 ${breadth}`, fill: 'none', stroke: color, 'stroke-width': width, 'stroke-linejoin': 'miter'}, marker);
                break;
            default:
                svg('path', {d: `M0 0L${length} ${half}L0 ${breadth}Z`, fill: color}, marker);
                break;
        }
        return `url(#${id})`;
    }

    // PowerPoint은 닫힌 path의 점선 무늬를 닫는 선분(마지막 점에서 첫 점으로 가는 선)의 시작에서 시작한다.
    // 닫힌 부분 path마다 그 선분을 맨 앞으로 옮긴다
    function dashStart(d) {
        return d.replace(/M[^M]*/g, (subpath) => {
            if (!/Z\s*$/.test(subpath)) {
                return subpath;
            }
            const numbers = subpath.match(/-?\d*\.?\d+(?:e[-+]?\d+)?/gi) || [];
            if (numbers.length < 4) {
                return subpath;
            }
            const [startX, startY] = numbers.slice(0, 2);
            const [endX, endY] = numbers.slice(-2);
            if (Math.abs(startX - endX) < 1e-3 && Math.abs(startY - endY) < 1e-3) {
                return subpath;
            }
            const body = subpath.replace(/^M[^A-Za-z]*/, '').replace(/Z\s*$/, '');
            return `M${endX} ${endY}L${startX} ${startY}${body}Z`;
        });
    }

    function markStroke(node, line) {
        node.classList.add('tl-stroke');
        node.dataset.width = String(line.w || 0);
    }

    // path의 명령과 좌표. axis는 M, L, Z만 쓰고 모든 선분이 가로나 세로인지다.
    // M, L, C, Q, Z 말고 다른 명령이 있거나 가로세로 선분이 하나도 없으면 null
    function pathCommands(d) {
        if (/[^MLCQZ\d\s.,eE+-]/.test(d)) {
            return null;
        }
        const commands = [];
        let axis = true;
        let straight = false; // 가로나 세로 선분이 하나라도 있는지
        let last = null;
        for (const [, command, rest] of d.matchAll(/([MLCQZ])([^MLCQZ]*)/g)) {
            const numbers = rest.trim() ? rest.trim().split(/[\s,]+/).map(Number) : [];
            commands.push([command, numbers]);
            if (command === 'C' || command === 'Q') {
                axis = false;
            }
            if (numbers.length >= 2) {
                const point = numbers.slice(-2);
                if (command === 'L' && last) {
                    const level = Math.abs(point[0] - last[0]) <= 1e-6 || Math.abs(point[1] - last[1]) <= 1e-6;
                    straight = straight || level;
                    axis = axis && level;
                }
                last = point;
            }
        }
        // 가로세로 선분이 없는 path(대각선, 타원)는 맞추지 않는다
        return axis || straight ? {commands, axis} : null;
    }

    // PowerPoint은 선을 화면 픽셀에 맞춰 흐리지 않게 그린다. 두께는 픽셀 수로 반올림하고(최소 1), 선의 가운데는 반올림한 픽셀
    // 위치에서 두께의 반 내림만큼 위(왼쪽)에서 시작한다. 가로세로 선분만 있는 path는 점마다 맞추고, 곡선이 있는 path는
    // 가장 바깥의 가로, 세로 경계를 맞추고 그 사이를 비례로 옮긴다. rect는 화면에서의 슬라이드 상자이고,
    // zoomed는 reveal이 transform 대신 CSS zoom으로 키웠는지다
    function snapStrokes(snaps, rect, W, zoomed) {
        const scale = rect.width / W;
        const ratio = window.devicePixelRatio || 1;
        const k = scale * ratio;
        if (!(k > 0)) {
            return;
        }
        for (const item of snaps) {
            const pixels = Math.max(1, Math.round(item.width * k));
            // 브라우저는 SVG를 슬라이드 좌표에서 반올림한 자리에 그리고, zoom이면 확대한 뒤 화면 픽셀에서 한 번 더 반올림한다
            const origin = (page, value) => {
                const device = (page + Math.round(value) * scale) * ratio;
                return zoomed ? Math.round(device) : device;
            };
            const originX = origin(rect.left, item.x);
            const originY = origin(rect.top, item.y);
            // 선이 갈 픽셀은 도형의 실제 자리로 정하고, 그 픽셀을 SVG가 그려진 자리에서 잰 좌표로 바꾼다
            const snapX = (x) => {
                const local = item.fh ? item.w - x : x;
                const start = Math.round((rect.left + (item.x + local) * scale) * ratio) - Math.floor(pixels / 2);
                const snapped = (start + pixels / 2 - originX) / k;
                return item.fh ? item.w - snapped : snapped;
            };
            const snapY = (y) => {
                const local = item.fv ? item.h - y : y;
                const start = Math.round((rect.top + (item.y + local) * scale) * ratio) - Math.floor(pixels / 2);
                const snapped = (start + pixels / 2 - originY) / k;
                return item.fv ? item.h - snapped : snapped;
            };
            let mapX = snapX;
            let mapY = snapY;
            if (!item.axis) {
                const xs = [];
                const ys = [];
                for (const [, numbers] of item.commands) {
                    for (let i = 0; i + 1 < numbers.length; i += 2) {
                        xs.push(numbers[i]);
                        ys.push(numbers[i + 1]);
                    }
                }
                const linear = (low, high, snap) => {
                    const a = snap(low);
                    const b = snap(high);
                    return high - low > 1e-6 ? (v) => a + (v - low) * (b - a) / (high - low) : (v) => v + a - low;
                };
                mapX = linear(Math.min(...xs), Math.max(...xs), snapX);
                mapY = linear(Math.min(...ys), Math.max(...ys), snapY);
            }
            let d = '';
            for (const [command, numbers] of item.commands) {
                d += command;
                for (let i = 0; i + 1 < numbers.length; i += 2) {
                    d += `${i ? ' ' : ''}${round(mapX(numbers[i]))} ${round(mapY(numbers[i + 1]))}`;
                }
            }
            item.node.setAttribute('d', d);
            item.node.setAttribute('stroke-width', round(pixels / k));
            item.node.removeAttribute('vector-effect');
        }
    }

    function strokeAttributes(line, defs) {
        const hairline = !line.w;
        const width = hairline ? 1 : line.w;
        const attributes = {
            stroke: line.c, 'stroke-width': width, fill: 'none',
            'stroke-linecap': CAPS[line.cap] || 'butt', 'stroke-linejoin': line.j || 'round', 'stroke-miterlimit': 8,
        };
        if (hairline) {
            attributes['vector-effect'] = 'non-scaling-stroke';
        }
        if (line.d && DASHES[line.d]) {
            // 둥글거나 네모난 끝은 대시 밖으로 두께의 반씩 나온다. PowerPoint은 대시의 앞을 두께만큼 줄이고 그만큼 틈을 늘린다
            const cap = line.cap === 'round' || line.cap === 'square' ? width : 0;
            attributes['stroke-dasharray'] = DASHES[line.d].map((value, index) => round(index % 2 ? value * width + cap : Math.max(value * width - cap, 0.001))).join(' ');
            if (cap) {
                attributes['stroke-dashoffset'] = round(-cap);
            }
        }
        if (line.he) {
            attributes['marker-start'] = arrowMarker(line.he, line.c, width, defs, true);
        }
        if (line.te) {
            attributes['marker-end'] = arrowMarker(line.te, line.c, width, defs, false);
        }
        return attributes;
    }

    // 겹선. 선 가운데를 mask로 비워 두 줄로 만들고, triple은 가운데에 가는 선을 하나 더 긋는다.
    // SVG는 선을 옆으로 옮길 수 없어 thick_thin, thin_thick도 가운데를 비운다
    function compoundLine(node, path, line, w, h, defs, group) {
        const gap = {double: 1 / 3, thick_thin: 0.2, thin_thick: 0.2, triple: 0.6}[line.cmpd];
        if (!gap) {
            return;
        }
        const width = line.w || 1;
        const margin = width * 4 + 10;
        const mask = svg('mask', {id: uid('mask'), maskUnits: 'userSpaceOnUse', x: -margin, y: -margin, width: w + 2 * margin, height: h + 2 * margin}, defs);
        svg('rect', {x: -margin, y: -margin, width: w + 2 * margin, height: h + 2 * margin, fill: 'white'}, mask);
        svg('path', {d: path.d, fill: 'none', stroke: 'black', 'stroke-width': width * gap}, mask);
        node.setAttribute('mask', `url(#${mask.id})`);
        if (line.cmpd === 'triple') {
            svg('path', {d: path.d, fill: 'none', stroke: line.c, 'stroke-width': width * 0.2}, group);
        }
    }

    // ---- 글자

    const ALIGNS = {l: 'left', ctr: 'center', r: 'right', just: 'justify'};

    function roman(value) {
        const table = [[1000, 'm'], [900, 'cm'], [500, 'd'], [400, 'cd'], [100, 'c'], [90, 'xc'], [50, 'l'], [40, 'xl'], [10, 'x'], [9, 'ix'], [5, 'v'], [4, 'iv'], [1, 'i']];
        let result = '';
        for (const [number, text] of table) {
            while (value >= number) {
                result += text;
                value -= number;
            }
        }
        return result;
    }

    function alpha(value) {
        let result = '';
        while (value > 0) {
            value--;
            result = String.fromCharCode(97 + value % 26) + result;
            value = Math.floor(value / 26);
        }
        return result;
    }

    function autoNumber(scheme, value) {
        switch (scheme) {
            case 'alphaLcPeriod': return alpha(value) + '.';
            case 'alphaUcPeriod': return alpha(value).toUpperCase() + '.';
            case 'romanLcPeriod': return roman(value) + '.';
            case 'romanUcPeriod': return roman(value).toUpperCase() + '.';
            case 'circleNumDbPlain': return value >= 1 && value <= 20 ? String.fromCharCode(0x2460 + value - 1) : value + '.';
            default: return value + '.';
        }
    }

    function fieldText(run, context) {
        if (run.fld === 'slidenum') {
            return String(context.page);
        }
        if (run.fld === 'datetime1') {
            // 컴파일할 때의 글자와 같은 YYYY-MM-DD
            const today = new Date();
            return `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`;
        }
        return run.t;
    }

    // run 하나. 글자 크기는 자동 맞춤이 줄일 수 있게 --tl-fs를 곱한다
    function runStyle(node, run) {
        const style = node.style;
        style.fontFamily = run.f;
        let size = run.sz;
        if (run.bl) {
            // PowerPoint은 위, 아래 첨자를 2/3 크기로 그리고 줄 높이는 그대로 둔다
            size = size * 2 / 3;
            style.position = 'relative';
            style.top = `${round(-run.bl / 100 * run.sz)}px`;
        }
        style.fontSize = `calc(${round(size)}px * var(--tl-fs, 1))`;
        if (run.b) {
            style.fontWeight = 'bold';
        }
        if (run.i) {
            style.fontStyle = 'italic';
        }
        const lines = [];
        if (run.u) {
            lines.push('underline');
        }
        if (run.st) {
            lines.push('line-through');
        }
        if (lines.length) {
            style.textDecorationLine = lines.join(' ');
            style.textDecorationStyle = run.u === 'dbl' || run.st === 'dbl' ? 'double' : run.u === 'wavy' ? 'wavy' : 'solid';
            style.textDecorationSkipInk = 'none';
        }
        if (run.cap === 'all') {
            style.textTransform = 'uppercase';
        } else if (run.cap === 'small') {
            style.fontVariant = 'small-caps';
        }
        if (run.sp) {
            style.letterSpacing = `${round(run.sp)}px`;
        }
        style.color = run.c;
        if (run.hl) {
            style.backgroundColor = run.hl;
        }
    }

    // 글자를 한 자씩(lt) 또는 한 단어씩(wd) 나눈다. 애니메이션이 글자마다 움직인다
    function splitText(text, mode, parent, parts) {
        const pieces = mode === 'wd' ? text.split(/(\s+)/) : Array.from(text);
        for (const piece of pieces) {
            if (!piece) {
                continue;
            }
            if (/^\s+$/.test(piece)) {
                parent.appendChild(document.createTextNode(piece));
                continue;
            }
            const span = html('span', 'tl-part');
            span.textContent = piece;
            parent.appendChild(span);
            parts.push(span);
        }
    }

    function runNode(run, context, view) {
        const node = html(run.k ? 'a' : 'span', 'tl-run');
        runStyle(node, run);
        const text = fieldText(run, context);
        const lines = text.split('\n');
        lines.forEach((line, index) => {
            if (index > 0) {
                node.appendChild(document.createElement('br'));
            }
            if (view && view.iterate) {
                splitText(line, view.iterate, node, view.partNodes);
            } else if (line) {
                node.appendChild(document.createTextNode(line));
            }
        });
        if (run.k) {
            node.href = run.k.url || '#';
            node.addEventListener('click', (event) => {
                event.preventDefault();
                event.stopPropagation();
                context.follow(run.k, node);
            });
        }
        if (run.kh) {
            node.addEventListener('mouseenter', () => context.follow(run.kh, node));
        }
        if (view) {
            view.runs.push({node, run});
        }
        return node;
    }

    const metricContext = typeof document !== 'undefined' ? document.createElement('canvas').getContext('2d') : null;
    const metricCache = new Map();

    // 글꼴의 ascent, descent (글자 크기에 대한 비율). Windows의 브라우저는 PowerPoint처럼 OS/2의 win 값을 쓴다
    function fontMetrics(family) {
        let metrics = metricCache.get(family);
        if (!metrics) {
            metricContext.font = `100px ${family}`;
            const measured = metricContext.measureText('H');
            metrics = {ascent: measured.fontBoundingBoxAscent / 100, descent: measured.fontBoundingBoxDescent / 100};
            metricCache.set(family, metrics);
        }
        return metrics;
    }

    // PowerPoint이 줄 안에 기준선을 두는 곳과 CSS가 두는 곳의 차이 (글자 크기에 대한 비율). 줄 사이 간격은 둘 다 1.2 x 크기 x lh다.
    // PowerPoint은 줄 간격 1이면 1.2 x ascent / (ascent + descent), 1보다 크면 줄 사이 간격의 0.75에 기준선을 두고,
    // 1보다 작으면 그 두 방식 중 아래쪽을 쓴다 (PowerPoint 그림에서 잰 규칙이다)
    function baselineShift(family, lh) {
        if (!metricContext) {
            return 0;
        }
        const {ascent, descent} = fontMetrics(family);
        if (!(ascent > 0) || !(descent >= 0)) {
            return 0;
        }
        const pitch = 1.2 * lh;
        const single = 1.2 * ascent / (ascent + descent);
        let target = single;
        if (lh > 1) {
            target = 0.75 * pitch;
        } else if (lh < 1) {
            target = Math.max(0.75 * pitch, single - (1 - lh) * 1.2);
        }
        return target - ((pitch - ascent - descent) / 2 + ascent);
    }

    function paragraphNode(paragraph, context, counters, view, vertical) {
        const node = html('div', 'tl-p');
        const style = node.style;
        style.textAlign = ALIGNS[paragraph.al] || 'left';
        // PowerPoint의 줄 간격 1은 글자 크기의 1.2배다
        const lineHeight = paragraph.lh !== undefined ? paragraph.lh : 1;
        style.lineHeight = String(round(lineHeight * 1.2));
        if (paragraph.sb) {
            style.marginTop = `${round(paragraph.sb)}px`;
        }
        if (paragraph.sa) {
            style.marginBottom = `${round(paragraph.sa)}px`;
        }
        const margin = paragraph.ml || 0;
        const indent = paragraph.ind || 0;
        style.paddingLeft = `${round(margin)}px`;
        style.textIndent = `${round(indent)}px`;
        // 줄 높이와 기준선은 가장 큰 글자를 따른다. 문단의 글자 크기와 글꼴을 그 run에 맞춘다
        const first = paragraph.rs[0];
        const effective = (run) => run.bl ? run.sz * 2 / 3 : run.sz;
        const dominant = paragraph.rs.reduce((best, run) => !best || effective(run) > effective(best) ? run : best, null);
        const size = dominant ? effective(dominant) : paragraph.sz || 24;
        const family = dominant ? dominant.f : '"Calibri","Malgun Gothic",sans-serif';
        style.fontSize = `calc(${round(size)}px * var(--tl-fs, 1))`;
        style.fontFamily = family;
        if (!vertical) {
            const shift = baselineShift(family, lineHeight);
            if (shift) {
                style.position = 'relative';
                style.top = `${round(shift)}em`;
            }
        }
        // 번호는 같은 수준에서 이어지고, 더 얕은 수준이나 번호 없는 문단에서 새로 시작한다
        const level = paragraph.lvl || 0;
        for (const key of Object.keys(counters)) {
            if (Number(key) > level || !paragraph.bu) {
                delete counters[key];
            }
        }
        if (paragraph.bu) {
            const bullet = html('span', 'tl-bullet');
            let text = paragraph.bu.ch;
            if (paragraph.bu.n) {
                // 시작 번호가 같은 문단들은 이어서 센다
                const counter = counters[level];
                const start = paragraph.bu.s || 1;
                const value = counter && counter.scheme === paragraph.bu.n && counter.start === start ? counter.value + 1 : start;
                counters[level] = {scheme: paragraph.bu.n, start, value};
                text = autoNumber(paragraph.bu.n, value);
            }
            bullet.textContent = text;
            bullet.style.fontFamily = paragraph.bu.n ? (first ? first.f : 'inherit') : 'Arial, sans-serif';
            bullet.style.fontSize = `calc(${round(size)}px * var(--tl-fs, 1))`;
            bullet.style.color = paragraph.bu.c || (first ? first.c : 'inherit');
            if (indent < 0) {
                bullet.style.minWidth = `${round(-indent)}px`;
            } else {
                bullet.style.marginRight = '0.4em';
            }
            node.appendChild(bullet);
        }
        for (const run of paragraph.rs) {
            node.appendChild(runNode(run, context, view));
        }
        if (!paragraph.rs.length || paragraph.rs.every((run) => !run.t && !run.fld)) {
            const empty = html('span');
            empty.textContent = '​';
            empty.style.fontSize = `calc(${round(size)}px * var(--tl-fs, 1))`;
            node.appendChild(empty);
        }
        return node;
    }

    // 글상자. 자리는 도형 모양의 글자 자리에서 여백을 뺀 곳이고, 넘친 글자는 잘리지 않는다
    function textNode(el, w, h, rect, context, view) {
        const text = el.tx;
        let [l, t, r, b] = rect;
        if (el.fh) {
            [l, r] = [w - r, w - l];
        }
        if (el.fv) {
            [t, b] = [h - b, h - t];
        }
        const inset = text.ins || [9.6, 4.8, 9.6, 4.8];
        const node = html('div', 'tl-text');
        const width = Math.max(0, r - l - inset[0] - inset[2]);
        const height = Math.max(0, b - t - inset[1] - inset[3]);
        Object.assign(node.style, {left: `${round(l + inset[0])}px`, top: `${round(t + inset[1])}px`, width: `${round(width)}px`, height: `${round(height)}px`});
        node.style.justifyContent = text.an === 'ctr' ? 'center' : text.an === 'b' ? 'flex-end' : 'flex-start';
        if (text.nowrap) {
            node.classList.add('tl-nowrap');
        }
        if (text.vert) {
            node.style.writingMode = 'vertical-rl';
            if (text.vert === 'vert' || text.vert === 'vert270') {
                node.style.textOrientation = 'sideways';
            } else if (text.vert === 'wordArtVert') {
                node.style.textOrientation = 'upright';
            }
            if (text.vert === 'vert270') {
                node.style.transform = 'rotate(180deg)';
            }
        }
        if (el.fv && !text.vert) {
            node.style.transform = 'rotate(180deg)';
        }
        let body = node;
        if (text.cols) {
            body = html('div', 'tl-columns', {columnCount: String(text.cols), columnGap: `${round(text.gap || 0)}px`, columnFill: 'auto', height: '100%'});
            node.appendChild(body);
        }
        const counters = {};
        for (const paragraph of text.ps) {
            body.appendChild(paragraphNode(paragraph, context, counters, view, !!text.vert));
        }
        return node;
    }

    // 자동 맞춤. shrink는 넘치지 않을 때까지 글자를 줄이고, resize는 도형 높이를 글자에 맞춘다
    function fitText(entry) {
        const {node, el, view} = entry;
        const text = node.querySelector(':scope > .tl-text');
        if (!text) {
            return;
        }
        const content = () => Array.from(text.children).reduce((sum, child) => sum + child.offsetHeight + parseFloat(getComputedStyle(child).marginTop) + parseFloat(getComputedStyle(child).marginBottom), 0);
        const available = text.clientHeight;
        if (el.tx.fit === 'shrink' && content() > available + 0.5) {
            let low = 0.25;
            let high = 1;
            for (let i = 0; i < 8; i++) {
                const middle = (low + high) / 2;
                text.style.setProperty('--tl-fs', String(middle));
                if (content() > available + 0.5) {
                    high = middle;
                } else {
                    low = middle;
                }
            }
            text.style.setProperty('--tl-fs', String(round(low)));
        } else if (el.tx.fit === 'resize') {
            const extra = content() - available;
            if (extra > 0.5) {
                const height = el.h + extra;
                node.style.height = `${round(height)}px`;
                text.style.height = `${round(available + extra)}px`;
                if (view) {
                    view.box.h = height;
                }
                const shape = node.querySelector(':scope > .tl-svg');
                if (shape) {
                    const replacement = shapeNode({...el, h: height}, el.w, height, entry.defs, null);
                    node.replaceChild(replacement, shape);
                }
            }
        }
    }

    // ---- element

    // 도형, 선, 연결선, freeform, 글상자의 SVG
    function shapeNode(el, w, h, defs, view) {
        // SVG는 가로나 세로가 0이면 아무것도 그리지 않아 수평선, 수직선을 위해 1px 이상으로 둔다
        const root = svg('svg', {class: 'tl-svg', width: Math.max(w, 1), height: Math.max(h, 1), overflow: 'visible'});
        const local = svg('defs', {}, root);
        const group = svg('g', {transform: flipTransform(el, w, h)}, root);
        const geometry = elementGeometry(el, w, h);
        const fill = svgPaint(el.f, w, h, local);
        for (const path of geometry.paths) {
            if (path.fill === 'none' || !el.f) {
                continue;
            }
            const node = svg('path', {d: path.d, fill, stroke: 'none'}, group);
            if (view) {
                view.fills.push(node);
            }
            // lighten, darken은 채우기 위에 흰색이나 검은색을 덮는다
            const shade = {lighten: 'rgba(255,255,255,0.4)', lightenLess: 'rgba(255,255,255,0.2)', darken: 'rgba(0,0,0,0.4)', darkenLess: 'rgba(0,0,0,0.2)'}[path.fill];
            if (shade) {
                svg('path', {d: path.d, fill: shade, stroke: 'none'}, group);
            }
        }
        if (el.l) {
            const strokes = geometry.paths.filter((path) => path.stroke);
            strokes.forEach((path, index) => {
                const attributes = strokeAttributes({...el.l, he: index === 0 ? el.l.he : undefined, te: index === strokes.length - 1 ? el.l.te : undefined}, local);
                const node = svg('path', {d: el.l.d ? dashStart(path.d) : path.d, ...attributes}, group);
                markStroke(node, el.l);
                if (view) {
                    view.strokes.push(node);
                }
                compoundLine(node, path, el.l, w, h, local, group);
            });
        }
        return root;
    }

    function flipTransform(el, w, h) {
        if (el.fh && el.fv) {
            return `matrix(-1 0 0 -1 ${round(w)} ${round(h)})`;
        }
        if (el.fh) {
            return `matrix(-1 0 0 1 ${round(w)} 0)`;
        }
        if (el.fv) {
            return `matrix(1 0 0 -1 0 ${round(h)})`;
        }
        return null;
    }

    // 그림. 도형 모양으로 자르고, crop은 보이는 부분이 상자를 채우도록 그림을 키운다
    function imageNode(el, w, h, view) {
        const root = svg('svg', {class: 'tl-svg', width: Math.max(w, 1), height: Math.max(h, 1), overflow: 'visible'});
        const local = svg('defs', {}, root);
        const group = svg('g', {transform: flipTransform(el, w, h)}, root);
        const geometry = elementGeometry(el, w, h);
        const clip = svg('clipPath', {id: uid('clip')}, local);
        for (const path of geometry.paths) {
            if (path.fill !== 'none') {
                svg('path', {d: path.d}, clip);
            }
        }
        const content = svg('g', {'clip-path': `url(#${clip.id})`}, group);
        if (el.f) {
            const fill = svgPaint(el.f, w, h, local);
            for (const path of geometry.paths) {
                if (path.fill !== 'none') {
                    svg('path', {d: path.d, fill}, content);
                }
            }
        }
        const [l, t, r, b] = el.crop || [0, 0, 0, 0];
        const visibleWidth = Math.max(1e-6, 1 - l - r);
        const visibleHeight = Math.max(1e-6, 1 - t - b);
        const image = svg('image', {
            href: el.src, x: -l / visibleWidth * w, y: -t / visibleHeight * h, width: w / visibleWidth, height: h / visibleHeight,
            preserveAspectRatio: 'none', opacity: el.o,
        }, content);
        if (view) {
            view.images.push(image);
        }
        if (el.l) {
            for (const path of geometry.paths.filter((item) => item.stroke)) {
                const node = svg('path', {d: el.l.d ? dashStart(path.d) : path.d, ...strokeAttributes(el.l, local)}, group);
                markStroke(node, el.l);
                if (view) {
                    view.strokes.push(node);
                }
            }
        }
        return root;
    }

    // backdrop. 뒤에 있는 것을 흐리게 보여 준다 (CSS backdrop-filter)
    function backdropNode(el, w, h) {
        const geometry = elementGeometry(el, w, h);
        const node = html('div', 'tl-backdrop');
        const blur = `blur(${round(el.blur || 0)}px)`;
        node.style.backdropFilter = blur;
        node.style.webkitBackdropFilter = blur;
        const outline = geometry.paths.filter((path) => path.fill !== 'none').map((path) => path.d).join(' ');
        if (outline && !(el.g && el.g.p === 'rect')) {
            node.dataset.clip = `path('${outline}')`;
            node.style.clipPath = node.dataset.clip;
        }
        if (el.o !== undefined) {
            node.style.opacity = String(el.o);
        }
        return node;
    }

    // ---- 비디오와 오디오

    // 편집기 덱은 파일을 "file:경로"로 가리킨다. 편집기가 Templide.mediaUrl로 읽을 수 있는 주소로 바꾼다
    function mediaUrl(src) {
        if (src && src.startsWith('file:') && typeof global.Templide.mediaUrl === 'function') {
            return global.Templide.mediaUrl(src.slice(5));
        }
        return src;
    }

    // 포스터가 없을 때의 그림. 비디오는 회색 판에 재생 단추, 오디오는 스피커다
    function mediaPlaceholder(el, w, h) {
        const root = svg('svg', {class: 'tl-svg', width: Math.max(w, 1), height: Math.max(h, 1), viewBox: `0 0 ${Math.max(w, 1)} ${Math.max(h, 1)}`});
        const r = Math.min(w, h) * (el.t === 'vid' ? 0.22 : 0.42);
        const cx = w / 2;
        const cy = h / 2;
        if (el.t === 'vid') {
            svg('rect', {width: w, height: h, fill: '#5a5e66'}, root);
            svg('circle', {cx, cy, r, fill: '#fff'}, root);
            svg('path', {d: `M${cx - r * 0.32} ${cy - r * 0.58} L${cx + r * 0.48} ${cy} L${cx - r * 0.32} ${cy + r * 0.58} Z`, fill: '#5a5e66'}, root);
        } else {
            svg('circle', {cx, cy, r, fill: '#5a5e66'}, root);
            svg('path', {d: `M${cx - r * 0.5} ${cy - r * 0.16} h${r * 0.28} l${r * 0.3} ${-r * 0.36} v${r * 1.04} l${-r * 0.3} ${-r * 0.36} h${-r * 0.28} Z`, fill: '#fff'}, root);
            for (const k of [0.27, 0.47]) {
                svg('path', {d: `M${cx + r * 0.16} ${cy - r * k} A${r * k} ${r * k} 0 0 1 ${cx + r * 0.16} ${cy + r * k}`, fill: 'none', stroke: '#fff', 'stroke-width': r * 0.08, 'stroke-linecap': 'round'}, root);
            }
        }
        return root;
    }

    // 비디오, 오디오 개체. 편집기 미리보기는 포스터만 그리고, 슬라이드 쇼(context.media)는 재생할 요소를 붙인다
    function mediaNode(el, w, h, context, node) {
        const box = html('div', 'tl-media', {width: `${round(w)}px`, height: `${round(h)}px`});
        if (el.o !== undefined) {
            box.style.opacity = String(el.o);
        }
        const cover = html('div', 'tl-media-cover');
        if (el.poster) {
            const image = html('img', 'tl-media-poster');
            image.src = el.poster;
            image.draggable = false;
            cover.appendChild(image);
        } else {
            cover.appendChild(mediaPlaceholder(el, w, h));
        }
        box.appendChild(cover);
        if (el.l) {
            const outline = svg('svg', {class: 'tl-svg', width: Math.max(w, 1), height: Math.max(h, 1), overflow: 'visible'}, box);
            svg('rect', {width: w, height: h, ...strokeAttributes(el.l, context.defs)}, outline);
        }
        if (!context.media) {
            return box;
        }
        const media = document.createElement(el.t === 'vid' ? 'video' : 'audio');
        media.preload = 'metadata';
        media.src = mediaUrl(el.src);
        if (el.t === 'vid') {
            media.className = 'tl-video';
            media.playsInline = true;
            box.insertBefore(media, cover);
        } else {
            media.style.display = 'none';
            box.appendChild(media);
        }
        const control = new MediaControl(el, media, node, cover);
        context.medias.push(control);
        if (el.id !== undefined) {
            context.mediaOf.set(el.id, control);
        }
        if (el.start === 'when_clicked') {
            node.classList.add('tl-link');
            node.addEventListener('click', (event) => {
                event.stopPropagation();
                control.toggle();
            });
        }
        return box;
    }

    // 재생 하나. 앞뒤 자르기(ts, te), 소리 키우고 줄이기(fi, fo), 반복, 되감기, 멈추면 숨기기를 맡는다
    class MediaControl {
        constructor(el, media, node, cover) {
            this.el = el;
            this.media = media;
            this.node = node;
            this.cover = cover;
            this.started = false;
            media.loop = false;
            media.addEventListener('timeupdate', () => this.update());
            media.addEventListener('ended', () => this.finish());
            this.reset();
        }

        get from() {
            return (this.el.ts || 0) / 1000;
        }

        get until() {
            const duration = this.media.duration;
            return Number.isFinite(duration) ? duration - (this.el.te || 0) / 1000 : Infinity;
        }

        // 숨기기: 오디오의 hide는 쇼 동안 늘, 비디오의 hide는 재생하지 않을 때 숨긴다
        visibility() {
            const hidden = this.el.hide && (this.el.t === 'aud' || this.media.paused);
            this.node.style.visibility = hidden ? 'hidden' : '';
        }

        play() {
            const media = this.media;
            if (!this.started || media.currentTime >= this.until - 0.01) {
                try {
                    media.currentTime = this.from;
                } catch (error) {
                    // 아직 메타데이터가 없으면 처음부터
                }
            }
            this.started = true;
            this.cover.style.display = this.el.t === 'vid' ? 'none' : '';
            this.update();
            media.play().catch(() => {});
            if (this.el.full && this.media.requestFullscreen && !document.fullscreenElement) {
                this.media.requestFullscreen().catch(() => {});
            }
            this.visibility();
        }

        pause() {
            this.media.pause();
            this.visibility();
        }

        stop() {
            this.media.pause();
            try {
                this.media.currentTime = this.from;
            } catch (error) {
                // 메타데이터가 없다
            }
            this.started = false;
            this.cover.style.display = '';
            this.leaveFullscreen();
            this.visibility();
        }

        toggle() {
            if (this.media.paused) {
                this.play();
            } else {
                this.pause();
            }
        }

        leaveFullscreen() {
            if (document.fullscreenElement === this.media && document.exitFullscreen) {
                document.exitFullscreen().catch(() => {});
            }
        }

        // 자른 끝에 닿으면 끝내고, 소리를 키우고 줄인다
        update() {
            const media = this.media;
            const time = media.currentTime;
            if (time >= this.until && !media.paused) {
                this.finish();
                return;
            }
            let volume = this.el.vol !== undefined ? this.el.vol : 1;
            if (this.el.fi) {
                volume *= clamp((time - this.from) / (this.el.fi / 1000), 0, 1);
            }
            if (this.el.fo && Number.isFinite(this.until)) {
                volume *= clamp((this.until - time) / (this.el.fo / 1000), 0, 1);
            }
            media.volume = clamp(volume, 0, 1);
        }

        finish() {
            if (this.el.loop) {
                this.media.currentTime = this.from;
                this.media.play().catch(() => {});
                return;
            }
            this.media.pause();
            this.leaveFullscreen();
            if (this.el.rew) {
                this.stop();
            }
            this.visibility();
        }

        // 슬라이드에 처음 들어온 상태
        reset() {
            this.stop();
        }

        // 슬라이드를 떠날 때. 여러 슬라이드에 걸쳐 재생하는 오디오는 계속한다
        leave() {
            if (!(this.el.across && !this.media.paused)) {
                this.stop();
            }
        }
    }

    // element의 기본 변환. 회전 뒤에 3차원 회전을 한다
    function baseTransform(el, w, h) {
        const parts = [];
        const fx = el.fx || {};
        // PowerPoint의 원근 카메라는 도형 크기와 관계없이 600px / tan(시야각 / 2) 떨어져 있다
        if (fx.ps) {
            const distance = 600 / Math.tan(fx.ps * DEG / 2);
            parts.push(`perspective(${round(distance)}px)`);
        }
        if (el.r) {
            parts.push(`rotate(${round(el.r)}deg)`);
        }
        if (fx.rx) {
            parts.push(`rotateX(${round(fx.rx)}deg)`);
        }
        if (fx.ry) {
            parts.push(`rotateY(${round(fx.ry)}deg)`);
        }
        return parts;
    }

    // 자식 element 상자를 모두 감싸는 상자
    function bounds(elements) {
        let result = null;
        for (const el of elements) {
            const box = el.t === 'grp' ? bounds(el.c || []) : {x: el.x, y: el.y, w: el.w, h: el.h};
            if (!box) {
                continue;
            }
            if (!result) {
                result = {...box};
                continue;
            }
            const right = Math.max(result.x + result.w, box.x + box.w);
            const bottom = Math.max(result.y + result.h, box.y + box.h);
            result.x = Math.min(result.x, box.x);
            result.y = Math.min(result.y, box.y);
            result.w = right - result.x;
            result.h = bottom - result.y;
        }
        return result;
    }

    function renderElement(el, context, originX, originY) {
        if (el.t === 'grp') {
            const box = bounds(el.c || []) || {x: originX, y: originY, w: 0, h: 0};
            const node = html('div', 'tl-el tl-group', {left: `${round(box.x - originX)}px`, top: `${round(box.y - originY)}px`, width: `${round(box.w)}px`, height: `${round(box.h)}px`});
            if (el.eid) {
                node.dataset.src = el.eid;
            }
            const view = context.animated && el.id !== undefined ? new View(el, node, context, box) : null;
            for (const child of el.c || []) {
                const childNode = renderElement(child, context, box.x, box.y);
                if (childNode) {
                    node.appendChild(childNode);
                }
            }
            if (view) {
                view.render();
            }
            return node;
        }
        const w = Math.max(el.w, 0);
        const h = Math.max(el.h, 0);
        const node = html('div', 'tl-el', {left: `${round(el.x - originX)}px`, top: `${round(el.y - originY)}px`, width: `${round(w)}px`, height: `${round(h)}px`});
        // 편집기 덱에만 있는 IR의 element id
        if (el.eid) {
            node.dataset.src = el.eid;
        }
        const view = context.animated && el.id !== undefined ? new View(el, node, context, {x: el.x, y: el.y, w, h}) : null;
        if (view && context.iterate.has(el.id)) {
            view.iterate = context.iterate.get(el.id);
        }
        switch (el.t) {
            case 'pic':
                node.appendChild(imageNode(el, w, h, view));
                break;
            case 'vid':
            case 'aud':
                node.appendChild(mediaNode(el, w, h, context, node));
                break;
            case 'bd': {
                node.appendChild(backdropNode(el, w, h));
                // 그림자와 네온은 도형 바깥만 그린 층으로 따로 둔다
                const outside = effectFilter(el.fx, w, h, context.defs, true);
                if (outside) {
                    const layer = shapeNode({...el, f: {k: 's', c: '#000'}, l: null}, w, h, context.defs, null);
                    layer.style.filter = `url(#${outside})`;
                    node.appendChild(layer);
                }
                if (el.l) {
                    node.appendChild(shapeNode({...el, f: null}, w, h, context.defs, view));
                }
                break;
            }
            default:
                node.appendChild(shapeNode(el, w, h, context.defs, view));
                break;
        }
        if (el.tx) {
            node.appendChild(textNode(el, w, h, elementGeometry(el, w, h).rect, context, view));
            if (el.tx.fit) {
                context.fits.push({node, el, view, defs: context.defs});
            }
        }
        const filter = el.t === 'bd' ? null : effectFilter(el.fx, w, h, context.defs);
        if (filter) {
            node.style.filter = `url(#${filter})`;
        }
        if (el.fx && el.fx.rf) {
            const reflection = el.fx.rf;
            const fade = `linear-gradient(to bottom, transparent ${round((1 - reflection.s) * 100)}%, rgba(0,0,0,${round(reflection.a)}) 100%)`;
            node.style.webkitBoxReflect = `below ${round(reflection.d)}px ${fade}`;
        }
        node.style.transform = baseTransform(el, w, h).join(' ');
        if (!el.r && !(el.fx && (el.fx.rx || el.fx.ry || el.fx.ps))) {
            for (const path of node.querySelectorAll(':scope > svg.tl-svg path.tl-stroke')) {
                const parsed = pathCommands(path.getAttribute('d'));
                if (parsed) {
                    context.snaps.push({node: path, commands: parsed.commands, axis: parsed.axis, x: el.x, y: el.y, w, h, fh: !!el.fh, fv: !!el.fv, width: Number(path.dataset.width)});
                }
            }
        }
        if (el.k) {
            node.classList.add('tl-link');
            node.addEventListener('click', (event) => {
                event.stopPropagation();
                context.follow(el.k, node);
            });
        }
        // 마우스를 올렸을 때 하는 일 (PowerPoint의 마우스 오버)
        if (el.kh) {
            node.addEventListener('mouseenter', () => context.follow(el.kh, node));
        }
        if (view) {
            view.render();
        }
        return node;
    }

    // ---- 애니메이션 상태

    // values는 덮어쓰는 절대값(ppt_x 등), dx, dy, sx, sy, rotation은 더하거나 곱하는 값이다
    function freshState(hidden) {
        return {
            visible: !hidden, opacity: 1, values: {}, dx: 0, dy: 0, sx: 1, sy: 1, scale: null, rotation: 0, clip: null,
            fill: null, stroke: null, color: null, weight: null, italic: null, underline: null, size: null, family: null,
        };
    }

    // 상태에서 이동, 배율, 회전, 기울임, 불투명도를 계산한다
    function motionOf(state, vars, context) {
        const values = state.values;
        const {W, H} = context;
        const scale = state.scale || [1, 1];
        return {
            dx: state.dx + ('ppt_x' in values ? (values.ppt_x - vars.ppt_x) * W : 0),
            dy: state.dy + ('ppt_y' in values ? (values.ppt_y - vars.ppt_y) * H : 0),
            sx: state.sx * scale[0] * ('ppt_w' in values && vars.ppt_w ? values.ppt_w / vars.ppt_w : 1),
            sy: state.sy * scale[1] * ('ppt_h' in values && vars.ppt_h ? values.ppt_h / vars.ppt_h : 1),
            rotation: state.rotation + (values.rotation || 0),
            skew: values.xshear ? Math.atan(values.xshear) / DEG : 0,
            opacity: state.opacity * ('opacity' in values ? clamp(values.opacity, 0, 1) : 1),
        };
    }

    // 애니메이션이 움직이는 element 하나. 상태를 매 프레임 처음부터 계산해 DOM에 적는다
    class View {
        constructor(el, node, context, box) {
            this.el = el;
            this.node = node;
            this.context = context;
            this.box = box;
            this.fills = [];
            this.strokes = [];
            this.images = [];
            this.runs = [];
            this.iterate = null;
            this.partNodes = [];
            this.parts = null;
            this.hiddenAtStart = false;
            context.views.set(el.id, this);
            this.reset();
        }

        // #ppt_x, #ppt_y는 상자 가운데, #ppt_w, #ppt_h는 크기. 모두 슬라이드 크기에 대한 비율이다
        variables() {
            const {W, H} = this.context;
            return {ppt_x: (this.box.x + this.box.w / 2) / W, ppt_y: (this.box.y + this.box.h / 2) / H, ppt_w: this.box.w / W, ppt_h: this.box.h / H};
        }

        reset() {
            this.state = freshState(this.hiddenAtStart);
            if (this.parts) {
                for (const part of this.parts) {
                    part.reset();
                }
            }
            if (this.background) {
                this.background.reset();
            }
        }

        baseFill() {
            const paint = this.el.f;
            if (paint && paint.k === 's') {
                return parseColor(paint.c);
            }
            if (paint && paint.st) {
                return parseColor(paint.st[0][1]);
            }
            return parseColor(this.context.theme.accent1);
        }

        baseStroke() {
            return parseColor(this.el.l ? this.el.l.c : this.context.theme.accent1);
        }

        baseColor() {
            return parseColor(this.runs.length ? this.runs[0].run.c : this.context.theme.tx1);
        }

        render() {
            const state = this.state;
            const node = this.node;
            node.style.visibility = state.visible ? '' : 'hidden';
            const motion = motionOf(state, this.variables(), this.context);
            // backdrop은 조상에 opacity나 clip-path가 있으면 뒤를 흐리게 할 수 없어 안쪽 층에 건다
            const layers = this.el.t === 'bd' ? Array.from(node.children) : [node];
            const opacity = motion.opacity < 1 ? String(round(Math.max(0, motion.opacity))) : '';
            const clip = state.clip ? revealClip(state.clip, this.box.w, this.box.h, this.el.id) : '';
            for (const layer of layers) {
                const own = layer.classList.contains('tl-backdrop') && this.el.o !== undefined ? this.el.o : 1;
                layer.style.opacity = opacity || own < 1 ? String(round(Math.max(0, motion.opacity) * own)) : '';
                if (layer !== node || this.el.t !== 'bd') {
                    layer.style.clipPath = clip || (layer.classList.contains('tl-backdrop') ? layer.dataset.clip || '' : '');
                }
            }
            const parts = [];
            if (motion.dx || motion.dy) {
                parts.push(`translate(${round(motion.dx)}px, ${round(motion.dy)}px)`);
            }
            const base = baseTransform(this.el, this.box.w, this.box.h);
            if (motion.rotation) {
                const index = base.findIndex((part) => part.startsWith('rotate('));
                const rotation = `rotate(${round((this.el.r || 0) + motion.rotation)}deg)`;
                if (index >= 0) {
                    base[index] = rotation;
                } else {
                    base.splice(base.length && base[0].startsWith('perspective') ? 1 : 0, 0, rotation);
                }
            }
            parts.push(...base);
            if (motion.skew) {
                parts.push(`skewX(${round(motion.skew)}deg)`);
            }
            if (motion.sx !== 1 || motion.sy !== 1) {
                parts.push(`scale(${round(motion.sx)}, ${round(motion.sy)})`);
            }
            node.style.transform = parts.join(' ');
            shapeColors(this.fills, this.strokes, state, this.background && this.background.state);
            for (const {node: span, run} of this.runs) {
                span.style.color = state.color ? formatColor(state.color) : run.c;
                span.style.fontWeight = state.weight || (run.b ? 'bold' : '');
                span.style.fontStyle = state.italic || (run.i ? 'italic' : '');
                if (state.underline) {
                    span.style.textDecorationLine = 'underline';
                } else if (!run.u && !run.st) {
                    span.style.textDecorationLine = '';
                }
                span.style.fontFamily = state.family || run.f;
                span.style.fontSize = `calc(${round((run.bl ? run.sz * 2 / 3 : run.sz) * (state.size || 1))}px * var(--tl-fs, 1))`;
            }
            if (this.parts) {
                for (const part of this.parts) {
                    part.render();
                }
            }
            if (this.background) {
                this.background.render();
            }
        }

        // 글자마다 움직이는 효과를 위해 나눈 글자들의 view
        ensureParts() {
            if (this.parts || !this.partNodes.length) {
                return this.parts;
            }
            const stage = this.context.stage;
            this.parts = this.partNodes.map((span) => new PartView(span, this, stage));
            return this.parts;
        }

        // 글자마다 움직이는 효과에서 따로 움직이는 도형 바탕
        ensureBackground() {
            if (!this.background) {
                const layer = this.node.querySelector(':scope > .tl-svg');
                if (layer) {
                    this.background = new PartView(layer, this, this.context.stage, this.box);
                }
            }
            return this.background;
        }
    }

    // 애니메이션이 바꾼 채우기와 선 색. 바탕만 움직이는 효과의 색이 먼저다
    function shapeColors(fills, strokes, state, background) {
        const pick = (name) => (background && background[name]) || state[name];
        const fill = pick('fill') ? formatColor(pick('fill')) : null;
        for (const path of fills) {
            if (fill) {
                path.dataset.fill = path.dataset.fill || path.getAttribute('fill');
                path.setAttribute('fill', fill);
            } else if (path.dataset.fill) {
                path.setAttribute('fill', path.dataset.fill);
            }
        }
        const stroke = pick('stroke') ? formatColor(pick('stroke')) : null;
        for (const path of strokes) {
            if (stroke) {
                path.dataset.stroke = path.dataset.stroke || path.getAttribute('stroke');
                path.setAttribute('stroke', stroke);
            } else if (path.dataset.stroke) {
                path.setAttribute('stroke', path.dataset.stroke);
            }
        }
    }

    // 글자 한 자나 한 단어. box가 있으면 도형 바탕(SVG) 층이다
    class PartView {
        constructor(node, owner, stage, box) {
            this.node = node;
            this.owner = owner;
            this.context = owner.context;
            this.stage = stage;
            this.box = box || null;
            this.hiddenAtStart = false;
            this.reset();
        }

        measure() {
            if (!this.box) {
                let x = 0;
                let y = 0;
                let node = this.node;
                while (node && node !== this.stage) {
                    x += node.offsetLeft;
                    y += node.offsetTop;
                    node = node.offsetParent;
                }
                this.box = {x, y, w: this.node.offsetWidth, h: this.node.offsetHeight};
            }
            return this.box;
        }

        variables() {
            const box = this.measure();
            const {W, H} = this.context;
            return {ppt_x: (box.x + box.w / 2) / W, ppt_y: (box.y + box.h / 2) / H, ppt_w: box.w / W, ppt_h: box.h / H};
        }

        reset() {
            this.state = freshState(this.hiddenAtStart);
        }

        baseFill() {
            return this.owner.baseFill();
        }

        baseStroke() {
            return this.owner.baseStroke();
        }

        baseColor() {
            return this.owner.baseColor();
        }

        render() {
            const state = this.state;
            const style = this.node.style;
            const motion = motionOf(state, this.variables(), this.context);
            style.visibility = state.visible ? '' : 'hidden';
            style.opacity = motion.opacity < 1 ? String(round(Math.max(0, motion.opacity))) : '';
            const parts = [];
            if (motion.dx || motion.dy) {
                parts.push(`translate(${round(motion.dx)}px, ${round(motion.dy)}px)`);
            }
            if (motion.rotation) {
                parts.push(`rotate(${round(motion.rotation)}deg)`);
            }
            if (motion.skew) {
                parts.push(`skewX(${round(motion.skew)}deg)`);
            }
            if (motion.sx !== 1 || motion.sy !== 1) {
                parts.push(`scale(${round(motion.sx)}, ${round(motion.sy)})`);
            }
            style.transform = parts.join(' ');
            style.clipPath = state.clip ? revealClip(state.clip, this.measure().w, this.measure().h, 0) : '';
            if (this.node.tagName.toLowerCase() === 'svg') {
                return;
            }
            style.color = state.color ? formatColor(state.color) : '';
            style.fontWeight = state.weight || '';
            style.fontStyle = state.italic || '';
            style.textDecorationLine = state.underline ? 'underline' : '';
            style.fontFamily = state.family || '';
            style.fontSize = state.size ? `${round(state.size * 100)}%` : '';
        }
    }

    // ---- 나타나기, 사라지기의 모양 (animEffect의 filter)

    // 같은 모양이 매번 같게 나오는 난수
    function random(seed) {
        let value = seed * 2654435761 % 4294967296 || 1;
        return () => {
            value ^= value << 13;
            value ^= value >>> 17;
            value ^= value << 5;
            return ((value >>> 0) % 100000) / 100000;
        };
    }

    const rect = (x, y, w, h) => w > 0 && h > 0 ? `M${round(x)} ${round(y)}h${round(w)}v${round(h)}h${round(-w)}Z` : '';

    function ellipsePath(cx, cy, rx, ry) {
        if (rx <= 0 || ry <= 0) {
            return '';
        }
        const pen = {x: cx + rx, y: cy, d: `M${round(cx + rx)} ${round(cy)}`};
        arcTo(pen, rx, ry, 0, 2 * Math.PI);
        return pen.d + 'Z';
    }

    function polygon(points) {
        return points.length ? 'M' + points.map(([x, y]) => `${round(x)} ${round(y)}`).join('L') + 'Z' : '';
    }

    // 가운데에서 위쪽을 0으로 시계 방향 from ~ to 사이의 부채꼴 (라디안). 상자를 덮을 만큼 크다
    function sector(w, h, from, to) {
        if (to <= from) {
            return '';
        }
        const cx = w / 2;
        const cy = h / 2;
        const radius = Math.hypot(w, h);
        const points = [[cx, cy]];
        const steps = Math.max(2, Math.ceil((to - from) / 0.1));
        for (let i = 0; i <= steps; i++) {
            const angle = from + (to - from) * i / steps;
            points.push([cx + radius * Math.sin(angle), cy - radius * Math.cos(angle)]);
        }
        return polygon(points);
    }

    // filter(방향)가 p(0 ~ 1)만큼 보여 준 영역. 결과는 CSS clip-path의 path
    function revealShape(name, option, p, w, h, seed) {
        const cx = w / 2;
        const cy = h / 2;
        switch (name) {
            case 'wipe':
                if (option === 'up') {
                    return rect(0, h * (1 - p), w, h * p);
                }
                if (option === 'left') {
                    return rect(w * (1 - p), 0, w * p, h);
                }
                if (option === 'right') {
                    return rect(0, 0, w * p, h);
                }
                return rect(0, 0, w, h * p);
            case 'blinds': {
                let d = '';
                for (let i = 0; i < 6; i++) {
                    d += option === 'vertical' ? rect(w * i / 6, 0, w / 6 * p, h) : rect(0, h * i / 6, w, h / 6 * p);
                }
                return d;
            }
            case 'checkerboard': {
                // 칸이 한 줄씩 어긋나 있고, 칸마다 반 칸 늦게 시작해 두 칸을 채운다
                let d = '';
                const count = 10;
                const across = option !== 'down';
                const cell = (across ? w : h) / count;
                const rows = Math.ceil((across ? h : w) / cell);
                for (let row = 0; row < rows; row++) {
                    const shift = row % 2 ? cell : 0;
                    for (let column = -1; column < count; column += 2) {
                        const start = column * cell + shift;
                        const size = clamp(p * 2 * cell, 0, 2 * cell);
                        d += across ? rect(Math.max(0, start), row * cell, Math.min(size, w - start), cell) : rect(row * cell, Math.max(0, start), cell, Math.min(size, h - start));
                    }
                }
                return d;
            }
            case 'box':
                if (option === 'in') {
                    return rect(0, 0, w, h) + rect(cx - w * (1 - p) / 2, cy - h * (1 - p) / 2, w * (1 - p), h * (1 - p));
                }
                return rect(cx - w * p / 2, cy - h * p / 2, w * p, h * p);
            case 'circle':
                if (option === 'in') {
                    return rect(0, 0, w, h) + ellipsePath(cx, cy, w * (1 - p) / Math.SQRT2, h * (1 - p) / Math.SQRT2);
                }
                return ellipsePath(cx, cy, w * p / Math.SQRT2, h * p / Math.SQRT2);
            case 'diamond':
                if (option === 'in') {
                    const q = 1 - p;
                    return rect(0, 0, w, h) + polygon([[cx, cy - h * q], [cx + w * q, cy], [cx, cy + h * q], [cx - w * q, cy]]);
                }
                return polygon([[cx, cy - h * p], [cx + w * p, cy], [cx, cy + h * p], [cx - w * p, cy]]);
            case 'plus':
                if (option === 'in') {
                    const q = (1 - p) / 2;
                    return rect(0, 0, w * (0.5 - q), h * (0.5 - q)) + rect(w * (0.5 + q), 0, w * (0.5 - q), h * (0.5 - q))
                        + rect(0, h * (0.5 + q), w * (0.5 - q), h * (0.5 - q)) + rect(w * (0.5 + q), h * (0.5 + q), w * (0.5 - q), h * (0.5 - q));
                }
                return rect(cx - w * p / 2, 0, w * p, h) + rect(0, cy - h * p / 2, w, h * p);
            case 'barn':
                switch (option) {
                    case 'inVertical': return rect(0, 0, w * p / 2, h) + rect(w - w * p / 2, 0, w * p / 2, h);
                    case 'inHorizontal': return rect(0, 0, w, h * p / 2) + rect(0, h - h * p / 2, w, h * p / 2);
                    case 'outHorizontal': return rect(0, cy - h * p / 2, w, h * p);
                    default: return rect(cx - w * p / 2, 0, w * p, h);
                }
            case 'strips': {
                // 대각선으로 쓸어 내린다. 방향은 움직이는 쪽이다
                const reach = p * 2;
                const corner = {downLeft: [w, 0, -1, 1], upLeft: [w, h, -1, -1], upRight: [0, h, 1, -1], downRight: [0, 0, 1, 1]}[option] || [0, 0, 1, 1];
                const [x0, y0, sx, sy] = corner;
                return polygon([[x0, y0], [x0 + sx * w * reach, y0], [x0, y0 + sy * h * reach]]);
            }
            case 'wedge':
                return sector(w, h, -Math.PI * p, Math.PI * p);
            case 'wheel': {
                const spokes = Number(option) || 1;
                let d = '';
                for (let i = 0; i < spokes; i++) {
                    const start = 2 * Math.PI * i / spokes;
                    d += sector(w, h, start, start + 2 * Math.PI / spokes * p);
                }
                return d;
            }
            case 'wheelReverse': {
                const spokes = Number(option) || 1;
                let d = '';
                for (let i = 0; i < spokes; i++) {
                    const end = 2 * Math.PI * (i + 1) / spokes;
                    d += sector(w, h, end - 2 * Math.PI / spokes * p, end);
                }
                return d;
            }
            case 'randombar': {
                const vertical = option === 'vertical';
                const count = Math.max(1, Math.round((vertical ? w : h) / 4));
                const next = random(seed + 7);
                let d = '';
                for (let i = 0; i < count; i++) {
                    if (next() < p) {
                        d += vertical ? rect(w * i / count, 0, w / count + 0.5, h) : rect(0, h * i / count, w, h / count + 0.5);
                    }
                }
                return d;
            }
            case 'dissolve': {
                const size = Math.max(4, Math.max(w, h) / 48);
                const columns = Math.ceil(w / size);
                const rows = Math.ceil(h / size);
                const next = random(seed + 3);
                let d = '';
                for (let y = 0; y < rows; y++) {
                    for (let x = 0; x < columns; x++) {
                        if (next() < p) {
                            d += rect(x * size, y * size, size + 0.5, size + 0.5);
                        }
                    }
                }
                return d;
            }
            default:
                return rect(0, 0, w, h);
        }
    }

    function revealClip(clip, w, h, seed) {
        const p = clamp(clip.p, 0, 1);
        const match = /^(\w+)(?:\((\w+)\))?$/.exec(clip.filter) || [];
        const d = revealShape(match[1], match[2], p, w, h, seed);
        if (!d) {
            return 'path("M0 0Z")';
        }
        return `path(evenodd, "${d}")`;
    }

    // ---- 애니메이션 수식 (#ppt_x+sin(pi*$)/3 같은 값)

    const formulaCache = new Map();

    function compileFormula(text) {
        const cached = formulaCache.get(text);
        if (cached) {
            return cached;
        }
        // 변수는 #ppt_x와 ppt_x 두 가지로 적혀 있다
        const tokens = text.match(/#?ppt_[xywh]|\$|\d*\.\d+(?:e[-+]?\d+)?|\d+(?:e[-+]?\d+)?|[a-z_]+|[-+*/^(),]/gi) || [];
        let index = 0;
        const peek = () => tokens[index];
        const next = () => tokens[index++];
        const functions = {
            sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan,
            abs: Math.abs, sqrt: Math.sqrt, ln: Math.log, exp: Math.exp, floor: Math.floor, ceil: Math.ceil,
            max: Math.max, min: Math.min, rand: (x) => Math.random() * x,
        };
        const primary = () => {
            const token = next();
            if (token === '(') {
                const value = expression();
                next();
                return value;
            }
            if (token === '-') {
                const value = unary();
                return (vars) => -value(vars);
            }
            if (token === '+') {
                return unary();
            }
            if (token === '$') {
                return (vars) => vars.$;
            }
            if (token && /^#?ppt_[xywh]$/.test(token)) {
                const name = token.replace('#', '');
                return (vars) => vars[name];
            }
            if (token === 'pi') {
                return () => Math.PI;
            }
            if (token === 'e') {
                return () => Math.E;
            }
            if (token && functions[token.toLowerCase()]) {
                const fn = functions[token.toLowerCase()];
                next();
                const args = [expression()];
                while (peek() === ',') {
                    next();
                    args.push(expression());
                }
                next();
                return (vars) => fn(...args.map((arg) => arg(vars)));
            }
            const value = Number(token);
            return () => value;
        };
        const unary = () => primary();
        const power = () => {
            let left = unary();
            while (peek() === '^') {
                next();
                const base = left;
                const exponent = unary();
                left = (vars) => Math.pow(base(vars), exponent(vars));
            }
            return left;
        };
        const term = () => {
            let left = power();
            while (peek() === '*' || peek() === '/') {
                const operator = next();
                const a = left;
                const b = power();
                left = operator === '*' ? (vars) => a(vars) * b(vars) : (vars) => a(vars) / b(vars);
            }
            return left;
        };
        const expression = () => {
            let left = term();
            while (peek() === '+' || peek() === '-') {
                const operator = next();
                const a = left;
                const b = term();
                left = operator === '+' ? (vars) => a(vars) + b(vars) : (vars) => a(vars) - b(vars);
            }
            return left;
        };
        let compiled;
        try {
            compiled = expression();
        } catch (error) {
            compiled = () => 0;
        }
        formulaCache.set(text, compiled);
        return compiled;
    }

    function evaluate(value, vars) {
        if (typeof value === 'number') {
            return value;
        }
        if (typeof value !== 'string') {
            return value;
        }
        if (/^[-+]?(\d+\.?\d*|\.\d+)$/.test(value.trim())) {
            return Number(value);
        }
        if (/ppt_|[$()*/^+]|\bpi\b/.test(value) || /^-/.test(value)) {
            return compileFormula(value)(vars);
        }
        return value;
    }

    // ---- 애니메이션 시간

    // accel, decel은 속도가 0에서 올라가고 0으로 내려가는 구간의 비율이다
    function ease(p, accel, decel) {
        accel = accel || 0;
        decel = decel || 0;
        if (!accel && !decel) {
            return p;
        }
        const speed = 1 / (1 - accel / 2 - decel / 2);
        if (p < accel) {
            return speed * p * p / (2 * accel);
        }
        if (p <= 1 - decel) {
            return speed * (p - accel / 2);
        }
        const q = 1 - p;
        return 1 - speed * q * q / (2 * decel);
    }

    function timeFilter(p, points) {
        if (!points) {
            return p;
        }
        for (let i = 1; i < points.length; i++) {
            const [x0, y0] = points[i - 1];
            const [x1, y1] = points[i];
            if (p <= x1) {
                return x1 === x0 ? y1 : y0 + (y1 - y0) * (p - x0) / (x1 - x0);
            }
        }
        return points[points.length - 1][1];
    }

    // 노드가 시작한 뒤 local ms에서의 진행률. 영향이 없으면 null
    function progress(node, local, factor) {
        const simple = node.dur === null || node.dur === undefined ? Infinity : Math.max(node.dur * factor, 0.001);
        const repeat = node.repeat === undefined ? 1 : node.repeat === null ? Infinity : node.repeat;
        const cycle = simple * (node.autoRev ? 2 : 1);
        const active = cycle * repeat;
        let p;
        if (local >= active) {
            if (node.fill !== 'hold' && node.fill !== 'freeze') {
                return null;
            }
            const rest = repeat % 1;
            if (rest && Number.isFinite(repeat)) {
                p = rest * (node.autoRev ? 2 : 1);
                p = p > 1 ? 2 - p : p;
            } else {
                p = node.autoRev ? 0 : 1;
            }
        } else if (!Number.isFinite(simple)) {
            p = 0;
        } else {
            p = (local % cycle) / simple;
            if (p > 1) {
                p = 2 - p;
            }
        }
        return timeFilter(ease(p, node.accel, node.decel), node.tmFilter);
    }

    function nodeEnd(node, factor) {
        const delay = (node.delay || 0) * factor;
        if (node.c) {
            return delay + Math.max(0, ...node.c.map((child) => nodeEnd(child, factor)));
        }
        const simple = node.dur === null || node.dur === undefined ? 0 : node.dur * factor;
        const repeat = node.repeat === undefined ? 1 : node.repeat === null ? 0 : node.repeat;
        return delay + simple * (node.autoRev ? 2 : 1) * repeat;
    }

    // PowerPoint 이동 경로(M, L, C, Z, E. 슬라이드에 대한 비율)를 고르게 나눈 점들
    function motionPoints(text, W, H) {
        const tokens = text.trim().split(/[\s,]+/);
        const points = [];
        let index = 0;
        let x = 0;
        let y = 0;
        let startX = 0;
        let startY = 0;
        const read = () => Number(tokens[index++]);
        while (index < tokens.length) {
            const command = tokens[index++];
            if (command === 'M' || command === 'm') {
                x = read();
                y = read();
                startX = x;
                startY = y;
                points.push([x, y]);
            } else if (command === 'L' || command === 'l') {
                x = read();
                y = read();
                points.push([x, y]);
            } else if (command === 'C' || command === 'c') {
                const [x1, y1, x2, y2, x3, y3] = [read(), read(), read(), read(), read(), read()];
                for (let i = 1; i <= 16; i++) {
                    const t = i / 16;
                    const u = 1 - t;
                    points.push([u * u * u * x + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3, u * u * u * y + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3]);
                }
                x = x3;
                y = y3;
            } else if (command === 'Z' || command === 'z') {
                x = startX;
                y = startY;
                points.push([x, y]);
            } else if (command === 'E' || command === 'e') {
                break;
            } else if (!Number.isNaN(Number(command))) {
                index--;
                x = read();
                y = read();
                points.push([x, y]);
            }
        }
        if (!points.length) {
            points.push([0, 0]);
        }
        const lengths = [0];
        for (let i = 1; i < points.length; i++) {
            lengths.push(lengths[i - 1] + Math.hypot((points[i][0] - points[i - 1][0]) * W, (points[i][1] - points[i - 1][1]) * H));
        }
        return {points, lengths};
    }

    function pointAt(motion, p) {
        const {points, lengths} = motion;
        const total = lengths[lengths.length - 1];
        if (total === 0 || points.length === 1) {
            return points[0];
        }
        const target = clamp(p, 0, 1) * total;
        for (let i = 1; i < points.length; i++) {
            if (target <= lengths[i]) {
                const span = lengths[i] - lengths[i - 1] || 1;
                const t = (target - lengths[i - 1]) / span;
                return [points[i - 1][0] + (points[i][0] - points[i - 1][0]) * t, points[i - 1][1] + (points[i][1] - points[i - 1][1]) * t];
            }
        }
        return points[points.length - 1];
    }

    function themeColor(value, theme) {
        if (value && typeof value === 'object') {
            if (value.scheme) {
                return parseColor(theme[value.scheme] || theme.accent1);
            }
            return parseColor({white: '#ffffff', black: '#000000'}[value.preset] || '#000000');
        }
        return parseColor(value);
    }

    // anim의 tav 목록이나 from, to, by에서 p일 때의 값
    function animValue(node, p, vars, current) {
        const list = node.tav;
        if (list && list.length) {
            let index = 0;
            while (index < list.length - 1 && list[index + 1][0] !== null && p >= list[index + 1][0]) {
                index++;
            }
            if (node.calc === 'discrete' || node.vt === 'str' || list.length === 1) {
                const [, raw, fmla] = list[index];
                const value = evaluate(raw, vars);
                return fmla && typeof value === 'number' ? compileFormula(fmla)({...vars, $: value}) : value;
            }
            // 끝에서는 마지막 구간의 끝값이다. 구간의 수식은 그 끝값에도 쓴다
            if (index === list.length - 1) {
                index--;
            }
            const [time, raw, fmla] = list[index];
            const [nextTime, nextRaw] = list[index + 1];
            const from = evaluate(raw, vars);
            const to = evaluate(nextRaw, vars);
            const t = nextTime === time ? 1 : (p - time) / (nextTime - time);
            const value = typeof from === 'number' && typeof to === 'number' ? from + (to - from) * t : t < 1 ? from : to;
            return fmla ? compileFormula(fmla)({...vars, $: value}) : value;
        }
        const from = node.from !== undefined ? evaluate(node.from, vars) : current;
        if (node.to !== undefined) {
            const to = evaluate(node.to, vars);
            return typeof to === 'number' && typeof from === 'number' ? from + (to - from) * p : p > 0 ? to : from;
        }
        if (node.by !== undefined) {
            const by = evaluate(node.by, vars);
            return typeof by === 'number' ? from + by * p : from;
        }
        return current;
    }

    // 절대값을 갖는 속성. 나중에 적용한 애니메이션이 앞의 것을 덮는다 (SMIL의 sandwich model)
    const ABSOLUTE = {ppt_x: 'ppt_x', ppt_y: 'ppt_y', ppt_w: 'ppt_w', ppt_h: 'ppt_h', r: 'rotation', 'style.rotation': 'rotation', xshear: 'xshear', 'style.opacity': 'opacity'};

    // 값 하나를 상태에 적는다
    function applyAttribute(state, name, value, vars) {
        if (name in ABSOLUTE) {
            const number = Number(evaluate(value, vars));
            if (Number.isFinite(number)) {
                state.values[ABSOLUTE[name]] = number;
            }
            return;
        }
        switch (name) {
            case 'style.visibility':
                state.visible = value === 'visible';
                break;
            case 'style.fontSize':
                state.size = Number(evaluate(value, vars)) || 1;
                break;
            case 'style.fontWeight':
                state.weight = value;
                break;
            case 'style.fontStyle':
                state.italic = value;
                break;
            case 'style.textDecorationUnderline':
                state.underline = value === 'true' || value === true;
                break;
            case 'style.fontFamily':
                state.family = `"${value}"`;
                break;
            default:
                break;
        }
    }

    function colorTarget(name) {
        if (name === 'style.color') {
            return 'color';
        }
        if (name === 'fillcolor' || name === 'fillColor') {
            return 'fill';
        }
        return name === 'stroke.color' ? 'stroke' : null;
    }

    // 효과 노드 하나를 view의 상태에 반영한다. local은 부모가 시작한 뒤의 ms
    function applyNode(node, local, factor, view, effect) {
        const start = local - (node.delay || 0) * factor;
        if (start < 0) {
            return;
        }
        if (node.t === 'par' || node.t === 'seq') {
            for (const child of node.c) {
                applyNode(child, start, factor, view, effect);
            }
            return;
        }
        const p = progress(node, start, factor);
        if (p === null) {
            return;
        }
        const state = view.state;
        const context = view.context;
        const vars = view.variables();
        switch (node.t) {
            case 'set':
                for (const name of node.attr || []) {
                    const target = colorTarget(name);
                    if (target) {
                        state[target] = themeColor(node.to, context.theme);
                    } else {
                        applyAttribute(state, name, node.to, vars);
                    }
                }
                break;
            case 'anim':
                for (const name of node.attr || []) {
                    // from이 없으면 지금 값에서 시작한다
                    const key = ABSOLUTE[name];
                    let current = name === 'style.fontSize' ? state.size || 1 : key in {rotation: 1, xshear: 1} ? 0 : key === 'opacity' ? 1 : vars[name];
                    if (key && key in state.values) {
                        current = state.values[key];
                    }
                    applyAttribute(state, name, animValue(node, p, vars, current), vars);
                }
                break;
            case 'effect': {
                const shown = node.tr === 'out' ? 1 - p : p;
                if (node.filter === 'fade') {
                    state.opacity *= shown;
                } else if (node.filter !== 'image') {
                    state.clip = shown >= 1 ? null : {filter: node.filter, p: shown};
                }
                break;
            }
            case 'scale': {
                // by는 곱하고, from, to는 원래 크기에 대한 배율이라 앞의 것을 덮는다
                if (node.by) {
                    state.sx *= 1 + (node.by[0] - 1) * p;
                    state.sy *= 1 + (node.by[1] - 1) * p;
                } else {
                    const from = node.from || state.scale || [1, 1];
                    const to = node.to || [1, 1];
                    state.scale = [from[0] + (to[0] - from[0]) * p, from[1] + (to[1] - from[1]) * p];
                }
                break;
            }
            case 'rot':
                if (node.by !== undefined) {
                    state.rotation += node.by * p;
                } else {
                    const from = node.from !== undefined ? node.from : state.values.rotation || 0;
                    state.values.rotation = from + ((node.to || 0) - from) * p;
                }
                break;
            case 'motion': {
                const path = effect.path || node.path;
                const key = `${path}|${context.W}|${context.H}`;
                let motion = effect.motions.get(key);
                if (!motion) {
                    motion = motionPoints(path, context.W, context.H);
                    effect.motions.set(key, motion);
                }
                const [x, y] = pointAt(motion, p);
                state.dx += x * context.W;
                state.dy += y * context.H;
                break;
            }
            case 'clr': {
                const target = colorTarget((node.attr || [])[0]);
                if (!target) {
                    break;
                }
                const base = state[target] || (target === 'fill' ? view.baseFill() : target === 'stroke' ? view.baseStroke() : view.baseColor());
                let color;
                if (node.by && typeof node.by === 'object' && 'h' in node.by) {
                    const hsl = toHsl(base);
                    color = fromHsl({h: hsl.h + node.by.h * p, s: hsl.s + node.by.s / 100 * p, l: hsl.l + node.by.l / 100 * p, a: hsl.a});
                } else if (node.to) {
                    const to = themeColor(node.to, context.theme);
                    if (node.space === 'hsl') {
                        const a = toHsl(base);
                        const b = toHsl(to);
                        let dh = b.h - a.h;
                        if (node.dir === 'cw' && dh < 0) {
                            dh += 360;
                        } else if (node.dir === 'ccw' && dh > 0) {
                            dh -= 360;
                        }
                        color = fromHsl({h: a.h + dh * p, s: a.s + (b.s - a.s) * p, l: a.l + (b.l - a.l) * p, a: a.a + (b.a - a.a) * p});
                    } else {
                        color = mix(base, {...to, a: base.a}, p);
                    }
                } else {
                    color = base;
                }
                state[target] = color;
                break;
            }
            default:
                break;
        }
    }

    // ---- 애니메이션 시간표

    // slide의 animate들을 클릭 묶음으로 나눈다. 묶음 안에서는 after_previous마다 앞 효과들이 끝난 뒤에 시작한다
    function buildTimeline(slide, context) {
        const groups = [];
        for (const item of slide.an || []) {
            const view = context.views.get(item.el);
            // 비디오, 오디오의 재생, 일시 중지, 중지는 그 차례에 한 번 부르는 명령이다. 재생은 미디어 길이만큼 걸린다
            const command = item.key.startsWith('media.') ? item.key.slice(6) : null;
            const preset = command ? {dur: command === 'play' && view ? view.el.len || 0 : 0, c: []} : DATA.animations[item.key];
            if (!view || !preset) {
                continue;
            }
            let duration = preset.dur;
            let factor = 1;
            if (item.d !== undefined) {
                factor = duration > 0 ? item.d / duration : 1;
                duration = item.d;
            }
            const delay = item.dl || 0;
            if (item.st === 'on_click' || !groups.length) {
                groups.push({automatic: item.st !== 'on_click', steps: [{start: 0, end: 0}], effects: [], length: 0, endless: false});
            } else if (item.st === 'after_previous') {
                const group = groups[groups.length - 1];
                const last = group.steps[group.steps.length - 1];
                group.steps.push({start: last.end, end: last.end});
            }
            const group = groups[groups.length - 1];
            const step = group.steps[group.steps.length - 1];
            const effect = {view, preset, begin: step.start + delay, factor, path: item.path, motions: new Map(), entrance: item.key.startsWith('enter.'), command};
            // 글자마다 시작하는 효과는 마지막 글자가 늦게 시작한 만큼 길어진다
            let length = Math.max(duration, 0);
            if (preset.it && view.iterate) {
                effect.iterate = preset.it;
            }
            group.effects.push(effect);
            step.end = Math.max(step.end, step.start + delay + length);
            group.length = Math.max(group.length, step.end, effect.begin + nodeEndAll(preset, factor));
            if (preset.dur < 0) {
                group.endless = true;
            }
        }
        // 들어오는 효과가 처음인 element는 처음에 숨는다
        const first = new Map();
        for (const group of groups) {
            for (const effect of group.effects) {
                if (!first.has(effect.view)) {
                    first.set(effect.view, effect);
                }
            }
        }
        for (const [view, effect] of first) {
            if (effect.entrance) {
                if (effect.iterate) {
                    view.hiddenPartsAtStart = true;
                    view.hiddenBackgroundAtStart = !!effect.preset.bg;
                } else {
                    view.hiddenAtStart = true;
                }
            }
        }
        return groups;
    }

    function nodeEndAll(preset, factor) {
        return Math.max(0, ...preset.c.map((child) => nodeEnd(child, factor)));
    }

    // 효과 하나를 group이 시작한 뒤 t ms일 때로 적용한다
    function applyEffect(effect, t) {
        const local = t - effect.begin;
        if (local < 0 || effect.command) {
            return;
        }
        if (effect.iterate) {
            const parts = effect.view.ensureParts() || [];
            const length = nodeEndAll(effect.preset, effect.factor);
            const interval = effect.iterate.ms !== undefined ? effect.iterate.ms * effect.factor : (effect.iterate.pct || 0) * length;
            // 글자마다 효과를 주는 동안 element 자체는 보이고, bg면 도형 바탕이 첫 글자와 함께 움직인다
            effect.view.state.visible = true;
            if (effect.preset.bg) {
                const background = effect.view.ensureBackground();
                if (background) {
                    for (const node of effect.preset.c) {
                        applyNode(node, local, effect.factor, background, effect);
                    }
                }
            }
            parts.forEach((part, index) => {
                for (const node of effect.preset.c) {
                    applyNode(node, local - index * interval, effect.factor, part, effect);
                }
            });
            return;
        }
        for (const node of effect.preset.c) {
            applyNode(node, local, effect.factor, effect.view, effect);
        }
    }

    // slide 하나의 애니메이션 재생. 상태는 매 프레임 처음부터 다시 계산한다
    class Player {
        constructor(slide, context) {
            this.context = context;
            this.groups = buildTimeline(slide, context);
            this.views = Array.from(context.views.values());
            this.frame = 0;
            this.clickGroups = this.groups.filter((group) => !group.automatic);
            this.onFinish = null;
            this.fired = new Set();
            for (const view of this.views) {
                if (view.hiddenPartsAtStart) {
                    for (const part of view.ensureParts() || []) {
                        part.hiddenAtStart = true;
                    }
                    const background = view.hiddenBackgroundAtStart ? view.ensureBackground() : null;
                    if (background) {
                        background.hiddenAtStart = true;
                    }
                }
            }
        }

        // 클릭 묶음 번호(자동으로 시작하는 첫 묶음 제외)를 전체 묶음 번호로
        groupIndex(click) {
            return this.groups.indexOf(this.clickGroups[click]);
        }

        // 재생 중인 묶음에서 시작 시간이 지난 미디어 명령을 한 번씩 부른다
        command(group, time) {
            for (const effect of group.effects) {
                if (effect.command && time >= effect.begin && !this.fired.has(effect)) {
                    this.fired.add(effect);
                    const control = this.context.mediaOf && this.context.mediaOf.get(effect.view.el.id);
                    if (control) {
                        control[effect.command]();
                    }
                }
            }
        }

        draw(done, current, time) {
            for (const view of this.views) {
                view.reset();
            }
            for (let i = 0; i < done; i++) {
                for (const effect of this.groups[i].effects) {
                    applyEffect(effect, Infinity);
                }
            }
            if (current !== null && current < this.groups.length) {
                for (const effect of this.groups[current].effects) {
                    applyEffect(effect, time);
                }
            }
            for (const view of this.views) {
                view.render();
            }
        }

        stop() {
            cancelAnimationFrame(this.frame);
            this.frame = 0;
        }

        // 앞의 count개 묶음이 끝난 상태로 둔다. 비디오와 오디오는 처음 상태로 되돌린다
        settle(count) {
            this.stop();
            for (const control of this.context.medias || []) {
                control.reset();
            }
            this.fired.clear();
            this.draw(count, null, 0);
        }

        // index번째 묶음을 재생한다. 앞의 묶음은 끝난 상태다
        play(index, finished) {
            this.stop();
            const group = this.groups[index];
            if (!group) {
                this.draw(index, null, 0);
                if (finished) {
                    finished();
                }
                return;
            }
            const started = performance.now();
            for (const effect of group.effects) {
                this.fired.delete(effect);
            }
            const tick = (now) => {
                const time = now - started;
                this.draw(index, index, time);
                this.command(group, time);
                if (time < group.length || group.endless) {
                    this.frame = requestAnimationFrame(tick);
                } else {
                    this.frame = 0;
                    if (finished) {
                        finished();
                    }
                }
            };
            this.frame = requestAnimationFrame(tick);
            this.draw(index, index, 0);
            this.command(group, 0);
        }
    }

    // ---- 전환

    const DIRECTION = {left: [-1, 0], right: [1, 0], up: [0, -1], down: [0, 1], left_up: [-1, -1], right_up: [1, -1], left_down: [-1, 1], right_down: [1, 1]};

    // 3차원 전환의 카메라는 슬라이드 높이의 1.5배 떨어져 있다. 아래 수치는 모두 PowerPoint이 내보낸 동영상에서 잰 값이다
    const CAMERA = 1.5;

    const sineEase = (p) => (1 - Math.cos(Math.PI * clamp(p, 0, 1))) / 2;
    const smoothstep = (t) => {
        t = clamp(t, 0, 1);
        return t * t * (3 - 2 * t);
    };

    // 정면에서 phi도 돌아간 면의 밝기
    const faceLight = (phi) => Math.min(1, 0.295 + Math.cos(phi * DEG));

    function lighten(node, light) {
        node.style.filter = light < 1 ? `brightness(${round(light)})` : '';
    }

    // slide의 개체 층. layout이 있으면 그 층 다음에 있다
    function contentLayer(stage) {
        const layers = Array.from(stage.children).filter((node) => node.classList.contains('tl-layer'));
        return layers[layers.length - 1];
    }

    // 개체만 남긴 복사본. 배경은 비운다
    function frontPart(stage) {
        const copy = cloneStage(stage);
        const front = contentLayer(copy);
        for (const node of Array.from(copy.children)) {
            if (node.classList.contains('tl-layer') && node !== front) {
                node.remove();
            }
        }
        copy.style.background = 'transparent';
        return copy;
    }

    // 배경(슬라이드 배경과 layout)은 제자리에서 사인 곡선으로 섞고 개체만 움직이는 전환의 층.
    // 아래부터 옛 배경, 새 배경, 옛 개체, 새 개체이고 진짜 새 슬라이드는 끝날 때까지 숨긴다
    function contentParts(ctx) {
        contentLayer(ctx.old).remove();
        const nextBack = cloneStage(ctx.stage);
        contentLayer(nextBack).remove();
        const parts = {nextBack, old: frontPart(ctx.oldStage), next: frontPart(ctx.stage)};
        for (const node of [parts.nextBack, parts.old, parts.next]) {
            ctx.layer.appendChild(node);
        }
        ctx.stage.style.visibility = 'hidden';
        return parts;
    }

    function crossfade(parts, p) {
        parts.nextBack.style.opacity = String(round(sineEase(p)));
    }

    // 슬라이드 전체가 검은 바탕 위에서 움직이는 전환의 층
    function wholeParts(ctx) {
        ctx.layer.style.background = '#000';
        const next = cloneStage(ctx.stage);
        ctx.layer.appendChild(next);
        ctx.stage.style.visibility = 'hidden';
        return {old: ctx.old, next};
    }

    // cube, rotate는 바깥에서, box, orbit는 안에서 보는 오각기둥이다. 면 사이는 72도이고 기둥은 사인 곡선으로 72도 돈다.
    // 바깥에서 보면 가장 가까운 모서리가 슬라이드 자리에 머물도록 물러나고, 안에서 보면 sin^2 곡선으로 물러난다
    function drawPrism(p, option, W, H, parts, inside) {
        const vertical = option === 'up' || option === 'down';
        const span = vertical ? H : W;
        const theta = 72 * sineEase(p);
        const sign = (option === 'left' || option === 'down' ? 1 : -1) * (inside ? -1 : 1);
        let a;
        let dz;
        if (inside) {
            a = 0.736 * span;
            dz = (vertical ? 0.378 : 0.512) * span * Math.sin(Math.PI * p) ** 2;
        } else {
            a = span / 2 / Math.tan(36 * DEG);
            dz = a * (Math.cos((36 - theta) * DEG) / Math.cos(36 * DEG) - 1);
        }
        const turn = vertical ? 'rotateX' : 'rotateY';
        const place = (node, phi) => placeFace(node, inside
            ? `perspective(${round(CAMERA * H)}px) translateZ(${round(a - dz)}px) ${turn}(${round(-phi)}deg) translateZ(${round(-a)}px)`
            : `perspective(${round(CAMERA * H)}px) translateZ(${round(-a - dz)}px) ${turn}(${round(-phi)}deg) translateZ(${round(a)}px)`, faceLight(phi));
        place(parts.old, sign * theta);
        place(parts.next, sign * (theta - 72));
    }

    // flyThrough의 새 슬라이드는 카메라에서 옛 슬라이드까지 거리의 1.2배 뒤(in) 또는 앞(out)에 있고, 카메라가 그만큼 움직인다.
    // bounce는 (p / 0.78)^2로 다가가 조금 지나쳤다 돌아온다. 옛 개체는 카메라가 움직인 만큼 흐려지고,
    // 새 개체는 out이면 시간에 비례해, in이면 잰 값을 이어 나타난다
    const FLY_DEPTH = 1.2;
    const FLY_BOUNCE = [[0.78, 1], [0.8, 1.019], [0.825, 1.029], [0.85, 1.025], [0.875, 1.011], [0.9, 0.994], [0.925, 0.99], [0.95, 0.992], [0.975, 0.998], [1, 1]];
    const FLY_IN = [[0, 0], [0.05, 0.028], [0.1, 0.061], [0.15, 0.078], [0.2, 0.09], [0.25, 0.11], [0.3, 0.125], [0.35, 0.16], [0.4, 0.206], [0.45, 0.253],
        [0.5, 0.306], [0.55, 0.377], [0.6, 0.468], [0.65, 0.554], [0.7, 0.69], [0.75, 0.762], [0.8, 0.817], [0.85, 0.866], [0.9, 0.923], [0.95, 0.967], [1, 1]];
    const FLY_IN_BOUNCE = [[0, 0], [0.05, 0.029], [0.1, 0.059], [0.15, 0.085], [0.2, 0.105], [0.25, 0.125], [0.3, 0.135], [0.35, 0.165], [0.4, 0.193], [0.45, 0.23],
        [0.5, 0.266], [0.55, 0.315], [0.6, 0.393], [0.65, 0.511], [0.7, 0.69], [0.75, 0.759], [0.8, 0.81], [0.85, 0.856], [0.9, 0.917], [0.95, 0.954], [1, 1]];

    // 카메라에서 depth(옛 슬라이드까지 거리를 1로 잰) 떨어진 개체. 카메라 뒤로 가면 그리지 않는다
    function placeDepth(node, depth, opacity) {
        if (depth <= 0.005) {
            node.style.visibility = 'hidden';
            return;
        }
        node.style.visibility = '';
        node.style.transform = `scale(${round(1 / depth)})`;
        node.style.opacity = String(round(clamp(opacity, 0, 1)));
    }

    // reveal: 개체가 가로로 시간이 어긋난 smoothstep 곡선으로 사라지고 나타난다 (left는 오른쪽부터 사라지고 왼쪽부터 나타난다).
    // black은 슬라이드 전체가 같은 방식으로 어두워졌다 밝아진다
    function revealRamp(value, mirror) {
        const stops = [];
        for (let i = 0; i <= 16; i++) {
            const x = i / 16;
            stops.push(`rgba(0,0,0,${round(clamp(value(mirror ? 1 - x : x), 0, 1))}) ${round(x * 100)}%`);
        }
        return `linear-gradient(to right, ${stops.join(', ')})`;
    }

    function maskWith(node, gradient) {
        node.style.webkitMaskImage = gradient;
        node.style.maskImage = gradient;
    }

    // flashbulb: 색 v를 v x (1 + a/78) + a로 밝힌다 (a는 0~255). 옛 슬라이드는 빠르게 하얘지고, 새 슬라이드는 하얀 데서 천천히 돌아온다
    function flashAmount(p) {
        if (p < 0.108) {
            return Math.max(0, 1365 * (p - 0.011));
        }
        return p < 0.154 ? 300 : Math.max(0, 345 * (0.886 - p));
    }

    const PLUS_LIGHTER = typeof CSS !== 'undefined' && CSS.supports && CSS.supports('mix-blend-mode', 'plus-lighter') ? 'plus-lighter' : 'screen';

    // comb: 옛 슬라이드를 띠로 나눠 띠마다 옆으로 밀어내고 새 슬라이드는 제자리에서 드러난다. 띠의 방향은 번갈아 바뀌고,
    // 이동은 시간의 제곱이다. horizontal은 7개의 가로 띠가 아래부터 차례로, vertical은 10개의 세로 띠가 한꺼번에 움직인다
    function combStrips(ctx) {
        const vertical = ctx.option === 'vertical';
        const count = vertical ? 10 : 7;
        const size = (vertical ? ctx.W : ctx.H) / count;
        ctx.old.style.display = 'none';
        const strips = [];
        for (let k = 0; k < count; k++) {
            const node = cloneStage(ctx.oldStage);
            node.style.clipPath = vertical ? `inset(0 ${round(ctx.W - (k + 1) * size)}px 0 ${round(k * size)}px)` : `inset(${round(k * size)}px 0 ${round(ctx.H - (k + 1) * size)}px 0)`;
            ctx.layer.appendChild(node);
            strips.push({node, start: vertical ? 0.165 : 0.0238 + 0.0477 * (count - 1 - k), length: vertical ? 0.4724 : 0.6287, sign: k % 2 ? -1 : 1});
        }
        return {vertical, strips};
    }

    // window, doors: 카메라가 (p / 0.705)^2로 다가가고 옛 슬라이드는 가운데에서 두 짝으로 나뉘어 바깥 가장자리를 축으로 뒤로 열린다.
    // 짝은 새 슬라이드보다 앞에 있고 카메라가 다가간 만큼 투명해진다. window는 개체만, doors는 슬라이드 전체가 움직인다
    const WINDOW_OPACITY = [[0, 0], [0.05, 0.05], [0.1, 0.098], [0.15, 0.111], [0.2, 0.168], [0.25, 0.226], [0.3, 0.27], [0.35, 0.313], [0.4, 0.354], [0.45, 0.395],
        [0.5, 0.458], [0.55, 0.507], [0.6, 0.538], [0.65, 0.584], [0.7, 0.658], [0.75, 0.747], [0.8, 0.795], [0.85, 0.839], [0.9, 0.892], [0.95, 0.942], [1, 1]];

    function paneParts(ctx, content) {
        const parts = content ? contentParts(ctx) : wholeParts(ctx);
        const vertical = ctx.option === 'vertical';
        const second = content ? frontPart(ctx.oldStage) : cloneStage(ctx.oldStage);
        parts.old.style.clipPath = vertical ? `inset(0 ${round(ctx.W / 2)}px 0 0)` : `inset(0 0 ${round(ctx.H / 2)}px 0)`;
        second.style.clipPath = vertical ? `inset(0 0 0 ${round(ctx.W / 2)}px)` : `inset(${round(ctx.H / 2)}px 0 0 0)`;
        ctx.layer.appendChild(parts.old);
        ctx.layer.appendChild(second);
        parts.panes = [parts.old, second];
        return parts;
    }

    // depth는 새 슬라이드가 옛 슬라이드 뒤로 떨어진 거리(카메라 거리 F에 대한 비율), turn은 짝이 다 열린 각도
    function drawPanes(p, option, W, H, parts, depth, turn) {
        const F = CAMERA * H;
        const q = Math.min(1, (p / 0.705) ** 2);
        const c = depth * F * q;
        const opacity = 1 - c / F;
        const vertical = option === 'vertical';
        parts.panes.forEach((node, k) => {
            if (opacity <= 0.005) {
                node.style.visibility = 'hidden';
                return;
            }
            const sign = k === 0 ? -1 : 1;
            const edge = round(sign * (vertical ? W : H) / 2);
            node.style.visibility = '';
            node.style.opacity = String(round(opacity));
            placeFace(node, vertical
                ? `perspective(${round(F)}px) translateZ(${round(c)}px) translateX(${edge}px) rotateY(${round(-sign * turn * q)}deg) translateX(${-edge}px)`
                : `perspective(${round(F)}px) translateZ(${round(c)}px) translateY(${edge}px) rotateX(${round(sign * turn * q)}deg) translateY(${-edge}px)`, 1);
        });
        parts.next.style.transform = `perspective(${round(F)}px) translateZ(${round(c - depth * F)}px)`;
    }

    // flip: 슬라이드가 세로축으로 180도 돌며 뒤로 물러났다 돌아온다. 각도와 물러나는 거리(폭 1280px 기준)는 잰 값이다
    const FLIP_TURN = [[0, 0], [0.05, 0.3], [0.1, 0.9], [0.15, 2.8], [0.2, 5.6], [0.25, 9.3], [0.275, 12], [0.3, 16.5], [0.325, 23.2], [0.35, 32.1],
        [0.375, 43.3], [0.4, 56.6], [0.425, 71.4], [0.45, 86.7], [0.475, 102], [0.5, 116.3], [0.525, 129], [0.55, 139.8], [0.575, 148.8], [0.6, 156.1],
        [0.625, 162], [0.65, 166.7], [0.675, 170.3], [0.7, 173], [0.725, 174.9], [0.75, 176.3], [0.8, 177.9], [0.85, 178.9], [0.9, 179.7], [1, 180]];
    const FLIP_DEPTH = [[0, 0], [0.05, -4.5], [0.1, -17], [0.15, -37], [0.2, -65], [0.25, -99], [0.3, -138], [0.35, -184], [0.4, -235], [0.425, -256],
        [0.45, -269], [0.475, -270], [0.5, -256], [0.525, -228], [0.55, -194], [0.575, -163], [0.6, -139], [0.65, -104], [0.7, -75], [0.75, -50.5],
        [0.8, -31], [0.85, -17], [0.9, -7], [0.95, -2], [1, 0]];

    // 카메라에서 본 3차원 판. 뒷면이 보이면 그리지 않는다. light는 밝기(0~1)
    function placeFace(node, transform, light) {
        node.style.backfaceVisibility = 'hidden';
        node.style.webkitBackfaceVisibility = 'hidden';
        node.style.transform = transform;
        lighten(node, light);
    }

    // switch: 두 슬라이드가 서로 거울처럼 옆으로 비켜나며 자리를 바꾼다. 옛 슬라이드는 왼쪽으로 나갔다 새 슬라이드 뒤로 돌아가고,
    // 새 슬라이드는 옛 슬라이드 뒤에서 오른쪽으로 나왔다 앞으로 온다. 거리는 폭 1280px 기준으로 잰 값이다
    const SWITCH_SHIFT = [[0, 0], [0.05, 15], [0.1, 66], [0.15, 130.6], [0.2, 208], [0.25, 292.5], [0.3, 394], [0.35, 475], [0.4, 545], [0.45, 599.5],
        [0.5, 631], [0.55, 628], [0.6, 594], [0.65, 533], [0.7, 436], [0.75, 342], [0.8, 247], [0.85, 157], [0.9, 70], [0.95, 21], [1, 0]];
    const SWITCH_OLD_TURN = [[0, 0], [0.05, 0.63], [0.1, 1.53], [0.15, 3.28], [0.2, 5.48], [0.25, 7.78], [0.3, 10.46], [0.35, 12.52], [0.4, 14.42], [0.45, 15.88],
        [0.5, 16.6], [0.55, 16.6], [0.6, 15.5], [0.65, 14], [0.7, 11.5], [0.75, 8.9], [0.8, 5.8], [0.9, 2.5], [1, 0]];
    const SWITCH_OLD_DEPTH = [[0, 0], [0.05, -2.4], [0.1, -4.2], [0.15, -7.7], [0.2, -11.8], [0.25, -18.2], [0.3, -27.6], [0.35, -37.1], [0.4, -47.2],
        [0.45, -57.9], [0.5, -73], [0.55, -88], [0.6, -108], [0.65, -131], [0.7, -161], [0.75, -187], [0.8, -211], [0.9, -240], [1, -255]];
    const SWITCH_NEW_TURN = [[0, 0], [0.15, -1.5], [0.2, -3], [0.25, -5], [0.3, -8], [0.35, -10.5], [0.4, -12], [0.45, -12.9], [0.5, -13.6], [0.55, -13.5],
        [0.6, -13.2], [0.65, -11.6], [0.7, -9.7], [0.75, -7.4], [0.8, -5.3], [0.85, -3.4], [0.9, -1.45], [0.95, -0.28], [1, 0]];
    const SWITCH_NEW_DEPTH = [[0, -250], [0.15, -230], [0.3, -200], [0.35, -186], [0.4, -159], [0.45, -141.5], [0.5, -121], [0.55, -106], [0.6, -89.5],
        [0.65, -75], [0.7, -56], [0.75, -43], [0.8, -29], [0.85, -17], [0.9, -7], [0.95, -1.6], [1, 0]];

    // gallery, conveyor: 카메라가 세로축으로 15도 돌며 물러난 뒤(p 0.325까지), 슬라이드들이 1410px(폭 1280 기준) 간격으로 줄지어
    // 옆으로 미끄러지고, 거울처럼 되돌아온다. 둘은 카메라가 움직이는 거리만 다르다. gallery는 바닥에 슬라이드가 비친다
    const GALLERY_TURN = [[0, 0], [0.025, 0.92], [0.05, 2.22], [0.075, 3.3], [0.1, 4.8], [0.125, 5.86], [0.15, 7.23], [0.175, 8.3], [0.2, 9.46], [0.225, 10.6],
        [0.25, 11.58], [0.275, 12.69], [0.3, 14.3], [0.325, 15]];
    const GALLERY_CAMERA = {
        gallery: {x: [[0, 0], [0.025, 0.7], [0.05, 1.9], [0.075, 3.2], [0.1, 6.9], [0.125, 10.4], [0.15, 14.6], [0.175, 20.2], [0.2, 25.7], [0.225, 32.9],
            [0.25, 39.5], [0.275, 47.9], [0.3, 59.9], [0.325, 64.6]],
        z: [[0, 0], [0.025, -18.6], [0.05, -37.9], [0.075, -57.7], [0.1, -82.5], [0.125, -101.7], [0.15, -121.5], [0.175, -140.3], [0.2, -159.1], [0.225, -176.8],
            [0.25, -194.9], [0.275, -213.3], [0.3, -236.9], [0.325, -249]]},
        conveyor: {x: [[0, 0], [0.025, 0.9], [0.075, 2.8], [0.125, 6.5], [0.175, 11.5], [0.225, 18.7], [0.275, 27.9], [0.325, 36.8]],
            z: [[0, 0], [0.025, -11.1], [0.075, -32.2], [0.125, -58.1], [0.175, -78.3], [0.225, -99], [0.275, -119.9], [0.325, -139]]},
    };
    const GALLERY_SLIDE = [[0, 0], [0.325, 0], [0.35, 22], [0.375, 68], [0.4, 137], [0.425, 233], [0.45, 352], [0.475, 500], [0.5, 705]];
    // 바닥에 비친 그림의 밝기. 슬라이드 아래 변에서 6px(높이 720 기준) 떨어져 시작한다
    const GALLERY_MIRROR = [[0, 0.98], [10, 0.885], [20, 0.79], [30, 0.7], [40, 0.62], [50, 0.545], [60, 0.475], [70, 0.41], [80, 0.355], [90, 0.305], [100, 0.255],
        [110, 0.2], [120, 0.125], [130, 0.075], [140, 0.03], [150, 0]];

    function galleryParts(ctx) {
        const k = ctx.H / 720;
        const mask = `linear-gradient(to top, ${GALLERY_MIRROR.map(([d, a]) => `rgba(0,0,0,${a}) ${round(d * k)}px`).join(', ')})`;
        // 새 슬라이드를 숨기기 전에 복사한다
        const mirrors = [cloneStage(ctx.oldStage), cloneStage(ctx.stage)];
        const parts = wholeParts(ctx);
        parts.mirrors = mirrors;
        for (const node of parts.mirrors) {
            maskWith(node, mask);
            ctx.layer.insertBefore(node, ctx.layer.firstChild);
        }
        return parts;
    }

    function drawGallery(p, option, W, H, parts, camera) {
        const k = W / 1280;
        const sign = option === 'right' ? -1 : 1;
        const t = Math.min(p <= 0.5 ? p : 1 - p, 0.325);
        const gap = 1410 * k;
        const slide = (p <= 0.5 ? timeFilter(p, GALLERY_SLIDE) : 1410 - timeFilter(1 - p, GALLERY_SLIDE)) * k;
        const view = `perspective(${round(CAMERA * H)}px) translate3d(${round(sign * timeFilter(t, camera.x) * k)}px, 0, ${round(timeFilter(t, camera.z) * k)}px) `
            + `rotateY(${round(-sign * timeFilter(t, GALLERY_TURN))}deg)`;
        const places = [`${view} translateX(${round(-sign * slide)}px)`, `${view} translateX(${round(sign * (gap - slide))}px)`];
        parts.old.style.transform = places[0];
        parts.next.style.transform = places[1];
        if (parts.mirrors) {
            parts.mirrors.forEach((node, i) => {
                node.style.transform = `${places[i]} translateY(${round(H + 6 * H / 720)}px) scaleY(-1)`;
            });
        }
    }

    // ferrisWheel: 개체가 기울고 비틀리며 큰 바퀴를 따라 돈다. 옛 개체는 아래 앞쪽으로 나가고 새 개체는 위 뒤쪽에서 내려온다.
    // 표는 (x축 회전, y축 회전, z축 회전, x, y, z)의 잰 값이고 거리는 폭 1280px 기준이다. 표 밖에서는 개체가 화면 밖에 있다
    const FERRIS = {
        old: {
            rx: [[0, 0], [0.075, 1.1], [0.125, 3.7], [0.175, 7], [0.225, 10.7], [0.275, 14.9], [0.325, 17.5], [0.375, 19.5], [0.425, 22], [0.475, 26], [0.5, 29]],
            ry: [[0, 0], [0.125, -0.4], [0.225, -0.9], [0.275, -1.4], [0.325, -1.5], [0.375, -0.5], [0.425, 1.6], [0.475, 5], [0.5, 6.8]],
            rz: [[0, 0], [0.075, 0.2], [0.125, 0.4], [0.175, 0.8], [0.225, 1.2], [0.275, 1.8], [0.325, 2.5], [0.375, 3.5], [0.425, 5.5], [0.475, 8.5], [0.5, 9]],
            x: [[0, 0], [0.025, 0.9], [0.075, 2.8], [0.125, 8], [0.175, 15.2], [0.225, 23.2], [0.275, 33.2], [0.325, 46.5], [0.375, 60.7], [0.425, 71.6], [0.475, 80], [0.5, 91.6]],
            y: [[0, 0], [0.025, 2.3], [0.075, 16.7], [0.125, 49.7], [0.175, 90.3], [0.225, 142], [0.275, 199.2], [0.325, 270.4], [0.375, 339], [0.425, 403.3], [0.475, 461.6],
                [0.5, 488.9]],
            z: [[0, 0], [0.025, 1.6], [0.075, 15.9], [0.125, 50.3], [0.175, 99.7], [0.225, 153.4], [0.275, 223.1], [0.325, 309.8], [0.375, 376.2], [0.425, 467], [0.475, 585.6],
                [0.5, 659.3]],
        },
        next: {
            rx: [[0.45, 16.1], [0.5, 10.3], [0.55, 4], [0.6, -2.8], [0.65, -10.2], [0.725, -11.3], [0.775, -6.2], [0.825, -3.8], [0.875, -2], [0.925, -0.5], [1, 0]],
            ry: [[0.45, -5.7], [0.5, -1.2], [0.55, 3.3], [0.6, 7.8], [0.65, 12.3], [0.725, 12.5], [0.775, 9.8], [0.825, 6.1], [0.875, 3.3], [0.925, 0.8], [1, 0]],
            rz: [[0.45, -8.4], [0.5, -9.4], [0.55, -10.4], [0.6, -11.5], [0.65, -12.5], [0.725, -10.1], [0.775, -7.1], [0.825, -4.4], [0.875, -2.4], [0.925, -0.7], [1, 0]],
            x: [[0.45, -790.5], [0.5, -649.9], [0.55, -533.2], [0.6, -429.2], [0.65, -336.8], [0.725, -201.7], [0.775, -127.2], [0.825, -74.6], [0.875, -36.9], [0.925, -11.4],
                [0.975, -1.8], [1, 0]],
            y: [[0.45, -981], [0.5, -815], [0.55, -688.1], [0.6, -581.2], [0.65, -493], [0.725, -334.3], [0.775, -234.4], [0.825, -147.4], [0.875, -79], [0.925, -24.6],
                [0.975, -2.6], [1, 0]],
            z: [[0.45, -1319.3], [0.5, -1122.2], [0.55, -952.6], [0.6, -799.3], [0.65, -663.4], [0.725, -461.9], [0.775, -349.6], [0.825, -235.1], [0.875, -131.3],
                [0.925, -43.6], [0.975, -4.9], [1, 0]],
        },
    };

    function placeFerris(node, path, p, W, H, sign) {
        const first = path.rx[0][0];
        const last = path.rx[path.rx.length - 1][0];
        if (p < first || p > last) {
            node.style.visibility = 'hidden';
            return;
        }
        const k = W / 1280;
        const v = (name) => timeFilter(p, path[name]);
        node.style.visibility = '';
        node.style.transform = `perspective(${round(CAMERA * H)}px) translate3d(${round(sign * v('x') * k)}px, ${round(v('y') * k)}px, ${round(v('z') * k)}px) `
            + `rotateY(${round(sign * v('ry'))}deg) rotateX(${round(v('rx'))}deg) rotateZ(${round(sign * v('rz'))}deg)`;
    }

    // 시간 비율 -> 진행률. PowerPoint 동영상에서 잰 값이다 (tools/probe_transitions.py --match)
    const COVER_STRAIGHT = [[0, 0], [0.1, 0], [0.15, 0.006], [0.2, 0.024], [0.25, 0.049], [0.3, 0.088], [0.35, 0.142], [0.4, 0.228], [0.45, 0.352], [0.5, 0.525],
        [0.55, 0.672], [0.6, 0.795], [0.65, 0.877], [0.7, 0.933], [0.75, 0.959], [0.8, 0.977], [0.85, 0.99], [0.9, 0.997], [0.95, 1], [1, 1]];
    const COVER_DIAGONAL = [[0, 0], [0.05, 0.01], [0.1, 0.085], [0.15, 0.18], [0.2, 0.288], [0.25, 0.413], [0.3, 0.555], [0.35, 0.67], [0.4, 0.767], [0.45, 0.839],
        [0.5, 0.889], [0.55, 0.918], [0.6, 0.942], [0.65, 0.96], [0.7, 0.976], [0.75, 0.986], [0.8, 0.992], [0.85, 0.996], [0.9, 0.999], [1, 1]];
    const UNCOVER_STRAIGHT = [[0, 0], [0.05, 0.002], [0.1, 0.008], [0.15, 0.02], [0.2, 0.039], [0.25, 0.066], [0.3, 0.105], [0.35, 0.159], [0.4, 0.243], [0.45, 0.363],
        [0.5, 0.528], [0.55, 0.669], [0.6, 0.788], [0.65, 0.869], [0.7, 0.927], [0.75, 0.956], [0.8, 0.978], [0.85, 0.993], [0.9, 1], [1, 1]];
    const UNCOVER_DIAGONAL = [[0, 0], [0.05, 0.006], [0.1, 0.04], [0.15, 0.096], [0.2, 0.235], [0.25, 0.495], [0.3, 0.782], [0.35, 0.902], [0.4, 0.964], [0.45, 0.996],
        [0.5, 1], [1, 1]];
    const diagonal = (option) => /_(up|down)$/.test(String(option));

    // PowerPoint 2010 이후의 wipe, split, strips, circle, diamond, plus, wedge, wheel: 새 슬라이드가 사인 곡선으로 흐린 경계를 따라
    // 드러난다. 모양마다 정한 d(0~1)가 큰 곳부터 드러나고, 폭 w인 경계가 시간에 비례해 1 + w만큼 지나간다
    const softReveal = (d, p, w) => sineEase((d - 1 + (1 + w) * p) / w);

    // 그라데이션 선 위 0~1 자리 s의 d가 d(s)인 정지점들
    function softStops(p, w, d, count) {
        const stops = [];
        for (let i = 0; i <= count; i++) {
            stops.push(`rgba(0,0,0,${round(softReveal(d(i / count), p, w))}) ${round(i / count * 100)}%`);
        }
        return stops.join(', ');
    }

    // conic-gradient. d는 슬라이드를 정사각형으로 본 좌표에서 위가 0이고 시계 방향인 각도(0~360)의 함수이고,
    // breaks는 d가 끊기는 그 각도들이다. conic-gradient의 각도는 화면 좌표라 바꿔 준다
    function softConic(p, w, d, W, H, breaks) {
        const toScreen = (n) => (Math.atan2(Math.sin(n * DEG) * W, Math.cos(n * DEG) * H) / DEG + 360) % 360;
        const toSquare = (a) => (Math.atan2(Math.sin(a * DEG) / W, Math.cos(a * DEG) / H) / DEG + 360) % 360;
        const points = [];
        for (let a = 0; a < 360; a++) {
            points.push([a, d(toSquare(a))]);
        }
        for (const n of breaks || []) {
            const a = toScreen(n);
            points.push([a - 0.01, d((n + 359.99) % 360)], [a + 0.01, d((n + 0.01) % 360)]);
        }
        points.push([360, d(359.99)]);
        points.sort((x, y) => x[0] - y[0]);
        const stops = points.filter(([a]) => a >= 0 && a <= 360).map(([a, value]) => `rgba(0,0,0,${round(softReveal(value, p, w))}) ${round(a)}deg`);
        return `conic-gradient(from 0deg at 50% 50%, ${stops.join(', ')})`;
    }

    function maskLayers(node, images, size, position, composite) {
        maskWith(node, images);
        node.style.webkitMaskSize = node.style.maskSize = size || '';
        node.style.webkitMaskPosition = node.style.maskPosition = position || '';
        node.style.webkitMaskRepeat = node.style.maskRepeat = 'no-repeat';
        node.style.maskComposite = composite || '';
        node.style.webkitMaskComposite = composite === 'intersect' ? 'source-in' : '';
    }

    // 새 슬라이드를 마스크로 드러내는 층. plus는 두 장(세로 띠와 가로 띠)을 쓴다
    function revealParts(ctx, kind) {
        const nodes = [];
        for (let i = 0; i < (kind === 'plus' ? 2 : 1); i++) {
            nodes.push(cloneStage(ctx.stage));
        }
        for (const node of nodes) {
            ctx.layer.appendChild(node);
        }
        ctx.stage.style.visibility = 'hidden';
        return nodes;
    }

    const fold = (s) => 1 - Math.abs(2 * s - 1); // 가운데가 1, 양 끝이 0

    function drawSoft(kind, p, option, W, H, nodes) {
        const third = 1 / 3;
        switch (kind) {
            case 'wipe':
                maskLayers(nodes[0], `linear-gradient(to ${{left: 'right', right: 'left', up: 'bottom', down: 'top'}[option || 'left']}, ${softStops(p, 1, (s) => s, 32)})`);
                break;
            case 'split': {
                const [orient, way] = String(option || 'horizontal_out').split('_');
                maskLayers(nodes[0], `linear-gradient(to ${orient === 'horizontal' ? 'bottom' : 'right'}, ${softStops(p, 1, way === 'out' ? fold : (s) => 1 - fold(s), 64)})`);
                break;
            }
            case 'strips':
                maskLayers(nodes[0], `linear-gradient(to ${{left_up: 'bottom right', right_down: 'top left', right_up: 'bottom left', left_down: 'top right'}[option || 'left_up']}, ${softStops(p, 1, (s) => s, 32)})`);
                break;
            case 'circle':
                maskLayers(nodes[0], `radial-gradient(circle ${round(Math.hypot(W, H) / 2)}px at 50% 50%, ${softStops(p, third, (s) => 1 - s, 48)})`);
                break;
            case 'diamond': {
                // 네 사분면마다 모서리 쪽으로 가는 그라데이션
                const stops = softStops(p, third, (s) => s, 32);
                maskLayers(nodes[0], ['bottom right', 'bottom left', 'top right', 'top left'].map((to) => `linear-gradient(to ${to}, ${stops})`).join(', '),
                    '50% 50%', '0 0, 100% 0, 0 100%, 100% 100%');
                break;
            }
            case 'plus': {
                // 세로 띠는 위아래 삼각형에, 가로 띠는 왼쪽 오른쪽 삼각형에 쓴다. 삼각형은 슬라이드의 대각선으로 나뉜다
                const corner = round(Math.atan2(W, H) / DEG);
                const upDown = `conic-gradient(from 0deg at 50% 50%, #000 0deg ${corner}deg, transparent ${corner}deg ${180 - corner}deg, #000 ${180 - corner}deg ${180 + corner}deg, transparent ${180 + corner}deg ${360 - corner}deg, #000 ${360 - corner}deg)`;
                const sides = `conic-gradient(from 0deg at 50% 50%, transparent 0deg ${corner}deg, #000 ${corner}deg ${180 - corner}deg, transparent ${180 - corner}deg ${180 + corner}deg, #000 ${180 + corner}deg ${360 - corner}deg, transparent ${360 - corner}deg)`;
                const stops = softStops(p, third, fold, 64);
                maskLayers(nodes[0], `linear-gradient(to right, ${stops}), ${upDown}`, '', '', 'intersect');
                maskLayers(nodes[1], `linear-gradient(to bottom, ${stops}), ${sides}`, '', '', 'intersect');
                break;
            }
            case 'wedge':
                maskLayers(nodes[0], softConic(p, third, (n) => 1 - Math.abs(n > 180 ? n - 360 : n) / 180, W, H));
                break;
            case 'wheel': {
                const spokes = Number(String(option || 'spokes4').replace('spokes', '')) || 4;
                const segment = 360 / spokes;
                maskLayers(nodes[0], softConic(p, 1 / 9, (n) => 1 - (n % segment) / segment, W, H, Array.from({length: spokes}, (_, i) => i * segment)));
                break;
            }
            case 'wheelReverse':
                maskLayers(nodes[0], softConic(p, 1 / 9, (n) => n / 360, W, H, [0]));
                break;
            default:
                break;
        }
    }

    // box.in은 옛 슬라이드가 앞 절반에, box.out은 새 슬라이드가 뒤 절반에 슬라이드 비율의 흐린 사각형 안에서만 보인다.
    // 내용은 크기가 그대로이고 사각형만 줄거나 커진다. 경계는 가장자리에서 잰 거리(가운데가 1)로 BOX_EDGE 폭의 사인 곡선이다
    const BOX_EDGE = 0.075;

    function boxParts(ctx) {
        if (ctx.option === 'in') {
            return {node: ctx.old};
        }
        const node = cloneStage(ctx.stage);
        ctx.layer.appendChild(node);
        ctx.stage.style.visibility = 'hidden';
        return {node};
    }

    // 경계 가운데가 가장자리에서 c만큼 떨어진 사각형. 가로와 세로 그라데이션을 겹친다
    function drawBox(p, option, node) {
        const c = option === 'in' ? 2.06 * p - 0.036 : 2.024 - 2.06 * p;
        const xs = [0, 0.5, 1];
        for (let i = 0; i <= 8; i++) {
            const s = c + BOX_EDGE * (i / 8 - 0.5);
            if (s > 0 && s < 1) {
                xs.push(s / 2, 1 - s / 2);
            }
        }
        xs.sort((a, b) => a - b);
        const stops = xs.map((x) => `rgba(0,0,0,${round(sineEase((fold(x) - c) / BOX_EDGE + 0.5))}) ${round(x * 100)}%`).join(', ');
        maskLayers(node, `linear-gradient(to right, ${stops}), linear-gradient(to bottom, ${stops})`, '', '', 'intersect');
    }

    // dissolve: 54x42 칸이 칸마다 정해진 시각(0~0.875)에 시작해 전체 시간의 1/8 동안 사인 곡선으로 나타난다.
    // 시각은 PowerPoint이 늘 쓰는 무늬를 잰 것으로 슬라이드 비율과 길이에 상관없이 같다. 칸마다 두 글자(64진수 0~4095)이고 왼쪽 위부터 줄 순서다
    const DISSOLVE_COLUMNS = 54;
    const DISSOLVE_ROWS = 42;
    const DISSOLVE_TABLE = 'qcQYWmQv32BusoWklVwXvIzZ9htjgWoBemeQHpz_ehjTTDAblcHuABUjwx2zNJPfqaEszMsRGNiHmQN7n69MfPN-M8ZLt6goG0bUNXw9-SGQIrd8HbDfeO7T52MfGMrz_Z4Edc21yJTU6TaGvNNQoXvsDiKENF7l5ygL7j5m9spw7foB9QbzSpiYoIldd-PTiyXW6aDGXxSnxY_g70APWt6w1aIZwAtQAujv2qU4YZW7tY6iCPV5sp00jrXFoUNcpbicvDjw-ck67ZGBPIOdXISvr1CjogkGl87OvUDzUVPYsflzeCdGd8sETSk-5WxbWPOpvSh7pYAUF0g5h7C13ltnnbETn3MAOmscAT0j81xMsaFVvdC_mUYo004RmyM6I8QRVkxCYld0CeU4xfeTn5FEZwIivDtmCvcZKqq57YH4HGta6fqPYCr_wrwbuSf21aosQmyJTrTLJ2LrMnkrK-Y-4x83l7JgdFqo4ITG0kpGCj6dz23FVFwSUgzT2xZ9pIol7eDwHeJPe2LoUMUFoGNGPQNz0tkH4fumjPoHtAXeOW_iPxTPtxa3uphb15-zFVANhGCkkn5Ru8w9fsDK5e_dBKMpcP3vIi1z458Bvj9V7eQf11w1BBI76HnMItH6WhbbETH7qBLPD4NeQNcQs2DzB7erVqCXyAqEsr1zHVH7loqMIaO4WWafduP2XJvZVsAfs4pOyF1H00cm7SkmrP02nbezbdEhrtvY_eOL7yr3aXqE8rQ4zLn6eCgkEGSnuU3sKgiSC5Haf4wyq4JjIqKxJwydmXiRk8erg7K1w3Tifd1x0kHBSHRW2DupyI8IYtWxXiDJWuC5RtmKGxQCIOZIZvtpkftAwrUQp6H-QPixhW5EOswYg2GmvmhWTQ4uXEP9YQXmnAPI5OYckeDEBFGlknvK2-VGCfZtUlDebpyoL5JlW6CPAJKVsbh21YPIE_0CMHLOMf9aLfhN264SiWaM00y8IwgXb_yBpyEt-CHpVTypEev6RML0F9yUyUH7cFRS8YkhkdwOURYa4CPUeHK5PWJ-KZWKKwdgMqbLrt3mnZLd28Wb77Iy2Aqux0BSpuXTh_NNbRRPzqPghOmoeE5orEnA_fVAITSpU3iV00ReKQp0z1ysa2vhe--8_FKQe8QYJYtYH0YFQBInXuPVnVqcznVk-TrZngVKzNOBaTRrKusbrquzDaG3EkEYyFzCCuiROxW-yZLltbiQrs7s9jfB1-9vdLDW8Q3sWoN04BTlNFj-17d6UbGqXM60cA7Fg8kTgVTRP7Xbk6d9cOggi4VjE26y8p97R9vb2M3ZUb008_Ek6Ui-VwYXBE0RjzAYah6ONR8_7f-8Ux29gy9cEslWKqapgJR9loabcuYlea-PjY38IlNGZthSMBR5tIAA65XMGOlxudUfmH1cyTJ0KqOT6RkDb4GiqBVhIXqOHQ572Sl2b5YI-w8P6zW4mOeEyKJoUYX9TiEFvx453rpIj1xWzCOhrMkXA-jt5AiT_BsReEhmiTmNp4G7fn6sCJIiNjJUq5Gs-BY2xPGMXfl8zT2iWOm0W_GNNmXNXIiLHVUvVokmuj2LypulNgArvVVOSQCFWKUzcRcCt0MMRh3WVM7z0_FoMrW12-UxolTs2sii1Iz2tnE27wxw3zheD0l6Y2P2edStQ0pmUCW5-yAyzZJmpqEACA64ux5AsYpj1IER9PB9FUd8omttRSvxBUrhjXpoklYLbReycLEcbI85lyNPOUSZQXmra37yCPldZnf5aGqelaBZYUVzeiUoQSCCErrOheHdTgCC35HgnsPwaO9rsNbuxIlOPmdnqXI9BSkWzCmKNzaZ8dLdPsPtweoIfAWI0mj1IkVGMKtfOck8rfAn7KJMH1eVJeBeKLe7-qPUk8954vSIWtEPHJlQSLXtyGHnUd_BG2KvTddNMRK3tyS0oAhZbzdqWIaPQnOBhikDRGQSjy3LiXQpKPWOvXcK5VJoRc-NDzKfi5pqM6YPTbAJV3hkT6ZP83zzxKo5RZyOT_rkakVLuuzXCVxfFIpW-oHcUBcE5YWKnHpV335T5mtD8oZgVwqhqN9_lvAZh9RehDMPMwtCjmVghsbyZAB8rPIytrM8hSASIePeFs1BLeLmJOZHdl4h3Mh-5nsJ-N4YRip9BgaBjzt1RLtF_IzfJQs2uGH76qlDZhUs-Zdo7_zT6tIiOWeA4cG9l11gIVqaCSW8c6O6YYVezfjwb0ztKfY-YgzAXh5xcuGCRBrvYnCQ52Pztl5WHtEsHpC43AUTSrK5p5sfJ7MgpIBG160jtOGl6lztvxB9WeCAfsbIvJNHVkGD2YRRQogOMaGz6TkpXCVX7ViRHRP6H75HICzd2TrrBz5sT0vowXCvwXMzxtXaXgGqNXPlHuNEKyLBh1D7LhjZYVdEkLOr0TJx7yLJb7g59ESQM9Wd83UAJhDK2F4BrhiaVAzIHFmKurGPbhNQjIu6H_CqHlUsn0NXof5taCWNDEhhW231O_0RrfzaD-87w92sDnE6J_nYL0WKGFJPqRIC_kaiIH8QSQZ9pVBusRbaCvavfowpSd-cQyG_BCIByhb5KrPzRN-WreT7TSYHxpKO8bJxXr_ea4Q5BIAW9roSRlob3cP5uMILm9NeGtqOmhSjn4l3TnR6Pu7R53mpYmSKj_MfYkQBdZN8XrrgTFE5IGlQqFlcuVxsfILQzw7s3O5VhGmPaD4qaQ5wFaei44K-tJtnit1gn48dYbgupDfAhZLVQED_vhVirq9hRwy6URBmtLr6IAeI8SuAQGcsibezuIQ4orya4pYtfTEGzdnU02uZjSnsJE7ZcdUSffp-8Ur5uZeg4JcOCirUgqCUxtB719gzU1znUNroEfGnGcWQrn9fAIrHPYBRNMqlt8w1Qpy-aLb5JNuOtDho1QtWYAjkdnFfSn0ilIwVEjccfPQSKfDn8J6jvMKOxsJ8tFJdN3V51HovzBivKK39NE0uGZY4Gi8tQZbMTUBE4gDx6nUK003K00dcLyMgU8bbop-0KnkevEl1t1v-yP3wDhxKLNhkmfzz23ljq4y0sEElTkznquWMYB0ona05g2dVvI0qVMl_aIdt2ZphElmN85kAQUMKzHCrtpg9r-5pQ9NOsqfMxz42WONwhAB5HyTbIEPAk06JN800oEVqyEWKxzeD9ZVYMA5T-0WZEA4t8m8SHBXuAT9AHcLsUNqrp-kmhnPP4Wkq_8ZFqEpihH8ovhCXxIDTB5rhgJBNZpWvDhn3MHeFeHr0E7PbW1OQbjr_stf_39u-CPHizUdYckZfAPdh3zxPonVRpPVLGfSPsxhxJv2Yhw6kFr3zyoPtGFVjupTGxv2h8cCIgKdPTOwHCLYACDVz0ukNpIR040Su3q1SSBhWIiymFMaPoAjCW74eIsK-JaiQj4BjzYLao7vLMzJ25TPkQLOA-37bfH83lYF-4vaNxfrJjTz2IBG1eDGsehPKCXrOh18VT7OYkGNi5kdYHzl4V_-DfDO10yUMiJuVEBkqnVL_RA2qkbXMEIfG279PEO404UMdtycqXNEBQ1_38RsB-G08W09ZVEMzQtfwaO007Gb6yOIvJRsfz1PO-H2CuuEBHANGSEaLWbyOINMkM32Sp1-u0pVetVqplLJ8torLGeFvm0ITTQmNDlft97nGV78y5sYBhd5RUfoZKp1bxB1VQ7B-NReO0dtcd23guchKUE5-AeuL38JWCpwR7EbGW6-geFq4rQoiPliCRLS7sCeQIVNYuJr6hqW4y6Rpj3UOrtXOqr3NtSKRhLtrK_Y93-1xDhV3u8h8UbTIwqIvR_FZrxM8IZqjFE-NmeAys3v5taAeu6Mir4gLV2IS3yfHLWWeLvZ6KT6r4cgzOZwcgpketUrnNTOjUdIPcBds4fP6WiT0SJsdYYyt0RGOK_NP11xjWO34uiz0b0W0g1FByiZZaXoZ47h7o44WhlNlC9UZHBnCOxzYWeHBmlmvzzMmhJHz8KOisNRChHtXC23nvySfV5V13TyWNJbIx3X2lqHAvTrWTFl36CPCw8q7eTwxyvTo0xyNqkOgggTCEnmCaIkDZT5b1TCwC3FBa7cP_QVmzQ2rpTU3pxY5MugXgIubJTJXDBAD2XnEEyuJFcGjDo_QjxJJj4RHLdLOssn98bUS4c41WKNMmul1YmAaGOfIVkgapLGpM8XkkYzsW-f8iuUIjEd_AzCiNTK0HZJPLZLIkhGJYLQ18hOoUYkmOHu6XLMbH-Z84s2RN8orx_GVlJe6lPJF9kt0YsWt5vPdYNgI5chetZQm5pDTcz-o00FeqvpWITbhSuD46i00odVfXxT7Rxpa_MMcq41f7E3AQjxvpwjuHQKkJUWXWiE6GUsw5fG-3UwrcdCICC6SH3SZovmB-EwjHUzdWxpq4mUjwSdnbqnTtwHB00fDH0G3K7BA1zdsHB5behg0Zou8Ij4Y1OWjjrO3vOsrzRNcEYEdNjlLNq4HgxqJf_lGAfRE8vTW57u7WJMq0D5A5gdpV0oU0zrYkxymUFOX';
    const DIGITS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_';
    let dissolveStarts = null;

    function dissolveParts(ctx) {
        if (!dissolveStarts) {
            dissolveStarts = [];
            for (let i = 0; i < DISSOLVE_TABLE.length; i += 2) {
                dissolveStarts.push((DIGITS.indexOf(DISSOLVE_TABLE[i]) * 64 + DIGITS.indexOf(DISSOLVE_TABLE[i + 1])) / 4095 * 0.875);
            }
        }
        const node = cloneStage(ctx.stage);
        ctx.layer.appendChild(node);
        ctx.stage.style.visibility = 'hidden';
        return node;
    }

    // 새 슬라이드를 SVG 마스크로 드러낸다. 같은 투명도(1/64 단위)로 이어진 칸들을 한 경로로 묶는다
    function drawDissolve(p, node) {
        const paths = new Map();
        for (let y = 0; y < DISSOLVE_ROWS; y++) {
            let start = 0;
            let level = -1;
            for (let x = 0; x <= DISSOLVE_COLUMNS; x++) {
                const value = x < DISSOLVE_COLUMNS ? Math.round(64 * sineEase((p - dissolveStarts[y * DISSOLVE_COLUMNS + x]) * 8)) : -1;
                if (value === level) {
                    continue;
                }
                if (level > 0) {
                    paths.set(level, (paths.get(level) || '') + `M${start} ${y}h${x - start}v1h-${x - start}z`);
                }
                start = x;
                level = value;
            }
        }
        let svg = `<svg xmlns='http://www.w3.org/2000/svg' width='${DISSOLVE_COLUMNS}' height='${DISSOLVE_ROWS}' viewBox='0 0 ${DISSOLVE_COLUMNS} ${DISSOLVE_ROWS}' preserveAspectRatio='none' shape-rendering='crispEdges'>`;
        for (const [level, d] of paths) {
            svg += `<path fill-opacity='${round(level / 64)}' d='${d}'/>`;
        }
        svg += '</svg>';
        maskLayers(node, `url("data:image/svg+xml,${encodeURIComponent(svg)}")`, '100% 100%');
    }

    // blinds: 슬라이드를 띠(horizontal은 가로 14개, vertical은 세로 18개)로 나눈 정삼각기둥들이 120도 돌아 옛 슬라이드 면 대신
    // 새 슬라이드 면을 보인다. 가장 가까운 모서리는 화면 면에 머문다. 가운데 두 띠부터 시작해 바깥으로 갈수록 늦어져
    // 맨 바깥 띠는 0.291 늦게 시작하고, 띠마다 0.557 동안 2차 곡선으로 돈다
    function blindsParts(ctx) {
        const vertical = ctx.option === 'vertical';
        const count = vertical ? 18 : 14;
        const size = (vertical ? ctx.W : ctx.H) / count;
        ctx.layer.style.background = '#000';
        ctx.old.style.display = 'none';
        const strips = [];
        for (let k = 0; k < count; k++) {
            const clip = vertical ? `inset(0 ${round(ctx.W - (k + 1) * size)}px 0 ${round(k * size)}px)` : `inset(${round(k * size)}px 0 ${round(ctx.H - (k + 1) * size)}px 0)`;
            const old = cloneStage(ctx.oldStage);
            const next = cloneStage(ctx.stage);
            for (const node of [old, next]) {
                node.style.clipPath = clip;
                ctx.layer.appendChild(node);
            }
            strips.push({old, next, center: (k + 0.5) * size, delay: (Math.abs(k - (count - 1) / 2) - 0.5) / (count / 2 - 1) * 0.291});
        }
        ctx.stage.style.visibility = 'hidden';
        return {vertical, size, strips};
    }

    function drawBlinds(p, W, H, parts) {
        const F = CAMERA * H;
        const inner = parts.size / (2 * Math.sqrt(3)); // 기둥 가운데에서 면까지
        for (const strip of parts.strips) {
            const theta = 120 * ease(clamp((p - strip.delay) / 0.557, 0, 1), 0.5, 0.5);
            const depth = -2 * inner * Math.cos((theta - 60) * DEG);
            const shift = strip.center - (parts.vertical ? W : H) / 2;
            const place = (node, phi) => placeFace(node, parts.vertical
                ? `perspective(${round(F)}px) translateX(${round(shift)}px) translateZ(${round(depth)}px) rotateY(${round(-phi)}deg) translateZ(${round(inner)}px) translateX(${round(-shift)}px)`
                : `perspective(${round(F)}px) translateY(${round(shift)}px) translateZ(${round(depth)}px) rotateX(${round(phi)}deg) translateZ(${round(inner)}px) translateY(${round(-shift)}px)`, faceLight(Math.abs(phi)));
            place(strip.old, theta);
            place(strip.next, theta - 120);
        }
    }

    // checkerboard: 슬라이드를 7x5 판으로 나눠 판마다 가운데 축으로 0.336 동안 고르게 180도 뒤집는다. 앞면은 옛 슬라이드, 뒷면은 새 슬라이드다.
    // across는 세로축(왼쪽 모서리가 앞으로), down은 가로축(위 모서리가 앞으로)으로 돈다. 판마다 시작 시각(왼쪽 위부터 줄 순서)은 잰 값이다
    const CHECKER_START = {
        across: [0.098, 0.315, 0.17, 0.219, 0.282, 0.465, 0.514, 0.25, 0.241, 0.331, 0.342, 0.285, 0.536, 0.414, 0.173, 0.202, 0.215, 0.396, 0.297, 0.439, 0.543,
            0.253, 0.164, 0.364, 0.255, 0.469, 0.345, 0.4, 0.031, 0.145, 0.299, 0.232, 0.337, 0.419, 0.456],
        down: [0.109, 0.264, 0.059, 0.046, 0.048, 0.17, 0.158, 0.348, 0.278, 0.307, 0.256, 0.137, 0.328, 0.144, 0.356, 0.325, 0.277, 0.396, 0.236, 0.316, 0.36,
            0.524, 0.374, 0.512, 0.342, 0.495, 0.31, 0.303, 0.388, 0.441, 0.534, 0.405, 0.449, 0.469, 0.446],
    };

    function checkerParts(ctx) {
        const down = ctx.option === 'down';
        const w = ctx.W / 7;
        const h = ctx.H / 5;
        ctx.layer.style.background = '#000';
        ctx.old.style.display = 'none';
        const tiles = [];
        for (let j = 0; j < 5; j++) {
            for (let i = 0; i < 7; i++) {
                const clip = `inset(${round(j * h)}px ${round(ctx.W - (i + 1) * w)}px ${round(ctx.H - (j + 1) * h)}px ${round(i * w)}px)`;
                const old = cloneStage(ctx.oldStage);
                const next = cloneStage(ctx.stage);
                for (const node of [old, next]) {
                    node.style.clipPath = clip;
                    ctx.layer.appendChild(node);
                }
                tiles.push({old, next, dx: (i + 0.5) * w - ctx.W / 2, dy: (j + 0.5) * h - ctx.H / 2, start: CHECKER_START[down ? 'down' : 'across'][j * 7 + i]});
            }
        }
        ctx.stage.style.visibility = 'hidden';
        return {down, tiles};
    }

    function drawChecker(p, H, parts) {
        const F = CAMERA * H;
        for (const tile of parts.tiles) {
            const theta = 180 * clamp((p - tile.start) / 0.336, 0, 1);
            const place = (node, phi) => placeFace(node, `perspective(${round(F)}px) translate(${round(tile.dx)}px, ${round(tile.dy)}px) `
                + `${parts.down ? `rotateX(${round(-phi)}deg)` : `rotateY(${round(phi)}deg)`} translate(${round(-tile.dx)}px, ${round(-tile.dy)}px)`, faceLight(Math.abs(phi)));
            place(tile.old, theta);
            place(tile.next, theta - 180);
        }
    }

    // 전환마다 (진행률 p, 옛 슬라이드 old, 새 슬라이드 next, 옵션)으로 모양을 정한다. top이 old면 옛 슬라이드가 위에 있다.
    // setup이 있으면 전환을 시작할 때 한 번 불러 층을 만들고, 그 결과를 draw의 마지막 인자로 받는다.
    // ease가 있으면 지난 시간의 비율(0~1)을 PowerPoint의 시간 곡선에 따른 진행률로 바꾼다. 없으면 시간 비율이 곧 진행률이다
    const TRANSITIONS = {
        cut: {draw() {}, black: (option) => option === 'through_black'},
        fade: {
            ease: (t, option) => option === 'through_black' ? t : sineEase(t),
            draw(p, old, next, option) {
                if (option === 'through_black') {
                    old.style.opacity = String(Math.max(0, 1 - p * 2));
                    next.style.opacity = String(Math.max(0, p * 2 - 1));
                } else {
                    next.style.opacity = String(p);
                }
            },
            black: (option) => option === 'through_black',
        },
        push: {
            ease: (t) => ease(t, 0.5, 0.5),
            draw(p, old, next, option, W, H) {
                const [dx, dy] = DIRECTION[option || 'left'];
                old.style.transform = `translate(${dx * p * W}px, ${dy * p * H}px)`;
                next.style.transform = `translate(${-dx * (1 - p) * W}px, ${-dy * (1 - p) * H}px)`;
            },
        },
        cover: {
            ease: (t, option) => timeFilter(t, diagonal(option) ? COVER_DIAGONAL : COVER_STRAIGHT),
            draw(p, old, next, option, W, H) {
                const [dx, dy] = DIRECTION[option || 'left'];
                next.style.transform = `translate(${-dx * (1 - p) * W}px, ${-dy * (1 - p) * H}px)`;
            },
        },
        uncover: {
            top: 'old',
            ease: (t, option) => timeFilter(t, diagonal(option) ? UNCOVER_DIAGONAL : UNCOVER_STRAIGHT),
            draw(p, old, next, option, W, H) {
                const [dx, dy] = DIRECTION[option || 'left'];
                old.style.transform = `translate(${dx * p * W}px, ${dy * p * H}px)`;
            },
        },
        wipe: {setup: (ctx) => revealParts(ctx, 'wipe'), draw: (p, old, next, option, W, H, nodes) => drawSoft('wipe', p, option, W, H, nodes)},
        blinds: {setup: blindsParts, draw: (p, old, next, option, W, H, parts) => drawBlinds(p, W, H, parts)},
        checkerboard: {setup: checkerParts, draw: (p, old, next, option, W, H, parts) => drawChecker(p, H, parts)},
        dissolve: {setup: dissolveParts, draw: (p, old, next, option, W, H, node) => drawDissolve(p, node)},
        strips: {setup: (ctx) => revealParts(ctx, 'strips'), draw: (p, old, next, option, W, H, nodes) => drawSoft('strips', p, option, W, H, nodes)},
        circle: {setup: (ctx) => revealParts(ctx, 'circle'), draw: (p, old, next, option, W, H, nodes) => drawSoft('circle', p, option, W, H, nodes)},
        diamond: {setup: (ctx) => revealParts(ctx, 'diamond'), draw: (p, old, next, option, W, H, nodes) => drawSoft('diamond', p, option, W, H, nodes)},
        plus: {setup: (ctx) => revealParts(ctx, 'plus'), draw: (p, old, next, option, W, H, nodes) => drawSoft('plus', p, option, W, H, nodes)},
        wedge: {setup: (ctx) => revealParts(ctx, 'wedge'), draw: (p, old, next, option, W, H, nodes) => drawSoft('wedge', p, option, W, H, nodes)},
        wheel: {setup: (ctx) => revealParts(ctx, 'wheel'), draw: (p, old, next, option, W, H, nodes) => drawSoft('wheel', p, option, W, H, nodes)},
        wheelReverse: {setup: (ctx) => revealParts(ctx, 'wheelReverse'), draw: (p, old, next, option, W, H, nodes) => drawSoft('wheelReverse', p, option, W, H, nodes)},
        split: {setup: (ctx) => revealParts(ctx, 'split'), draw: (p, old, next, option, W, H, nodes) => drawSoft('split', p, option, W, H, nodes)},
        box: {
            top: 'old',
            setup: (ctx) => ctx.option === 'in' || ctx.option === 'out' || !ctx.option ? boxParts(ctx) : wholeParts(ctx),
            draw(p, old, next, option, W, H, parts) {
                if (parts.node) {
                    drawBox(p, option, parts.node);
                } else {
                    drawPrism(p, option, W, H, parts, true);
                }
            },
        },
        random: {},
        pan: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                const [dx, dy] = DIRECTION[option || 'left'];
                const q = p * p;
                crossfade(parts, p);
                parts.old.style.transform = `translate(${round(dx * q * W)}px, ${round(dy * q * H)}px)`;
                parts.next.style.transform = `translate(${round(-dx * (1 - q) * W)}px, ${round(-dy * (1 - q) * H)}px)`;
            },
        },
        flyThrough: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                const out = String(option).startsWith('out');
                const bounce = String(option).endsWith('bounce');
                const m = !bounce ? ease(p, 0.5, 0.5) : p < 0.78 ? (p / 0.78) ** 2 : timeFilter(p, FLY_BOUNCE);
                crossfade(parts, p);
                placeDepth(parts.old, out ? 1 + FLY_DEPTH * m : 1 - FLY_DEPTH * m, 1 - 1.2 * m);
                placeDepth(parts.next, out ? 1 - FLY_DEPTH * (1 - m) : 1 + FLY_DEPTH * (1 - m), out ? p : timeFilter(p, bounce ? FLY_IN_BOUNCE : FLY_IN));
            },
        },
        reveal: {
            setup: (ctx) => String(ctx.option).startsWith('black') ? wholeParts(ctx) : contentParts(ctx),
            draw(p, old, next, option, W, H, parts) {
                const mirror = String(option).endsWith('right');
                // 옛 개체는 오른쪽 끝(W의 0.894)을, 새 개체는 왼쪽 끝(0.121)을 기준으로 조금 커진 데서 시작한다
                parts.old.style.transformOrigin = `${round(0.894 * W)}px 50%`;
                parts.next.style.transformOrigin = `${round(0.121 * W)}px 50%`;
                parts.old.style.transform = `scale(${round(1 + 0.07 * p)})`;
                parts.next.style.transform = `scale(${round(1 + 0.07 * (1 - p))})`;
                if (String(option).startsWith('black')) {
                    parts.old.style.visibility = p < 0.5 ? '' : 'hidden';
                    parts.next.style.visibility = p < 0.5 ? 'hidden' : '';
                    const shown = p < 0.5 ? parts.old : parts.next;
                    const light = p < 0.5 ? (x) => 1 - smoothstep((p - 0.008 - 0.11 * x) / 0.422) : (x) => smoothstep((p - 0.568 + 0.11 * x) / 0.425);
                    let veil = shown.querySelector('.tl-veil');
                    if (!veil) {
                        veil = html('div', 'tl-veil');
                        shown.appendChild(veil);
                    }
                    veil.style.background = revealRamp((x) => 1 - light(x), mirror);
                    return;
                }
                crossfade(parts, p);
                maskWith(parts.old, revealRamp((x) => 1 - smoothstep((p - 0.1115 + 0.0971 * x) / 0.374), mirror));
                maskWith(parts.next, revealRamp((x) => smoothstep((p - 0.5508 - 0.0771 * x) / 0.362), mirror));
            },
        },
        newsflash: {
            draw(p, old, next) {
                next.style.transform = `rotate(${round(400 * (1 - p))}deg) scale(${round(p)})`;
                next.style.opacity = String(round(1 - (1 - p) ** 3));
            },
        },
        flashbulb: {
            setup(ctx) {
                const next = cloneStage(ctx.stage);
                const flash = html('div', 'tl-flash', {mixBlendMode: PLUS_LIGHTER});
                ctx.layer.appendChild(next);
                ctx.layer.appendChild(flash);
                ctx.stage.style.visibility = 'hidden';
                return {old: ctx.old, next, flash};
            },
            draw(p, old, next, option, W, H, parts) {
                const a = flashAmount(p);
                const shown = p < 0.212 ? parts.old : parts.next;
                parts.old.style.visibility = shown === parts.old ? '' : 'hidden';
                parts.next.style.visibility = shown === parts.next ? '' : 'hidden';
                shown.style.filter = a > 0 ? `brightness(${round(1 + a / 78)})` : '';
                const level = Math.round(Math.min(255, a));
                parts.flash.style.background = `rgb(${level},${level},${level})`;
            },
        },
        comb: {
            top: 'old',
            setup: combStrips,
            draw(p, old, next, option, W, H, comb) {
                for (const strip of comb.strips) {
                    const t = clamp((p - strip.start) / strip.length, 0, 1);
                    const offset = strip.sign * t * t * (comb.vertical ? H : W);
                    strip.node.style.transform = comb.vertical ? `translateY(${round(-offset)}px)` : `translateX(${round(offset)}px)`;
                }
            },
        },
        window: {
            setup: (ctx) => paneParts(ctx, true),
            draw(p, old, next, option, W, H, parts) {
                crossfade(parts, p);
                drawPanes(p, option, W, H, parts, 1.213, 101);
                parts.next.style.opacity = String(round(timeFilter(p, WINDOW_OPACITY)));
            },
        },
        doors: {
            setup: (ctx) => paneParts(ctx, false),
            draw(p, old, next, option, W, H, parts) {
                drawPanes(p, option, W, H, parts, 1, 88);
            },
        },
        flip: {
            setup: wholeParts,
            draw(p, old, next, option, W, H, parts) {
                const sign = option === 'right' ? -1 : 1;
                const phi = timeFilter(p, FLIP_TURN);
                const camera = `perspective(${round(CAMERA * H)}px) translateZ(${round(timeFilter(p, FLIP_DEPTH) * W / 1280)}px)`;
                // flip의 조명은 돈 각도에 비례해 어두워진다
                placeFace(parts.old, `${camera} rotateY(${round(-sign * phi)}deg)`, 1 - 0.0033 * Math.min(90, phi));
                placeFace(parts.next, `${camera} rotateY(${round(sign * (180 - phi))}deg)`, 1 - 0.0033 * Math.min(90, 180 - phi));
            },
        },
        switch: {
            setup: wholeParts,
            draw(p, old, next, option, W, H, parts) {
                const sign = option === 'right' ? -1 : 1;
                const k = W / 1280;
                const camera = `perspective(${round(CAMERA * H)}px)`;
                const shift = timeFilter(p, SWITCH_SHIFT) * k;
                const oldTurn = timeFilter(p, SWITCH_OLD_TURN);
                const newTurn = timeFilter(p, SWITCH_NEW_TURN);
                placeFace(parts.old, `${camera} translate3d(${round(-sign * shift)}px, 0, ${round(timeFilter(p, SWITCH_OLD_DEPTH) * k)}px) rotateY(${round(sign * oldTurn)}deg)`,
                    1 - 0.0033 * Math.abs(oldTurn));
                placeFace(parts.next, `${camera} translate3d(${round(sign * shift)}px, 0, ${round(timeFilter(p, SWITCH_NEW_DEPTH) * k)}px) rotateY(${round(sign * newTurn)}deg)`,
                    1 - 0.0033 * Math.abs(newTurn));
                // 가운데를 지나면 새 슬라이드가 앞에 온다
                parts.old.style.zIndex = p < 0.5 ? '2' : '1';
                parts.next.style.zIndex = p < 0.5 ? '1' : '2';
            },
        },
        gallery: {
            setup: galleryParts,
            draw(p, old, next, option, W, H, parts) {
                drawGallery(p, option, W, H, parts, GALLERY_CAMERA.gallery);
            },
        },
        conveyor: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                crossfade(parts, p);
                drawGallery(p, option, W, H, parts, GALLERY_CAMERA.conveyor);
            },
        },
        ferrisWheel: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                const sign = option === 'right' ? -1 : 1;
                crossfade(parts, p);
                placeFerris(parts.old, FERRIS.old, p, W, H, sign);
                placeFerris(parts.next, FERRIS.next, p, W, H, sign);
            },
        },
        cube: {
            setup: wholeParts,
            draw(p, old, next, option, W, H, parts) {
                drawPrism(p, option || 'left', W, H, parts, false);
            },
        },
        rotate: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                crossfade(parts, p);
                drawPrism(p, option || 'left', W, H, parts, false);
            },
        },
        orbit: {
            setup: contentParts,
            draw(p, old, next, option, W, H, parts) {
                crossfade(parts, p);
                drawPrism(p, option || 'left', W, H, parts, true);
            },
        },
    };

    const RANDOM_TRANSITIONS = ['fade', 'push', 'wipe', 'cover', 'uncover', 'split', 'blinds', 'checkerboard', 'dissolve', 'circle', 'diamond', 'plus', 'wedge', 'wheel'];

    // 옛 슬라이드 그림을 복사해 새 슬라이드와 함께 움직인다. 복사본의 id는 겹치지 않게 바꾼다
    function cloneStage(stage) {
        const copy = stage.cloneNode(true);
        const renamed = new Map();
        for (const node of copy.querySelectorAll('[id]')) {
            const id = uid('copy');
            renamed.set(node.id, id);
            node.id = id;
        }
        const pattern = /url\(#([^)]+)\)/g;
        const rename = (text) => text.replace(pattern, (match, id) => renamed.has(id) ? `url(#${renamed.get(id)})` : match);
        for (const node of copy.querySelectorAll('*')) {
            for (const attribute of Array.from(node.attributes)) {
                if (attribute.value.includes('url(#')) {
                    node.setAttribute(attribute.name, rename(attribute.value));
                }
            }
        }
        copy.classList.add('tl-copy');
        return copy;
    }

    function runTransition(transition, oldStage, section, stage, W, H, finished) {
        let kind = transition.k;
        let option = transition.o;
        if (kind === 'random') {
            kind = RANDOM_TRANSITIONS[Math.floor(Math.random() * RANDOM_TRANSITIONS.length)];
            option = undefined;
        }
        const spec = TRANSITIONS[kind] || TRANSITIONS.fade;
        const duration = Math.max(1, transition.d || 500);
        const layer = html('div', 'tl-transition', {width: `${W}px`, height: `${H}px`});
        const old = cloneStage(oldStage);
        layer.appendChild(old);
        section.appendChild(layer);
        layer.style.zIndex = spec.top === 'old' ? '3' : '0';
        const state = spec.setup ? spec.setup({layer, old, oldStage, stage, option, W, H}) : null;
        stage.style.zIndex = '1';
        let black = null;
        if (spec.black && spec.black(option)) {
            black = html('div', 'tl-black', {width: `${W}px`, height: `${H}px`});
            section.insertBefore(black, section.firstChild);
        }
        const started = performance.now();
        let frame = 0;
        const cleanup = () => {
            cancelAnimationFrame(frame);
            for (const node of [layer, black]) {
                if (node && node.parentNode) {
                    node.parentNode.removeChild(node);
                }
            }
            for (const property of ['opacity', 'transform', 'clipPath', 'visibility', 'zIndex']) {
                stage.style[property] = '';
            }
        };
        const step = (now) => {
            const time = clamp((now - started) / duration, 0, 1);
            const p = spec.ease ? spec.ease(time, option) : time;
            if (spec.draw) {
                spec.draw(p, old, stage, option, W, H, state);
            }
            if (kind === 'cut' && black) {
                stage.style.visibility = p < 1 ? 'hidden' : '';
                old.style.visibility = 'hidden';
            }
            if (time < 1) {
                frame = requestAnimationFrame(step);
            } else {
                cleanup();
                finished();
            }
        };
        step(started);
        return () => {
            cleanup();
            finished();
        };
    }

    // ---- 슬라이드

    function notesHtml(paragraphs) {
        return paragraphs.map((paragraph) => '<p>' + paragraph.rs.map((run) => {
            let text = run.t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/\n/g, '<br>');
            if (run.b) {
                text = `<b>${text}</b>`;
            }
            if (run.i) {
                text = `<i>${text}</i>`;
            }
            return text;
        }).join('') + '</p>').join('');
    }

    // play면 비디오와 오디오를 재생할 수 있게 만든다 (슬라이드 쇼)
    function renderStage(deck, slide, index, follow, play) {
        const W = deck.w;
        const H = deck.h;
        const stage = html('div', 'tl-slide', {width: `${W}px`, height: `${H}px`, background: cssPaint(slide.bg)});
        const defsRoot = svg('svg', {class: 'tl-defs', width: 0, height: 0}, stage);
        const defs = svg('defs', {}, defsRoot);
        const iterate = new Map();
        for (const item of slide.an || []) {
            const preset = DATA.animations[item.key];
            if (preset && preset.it) {
                iterate.set(item.el, preset.it.type === 'wd' ? 'wd' : 'lt');
            }
        }
        const theme = deck.themes[slide.th] || {};
        const base = {W, H, defs, page: slide.num, follow, theme, fits: [], snaps: [], iterate: new Map(), views: new Map(), animated: false, stage};
        const layout = slide.lay !== undefined ? deck.layouts[slide.lay] : null;
        if (layout) {
            const layer = html('div', 'tl-layer');
            for (const el of layout.els) {
                layer.appendChild(renderElement(el, base, 0, 0));
            }
            stage.appendChild(layer);
        }
        const context = {...base, animated: true, iterate, views: new Map(), fits: base.fits, snaps: base.snaps, media: !!play, medias: [], mediaOf: new Map()};
        const layer = html('div', 'tl-layer');
        for (const el of slide.els) {
            layer.appendChild(renderElement(el, context, 0, 0));
        }
        stage.appendChild(layer);
        return {stage, context};
    }

    // ---- 보기

    const STYLE = `
.reveal-viewport, .reveal-viewport body { background: #000; }
.reveal .slides { text-align: left; }
.reveal .slides > section { padding: 0 !important; }
.tl-slide { position: absolute; left: 0; top: 0; overflow: hidden; font-size: 16px; line-height: normal; color: #000; text-align: left; font-family: Calibri, "Malgun Gothic", sans-serif; letter-spacing: normal; text-transform: none; }
.tl-slide * { box-sizing: content-box; }
.tl-defs { position: absolute; width: 0; height: 0; overflow: hidden; }
.tl-layer { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.tl-el { position: absolute; transform-origin: 50% 50%; }
.tl-svg { position: absolute; left: 0; top: 0; overflow: visible; }
.tl-text { position: absolute; display: flex; flex-direction: column; box-sizing: border-box !important; overflow: visible; }
.tl-columns { box-sizing: border-box !important; }
.tl-p { margin: 0; white-space: pre-wrap; overflow-wrap: break-word; }
.tl-nowrap .tl-p { white-space: pre; }
.tl-run { white-space: inherit; }
.tl-bullet { display: inline-block; text-indent: 0; }
.tl-part { display: inline-block; white-space: pre; }
.tl-backdrop { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.tl-link { cursor: pointer; }
.tl-media { position: absolute; left: 0; top: 0; overflow: hidden; }
.tl-media-cover, .tl-video, .tl-media-poster { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.tl-video, .tl-media-poster { object-fit: fill; }
.tl-highlight { animation: tl-highlight 0.4s ease-out; }
@keyframes tl-highlight { 0%, 100% { filter: none; } 30% { filter: brightness(1.25) drop-shadow(0 0 4px rgba(0, 120, 215, 0.9)); } }
a.tl-run { cursor: pointer; }
.tl-transition, .tl-black { position: absolute; left: 0; top: 0; pointer-events: none; }
.tl-transition .tl-slide { position: absolute; left: 0; top: 0; }
.tl-black { background: #000; z-index: 0; }
.tl-veil, .tl-flash { position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; }
.reveal .fragment.tl-step { display: none; }
.tl-notes { position: fixed; left: 0; right: 0; bottom: 0; max-height: 35%; overflow: auto; padding: 12px 20px; background: rgba(20, 20, 20, 0.92); color: #eee; font: 16px/1.5 "Malgun Gothic", sans-serif; z-index: 30; display: none; }
.tl-notes.tl-open { display: block; }
.tl-notes p { margin: 0 0 6px; }
.tl-review { position: absolute; z-index: 20; display: none; font: 13px/1.4 "Malgun Gothic", sans-serif; }
.tl-show-reviews .tl-review { display: block; }
.tl-review-mark { width: 18px; height: 18px; border-radius: 9px 9px 9px 0; background: #ffd23f; border: 1px solid #b38b00; }
.tl-review-body { position: absolute; left: 22px; top: 0; min-width: 160px; max-width: 320px; padding: 6px 8px; background: #fffbe6; border: 1px solid #d9c36a; color: #333; white-space: pre-wrap; }
.tl-review-author { font-weight: bold; margin-bottom: 2px; }
`;

    // 컴파일러가 없는 글꼴 대신 쓰라고 넣은 "templide latin 글꼴"은 라틴 문자 범위만 가진 별칭이다.
    // 한글은 그 뒤의 한글 대체 글꼴로 간다
    const LATIN_RANGE = 'U+0000-052F, U+1E00-1FFF, U+2000-206F, U+20A0-20CF, U+2100-214F';

    function latinAliases(deck) {
        const names = new Set();
        for (const match of JSON.stringify(deck).matchAll(/templide latin ([^"\\]+)/g)) {
            names.add(match[1]);
        }
        const faces = [['normal', 'normal', ''], ['bold', 'normal', ' Bold'], ['normal', 'italic', ' Italic'], ['bold', 'italic', ' Bold Italic']];
        let css = '';
        for (const name of names) {
            for (const [weight, style, suffix] of faces) {
                const sources = [`local("${name}${suffix}")`, `local("${name}")`].join(', ');
                css += `@font-face { font-family: "templide latin ${name}"; src: ${sources}; font-weight: ${weight}; font-style: ${style}; unicode-range: ${LATIN_RANGE}; }\n`;
            }
        }
        return css;
    }

    function injectStyle(deck) {
        if (document.getElementById('templide-style')) {
            return;
        }
        const style = document.createElement('style');
        style.id = 'templide-style';
        style.textContent = STYLE + latinAliases(deck);
        document.head.appendChild(style);
    }

    // run(...) 동작. name은 전역 함수 이름이고 app.onClick처럼 점으로 객체 안의 함수를 가리킬 수 있다
    function callFunction(name, args) {
        let owner = global;
        let fn = global;
        for (const part of name.split('.')) {
            owner = fn;
            fn = fn == null ? undefined : fn[part];
        }
        if (typeof fn !== 'function') {
            console.warn(`templide: there is no function ${name} to run; load it with the target's script`);
            return;
        }
        try {
            fn.apply(owner, args);
        } catch (error) {
            console.error(error);
        }
    }

    // 편집기 미리보기. 슬라이드 index 하나를 애니메이션 없이 그린 .tl-slide(deck.w x deck.h)를 돌려준다.
    // 편집기 덱(컴파일러 --serve)의 element에는 data-src로 IR의 id가 붙는다. 문서에 붙인 뒤에 fit()으로 글자 크기를 맞춘다
    function renderSlide(deck, index) {
        injectStyle(deck);
        const {stage, context} = renderStage(deck, deck.slides[index], index, () => {});
        new Player({}, context);
        return {stage, fit: () => context.fits.forEach(fitText)};
    }

    // 편집기의 미리 보기. container(슬라이드 크기)에 슬라이드 index를 그리고, options.transition이면 앞 슬라이드에서 전환한 뒤
    // 클릭 묶음을 차례로 저절로 재생한다. 끝나면 options.onEnd를 부른다. 돌려준 함수를 부르면 멈춘다
    function preview(container, deck, index, options) {
        injectStyle(deck);
        options = options || {};
        const slide = deck.slides[index];
        let stopped = false;
        let timer = 0;
        let cancel = null;
        let player = null;
        let medias = [];
        const finish = () => {
            if (!stopped) {
                stopped = true;
                if (options.onEnd) {
                    options.onEnd();
                }
            }
        };
        const playFrom = (group) => {
            if (stopped) {
                return;
            }
            if (group >= player.groups.length) {
                timer = setTimeout(finish, 600);
                return;
            }
            player.play(group, () => {
                const next = player.groups[group + 1];
                timer = setTimeout(() => playFrom(group + 1), next && next.automatic ? 0 : 500);
            });
        };
        const {stage, context} = renderStage(deck, slide, index, () => {}, true);
        medias = context.medias;
        player = new Player(slide, context);
        let old = null;
        if (options.transition && slide.tr && index > 0) {
            old = renderStage(deck, deck.slides[index - 1], index - 1, () => {}, false);
            container.replaceChildren(old.stage, stage);
            old.context.fits.forEach(fitText);
        } else {
            container.replaceChildren(stage);
        }
        context.fits.forEach(fitText);
        player.settle(0);
        if (old) {
            cancel = runTransition(slide.tr, old.stage, container, stage, deck.w, deck.h, () => {
                cancel = null;
                playFrom(0);
            });
            old.stage.remove();
        } else {
            playFrom(0);
        }
        return () => {
            stopped = true;
            clearTimeout(timer);
            player.stop();
            if (cancel) {
                cancel();
            }
            for (const control of medias) {
                control.stop();
            }
        };
    }

    // 도형 종류의 조정값(adj1 ~)과 PowerPoint의 기본값
    function adjustments(kind) {
        const shape = DATA.shapes[kind];
        return shape && shape.av ? shape.av.map(([name, value]) => ({name, value: value[1]})) : [];
    }

    // options.animations가 false이거나 주소에 print-pdf, templide-static이 있으면 애니메이션과 전환 없이 모든 개체를 보여 준다
    function mount(root, deck, options) {
        injectStyle(deck);
        const query = window.location.search;
        const animations = !(options && options.animations === false) && !/print-pdf|templide-static/.test(query);
        const RevealClass = global.Reveal;
        const container = root.querySelector('.slides');
        const W = deck.w;
        const H = deck.h;
        const slides = [];
        let reveal = null;
        // 숨긴 슬라이드는 넘길 때 건너뛰고, 링크(slide(n))나 주소(#n)로만 간다
        let allowHidden = null; // 링크나 주소로 가는 숨긴 슬라이드
        let skipping = null;    // 숨긴 슬라이드를 건너뛰는 중이면 앞으로 가는지

        // index부터 step 방향으로 가장 가까운 보이는 슬라이드. loop면 끝에서 처음으로 돈다
        const visibleFrom = (index, step) => {
            for (let i = index; i >= 0 && i < slides.length; i += step) {
                if (!slides[i].slide.hid) {
                    return i;
                }
            }
            if (deck.loop) {
                for (let i = step > 0 ? 0 : slides.length - 1; i >= 0 && i < slides.length; i += step) {
                    if (!slides[i].slide.hid) {
                        return i;
                    }
                }
            }
            return -1;
        };

        const goto = (index, allowed) => {
            if (index >= 0 && index < slides.length) {
                allowHidden = allowed ? slides[index] : null;
                reveal.slide(index);
            }
        };

        // 슬라이드 쇼 끝내기. 받는 쪽(편집기의 슬라이드 쇼 창 등)이 templide-endshow를 취소하지 않으면 전체 화면에서 나온다
        const endShow = () => {
            const event = new CustomEvent('templide-endshow', {bubbles: true, cancelable: true});
            if (root.dispatchEvent(event) && document.fullscreenElement && document.exitFullscreen) {
                document.exitFullscreen().catch(() => {});
            }
        };

        // 링크와 실행 설정. 소리(snd)와 강조(hl)는 함께 하고, run은 사용자 script의 함수를 부른다.
        // next_slide, previous_slide는 넘기기처럼 숨긴 슬라이드를 건너뛴다
        const follow = (link, node) => {
            if (link.snd) {
                try {
                    new Audio(link.snd).play().catch(() => {});
                } catch (error) {
                    // 소리를 낼 수 없는 환경
                }
            }
            if (link.hl && node) {
                node.classList.remove('tl-highlight');
                void node.offsetWidth;
                node.classList.add('tl-highlight');
            }
            if (link.run) {
                callFunction(link.run, link.a || []);
                return;
            }
            if (link.url) {
                window.open(link.url, '_blank', 'noopener');
                return;
            }
            if (!reveal) {
                return;
            }
            const current = reveal.getIndices().h;
            if (link.jump === 'last_viewed_slide') {
                if (lastViewed) {
                    goto(slides.indexOf(lastViewed), true);
                }
            } else if (link.jump === 'end_show') {
                endShow();
            } else if (link.slide) {
                goto(slides.findIndex((entry) => entry.slide.num === link.slide), true);
            } else if (link.jump === 'next_slide') {
                goto(current + 1 < slides.length ? current + 1 : deck.loop ? 0 : current, false);
            } else if (link.jump === 'previous_slide') {
                goto(current > 0 ? current - 1 : deck.loop ? slides.length - 1 : current, false);
            } else if (link.jump === 'first_slide') {
                goto(visibleFrom(0, 1), false);
            } else if (link.jump === 'last_slide') {
                goto(visibleFrom(slides.length - 1, -1), false);
            }
        };

        const printing = /print-pdf/.test(query);
        deck.slides.forEach((slide, index) => {
            // 인쇄할 때는 PowerPoint처럼 숨긴 슬라이드를 뺀다
            if (printing && slide.hid) {
                return;
            }
            const section = document.createElement('section');
            section.dataset.page = String(slide.num);
            const {stage, context} = renderStage(deck, slide, index, follow, true);
            section.appendChild(stage);
            const entry = {slide, section, stage, context, player: null};
            // 클릭 묶음마다 보이지 않는 fragment 하나
            const player = animations ? new Player(slide, context) : new Player({}, context);
            entry.player = player;
            player.clickGroups.forEach((group, click) => {
                const step = html('span', 'fragment tl-step');
                step.dataset.fragmentIndex = String(click);
                step.dataset.click = String(click);
                section.appendChild(step);
            });
            if (slide.notes) {
                const notes = document.createElement('aside');
                notes.className = 'notes';
                notes.innerHTML = notesHtml(slide.notes);
                section.appendChild(notes);
            }
            for (const review of slide.rv || []) {
                const note = html('div', 'tl-review', {left: `${review.x}px`, top: `${review.y}px`});
                note.innerHTML = '<div class="tl-review-mark"></div><div class="tl-review-body"><div class="tl-review-author"></div><div class="tl-review-text"></div></div>';
                note.querySelector('.tl-review-author').textContent = review.au || '';
                note.querySelector('.tl-review-text').textContent = review.tx || '';
                stage.appendChild(note);
            }
            container.appendChild(section);
            slides.push(entry);
        });

        const notesPanel = html('div', 'tl-notes');
        document.body.appendChild(notesPanel);

        const plugins = [];
        if (global.RevealNotes) {
            plugins.push(global.RevealNotes);
        }
        reveal = new RevealClass(root, {
            width: W, height: H, margin: 0, minScale: 0.01, maxScale: 100, center: false,
            transition: 'none', backgroundTransition: 'none', controls: false, progress: false, slideNumber: false,
            hash: false, history: false, respondToHashChanges: false, loop: !!deck.loop, overview: true, touch: true,
            fragments: true, fragmentInURL: false, autoSlide: 0, mouseWheel: false, previewLinks: false, help: true,
            viewDistance: 2, navigationMode: 'linear', pdfSeparateFragments: false, plugins,
            keyboard: {
                78: () => toggleNotes(), // N
                82: () => root.classList.toggle('tl-show-reviews'), // R
            },
        });

        const entryOf = (section) => slides.find((item) => item.section === section);
        let active = null;
        let shown = null;     // 마지막으로 보인 슬라이드. 숨긴 슬라이드를 건너뛸 때 전환의 옛 슬라이드다
        let lastViewed = null; // 지금 슬라이드 바로 전에 본 슬라이드 (last_viewed_slide)
        let handled = false;  // slidechanged가 첫 슬라이드를 이미 맡았는지
        let cancelTransition = null;
        let advanceTimer = 0;

        const showNotes = () => {
            const entry = active;
            notesPanel.innerHTML = entry && entry.slide.notes ? notesHtml(entry.slide.notes) : '<p>(메모 없음)</p>';
        };
        const toggleNotes = () => {
            notesPanel.classList.toggle('tl-open');
            showNotes();
        };

        // 자동으로 넘기는 시간은 슬라이드가 보인 때부터 잰다. 남은 클릭이 있으면 그것부터 한다
        const scheduleAdvance = () => {
            clearTimeout(advanceTimer);
            if (active && active.slide.adv !== undefined) {
                advanceTimer = setTimeout(() => reveal.next(), active.slide.adv);
            }
        };

        const shownClicks = (entry) => entry.section.querySelectorAll('.fragment.tl-step.visible').length;

        const enter = (entry, forward) => {
            const player = entry.player;
            if (!animations) {
                return;
            }
            const shown = shownClicks(entry);
            const automatic = player.groups.length && player.groups[0].automatic;
            if (!forward || shown > 0) {
                // 뒤로 왔으면 보인 클릭까지 끝난 상태로
                player.settle((automatic ? 1 : 0) + shown);
            } else if (automatic) {
                player.settle(0);
                player.play(0);
            } else {
                player.settle(0);
            }
        };

        // 가로, 세로 선을 지금 화면 픽셀에 맞춘다. 슬라이드는 모두 같은 자리에 있다
        const snapAll = () => {
            const current = reveal.getCurrentSlide();
            const stage = current && current.querySelector('.tl-slide');
            if (!stage) {
                return;
            }
            const rect = stage.getBoundingClientRect();
            const zoom = parseFloat(getComputedStyle(container).zoom || '1');
            for (const entry of slides) {
                snapStrokes(entry.context.snaps, rect, W, zoom > 1.0001);
            }
        };

        const fitAll = () => {
            for (const entry of slides) {
                for (const item of entry.context.fits) {
                    fitText(item);
                }
            }
        };

        const location = () => {
            const match = /^#\/?(\d+)/.exec(window.location.hash);
            if (!match) {
                return;
            }
            const page = Number(match[1]);
            const target = slides.findIndex((entry) => entry.slide.num === page);
            if (target >= 0 && target !== reveal.getIndices().h) {
                goto(target, true);
            }
        };

        window.addEventListener('hashchange', location);

        reveal.on('resize', snapAll);

        reveal.on('ready', () => {
            fitAll();
            snapAll();
            location();
            if (handled) {
                return;
            }
            const current = entryOf(reveal.getCurrentSlide());
            if (!current) {
                return;
            }
            const index = slides.indexOf(current);
            if (current.slide.hid && current !== allowHidden) {
                const target = visibleFrom(index, 1);
                if (target >= 0 && target !== index) {
                    reveal.slide(target);
                    return;
                }
            }
            allowHidden = null;
            shown = current;
            active = current;
            enter(current, true);
            scheduleAdvance();
        });

        reveal.on('slidechanged', (event) => {
            handled = true;
            if (cancelTransition) {
                cancelTransition();
            }
            const current = entryOf(event.currentSlide);
            if (!current) {
                return;
            }
            const index = slides.indexOf(current);
            let forward;
            if (skipping !== null) {
                forward = skipping;
                skipping = null;
            } else {
                const before = slides.indexOf(entryOf(event.previousSlide));
                forward = before < 0 || index > before || (deck.loop && index === 0 && before === slides.length - 1);
            }
            // 넘기다 닿은 숨긴 슬라이드는 같은 방향으로 건너뛰고, 그쪽에 없으면 반대쪽으로 간다
            if (current.slide.hid && current !== allowHidden) {
                const step = forward ? 1 : -1;
                let target = visibleFrom(index + step, step);
                if (target < 0) {
                    target = visibleFrom(index - step, -step);
                }
                if (target >= 0 && target !== index) {
                    skipping = forward;
                    reveal.slide(target);
                    return;
                }
            }
            allowHidden = null;
            const previous = shown;
            if (previous) {
                previous.player.stop();
                if (previous !== current) {
                    lastViewed = previous;
                    for (const control of previous.context.medias) {
                        control.leave();
                    }
                }
            }
            shown = current;
            active = current;
            showNotes();
            if (history.replaceState) {
                history.replaceState(null, '', `#${current.slide.num}`);
            }
            const begin = () => {
                cancelTransition = null;
                enter(current, forward);
                scheduleAdvance();
            };
            if (forward && current.slide.snd) {
                try {
                    new Audio(current.slide.snd).play().catch(() => {});
                } catch (error) {
                    // 소리를 낼 수 없는 환경
                }
            }
            const transition = current.slide.tr;
            if (animations && forward && previous && previous !== current && transition && (transition.k !== 'cut' || transition.o === 'through_black')) {
                current.player.settle(0);
                cancelTransition = runTransition(transition, previous.stage, current.section, current.stage, W, H, begin);
            } else {
                begin();
            }
        });

        reveal.on('fragmentshown', (event) => {
            const entry = entryOf(reveal.getCurrentSlide());
            const click = Number(event.fragment.dataset.click);
            if (!entry || Number.isNaN(click)) {
                return;
            }
            if (cancelTransition) {
                cancelTransition();
            }
            const player = entry.player;
            const index = player.groupIndex(click);
            player.play(index);
            scheduleAdvance();
        });

        reveal.on('fragmenthidden', (event) => {
            const entry = entryOf(reveal.getCurrentSlide());
            const click = Number(event.fragment.dataset.click);
            if (!entry || Number.isNaN(click)) {
                return;
            }
            entry.player.settle(entry.player.groupIndex(click));
            scheduleAdvance();
        });

        // PowerPoint처럼 화면을 누르면 다음으로 간다
        root.addEventListener('click', (event) => {
            if (event.button !== 0 || event.target.closest('a, button, .tl-link, .controls')) {
                return;
            }
            reveal.next();
        });

        reveal.initialize();
        return reveal;
    }

    global.Templide = Object.assign(global.Templide || {}, {mount, renderSlide, preview, adjustments, version: 1});

    // tools/gen_templide_data.py가 만드는 자료. 직접 고치지 않는다
    const DATA = /*@generated-begin*/{"shapes":{"accentBorderCallout1":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",112500]],["adj4",["val",-38333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"]]}]},"accentBorderCallout2":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",112500]],["adj6",["val",-46667]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"]]}]},"accentBorderCallout3":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",100000]],["adj6",["val",-16667]],["adj7",["val",112963]],["adj8",["val",-8333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]],["y4",["*/","h","adj7",100000]],["x4",["*/","w","adj8",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"],["L","x4","y4"]]}]},"accentCallout1":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",112500]],["adj4",["val",-38333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"]]}]},"accentCallout2":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",112500]],["adj6",["val",-46667]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"]]}]},"accentCallout3":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",100000]],["adj6",["val",-16667]],["adj7",["val",112963]],["adj8",["val",-8333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]],["y4",["*/","h","adj7",100000]],["x4",["*/","w","adj8",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","t"],["Z"],["L","x1","b"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"],["L","x4","y4"]]}]},"actionButtonBackPrevious":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g11","vc"],["L","g12","g9"],["L","g12","g10"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g11","vc"],["L","g12","g9"],["L","g12","g10"],["Z"]]},{"fill":"none","d":[["M","g11","vc"],["L","g12","g9"],["L","g12","g10"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonBeginning":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1,8]],["g15",["*/","g13",1,4]],["g16",["+-","g11","g14",0]],["g17",["+-","g11","g15",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g17","vc"],["L","g12","g9"],["L","g12","g10"],["Z"],["M","g16","g9"],["L","g11","g9"],["L","g11","g10"],["L","g16","g10"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g17","vc"],["L","g12","g9"],["L","g12","g10"],["Z"],["M","g16","g9"],["L","g11","g9"],["L","g11","g10"],["L","g16","g10"],["Z"]]},{"fill":"none","d":[["M","g17","vc"],["L","g12","g9"],["L","g12","g10"],["Z"],["M","g16","g9"],["L","g16","g10"],["L","g11","g10"],["L","g11","g9"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonBlank":{"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonDocument":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["dx1",["*/","ss",9,32]],["g11",["+-","hc",0,"dx1"]],["g12",["+-","hc","dx1",0]],["g13",["*/","ss",3,16]],["g14",["+-","g12",0,"g13"]],["g15",["+-","g9","g13",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g11","g9"],["L","g14","g9"],["L","g12","g15"],["L","g12","g10"],["L","g11","g10"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","g11","g9"],["L","g14","g9"],["L","g14","g15"],["L","g12","g15"],["L","g12","g10"],["L","g11","g10"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g14","g9"],["L","g14","g15"],["L","g12","g15"],["Z"]]},{"fill":"none","d":[["M","g11","g9"],["L","g14","g9"],["L","g12","g15"],["L","g12","g10"],["L","g11","g10"],["Z"],["M","g12","g15"],["L","g14","g15"],["L","g14","g9"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonEnd":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",3,4]],["g15",["*/","g13",7,8]],["g16",["+-","g11","g14",0]],["g17",["+-","g11","g15",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g16","vc"],["L","g11","g9"],["L","g11","g10"],["Z"],["M","g17","g9"],["L","g12","g9"],["L","g12","g10"],["L","g17","g10"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g16","vc"],["L","g11","g9"],["L","g11","g10"],["Z"],["M","g17","g9"],["L","g12","g9"],["L","g12","g10"],["L","g17","g10"],["Z"]]},{"fill":"none","d":[["M","g16","vc"],["L","g11","g10"],["L","g11","g9"],["Z"],["M","g17","g9"],["L","g12","g9"],["L","g12","g10"],["L","g17","g10"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonForwardNext":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g12","vc"],["L","g11","g9"],["L","g11","g10"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g12","vc"],["L","g11","g9"],["L","g11","g10"],["Z"]]},{"fill":"none","d":[["M","g12","vc"],["L","g11","g10"],["L","g11","g9"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonHelp":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g11",["+-","hc",0,"dx2"]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1,7]],["g15",["*/","g13",3,14]],["g16",["*/","g13",2,7]],["g19",["*/","g13",3,7]],["g20",["*/","g13",4,7]],["g21",["*/","g13",17,28]],["g23",["*/","g13",21,28]],["g24",["*/","g13",11,14]],["g27",["+-","g9","g16",0]],["g29",["+-","g9","g21",0]],["g30",["+-","g9","g23",0]],["g31",["+-","g9","g24",0]],["g33",["+-","g11","g15",0]],["g36",["+-","g11","g19",0]],["g37",["+-","g11","g20",0]],["g41",["*/","g13",1,14]],["g42",["*/","g13",3,28]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g33","g27"],["A","g16","g16","cd2","cd2"],["A","g14","g15",0,"cd4"],["A","g41","g42","3cd4",-5400000],["L","g37","g30"],["L","g36","g30"],["L","g36","g29"],["A","g14","g15","cd2","cd4"],["A","g41","g42","cd4",-5400000],["A","g14","g14",0,-10800000],["Z"],["M","hc","g31"],["A","g42","g42","3cd4",21600000],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g33","g27"],["A","g16","g16","cd2","cd2"],["A","g14","g15",0,"cd4"],["A","g41","g42","3cd4",-5400000],["L","g37","g30"],["L","g36","g30"],["L","g36","g29"],["A","g14","g15","cd2","cd4"],["A","g41","g42","cd4",-5400000],["A","g14","g14",0,-10800000],["Z"],["M","hc","g31"],["A","g42","g42","3cd4",21600000],["Z"]]},{"fill":"none","d":[["M","g33","g27"],["A","g16","g16","cd2","cd2"],["A","g14","g15",0,"cd4"],["A","g41","g42","3cd4",-5400000],["L","g37","g30"],["L","g36","g30"],["L","g36","g29"],["A","g14","g15","cd2","cd4"],["A","g41","g42","cd4",-5400000],["A","g14","g14",0,-10800000],["Z"],["M","hc","g31"],["A","g42","g42","3cd4",21600000],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonHome":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1,16]],["g15",["*/","g13",1,8]],["g16",["*/","g13",3,16]],["g17",["*/","g13",5,16]],["g18",["*/","g13",7,16]],["g19",["*/","g13",9,16]],["g20",["*/","g13",11,16]],["g21",["*/","g13",3,4]],["g22",["*/","g13",13,16]],["g23",["*/","g13",7,8]],["g24",["+-","g9","g14",0]],["g25",["+-","g9","g16",0]],["g26",["+-","g9","g17",0]],["g27",["+-","g9","g21",0]],["g28",["+-","g11","g15",0]],["g29",["+-","g11","g18",0]],["g30",["+-","g11","g19",0]],["g31",["+-","g11","g20",0]],["g32",["+-","g11","g22",0]],["g33",["+-","g11","g23",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","hc","g9"],["L","g11","vc"],["L","g28","vc"],["L","g28","g10"],["L","g33","g10"],["L","g33","vc"],["L","g12","vc"],["L","g32","g26"],["L","g32","g24"],["L","g31","g24"],["L","g31","g25"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","g32","g26"],["L","g32","g24"],["L","g31","g24"],["L","g31","g25"],["Z"],["M","g28","vc"],["L","g28","g10"],["L","g29","g10"],["L","g29","g27"],["L","g30","g27"],["L","g30","g10"],["L","g33","g10"],["L","g33","vc"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","hc","g9"],["L","g11","vc"],["L","g12","vc"],["Z"],["M","g29","g27"],["L","g30","g27"],["L","g30","g10"],["L","g29","g10"],["Z"]]},{"fill":"none","d":[["M","hc","g9"],["L","g31","g25"],["L","g31","g24"],["L","g32","g24"],["L","g32","g26"],["L","g12","vc"],["L","g33","vc"],["L","g33","g10"],["L","g28","g10"],["L","g28","vc"],["L","g11","vc"],["Z"],["M","g31","g25"],["L","g32","g26"],["M","g33","vc"],["L","g28","vc"],["M","g29","g10"],["L","g29","g27"],["L","g30","g27"],["L","g30","g10"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonInformation":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g11",["+-","hc",0,"dx2"]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1,32]],["g17",["*/","g13",5,16]],["g18",["*/","g13",3,8]],["g19",["*/","g13",13,32]],["g20",["*/","g13",19,32]],["g22",["*/","g13",11,16]],["g23",["*/","g13",13,16]],["g24",["*/","g13",7,8]],["g25",["+-","g9","g14",0]],["g28",["+-","g9","g17",0]],["g29",["+-","g9","g18",0]],["g30",["+-","g9","g23",0]],["g31",["+-","g9","g24",0]],["g32",["+-","g11","g17",0]],["g34",["+-","g11","g19",0]],["g35",["+-","g11","g20",0]],["g37",["+-","g11","g22",0]],["g38",["*/","g13",3,32]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","hc","g9"],["A","dx2","dx2","3cd4",21600000],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","hc","g9"],["A","dx2","dx2","3cd4",21600000],["Z"],["M","hc","g25"],["A","g38","g38","3cd4",21600000],["M","g32","g28"],["L","g32","g29"],["L","g34","g29"],["L","g34","g30"],["L","g32","g30"],["L","g32","g31"],["L","g37","g31"],["L","g37","g30"],["L","g35","g30"],["L","g35","g28"],["Z"]]},{"fill":"lighten","stroke":false,"d":[["M","hc","g25"],["A","g38","g38","3cd4",21600000],["M","g32","g28"],["L","g35","g28"],["L","g35","g30"],["L","g37","g30"],["L","g37","g31"],["L","g32","g31"],["L","g32","g30"],["L","g34","g30"],["L","g34","g29"],["L","g32","g29"],["Z"]]},{"fill":"none","d":[["M","hc","g9"],["A","dx2","dx2","3cd4",21600000],["Z"],["M","hc","g25"],["A","g38","g38","3cd4",21600000],["M","g32","g28"],["L","g35","g28"],["L","g35","g30"],["L","g37","g30"],["L","g37","g31"],["L","g32","g31"],["L","g32","g30"],["L","g34","g30"],["L","g34","g29"],["L","g32","g29"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonMovie":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1455,21600]],["g15",["*/","g13",1905,21600]],["g16",["*/","g13",2325,21600]],["g17",["*/","g13",16155,21600]],["g18",["*/","g13",17010,21600]],["g19",["*/","g13",19335,21600]],["g20",["*/","g13",19725,21600]],["g21",["*/","g13",20595,21600]],["g22",["*/","g13",5280,21600]],["g23",["*/","g13",5730,21600]],["g24",["*/","g13",6630,21600]],["g25",["*/","g13",7492,21600]],["g26",["*/","g13",9067,21600]],["g27",["*/","g13",9555,21600]],["g28",["*/","g13",13342,21600]],["g29",["*/","g13",14580,21600]],["g30",["*/","g13",15592,21600]],["g31",["+-","g11","g14",0]],["g32",["+-","g11","g15",0]],["g33",["+-","g11","g16",0]],["g34",["+-","g11","g17",0]],["g35",["+-","g11","g18",0]],["g36",["+-","g11","g19",0]],["g37",["+-","g11","g20",0]],["g38",["+-","g11","g21",0]],["g39",["+-","g9","g22",0]],["g40",["+-","g9","g23",0]],["g41",["+-","g9","g24",0]],["g42",["+-","g9","g25",0]],["g43",["+-","g9","g26",0]],["g44",["+-","g9","g27",0]],["g45",["+-","g9","g28",0]],["g46",["+-","g9","g29",0]],["g47",["+-","g9","g30",0]],["g48",["+-","g9","g31",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g11","g39"],["L","g11","g44"],["L","g31","g44"],["L","g32","g43"],["L","g33","g43"],["L","g33","g47"],["L","g35","g47"],["L","g35","g45"],["L","g36","g45"],["L","g38","g46"],["L","g12","g46"],["L","g12","g41"],["L","g38","g41"],["L","g37","g42"],["L","g35","g42"],["L","g35","g41"],["L","g34","g40"],["L","g32","g40"],["L","g31","g39"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g11","g39"],["L","g11","g44"],["L","g31","g44"],["L","g32","g43"],["L","g33","g43"],["L","g33","g47"],["L","g35","g47"],["L","g35","g45"],["L","g36","g45"],["L","g38","g46"],["L","g12","g46"],["L","g12","g41"],["L","g38","g41"],["L","g37","g42"],["L","g35","g42"],["L","g35","g41"],["L","g34","g40"],["L","g32","g40"],["L","g31","g39"],["Z"]]},{"fill":"none","d":[["M","g11","g39"],["L","g31","g39"],["L","g32","g40"],["L","g34","g40"],["L","g35","g41"],["L","g35","g42"],["L","g37","g42"],["L","g38","g41"],["L","g12","g41"],["L","g12","g46"],["L","g38","g46"],["L","g36","g45"],["L","g35","g45"],["L","g35","g47"],["L","g33","g47"],["L","g33","g43"],["L","g32","g43"],["L","g31","g44"],["L","g11","g44"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonReturn":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",7,8]],["g15",["*/","g13",3,4]],["g16",["*/","g13",5,8]],["g17",["*/","g13",3,8]],["g18",["*/","g13",1,4]],["g19",["+-","g9","g15",0]],["g20",["+-","g9","g16",0]],["g21",["+-","g9","g18",0]],["g22",["+-","g11","g14",0]],["g23",["+-","g11","g15",0]],["g24",["+-","g11","g16",0]],["g25",["+-","g11","g17",0]],["g26",["+-","g11","g18",0]],["g27",["*/","g13",1,8]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g12","g21"],["L","g23","g9"],["L","hc","g21"],["L","g24","g21"],["L","g24","g20"],["A","g27","g27",0,"cd4"],["L","g25","g19"],["A","g27","g27","cd4","cd4"],["L","g26","g21"],["L","g11","g21"],["L","g11","g20"],["A","g17","g17","cd2",-5400000],["L","hc","g10"],["A","g17","g17","cd4",-5400000],["L","g22","g21"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g12","g21"],["L","g23","g9"],["L","hc","g21"],["L","g24","g21"],["L","g24","g20"],["A","g27","g27",0,"cd4"],["L","g25","g19"],["A","g27","g27","cd4","cd4"],["L","g26","g21"],["L","g11","g21"],["L","g11","g20"],["A","g17","g17","cd2",-5400000],["L","hc","g10"],["A","g17","g17","cd4",-5400000],["L","g22","g21"],["Z"]]},{"fill":"none","d":[["M","g12","g21"],["L","g22","g21"],["L","g22","g20"],["A","g17","g17",0,"cd4"],["L","g25","g10"],["A","g17","g17","cd4","cd4"],["L","g11","g21"],["L","g26","g21"],["L","g26","g20"],["A","g27","g27","cd2",-5400000],["L","hc","g19"],["A","g27","g27","cd4",-5400000],["L","g24","g21"],["L","hc","g21"],["L","g23","g9"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"actionButtonSound":{"gd":[["dx2",["*/","ss",3,8]],["g9",["+-","vc",0,"dx2"]],["g10",["+-","vc","dx2",0]],["g11",["+-","hc",0,"dx2"]],["g12",["+-","hc","dx2",0]],["g13",["*/","ss",3,4]],["g14",["*/","g13",1,8]],["g15",["*/","g13",5,16]],["g16",["*/","g13",5,8]],["g17",["*/","g13",11,16]],["g18",["*/","g13",3,4]],["g19",["*/","g13",7,8]],["g20",["+-","g9","g14",0]],["g21",["+-","g9","g15",0]],["g22",["+-","g9","g17",0]],["g23",["+-","g9","g19",0]],["g24",["+-","g11","g15",0]],["g25",["+-","g11","g16",0]],["g26",["+-","g11","g18",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","g11","g21"],["L","g11","g22"],["L","g24","g22"],["L","g25","g10"],["L","g25","g9"],["L","g24","g21"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","g11","g21"],["L","g11","g22"],["L","g24","g22"],["L","g25","g10"],["L","g25","g9"],["L","g24","g21"],["Z"]]},{"fill":"none","d":[["M","g11","g21"],["L","g24","g21"],["L","g25","g9"],["L","g25","g10"],["L","g24","g22"],["L","g11","g22"],["Z"],["M","g26","g21"],["L","g12","g20"],["M","g26","vc"],["L","g12","vc"],["M","g26","g22"],["L","g12","g23"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"arc":{"av":[["adj1",["val",16200000]],["adj2",["val",0]]],"gd":[["stAng",["pin",0,"adj1",21599999]],["enAng",["pin",0,"adj2",21599999]],["sw11",["+-","enAng",0,"stAng"]],["sw12",["+-","sw11",21600000,0]],["swAng",["?:","sw11","sw11","sw12"]],["wt1",["sin","wd2","stAng"]],["ht1",["cos","hd2","stAng"]],["dx1",["cat2","wd2","ht1","wt1"]],["dy1",["sat2","hd2","ht1","wt1"]],["wt2",["sin","wd2","enAng"]],["ht2",["cos","hd2","enAng"]],["dx2",["cat2","wd2","ht2","wt2"]],["dy2",["sat2","hd2","ht2","wt2"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["x2",["+-","hc","dx2",0]],["y2",["+-","vc","dy2",0]],["sw0",["+-",21600000,0,"stAng"]],["da1",["+-","swAng",0,"sw0"]],["g1",["max","x1","x2"]],["ir",["?:","da1","r","g1"]],["sw1",["+-","cd4",0,"stAng"]],["sw2",["+-",27000000,0,"stAng"]],["sw3",["?:","sw1","sw1","sw2"]],["da2",["+-","swAng",0,"sw3"]],["g5",["max","y1","y2"]],["ib",["?:","da2","b","g5"]],["sw4",["+-","cd2",0,"stAng"]],["sw5",["+-",32400000,0,"stAng"]],["sw6",["?:","sw4","sw4","sw5"]],["da3",["+-","swAng",0,"sw6"]],["g9",["min","x1","x2"]],["il",["?:","da3","l","g9"]],["sw7",["+-","3cd4",0,"stAng"]],["sw8",["+-",37800000,0,"stAng"]],["sw9",["?:","sw7","sw7","sw8"]],["da4",["+-","swAng",0,"sw9"]],["g13",["min","y1","y2"]],["it",["?:","da4","t","g13"]],["cang1",["+-","stAng",0,"cd4"]],["cang2",["+-","enAng","cd4",0]],["cang3",["+/","cang1","cang2",2]]],"rect":["il","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","x1","y1"],["A","wd2","hd2","stAng","swAng"],["L","hc","vc"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["A","wd2","hd2","stAng","swAng"]]}]},"bentArrow":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",43750]]],"gd":[["a2",["pin",0,"adj2",50000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["a3",["pin",0,"adj3",50000]],["th",["*/","ss","a1",100000]],["aw2",["*/","ss","a2",100000]],["th2",["*/","th",1,2]],["dh2",["+-","aw2",0,"th2"]],["ah",["*/","ss","a3",100000]],["bw",["+-","r",0,"ah"]],["bh",["+-","b",0,"dh2"]],["bs",["min","bw","bh"]],["maxAdj4",["*/",100000,"bs","ss"]],["a4",["pin",0,"adj4","maxAdj4"]],["bd",["*/","ss","a4",100000]],["bd3",["+-","bd",0,"th"]],["bd2",["max","bd3",0]],["x3",["+-","th","bd2",0]],["x4",["+-","r",0,"ah"]],["y3",["+-","dh2","th",0]],["y4",["+-","y3","dh2",0]],["y5",["+-","dh2","bd",0]],["y6",["+-","y3","bd2",0]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","b"],["L","l","y5"],["A","bd","bd","cd2","cd4"],["L","x4","dh2"],["L","x4","t"],["L","r","aw2"],["L","x4","y4"],["L","x4","y3"],["L","x3","y3"],["A","bd2","bd2","3cd4",-5400000],["L","th","b"],["Z"]]}]},"bentConnector2":{"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"]]}]},"bentConnector3":{"av":[["adj1",["val",50000]]],"gd":[["x1",["*/","w","adj1",100000]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["L","x1","t"],["L","x1","b"],["L","r","b"]]}]},"bentConnector4":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["x1",["*/","w","adj1",100000]],["x2",["+/","x1","r",2]],["y2",["*/","h","adj2",100000]],["y1",["+/","t","y2",2]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["L","x1","t"],["L","x1","y2"],["L","r","y2"],["L","r","b"]]}]},"bentConnector5":{"av":[["adj1",["val",50000]],["adj2",["val",50000]],["adj3",["val",50000]]],"gd":[["x1",["*/","w","adj1",100000]],["x3",["*/","w","adj3",100000]],["x2",["+/","x1","x3",2]],["y2",["*/","h","adj2",100000]],["y1",["+/","t","y2",2]],["y3",["+/","b","y2",2]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["L","x1","t"],["L","x1","y2"],["L","x3","y2"],["L","x3","b"],["L","r","b"]]}]},"bentUpArrow":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["a3",["pin",0,"adj3",50000]],["y1",["*/","ss","a3",100000]],["dx1",["*/","ss","a2",50000]],["x1",["+-","r",0,"dx1"]],["dx3",["*/","ss","a2",100000]],["x3",["+-","r",0,"dx3"]],["dx2",["*/","ss","a1",200000]],["x2",["+-","x3",0,"dx2"]],["x4",["+-","x3","dx2",0]],["dy2",["*/","ss","a1",100000]],["y2",["+-","b",0,"dy2"]],["x0",["*/","x4",1,2]],["y3",["+/","y2","b",2]],["y15",["+/","y1","b",2]]],"rect":["l","y2","x4","b"],"paths":[{"d":[["M","l","y2"],["L","x2","y2"],["L","x2","y1"],["L","x1","y1"],["L","x3","t"],["L","r","y1"],["L","x4","y1"],["L","x4","b"],["L","l","b"],["Z"]]}]},"bevel":{"av":[["adj",["val",12500]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]]],"rect":["x1","x1","x2","y2"],"paths":[{"stroke":false,"d":[["M","x1","x1"],["L","x2","x1"],["L","x2","y2"],["L","x1","y2"],["Z"]]},{"fill":"lightenLess","stroke":false,"d":[["M","l","t"],["L","r","t"],["L","x2","x1"],["L","x1","x1"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","l","b"],["L","x1","y2"],["L","x2","y2"],["L","r","b"],["Z"]]},{"fill":"lighten","stroke":false,"d":[["M","l","t"],["L","x1","x1"],["L","x1","y2"],["L","l","b"],["Z"]]},{"fill":"darken","stroke":false,"d":[["M","r","t"],["L","r","b"],["L","x2","y2"],["L","x2","x1"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","x1","x1"],["L","x2","x1"],["L","x2","y2"],["L","x1","y2"],["Z"],["M","l","t"],["L","x1","x1"],["M","l","b"],["L","x1","y2"],["M","r","t"],["L","x2","x1"],["M","r","b"],["L","x2","y2"]]}]},"blockArc":{"av":[["adj1",["val",10800000]],["adj2",["val",0]],["adj3",["val",25000]]],"gd":[["stAng",["pin",0,"adj1",21599999]],["istAng",["pin",0,"adj2",21599999]],["a3",["pin",0,"adj3",50000]],["sw11",["+-","istAng",0,"stAng"]],["sw12",["+-","sw11",21600000,0]],["swAng",["?:","sw11","sw11","sw12"]],["iswAng",["+-",0,0,"swAng"]],["wt1",["sin","wd2","stAng"]],["ht1",["cos","hd2","stAng"]],["wt3",["sin","wd2","istAng"]],["ht3",["cos","hd2","istAng"]],["dx1",["cat2","wd2","ht1","wt1"]],["dy1",["sat2","hd2","ht1","wt1"]],["dx3",["cat2","wd2","ht3","wt3"]],["dy3",["sat2","hd2","ht3","wt3"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["x3",["+-","hc","dx3",0]],["y3",["+-","vc","dy3",0]],["dr",["*/","ss","a3",100000]],["iwd2",["+-","wd2",0,"dr"]],["ihd2",["+-","hd2",0,"dr"]],["wt2",["sin","iwd2","istAng"]],["ht2",["cos","ihd2","istAng"]],["wt4",["sin","iwd2","stAng"]],["ht4",["cos","ihd2","stAng"]],["dx2",["cat2","iwd2","ht2","wt2"]],["dy2",["sat2","ihd2","ht2","wt2"]],["dx4",["cat2","iwd2","ht4","wt4"]],["dy4",["sat2","ihd2","ht4","wt4"]],["x2",["+-","hc","dx2",0]],["y2",["+-","vc","dy2",0]],["x4",["+-","hc","dx4",0]],["y4",["+-","vc","dy4",0]],["sw0",["+-",21600000,0,"stAng"]],["da1",["+-","swAng",0,"sw0"]],["g1",["max","x1","x2"]],["g2",["max","x3","x4"]],["g3",["max","g1","g2"]],["ir",["?:","da1","r","g3"]],["sw1",["+-","cd4",0,"stAng"]],["sw2",["+-",27000000,0,"stAng"]],["sw3",["?:","sw1","sw1","sw2"]],["da2",["+-","swAng",0,"sw3"]],["g5",["max","y1","y2"]],["g6",["max","y3","y4"]],["g7",["max","g5","g6"]],["ib",["?:","da2","b","g7"]],["sw4",["+-","cd2",0,"stAng"]],["sw5",["+-",32400000,0,"stAng"]],["sw6",["?:","sw4","sw4","sw5"]],["da3",["+-","swAng",0,"sw6"]],["g9",["min","x1","x2"]],["g10",["min","x3","x4"]],["g11",["min","g9","g10"]],["il",["?:","da3","l","g11"]],["sw7",["+-","3cd4",0,"stAng"]],["sw8",["+-",37800000,0,"stAng"]],["sw9",["?:","sw7","sw7","sw8"]],["da4",["+-","swAng",0,"sw9"]],["g13",["min","y1","y2"]],["g14",["min","y3","y4"]],["g15",["min","g13","g14"]],["it",["?:","da4","t","g15"]],["x5",["+/","x1","x4",2]],["y5",["+/","y1","y4",2]],["x6",["+/","x3","x2",2]],["y6",["+/","y3","y2",2]],["cang1",["+-","stAng",0,"cd4"]],["cang2",["+-","istAng","cd4",0]],["cang3",["+/","cang1","cang2",2]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","x1","y1"],["A","wd2","hd2","stAng","swAng"],["L","x2","y2"],["A","iwd2","ihd2","istAng","iswAng"],["Z"]]}]},"borderCallout1":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",112500]],["adj4",["val",-38333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"]]}]},"borderCallout2":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",112500]],["adj6",["val",-46667]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"]]}]},"borderCallout3":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",100000]],["adj6",["val",-16667]],["adj7",["val",112963]],["adj8",["val",-8333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]],["y4",["*/","h","adj7",100000]],["x4",["*/","w","adj8",100000]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"],["L","x4","y4"]]}]},"bracePair":{"av":[["adj",["val",8333]]],"gd":[["a",["pin",0,"adj",25000]],["x1",["*/","ss","a",100000]],["x2",["*/","ss","a",50000]],["x3",["+-","r",0,"x2"]],["x4",["+-","r",0,"x1"]],["y2",["+-","vc",0,"x1"]],["y3",["+-","vc","x1",0]],["y4",["+-","b",0,"x1"]],["it",["*/","x1",29289,100000]],["il",["+-","x1","it",0]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"it"]]],"rect":["il","il","ir","ib"],"paths":[{"stroke":false,"d":[["M","x2","b"],["A","x1","x1","cd4","cd4"],["L","x1","y3"],["A","x1","x1",0,-5400000],["A","x1","x1","cd4",-5400000],["L","x1","x1"],["A","x1","x1","cd2","cd4"],["L","x3","t"],["A","x1","x1","3cd4","cd4"],["L","x4","y2"],["A","x1","x1","cd2",-5400000],["A","x1","x1","3cd4",-5400000],["L","x4","y4"],["A","x1","x1",0,"cd4"],["Z"]]},{"fill":"none","d":[["M","x2","b"],["A","x1","x1","cd4","cd4"],["L","x1","y3"],["A","x1","x1",0,-5400000],["A","x1","x1","cd4",-5400000],["L","x1","x1"],["A","x1","x1","cd2","cd4"],["M","x3","t"],["A","x1","x1","3cd4","cd4"],["L","x4","y2"],["A","x1","x1","cd2",-5400000],["A","x1","x1","3cd4",-5400000],["L","x4","y4"],["A","x1","x1",0,"cd4"]]}]},"bracketPair":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]],["il",["*/","x1",29289,100000]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","x1"],["A","x1","x1","cd2","cd4"],["L","x2","t"],["A","x1","x1","3cd4","cd4"],["L","r","y2"],["A","x1","x1",0,"cd4"],["L","x1","b"],["A","x1","x1","cd4","cd4"],["Z"]]},{"fill":"none","d":[["M","x1","b"],["A","x1","x1","cd4","cd4"],["L","l","x1"],["A","x1","x1","cd2","cd4"],["M","x2","t"],["A","x1","x1","3cd4","cd4"],["L","r","y2"],["A","x1","x1",0,"cd4"]]}]},"callout1":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",112500]],["adj4",["val",-38333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"]]}]},"callout2":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",112500]],["adj6",["val",-46667]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"]]}]},"callout3":{"av":[["adj1",["val",18750]],["adj2",["val",-8333]],["adj3",["val",18750]],["adj4",["val",-16667]],["adj5",["val",100000]],["adj6",["val",-16667]],["adj7",["val",112963]],["adj8",["val",-8333]]],"gd":[["y1",["*/","h","adj1",100000]],["x1",["*/","w","adj2",100000]],["y2",["*/","h","adj3",100000]],["x2",["*/","w","adj4",100000]],["y3",["*/","h","adj5",100000]],["x3",["*/","w","adj6",100000]],["y4",["*/","h","adj7",100000]],["x4",["*/","w","adj8",100000]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]},{"fill":"none","d":[["M","x1","y1"],["L","x2","y2"],["L","x3","y3"],["L","x4","y4"]]}]},"can":{"av":[["adj",["val",25000]]],"gd":[["maxAdj",["*/",50000,"h","ss"]],["a",["pin",0,"adj","maxAdj"]],["y1",["*/","ss","a",200000]],["y2",["+-","y1","y1",0]],["y3",["+-","b",0,"y1"]]],"rect":["l","y2","r","y3"],"paths":[{"stroke":false,"d":[["M","l","y1"],["A","wd2","y1","cd2",-10800000],["L","r","y3"],["A","wd2","y1",0,"cd2"],["Z"]]},{"fill":"lighten","stroke":false,"d":[["M","l","y1"],["A","wd2","y1","cd2","cd2"],["A","wd2","y1",0,"cd2"],["Z"]]},{"fill":"none","d":[["M","r","y1"],["A","wd2","y1",0,"cd2"],["A","wd2","y1","cd2","cd2"],["L","r","y3"],["A","wd2","y1",0,"cd2"],["L","l","y1"]]}]},"chartPlus":{"paths":[{"w":10,"h":10,"fill":"none","d":[["M",5,0],["L",5,10],["M",0,5],["L",10,5]]},{"w":10,"h":10,"stroke":false,"d":[["M",0,0],["L",0,10],["L",10,10],["L",10,0],["Z"]]}]},"chartStar":{"paths":[{"w":10,"h":10,"fill":"none","d":[["M",0,0],["L",10,10],["M",0,10],["L",10,0],["M",5,0],["L",5,10]]},{"w":10,"h":10,"stroke":false,"d":[["M",0,0],["L",0,10],["L",10,10],["L",10,0],["Z"]]}]},"chartX":{"paths":[{"w":10,"h":10,"fill":"none","d":[["M",0,0],["L",10,10],["M",0,10],["L",10,0]]},{"w":10,"h":10,"stroke":false,"d":[["M",0,0],["L",0,10],["L",10,10],["L",10,0],["Z"]]}]},"chevron":{"av":[["adj",["val",50000]]],"gd":[["maxAdj",["*/",100000,"w","ss"]],["a",["pin",0,"adj","maxAdj"]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["x3",["*/","x2",1,2]],["dx",["+-","x2",0,"x1"]],["il",["?:","dx","x1","l"]],["ir",["?:","dx","x2","r"]]],"rect":["il","t","ir","b"],"paths":[{"d":[["M","l","t"],["L","x2","t"],["L","r","vc"],["L","x2","b"],["L","l","b"],["L","x1","vc"],["Z"]]}]},"chord":{"av":[["adj1",["val",2700000]],["adj2",["val",16200000]]],"gd":[["stAng",["pin",0,"adj1",21599999]],["enAng",["pin",0,"adj2",21599999]],["sw1",["+-","enAng",0,"stAng"]],["sw2",["+-","sw1",21600000,0]],["swAng",["?:","sw1","sw1","sw2"]],["wt1",["sin","wd2","stAng"]],["ht1",["cos","hd2","stAng"]],["dx1",["cat2","wd2","ht1","wt1"]],["dy1",["sat2","hd2","ht1","wt1"]],["wt2",["sin","wd2","enAng"]],["ht2",["cos","hd2","enAng"]],["dx2",["cat2","wd2","ht2","wt2"]],["dy2",["sat2","hd2","ht2","wt2"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["x2",["+-","hc","dx2",0]],["y2",["+-","vc","dy2",0]],["x3",["+/","x1","x2",2]],["y3",["+/","y1","y2",2]],["midAng0",["*/","swAng",1,2]],["midAng",["+-","stAng","midAng0","cd2"]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","x1","y1"],["A","wd2","hd2","stAng","swAng"],["Z"]]}]},"circularArrow":{"av":[["adj1",["val",12500]],["adj2",["val",1142319]],["adj3",["val",20457681]],["adj4",["val",10800000]],["adj5",["val",12500]]],"gd":[["a5",["pin",0,"adj5",25000]],["maxAdj1",["*/","a5",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["enAng",["pin",1,"adj3",21599999]],["stAng",["pin",0,"adj4",21599999]],["th",["*/","ss","a1",100000]],["thh",["*/","ss","a5",100000]],["th2",["*/","th",1,2]],["rw1",["+-","wd2","th2","thh"]],["rh1",["+-","hd2","th2","thh"]],["rw2",["+-","rw1",0,"th"]],["rh2",["+-","rh1",0,"th"]],["rw3",["+-","rw2","th2",0]],["rh3",["+-","rh2","th2",0]],["wtH",["sin","rw3","enAng"]],["htH",["cos","rh3","enAng"]],["dxH",["cat2","rw3","htH","wtH"]],["dyH",["sat2","rh3","htH","wtH"]],["xH",["+-","hc","dxH",0]],["yH",["+-","vc","dyH",0]],["rI",["min","rw2","rh2"]],["u1",["*/","dxH","dxH",1]],["u2",["*/","dyH","dyH",1]],["u3",["*/","rI","rI",1]],["u4",["+-","u1",0,"u3"]],["u5",["+-","u2",0,"u3"]],["u6",["*/","u4","u5","u1"]],["u7",["*/","u6",1,"u2"]],["u8",["+-",1,0,"u7"]],["u9",["sqrt","u8"]],["u10",["*/","u4",1,"dxH"]],["u11",["*/","u10",1,"dyH"]],["u12",["+/",1,"u9","u11"]],["u13",["at2",1,"u12"]],["u14",["+-","u13",21600000,0]],["u15",["?:","u13","u13","u14"]],["u16",["+-","u15",0,"enAng"]],["u17",["+-","u16",21600000,0]],["u18",["?:","u16","u16","u17"]],["u19",["+-","u18",0,"cd2"]],["u20",["+-","u18",0,21600000]],["u21",["?:","u19","u20","u18"]],["maxAng",["abs","u21"]],["aAng",["pin",0,"adj2","maxAng"]],["ptAng",["+-","enAng","aAng",0]],["wtA",["sin","rw3","ptAng"]],["htA",["cos","rh3","ptAng"]],["dxA",["cat2","rw3","htA","wtA"]],["dyA",["sat2","rh3","htA","wtA"]],["xA",["+-","hc","dxA",0]],["yA",["+-","vc","dyA",0]],["wtE",["sin","rw1","stAng"]],["htE",["cos","rh1","stAng"]],["dxE",["cat2","rw1","htE","wtE"]],["dyE",["sat2","rh1","htE","wtE"]],["xE",["+-","hc","dxE",0]],["yE",["+-","vc","dyE",0]],["dxG",["cos","thh","ptAng"]],["dyG",["sin","thh","ptAng"]],["xG",["+-","xH","dxG",0]],["yG",["+-","yH","dyG",0]],["dxB",["cos","thh","ptAng"]],["dyB",["sin","thh","ptAng"]],["xB",["+-","xH",0,"dxB",0]],["yB",["+-","yH",0,"dyB",0]],["sx1",["+-","xB",0,"hc"]],["sy1",["+-","yB",0,"vc"]],["sx2",["+-","xG",0,"hc"]],["sy2",["+-","yG",0,"vc"]],["rO",["min","rw1","rh1"]],["x1O",["*/","sx1","rO","rw1"]],["y1O",["*/","sy1","rO","rh1"]],["x2O",["*/","sx2","rO","rw1"]],["y2O",["*/","sy2","rO","rh1"]],["dxO",["+-","x2O",0,"x1O"]],["dyO",["+-","y2O",0,"y1O"]],["dO",["mod","dxO","dyO",0]],["q1",["*/","x1O","y2O",1]],["q2",["*/","x2O","y1O",1]],["DO",["+-","q1",0,"q2"]],["q3",["*/","rO","rO",1]],["q4",["*/","dO","dO",1]],["q5",["*/","q3","q4",1]],["q6",["*/","DO","DO",1]],["q7",["+-","q5",0,"q6"]],["q8",["max","q7",0]],["sdelO",["sqrt","q8"]],["ndyO",["*/","dyO",-1,1]],["sdyO",["?:","ndyO",-1,1]],["q9",["*/","sdyO","dxO",1]],["q10",["*/","q9","sdelO",1]],["q11",["*/","DO","dyO",1]],["dxF1",["+/","q11","q10","q4"]],["q12",["+-","q11",0,"q10"]],["dxF2",["*/","q12",1,"q4"]],["adyO",["abs","dyO"]],["q13",["*/","adyO","sdelO",1]],["q14",["*/","DO","dxO",-1]],["dyF1",["+/","q14","q13","q4"]],["q15",["+-","q14",0,"q13"]],["dyF2",["*/","q15",1,"q4"]],["q16",["+-","x2O",0,"dxF1"]],["q17",["+-","x2O",0,"dxF2"]],["q18",["+-","y2O",0,"dyF1"]],["q19",["+-","y2O",0,"dyF2"]],["q20",["mod","q16","q18",0]],["q21",["mod","q17","q19",0]],["q22",["+-","q21",0,"q20"]],["dxF",["?:","q22","dxF1","dxF2"]],["dyF",["?:","q22","dyF1","dyF2"]],["sdxF",["*/","dxF","rw1","rO"]],["sdyF",["*/","dyF","rh1","rO"]],["xF",["+-","hc","sdxF",0]],["yF",["+-","vc","sdyF",0]],["x1I",["*/","sx1","rI","rw2"]],["y1I",["*/","sy1","rI","rh2"]],["x2I",["*/","sx2","rI","rw2"]],["y2I",["*/","sy2","rI","rh2"]],["dxI",["+-","x2I",0,"x1I"]],["dyI",["+-","y2I",0,"y1I"]],["dI",["mod","dxI","dyI",0]],["v1",["*/","x1I","y2I",1]],["v2",["*/","x2I","y1I",1]],["DI",["+-","v1",0,"v2"]],["v3",["*/","rI","rI",1]],["v4",["*/","dI","dI",1]],["v5",["*/","v3","v4",1]],["v6",["*/","DI","DI",1]],["v7",["+-","v5",0,"v6"]],["v8",["max","v7",0]],["sdelI",["sqrt","v8"]],["v9",["*/","sdyO","dxI",1]],["v10",["*/","v9","sdelI",1]],["v11",["*/","DI","dyI",1]],["dxC1",["+/","v11","v10","v4"]],["v12",["+-","v11",0,"v10"]],["dxC2",["*/","v12",1,"v4"]],["adyI",["abs","dyI"]],["v13",["*/","adyI","sdelI",1]],["v14",["*/","DI","dxI",-1]],["dyC1",["+/","v14","v13","v4"]],["v15",["+-","v14",0,"v13"]],["dyC2",["*/","v15",1,"v4"]],["v16",["+-","x1I",0,"dxC1"]],["v17",["+-","x1I",0,"dxC2"]],["v18",["+-","y1I",0,"dyC1"]],["v19",["+-","y1I",0,"dyC2"]],["v20",["mod","v16","v18",0]],["v21",["mod","v17","v19",0]],["v22",["+-","v21",0,"v20"]],["dxC",["?:","v22","dxC1","dxC2"]],["dyC",["?:","v22","dyC1","dyC2"]],["sdxC",["*/","dxC","rw2","rI"]],["sdyC",["*/","dyC","rh2","rI"]],["xC",["+-","hc","sdxC",0]],["yC",["+-","vc","sdyC",0]],["ist0",["at2","sdxC","sdyC"]],["ist1",["+-","ist0",21600000,0]],["istAng",["?:","ist0","ist0","ist1"]],["isw1",["+-","stAng",0,"istAng"]],["isw2",["+-","isw1",0,21600000]],["iswAng",["?:","isw1","isw2","isw1"]],["p1",["+-","xF",0,"xC"]],["p2",["+-","yF",0,"yC"]],["p3",["mod","p1","p2",0]],["p4",["*/","p3",1,2]],["p5",["+-","p4",0,"thh"]],["xGp",["?:","p5","xF","xG"]],["yGp",["?:","p5","yF","yG"]],["xBp",["?:","p5","xC","xB"]],["yBp",["?:","p5","yC","yB"]],["en0",["at2","sdxF","sdyF"]],["en1",["+-","en0",21600000,0]],["en2",["?:","en0","en0","en1"]],["sw0",["+-","en2",0,"stAng"]],["sw1",["+-","sw0",21600000,0]],["swAng",["?:","sw0","sw0","sw1"]],["wtI",["sin","rw3","stAng"]],["htI",["cos","rh3","stAng"]],["dxI",["cat2","rw3","htI","wtI"]],["dyI",["sat2","rh3","htI","wtI"]],["xI",["+-","hc","dxI",0]],["yI",["+-","vc","dyI",0]],["aI",["+-","stAng",0,"cd4"]],["aA",["+-","ptAng","cd4",0]],["aB",["+-","ptAng","cd2",0]],["idx",["cos","rw1",2700000]],["idy",["sin","rh1",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","xE","yE"],["A","rw1","rh1","stAng","swAng"],["L","xGp","yGp"],["L","xA","yA"],["L","xBp","yBp"],["L","xC","yC"],["A","rw2","rh2","istAng","iswAng"],["Z"]]}]},"cloud":{"gd":[["il",["*/","w",2977,21600]],["it",["*/","h",3262,21600]],["ir",["*/","w",17087,21600]],["ib",["*/","h",17337,21600]],["g27",["*/","w",67,21600]],["g28",["*/","h",21577,21600]],["g29",["*/","w",21582,21600]],["g30",["*/","h",1235,21600]]],"rect":["il","it","ir","ib"],"paths":[{"w":43200,"h":43200,"d":[["M",3900,14370],["A",6753,9190,-11429249,7426832],["A",5333,7267,-8646143,5396714],["A",4365,5945,-8748475,5983381],["A",4857,6595,-7859164,7034504],["A",5333,7273,-4722533,6541615],["A",6775,9220,-2776035,7816140],["A",5785,7867,37501,6842000],["A",6752,9215,1347096,6910353],["A",7720,10543,3974558,4542661],["A",4360,5918,-16496525,8804134],["A",4345,5945,-14809710,9151131],["Z"]]},{"w":43200,"h":43200,"fill":"none","d":[["M",4693,26177],["A",4345,5945,5204520,1585770],["M",6928,34899],["A",4360,5918,4416628,686848],["M",16478,39090],["A",6752,9215,8257449,844866],["M",28827,34751],["A",6752,9215,387196,959901],["M",34129,22954],["A",5785,7867,-4217541,4255042],["M",41798,15354],["A",5333,7273,1819082,1665090],["M",38324,5426],["A",4857,6595,-824660,891534],["M",29078,3952],["A",4857,6595,-8950887,1091722],["M",22141,4720],["A",4365,5945,-9809656,1061181],["M",14000,5192],["A",6753,9190,-4002417,739161],["M",4127,15789],["A",6753,9190,9459261,711490]]}]},"cloudCallout":{"av":[["adj1",["val",-20833]],["adj2",["val",62500]]],"gd":[["dxPos",["*/","w","adj1",100000]],["dyPos",["*/","h","adj2",100000]],["xPos",["+-","hc","dxPos",0]],["yPos",["+-","vc","dyPos",0]],["ht",["cat2","hd2","dxPos","dyPos"]],["wt",["sat2","wd2","dxPos","dyPos"]],["g2",["cat2","wd2","ht","wt"]],["g3",["sat2","hd2","ht","wt"]],["g4",["+-","hc","g2",0]],["g5",["+-","vc","g3",0]],["g6",["+-","g4",0,"xPos"]],["g7",["+-","g5",0,"yPos"]],["g8",["mod","g6","g7",0]],["g9",["*/","ss",6600,21600]],["g10",["+-","g8",0,"g9"]],["g11",["*/","g10",1,3]],["g12",["*/","ss",1800,21600]],["g13",["+-","g11","g12",0]],["g14",["*/","g13","g6","g8"]],["g15",["*/","g13","g7","g8"]],["g16",["+-","g14","xPos",0]],["g17",["+-","g15","yPos",0]],["g18",["*/","ss",4800,21600]],["g19",["*/","g11",2,1]],["g20",["+-","g18","g19",0]],["g21",["*/","g20","g6","g8"]],["g22",["*/","g20","g7","g8"]],["g23",["+-","g21","xPos",0]],["g24",["+-","g22","yPos",0]],["g25",["*/","ss",1200,21600]],["g26",["*/","ss",600,21600]],["x23",["+-","xPos","g26",0]],["x24",["+-","g16","g25",0]],["x25",["+-","g23","g12",0]],["il",["*/","w",2977,21600]],["it",["*/","h",3262,21600]],["ir",["*/","w",17087,21600]],["ib",["*/","h",17337,21600]],["g27",["*/","w",67,21600]],["g28",["*/","h",21577,21600]],["g29",["*/","w",21582,21600]],["g30",["*/","h",1235,21600]],["pang",["at2","dxPos","dyPos"]]],"rect":["il","it","ir","ib"],"paths":[{"w":43200,"h":43200,"d":[["M",3900,14370],["A",6753,9190,-11429249,7426832],["A",5333,7267,-8646143,5396714],["A",4365,5945,-8748475,5983381],["A",4857,6595,-7859164,7034504],["A",5333,7273,-4722533,6541615],["A",6775,9220,-2776035,7816140],["A",5785,7867,37501,6842000],["A",6752,9215,1347096,6910353],["A",7720,10543,3974558,4542661],["A",4360,5918,-16496525,8804134],["A",4345,5945,-14809710,9151131],["Z"]]},{"d":[["M","x23","yPos"],["A","g26","g26",0,21600000],["Z"]]},{"d":[["M","x24","g17"],["A","g25","g25",0,21600000],["Z"]]},{"d":[["M","x25","g24"],["A","g12","g12",0,21600000],["Z"]]},{"w":43200,"h":43200,"fill":"none","d":[["M",4693,26177],["A",4345,5945,5204520,1585770],["M",6928,34899],["A",4360,5918,4416628,686848],["M",16478,39090],["A",6752,9215,8257449,844866],["M",28827,34751],["A",6752,9215,387196,959901],["M",34129,22954],["A",5785,7867,-4217541,4255042],["M",41798,15354],["A",5333,7273,1819082,1665090],["M",38324,5426],["A",4857,6595,-824660,891534],["M",29078,3952],["A",4857,6595,-8950887,1091722],["M",22141,4720],["A",4365,5945,-9809656,1061181],["M",14000,5192],["A",6753,9190,-4002417,739161],["M",4127,15789],["A",6753,9190,9459261,711490]]}]},"corner":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj1",["*/",100000,"h","ss"]],["maxAdj2",["*/",100000,"w","ss"]],["a1",["pin",0,"adj1","maxAdj1"]],["a2",["pin",0,"adj2","maxAdj2"]],["x1",["*/","ss","a2",100000]],["dy1",["*/","ss","a1",100000]],["y1",["+-","b",0,"dy1"]],["cx1",["*/","x1",1,2]],["cy1",["+/","y1","b",2]],["d",["+-","w",0,"h"]],["it",["?:","d","y1","t"]],["ir",["?:","d","r","x1"]]],"rect":["l","it","ir","b"],"paths":[{"d":[["M","l","t"],["L","x1","t"],["L","x1","y1"],["L","r","y1"],["L","r","b"],["L","l","b"],["Z"]]}]},"cornerTabs":{"gd":[["md",["mod","w","h",0]],["dx",["*/",1,"md",20]],["y1",["+-",0,"b","dx"]],["x1",["+-",0,"r","dx"]]],"rect":["dx","dx","x1","y1"],"paths":[{"d":[["M","l","t"],["L","dx","t"],["L","l","dx"],["Z"]]},{"d":[["M","l","y1"],["L","dx","b"],["L","l","b"],["Z"]]},{"d":[["M","x1","t"],["L","r","t"],["L","r","dx"],["Z"]]},{"d":[["M","r","y1"],["L","r","b"],["L","x1","b"],["Z"]]}]},"cube":{"av":[["adj",["val",25000]]],"gd":[["a",["pin",0,"adj",100000]],["y1",["*/","ss","a",100000]],["y4",["+-","b",0,"y1"]],["y2",["*/","y4",1,2]],["y3",["+/","y1","b",2]],["x4",["+-","r",0,"y1"]],["x2",["*/","x4",1,2]],["x3",["+/","y1","r",2]]],"rect":["l","y1","x4","b"],"paths":[{"stroke":false,"d":[["M","l","y1"],["L","x4","y1"],["L","x4","b"],["L","l","b"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x4","y1"],["L","r","t"],["L","r","y4"],["L","x4","b"],["Z"]]},{"fill":"lightenLess","stroke":false,"d":[["M","l","y1"],["L","y1","t"],["L","r","t"],["L","x4","y1"],["Z"]]},{"fill":"none","d":[["M","l","y1"],["L","y1","t"],["L","r","t"],["L","r","y4"],["L","x4","b"],["L","l","b"],["Z"],["M","l","y1"],["L","x4","y1"],["L","r","t"],["M","x4","y1"],["L","x4","b"]]}]},"curvedConnector2":{"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["C","wd2","t","r","hd2","r","b"]]}]},"curvedConnector3":{"av":[["adj1",["val",50000]]],"gd":[["x2",["*/","w","adj1",100000]],["x1",["+/","l","x2",2]],["x3",["+/","r","x2",2]],["y3",["*/","h",3,4]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["C","x1","t","x2","hd4","x2","vc"],["C","x2","y3","x3","b","r","b"]]}]},"curvedConnector4":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["x2",["*/","w","adj1",100000]],["x1",["+/","l","x2",2]],["x3",["+/","r","x2",2]],["x4",["+/","x2","x3",2]],["x5",["+/","x3","r",2]],["y4",["*/","h","adj2",100000]],["y1",["+/","t","y4",2]],["y2",["+/","t","y1",2]],["y3",["+/","y1","y4",2]],["y5",["+/","b","y4",2]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["C","x1","t","x2","y2","x2","y1"],["C","x2","y3","x4","y4","x3","y4"],["C","x5","y4","r","y5","r","b"]]}]},"curvedConnector5":{"av":[["adj1",["val",50000]],["adj2",["val",50000]],["adj3",["val",50000]]],"gd":[["x3",["*/","w","adj1",100000]],["x6",["*/","w","adj3",100000]],["x1",["+/","x3","x6",2]],["x2",["+/","l","x3",2]],["x4",["+/","x3","x1",2]],["x5",["+/","x6","x1",2]],["x7",["+/","x6","r",2]],["y4",["*/","h","adj2",100000]],["y1",["+/","t","y4",2]],["y2",["+/","t","y1",2]],["y3",["+/","y1","y4",2]],["y5",["+/","b","y4",2]],["y6",["+/","y5","y4",2]],["y7",["+/","y5","b",2]]],"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["C","x2","t","x3","y2","x3","y1"],["C","x3","y3","x4","y4","x1","y4"],["C","x5","y4","x6","y6","x6","y5"],["C","x6","y7","x7","b","r","b"]]}]},"curvedDownArrow":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",25000]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["a1",["pin",0,"adj1",100000]],["th",["*/","ss","a1",100000]],["aw",["*/","ss","a2",100000]],["q1",["+/","th","aw",4]],["wR",["+-","wd2",0,"q1"]],["q7",["*/","wR",2,1]],["q8",["*/","q7","q7",1]],["q9",["*/","th","th",1]],["q10",["+-","q8",0,"q9"]],["q11",["sqrt","q10"]],["idy",["*/","q11","h","q7"]],["maxAdj3",["*/",100000,"idy","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["ah",["*/","ss","adj3",100000]],["x3",["+-","wR","th",0]],["q2",["*/","h","h",1]],["q3",["*/","ah","ah",1]],["q4",["+-","q2",0,"q3"]],["q5",["sqrt","q4"]],["dx",["*/","q5","wR","h"]],["x5",["+-","wR","dx",0]],["x7",["+-","x3","dx",0]],["q6",["+-","aw",0,"th"]],["dh",["*/","q6",1,2]],["x4",["+-","x5",0,"dh"]],["x8",["+-","x7","dh",0]],["aw2",["*/","aw",1,2]],["x6",["+-","r",0,"aw2"]],["y1",["+-","b",0,"ah"]],["swAng",["at2","ah","dx"]],["mswAng",["+-",0,0,"swAng"]],["iy",["+-","b",0,"idy"]],["ix",["+/","wR","x3",2]],["q12",["*/","th",1,2]],["dang2",["at2","idy","q12"]],["stAng",["+-","3cd4","swAng",0]],["stAng2",["+-","3cd4",0,"dang2"]],["swAng2",["+-","dang2",0,"cd4"]],["swAng3",["+-","cd4","dang2",0]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","x6","b"],["L","x4","y1"],["L","x5","y1"],["A","wR","h","stAng","mswAng"],["L","x3","t"],["A","wR","h","3cd4","swAng"],["L","x8","y1"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","ix","iy"],["A","wR","h","stAng2","swAng2"],["L","l","b"],["A","wR","h","cd2","swAng3"],["Z"]]},{"fill":"none","d":[["M","ix","iy"],["A","wR","h","stAng2","swAng2"],["L","l","b"],["A","wR","h","cd2","cd4"],["L","x3","t"],["A","wR","h","3cd4","swAng"],["L","x8","y1"],["L","x6","b"],["L","x4","y1"],["L","x5","y1"],["A","wR","h","stAng","mswAng"]]}]},"curvedLeftArrow":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",25000]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["a1",["pin",0,"adj1","a2"]],["th",["*/","ss","a1",100000]],["aw",["*/","ss","a2",100000]],["q1",["+/","th","aw",4]],["hR",["+-","hd2",0,"q1"]],["q7",["*/","hR",2,1]],["q8",["*/","q7","q7",1]],["q9",["*/","th","th",1]],["q10",["+-","q8",0,"q9"]],["q11",["sqrt","q10"]],["idx",["*/","q11","w","q7"]],["maxAdj3",["*/",100000,"idx","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["ah",["*/","ss","a3",100000]],["y3",["+-","hR","th",0]],["q2",["*/","w","w",1]],["q3",["*/","ah","ah",1]],["q4",["+-","q2",0,"q3"]],["q5",["sqrt","q4"]],["dy",["*/","q5","hR","w"]],["y5",["+-","hR","dy",0]],["y7",["+-","y3","dy",0]],["q6",["+-","aw",0,"th"]],["dh",["*/","q6",1,2]],["y4",["+-","y5",0,"dh"]],["y8",["+-","y7","dh",0]],["aw2",["*/","aw",1,2]],["y6",["+-","b",0,"aw2"]],["x1",["+-","l","ah",0]],["swAng",["at2","ah","dy"]],["mswAng",["+-",0,0,"swAng"]],["ix",["+-","l","idx",0]],["iy",["+/","hR","y3",2]],["q12",["*/","th",1,2]],["dang2",["at2","idx","q12"]],["swAng2",["+-","dang2",0,"swAng"]],["swAng3",["+-","swAng","dang2",0]],["stAng3",["+-",0,0,"dang2"]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","y6"],["L","x1","y4"],["L","x1","y5"],["A","w","hR","swAng","swAng2"],["A","w","hR","stAng3","swAng3"],["L","x1","y8"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","r","y3"],["A","w","hR",0,-5400000],["L","l","t"],["A","w","hR","3cd4","cd4"],["Z"]]},{"fill":"none","d":[["M","r","y3"],["A","w","hR",0,-5400000],["L","l","t"],["A","w","hR","3cd4","cd4"],["L","r","y3"],["A","w","hR",0,"swAng"],["L","x1","y8"],["L","l","y6"],["L","x1","y4"],["L","x1","y5"],["A","w","hR","swAng","swAng2"]]}]},"curvedRightArrow":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",25000]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["a1",["pin",0,"adj1","a2"]],["th",["*/","ss","a1",100000]],["aw",["*/","ss","a2",100000]],["q1",["+/","th","aw",4]],["hR",["+-","hd2",0,"q1"]],["q7",["*/","hR",2,1]],["q8",["*/","q7","q7",1]],["q9",["*/","th","th",1]],["q10",["+-","q8",0,"q9"]],["q11",["sqrt","q10"]],["idx",["*/","q11","w","q7"]],["maxAdj3",["*/",100000,"idx","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["ah",["*/","ss","a3",100000]],["y3",["+-","hR","th",0]],["q2",["*/","w","w",1]],["q3",["*/","ah","ah",1]],["q4",["+-","q2",0,"q3"]],["q5",["sqrt","q4"]],["dy",["*/","q5","hR","w"]],["y5",["+-","hR","dy",0]],["y7",["+-","y3","dy",0]],["q6",["+-","aw",0,"th"]],["dh",["*/","q6",1,2]],["y4",["+-","y5",0,"dh"]],["y8",["+-","y7","dh",0]],["aw2",["*/","aw",1,2]],["y6",["+-","b",0,"aw2"]],["x1",["+-","r",0,"ah"]],["swAng",["at2","ah","dy"]],["stAng",["+-","cd2",0,"swAng"]],["mswAng",["+-",0,0,"swAng"]],["ix",["+-","r",0,"idx"]],["iy",["+/","hR","y3",2]],["q12",["*/","th",1,2]],["dang2",["at2","idx","q12"]],["swAng2",["+-","dang2",0,"cd4"]],["swAng3",["+-","cd4","dang2",0]],["stAng3",["+-","cd2",0,"dang2"]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","l","hR"],["A","w","hR","cd2","mswAng"],["L","x1","y4"],["L","r","y6"],["L","x1","y8"],["L","x1","y7"],["A","w","hR","stAng","swAng"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","r","th"],["A","w","hR","3cd4","swAng2"],["A","w","hR","stAng3","swAng3"],["Z"]]},{"fill":"none","d":[["M","l","hR"],["A","w","hR","cd2","mswAng"],["L","x1","y4"],["L","r","y6"],["L","x1","y8"],["L","x1","y7"],["A","w","hR","stAng","swAng"],["L","l","hR"],["A","w","hR","cd2","cd4"],["L","r","th"],["A","w","hR","3cd4","swAng2"]]}]},"curvedUpArrow":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",25000]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["a1",["pin",0,"adj1",100000]],["th",["*/","ss","a1",100000]],["aw",["*/","ss","a2",100000]],["q1",["+/","th","aw",4]],["wR",["+-","wd2",0,"q1"]],["q7",["*/","wR",2,1]],["q8",["*/","q7","q7",1]],["q9",["*/","th","th",1]],["q10",["+-","q8",0,"q9"]],["q11",["sqrt","q10"]],["idy",["*/","q11","h","q7"]],["maxAdj3",["*/",100000,"idy","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["ah",["*/","ss","adj3",100000]],["x3",["+-","wR","th",0]],["q2",["*/","h","h",1]],["q3",["*/","ah","ah",1]],["q4",["+-","q2",0,"q3"]],["q5",["sqrt","q4"]],["dx",["*/","q5","wR","h"]],["x5",["+-","wR","dx",0]],["x7",["+-","x3","dx",0]],["q6",["+-","aw",0,"th"]],["dh",["*/","q6",1,2]],["x4",["+-","x5",0,"dh"]],["x8",["+-","x7","dh",0]],["aw2",["*/","aw",1,2]],["x6",["+-","r",0,"aw2"]],["y1",["+-","t","ah",0]],["swAng",["at2","ah","dx"]],["mswAng",["+-",0,0,"swAng"]],["iy",["+-","t","idy",0]],["ix",["+/","wR","x3",2]],["q12",["*/","th",1,2]],["dang2",["at2","idy","q12"]],["swAng2",["+-","dang2",0,"swAng"]],["mswAng2",["+-",0,0,"swAng2"]],["stAng3",["+-","cd4",0,"swAng"]],["swAng3",["+-","swAng","dang2",0]],["stAng2",["+-","cd4",0,"dang2"]]],"rect":["l","t","r","b"],"paths":[{"stroke":false,"d":[["M","x6","t"],["L","x8","y1"],["L","x7","y1"],["A","wR","h","stAng3","swAng3"],["A","wR","h","stAng2","swAng2"],["L","x4","y1"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","wR","b"],["A","wR","h","cd4","cd4"],["L","th","t"],["A","wR","h","cd2",-5400000],["Z"]]},{"fill":"none","d":[["M","ix","iy"],["A","wR","h","stAng2","swAng2"],["L","x4","y1"],["L","x6","t"],["L","x8","y1"],["L","x7","y1"],["A","wR","h","stAng3","swAng"],["L","wR","b"],["A","wR","h","cd4","cd4"],["L","th","t"],["A","wR","h","cd2",-5400000]]}]},"decagon":{"av":[["vf",["val",105146]]],"gd":[["shd2",["*/","hd2","vf",100000]],["dx1",["cos","wd2",2160000]],["dx2",["cos","wd2",4320000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["dy1",["sin","shd2",4320000]],["dy2",["sin","shd2",2160000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y4",["+-","vc","dy1",0]]],"rect":["x1","y2","x4","y3"],"paths":[{"d":[["M","l","vc"],["L","x1","y2"],["L","x2","y1"],["L","x3","y1"],["L","x4","y2"],["L","r","vc"],["L","x4","y3"],["L","x3","y4"],["L","x2","y4"],["L","x1","y3"],["Z"]]}]},"diagStripe":{"av":[["adj",["val",50000]]],"gd":[["a",["pin",0,"adj",100000]],["x2",["*/","w","a",100000]],["x1",["*/","x2",1,2]],["x3",["+/","x2","r",2]],["y2",["*/","h","a",100000]],["y1",["*/","y2",1,2]],["y3",["+/","y2","b",2]]],"rect":["l","t","x3","y3"],"paths":[{"d":[["M","l","y2"],["L","x2","t"],["L","r","t"],["L","l","b"],["Z"]]}]},"diamond":{"gd":[["ir",["*/","w",3,4]],["ib",["*/","h",3,4]]],"rect":["wd4","hd4","ir","ib"],"paths":[{"d":[["M","l","vc"],["L","hc","t"],["L","r","vc"],["L","hc","b"],["Z"]]}]},"dodecagon":{"gd":[["x1",["*/","w",2894,21600]],["x2",["*/","w",7906,21600]],["x3",["*/","w",13694,21600]],["x4",["*/","w",18706,21600]],["y1",["*/","h",2894,21600]],["y2",["*/","h",7906,21600]],["y3",["*/","h",13694,21600]],["y4",["*/","h",18706,21600]]],"rect":["x1","y1","x4","y4"],"paths":[{"d":[["M","l","y2"],["L","x1","y1"],["L","x2","t"],["L","x3","t"],["L","x4","y1"],["L","r","y2"],["L","r","y3"],["L","x4","y4"],["L","x3","b"],["L","x2","b"],["L","x1","y4"],["L","l","y3"],["Z"]]}]},"donut":{"av":[["adj",["val",25000]]],"gd":[["a",["pin",0,"adj",50000]],["dr",["*/","ss","a",100000]],["iwd2",["+-","wd2",0,"dr"]],["ihd2",["+-","hd2",0,"dr"]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"],["M","dr","vc"],["A","iwd2","ihd2","cd2",-5400000],["A","iwd2","ihd2","cd4",-5400000],["A","iwd2","ihd2",0,-5400000],["A","iwd2","ihd2","3cd4",-5400000],["Z"]]}]},"doubleWave":{"av":[["adj1",["val",6250]],["adj2",["val",0]]],"gd":[["a1",["pin",0,"adj1",12500]],["a2",["pin",-10000,"adj2",10000]],["y1",["*/","h","a1",100000]],["dy2",["*/","y1",10,3]],["y2",["+-","y1",0,"dy2"]],["y3",["+-","y1","dy2",0]],["y4",["+-","b",0,"y1"]],["y5",["+-","y4",0,"dy2"]],["y6",["+-","y4","dy2",0]],["dx1",["*/","w","a2",100000]],["of2",["*/","w","a2",50000]],["x1",["abs","dx1"]],["dx2",["?:","of2",0,"of2"]],["x2",["+-","l",0,"dx2"]],["dx8",["?:","of2","of2",0]],["x8",["+-","r",0,"dx8"]],["dx3",["+/","dx2","x8",6]],["x3",["+-","x2","dx3",0]],["dx4",["+/","dx2","x8",3]],["x4",["+-","x2","dx4",0]],["x5",["+/","x2","x8",2]],["x6",["+-","x5","dx3",0]],["x7",["+/","x6","x8",2]],["x9",["+-","l","dx8",0]],["x15",["+-","r","dx2",0]],["x10",["+-","x9","dx3",0]],["x11",["+-","x9","dx4",0]],["x12",["+/","x9","x15",2]],["x13",["+-","x12","dx3",0]],["x14",["+/","x13","x15",2]],["x16",["+-","r",0,"x1"]],["xAdj",["+-","hc","dx1",0]],["il",["max","x2","x9"]],["ir",["min","x8","x15"]],["it",["*/","h","a1",50000]],["ib",["+-","b",0,"it"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","x2","y1"],["C","x3","y2","x4","y3","x5","y1"],["C","x6","y2","x7","y3","x8","y1"],["L","x15","y4"],["C","x14","y6","x13","y5","x12","y4"],["C","x11","y6","x10","y5","x9","y4"],["Z"]]}]},"downArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",100000,"h","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["dy1",["*/","ss","a2",100000]],["y1",["+-","b",0,"dy1"]],["dx1",["*/","w","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["dy2",["*/","x1","dy1","wd2"]],["y2",["+-","y1","dy2",0]]],"rect":["x1","t","x2","y2"],"paths":[{"d":[["M","l","y1"],["L","x1","y1"],["L","x1","t"],["L","x2","t"],["L","x2","y1"],["L","r","y1"],["L","hc","b"],["Z"]]}]},"downArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",64977]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",100000,"h","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","h"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dx1",["*/","ss","a2",100000]],["dx2",["*/","ss","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["dy3",["*/","ss","a3",100000]],["y3",["+-","b",0,"dy3"]],["y2",["*/","h","a4",100000]],["y1",["*/","y2",1,2]]],"rect":["l","t","r","y2"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","y2"],["L","x3","y2"],["L","x3","y3"],["L","x4","y3"],["L","hc","b"],["L","x1","y3"],["L","x2","y3"],["L","x2","y2"],["L","l","y2"],["Z"]]}]},"ellipse":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]}]},"ellipseRibbon":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",12500]]],"gd":[["a1",["pin",0,"adj1",100000]],["a2",["pin",25000,"adj2",75000]],["q10",["+-",100000,0,"a1"]],["q11",["*/","q10",1,2]],["q12",["+-","a1",0,"q11"]],["minAdj3",["max",0,"q12"]],["a3",["pin","minAdj3","adj3","a1"]],["dx2",["*/","w","a2",200000]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","x2","wd8",0]],["x4",["+-","r",0,"x3"]],["x5",["+-","r",0,"x2"]],["x6",["+-","r",0,"wd8"]],["dy1",["*/","h","a3",100000]],["f1",["*/",4,"dy1","w"]],["q1",["*/","x3","x3","w"]],["q2",["+-","x3",0,"q1"]],["y1",["*/","f1","q2",1]],["cx1",["*/","x3",1,2]],["cy1",["*/","f1","cx1",1]],["cx2",["+-","r",0,"cx1"]],["q1",["*/","h","a1",100000]],["dy3",["+-","q1",0,"dy1"]],["q3",["*/","x2","x2","w"]],["q4",["+-","x2",0,"q3"]],["q5",["*/","f1","q4",1]],["y3",["+-","q5","dy3",0]],["q6",["+-","dy1","dy3","y3"]],["q7",["+-","q6","dy1",0]],["cy3",["+-","q7","dy3",0]],["rh",["+-","b",0,"q1"]],["q8",["*/","dy1",14,16]],["y2",["+/","q8","rh",2]],["y5",["+-","q5","rh",0]],["y6",["+-","y3","rh",0]],["cx4",["*/","x2",1,2]],["q9",["*/","f1","cx4",1]],["cy4",["+-","q9","rh",0]],["cx5",["+-","r",0,"cx4"]],["cy6",["+-","cy3","rh",0]],["y7",["+-","y1","dy3",0]],["cy7",["+-","q1","q1","y7"]],["y8",["+-","b",0,"dy1"]]],"rect":["x2","q1","x5","y6"],"paths":[{"stroke":false,"d":[["M","l","t"],["Q","cx1","cy1","x3","y1"],["L","x2","y3"],["Q","hc","cy3","x5","y3"],["L","x4","y1"],["Q","cx2","cy1","r","t"],["L","x6","y2"],["L","r","rh"],["Q","cx5","cy4","x5","y5"],["L","x5","y6"],["Q","hc","cy6","x2","y6"],["L","x2","y5"],["Q","cx4","cy4","l","rh"],["L","wd8","y2"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x3","y7"],["L","x3","y1"],["L","x2","y3"],["Q","hc","cy3","x5","y3"],["L","x4","y1"],["L","x4","y7"],["Q","hc","cy7","x3","y7"],["Z"]]},{"fill":"none","d":[["M","l","t"],["Q","cx1","cy1","x3","y1"],["L","x2","y3"],["Q","hc","cy3","x5","y3"],["L","x4","y1"],["Q","cx2","cy1","r","t"],["L","x6","y2"],["L","r","rh"],["Q","cx5","cy4","x5","y5"],["L","x5","y6"],["Q","hc","cy6","x2","y6"],["L","x2","y5"],["Q","cx4","cy4","l","rh"],["L","wd8","y2"],["Z"],["M","x2","y5"],["L","x2","y3"],["M","x5","y3"],["L","x5","y5"],["M","x3","y1"],["L","x3","y7"],["M","x4","y7"],["L","x4","y1"]]}]},"ellipseRibbon2":{"av":[["adj1",["val",25000]],["adj2",["val",50000]],["adj3",["val",12500]]],"gd":[["a1",["pin",0,"adj1",100000]],["a2",["pin",25000,"adj2",75000]],["q10",["+-",100000,0,"a1"]],["q11",["*/","q10",1,2]],["q12",["+-","a1",0,"q11"]],["minAdj3",["max",0,"q12"]],["a3",["pin","minAdj3","adj3","a1"]],["dx2",["*/","w","a2",200000]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","x2","wd8",0]],["x4",["+-","r",0,"x3"]],["x5",["+-","r",0,"x2"]],["x6",["+-","r",0,"wd8"]],["dy1",["*/","h","a3",100000]],["f1",["*/",4,"dy1","w"]],["q1",["*/","x3","x3","w"]],["q2",["+-","x3",0,"q1"]],["u1",["*/","f1","q2",1]],["y1",["+-","b",0,"u1"]],["cx1",["*/","x3",1,2]],["cu1",["*/","f1","cx1",1]],["cy1",["+-","b",0,"cu1"]],["cx2",["+-","r",0,"cx1"]],["q1",["*/","h","a1",100000]],["dy3",["+-","q1",0,"dy1"]],["q3",["*/","x2","x2","w"]],["q4",["+-","x2",0,"q3"]],["q5",["*/","f1","q4",1]],["u3",["+-","q5","dy3",0]],["y3",["+-","b",0,"u3"]],["q6",["+-","dy1","dy3","u3"]],["q7",["+-","q6","dy1",0]],["cu3",["+-","q7","dy3",0]],["cy3",["+-","b",0,"cu3"]],["rh",["+-","b",0,"q1"]],["q8",["*/","dy1",14,16]],["u2",["+/","q8","rh",2]],["y2",["+-","b",0,"u2"]],["u5",["+-","q5","rh",0]],["y5",["+-","b",0,"u5"]],["u6",["+-","u3","rh",0]],["y6",["+-","b",0,"u6"]],["cx4",["*/","x2",1,2]],["q9",["*/","f1","cx4",1]],["cu4",["+-","q9","rh",0]],["cy4",["+-","b",0,"cu4"]],["cx5",["+-","r",0,"cx4"]],["cu6",["+-","cu3","rh",0]],["cy6",["+-","b",0,"cu6"]],["u7",["+-","u1","dy3",0]],["y7",["+-","b",0,"u7"]],["cu7",["+-","q1","q1","u7"]],["cy7",["+-","b",0,"cu7"]]],"rect":["x2","y6","x5","rh"],"paths":[{"stroke":false,"d":[["M","l","b"],["Q","cx1","cy1","x3","y1"],["L","x2","y3"],["Q","hc","cy3","x5","y3"],["L","x4","y1"],["Q","cx2","cy1","r","b"],["L","x6","y2"],["L","r","q1"],["Q","cx5","cy4","x5","y5"],["L","x5","y6"],["Q","hc","cy6","x2","y6"],["L","x2","y5"],["Q","cx4","cy4","l","q1"],["L","wd8","y2"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x3","y7"],["L","x3","y1"],["L","x2","y3"],["Q","hc","cy3","x5","y3"],["L","x4","y1"],["L","x4","y7"],["Q","hc","cy7","x3","y7"],["Z"]]},{"fill":"none","d":[["M","l","b"],["L","wd8","y2"],["L","l","q1"],["Q","cx4","cy4","x2","y5"],["L","x2","y6"],["Q","hc","cy6","x5","y6"],["L","x5","y5"],["Q","cx5","cy4","r","q1"],["L","x6","y2"],["L","r","b"],["Q","cx2","cy1","x4","y1"],["L","x5","y3"],["Q","hc","cy3","x2","y3"],["L","x3","y1"],["Q","cx1","cy1","l","b"],["Z"],["M","x2","y3"],["L","x2","y5"],["M","x5","y5"],["L","x5","y3"],["M","x3","y7"],["L","x3","y1"],["M","x4","y1"],["L","x4","y7"]]}]},"flowChartAlternateProcess":{"gd":[["x2",["+-","r",0,"ssd6"]],["y2",["+-","b",0,"ssd6"]],["il",["*/","ssd6",29289,100000]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","l","ssd6"],["A","ssd6","ssd6","cd2","cd4"],["L","x2","t"],["A","ssd6","ssd6","3cd4","cd4"],["L","r","y2"],["A","ssd6","ssd6",0,"cd4"],["L","ssd6","b"],["A","ssd6","ssd6","cd4","cd4"],["Z"]]}]},"flowChartCollate":{"gd":[["ir",["*/","w",3,4]],["ib",["*/","h",3,4]]],"rect":["wd4","hd4","ir","ib"],"paths":[{"w":2,"h":2,"d":[["M",0,0],["L",2,0],["L",1,1],["L",2,2],["L",0,2],["L",1,1],["Z"]]}]},"flowChartConnector":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]}]},"flowChartDecision":{"gd":[["ir",["*/","w",3,4]],["ib",["*/","h",3,4]]],"rect":["wd4","hd4","ir","ib"],"paths":[{"w":2,"h":2,"d":[["M",0,1],["L",1,0],["L",2,1],["L",1,2],["Z"]]}]},"flowChartDelay":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["l","it","ir","ib"],"paths":[{"d":[["M","l","t"],["L","hc","t"],["A","wd2","hd2","3cd4","cd2"],["L","l","b"],["Z"]]}]},"flowChartDisplay":{"gd":[["x2",["*/","w",5,6]]],"rect":["wd6","t","x2","b"],"paths":[{"w":6,"h":6,"d":[["M",0,3],["L",1,0],["L",5,0],["A",1,3,"3cd4","cd2"],["L",1,6],["Z"]]}]},"flowChartDocument":{"gd":[["y1",["*/","h",17322,21600]],["y2",["*/","h",20172,21600]]],"rect":["l","t","r","y1"],"paths":[{"w":21600,"h":21600,"d":[["M",0,0],["L",21600,0],["L",21600,17322],["C",10800,17322,10800,23922,0,20172],["Z"]]}]},"flowChartExtract":{"gd":[["x2",["*/","w",3,4]]],"rect":["wd4","vc","x2","b"],"paths":[{"w":2,"h":2,"d":[["M",0,2],["L",1,0],["L",2,2],["Z"]]}]},"flowChartInputOutput":{"gd":[["x3",["*/","w",2,5]],["x4",["*/","w",3,5]],["x5",["*/","w",4,5]],["x6",["*/","w",9,10]]],"rect":["wd5","t","x5","b"],"paths":[{"w":5,"h":5,"d":[["M",0,5],["L",1,0],["L",5,0],["L",4,5],["Z"]]}]},"flowChartInternalStorage":{"rect":["wd8","hd8","r","b"],"paths":[{"w":1,"h":1,"stroke":false,"d":[["M",0,0],["L",1,0],["L",1,1],["L",0,1],["Z"]]},{"w":8,"h":8,"fill":"none","d":[["M",1,0],["L",1,8],["M",0,1],["L",8,1]]},{"w":1,"h":1,"fill":"none","d":[["M",0,0],["L",1,0],["L",1,1],["L",0,1],["Z"]]}]},"flowChartMagneticDisk":{"gd":[["y3",["*/","h",5,6]]],"rect":["l","hd3","r","y3"],"paths":[{"w":6,"h":6,"stroke":false,"d":[["M",0,1],["A",3,1,"cd2","cd2"],["L",6,5],["A",3,1,0,"cd2"],["Z"]]},{"w":6,"h":6,"fill":"none","d":[["M",6,1],["A",3,1,0,"cd2"]]},{"w":6,"h":6,"fill":"none","d":[["M",0,1],["A",3,1,"cd2","cd2"],["L",6,5],["A",3,1,0,"cd2"],["Z"]]}]},"flowChartMagneticDrum":{"gd":[["x2",["*/","w",2,3]]],"rect":["wd6","t","x2","b"],"paths":[{"w":6,"h":6,"stroke":false,"d":[["M",1,0],["L",5,0],["A",1,3,"3cd4","cd2"],["L",1,6],["A",1,3,"cd4","cd2"],["Z"]]},{"w":6,"h":6,"fill":"none","d":[["M",5,6],["A",1,3,"cd4","cd2"]]},{"w":6,"h":6,"fill":"none","d":[["M",1,0],["L",5,0],["A",1,3,"3cd4","cd2"],["L",1,6],["A",1,3,"cd4","cd2"],["Z"]]}]},"flowChartMagneticTape":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]],["ang1",["at2","w","h"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","hc","b"],["A","wd2","hd2","cd4","cd4"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"ang1"],["L","r","ib"],["L","r","b"],["Z"]]}]},"flowChartManualInput":{"rect":["l","hd5","r","b"],"paths":[{"w":5,"h":5,"d":[["M",0,1],["L",5,0],["L",5,5],["L",0,5],["Z"]]}]},"flowChartManualOperation":{"gd":[["x3",["*/","w",4,5]],["x4",["*/","w",9,10]]],"rect":["wd5","t","x3","b"],"paths":[{"w":5,"h":5,"d":[["M",0,0],["L",5,0],["L",4,5],["L",1,5],["Z"]]}]},"flowChartMerge":{"gd":[["x2",["*/","w",3,4]]],"rect":["wd4","t","x2","vc"],"paths":[{"w":2,"h":2,"d":[["M",0,0],["L",2,0],["L",1,2],["Z"]]}]},"flowChartMultidocument":{"gd":[["y2",["*/","h",3675,21600]],["y8",["*/","h",20782,21600]],["x3",["*/","w",9298,21600]],["x4",["*/","w",12286,21600]],["x5",["*/","w",18595,21600]]],"rect":["l","y2","x5","y8"],"paths":[{"w":21600,"h":21600,"stroke":false,"d":[["M",0,20782],["C",9298,23542,9298,18022,18595,18022],["L",18595,3675],["L",0,3675],["Z"],["M",1532,3675],["L",1532,1815],["L",20000,1815],["L",20000,16252],["C",19298,16252,18595,16352,18595,16352],["L",18595,3675],["Z"],["M",2972,1815],["L",2972,0],["L",21600,0],["L",21600,14392],["C",20800,14392,20000,14467,20000,14467],["L",20000,1815],["Z"]]},{"w":21600,"h":21600,"fill":"none","d":[["M",0,3675],["L",18595,3675],["L",18595,18022],["C",9298,18022,9298,23542,0,20782],["Z"],["M",1532,3675],["L",1532,1815],["L",20000,1815],["L",20000,16252],["C",19298,16252,18595,16352,18595,16352],["M",2972,1815],["L",2972,0],["L",21600,0],["L",21600,14392],["C",20800,14392,20000,14467,20000,14467]]},{"w":21600,"h":21600,"fill":"none","stroke":false,"d":[["M",0,20782],["C",9298,23542,9298,18022,18595,18022],["L",18595,16352],["C",18595,16352,19298,16252,20000,16252],["L",20000,14467],["C",20000,14467,20800,14392,21600,14392],["L",21600,0],["L",2972,0],["L",2972,1815],["L",1532,1815],["L",1532,3675],["L",0,3675],["Z"]]}]},"flowChartOfflineStorage":{"gd":[["x4",["*/","w",3,4]]],"rect":["wd4","t","x4","vc"],"paths":[{"w":2,"h":2,"stroke":false,"d":[["M",0,0],["L",2,0],["L",1,2],["Z"]]},{"w":5,"h":5,"fill":"none","d":[["M",2,4],["L",3,4]]},{"w":2,"h":2,"fill":"none","d":[["M",0,0],["L",2,0],["L",1,2],["Z"]]}]},"flowChartOffpageConnector":{"gd":[["y1",["*/","h",4,5]]],"rect":["l","t","r","y1"],"paths":[{"w":10,"h":10,"d":[["M",0,0],["L",10,0],["L",10,8],["L",5,10],["L",0,8],["Z"]]}]},"flowChartOnlineStorage":{"gd":[["x2",["*/","w",5,6]]],"rect":["wd6","t","x2","b"],"paths":[{"w":6,"h":6,"d":[["M",1,0],["L",6,0],["A",1,3,"3cd4",-10800000],["L",1,6],["A",1,3,"cd4","cd2"],["Z"]]}]},"flowChartOr":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]},{"fill":"none","d":[["M","hc","t"],["L","hc","b"],["M","l","vc"],["L","r","vc"]]},{"fill":"none","d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]}]},"flowChartPredefinedProcess":{"gd":[["x2",["*/","w",7,8]]],"rect":["wd8","t","x2","b"],"paths":[{"w":1,"h":1,"stroke":false,"d":[["M",0,0],["L",1,0],["L",1,1],["L",0,1],["Z"]]},{"w":8,"h":8,"fill":"none","d":[["M",1,0],["L",1,8],["M",7,0],["L",7,8]]},{"w":1,"h":1,"fill":"none","d":[["M",0,0],["L",1,0],["L",1,1],["L",0,1],["Z"]]}]},"flowChartPreparation":{"gd":[["x2",["*/","w",4,5]]],"rect":["wd5","t","x2","b"],"paths":[{"w":10,"h":10,"d":[["M",0,5],["L",2,0],["L",8,0],["L",10,5],["L",8,10],["L",2,10],["Z"]]}]},"flowChartProcess":{"rect":["l","t","r","b"],"paths":[{"w":1,"h":1,"d":[["M",0,0],["L",1,0],["L",1,1],["L",0,1],["Z"]]}]},"flowChartPunchedCard":{"rect":["l","hd5","r","b"],"paths":[{"w":5,"h":5,"d":[["M",0,1],["L",1,0],["L",5,0],["L",5,5],["L",0,5],["Z"]]}]},"flowChartPunchedTape":{"gd":[["y2",["*/","h",9,10]],["ib",["*/","h",4,5]]],"rect":["l","hd5","r","ib"],"paths":[{"w":20,"h":20,"d":[["M",0,2],["A",5,2,"cd2",-10800000],["A",5,2,"cd2","cd2"],["L",20,18],["A",5,2,0,-10800000],["A",5,2,0,"cd2"],["Z"]]}]},"flowChartSort":{"gd":[["ir",["*/","w",3,4]],["ib",["*/","h",3,4]]],"rect":["wd4","hd4","ir","ib"],"paths":[{"w":2,"h":2,"stroke":false,"d":[["M",0,1],["L",1,0],["L",2,1],["L",1,2],["Z"]]},{"w":2,"h":2,"fill":"none","d":[["M",0,1],["L",2,1]]},{"w":2,"h":2,"fill":"none","d":[["M",0,1],["L",1,0],["L",2,1],["L",1,2],["Z"]]}]},"flowChartSummingJunction":{"gd":[["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]},{"fill":"none","d":[["M","il","it"],["L","ir","ib"],["M","ir","it"],["L","il","ib"]]},{"fill":"none","d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]}]},"flowChartTerminator":{"gd":[["il",["*/","w",1018,21600]],["ir",["*/","w",20582,21600]],["it",["*/","h",3163,21600]],["ib",["*/","h",18437,21600]]],"rect":["il","it","ir","ib"],"paths":[{"w":21600,"h":21600,"d":[["M",3475,0],["L",18125,0],["A",3475,10800,"3cd4","cd2"],["L",3475,21600],["A",3475,10800,"cd4","cd2"],["Z"]]}]},"foldedCorner":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["dy2",["*/","ss","a",100000]],["dy1",["*/","dy2",1,5]],["x1",["+-","r",0,"dy2"]],["x2",["+-","x1","dy1",0]],["y2",["+-","b",0,"dy2"]],["y1",["+-","y2","dy1",0]]],"rect":["l","t","r","y2"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","r","t"],["L","r","y2"],["L","x1","b"],["L","l","b"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x1","b"],["L","x2","y1"],["L","r","y2"],["Z"]]},{"fill":"none","d":[["M","x1","b"],["L","x2","y1"],["L","r","y2"],["L","x1","b"],["L","l","b"],["L","l","t"],["L","r","t"],["L","r","y2"]]}]},"frame":{"av":[["adj1",["val",12500]]],"gd":[["a1",["pin",0,"adj1",50000]],["x1",["*/","ss","a1",100000]],["x4",["+-","r",0,"x1"]],["y4",["+-","b",0,"x1"]]],"rect":["x1","x1","x4","y4"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"],["M","x1","x1"],["L","x1","y4"],["L","x4","y4"],["L","x4","x1"],["Z"]]}]},"funnel":{"gd":[["d",["*/","ss",1,20]],["rw2",["+-","wd2",0,"d"]],["rh2",["+-","hd4",0,"d"]],["t1",["cos","wd2",480000]],["t2",["sin","hd4",480000]],["da",["at2","t1","t2"]],["2da",["*/","da",2,1]],["stAng1",["+-","cd2",0,"da"]],["swAng1",["+-","cd2","2da",0]],["swAng3",["+-","cd2",0,"2da"]],["rw3",["*/","wd2",1,4]],["rh3",["*/","hd4",1,4]],["ct1",["cos","hd4","stAng1"]],["st1",["sin","wd2","stAng1"]],["m1",["mod","ct1","st1",0]],["n1",["*/","wd2","hd4","m1"]],["dx1",["cos","n1","stAng1"]],["dy1",["sin","n1","stAng1"]],["x1",["+-","hc","dx1",0]],["y1",["+-","hd4","dy1",0]],["ct3",["cos","rh3","da"]],["st3",["sin","rw3","da"]],["m3",["mod","ct3","st3",0]],["n3",["*/","rw3","rh3","m3"]],["dx3",["cos","n3","da"]],["dy3",["sin","n3","da"]],["x3",["+-","hc","dx3",0]],["vc3",["+-","b",0,"rh3"]],["y2",["+-","vc3","dy3",0]],["x2",["+-","wd2",0,"rw2"]],["cd",["*/","cd2",2,1]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","x1","y1"],["A","wd2","hd4","stAng1","swAng1"],["L","x3","y2"],["A","rw3","rh3","da","swAng3"],["Z"],["M","x2","hd4"],["A","rw2","rh2","cd2",-21600000],["Z"]]}]},"gear6":{"av":[["adj1",["val",15000]],["adj2",["val",3526]]],"gd":[["a1",["pin",0,"adj1",20000]],["a2",["pin",0,"adj2",5358]],["th",["*/","ss","a1",100000]],["lFD",["*/","ss","a2",100000]],["th2",["*/","th",1,2]],["l2",["*/","lFD",1,2]],["l3",["+-","th2","l2",0]],["rh",["+-","hd2",0,"th"]],["rw",["+-","wd2",0,"th"]],["dr",["+-","rw",0,"rh"]],["maxr",["?:","dr","rh","rw"]],["ha",["at2","maxr","l3"]],["aA1",["+-",19800000,0,"ha"]],["aD1",["+-",19800000,"ha",0]],["ta11",["cos","rw","aA1"]],["ta12",["sin","rh","aA1"]],["bA1",["at2","ta11","ta12"]],["cta1",["cos","rh","bA1"]],["sta1",["sin","rw","bA1"]],["ma1",["mod","cta1","sta1",0]],["na1",["*/","rw","rh","ma1"]],["dxa1",["cos","na1","bA1"]],["dya1",["sin","na1","bA1"]],["xA1",["+-","hc","dxa1",0]],["yA1",["+-","vc","dya1",0]],["td11",["cos","rw","aD1"]],["td12",["sin","rh","aD1"]],["bD1",["at2","td11","td12"]],["ctd1",["cos","rh","bD1"]],["std1",["sin","rw","bD1"]],["md1",["mod","ctd1","std1",0]],["nd1",["*/","rw","rh","md1"]],["dxd1",["cos","nd1","bD1"]],["dyd1",["sin","nd1","bD1"]],["xD1",["+-","hc","dxd1",0]],["yD1",["+-","vc","dyd1",0]],["xAD1",["+-","xA1",0,"xD1"]],["yAD1",["+-","yA1",0,"yD1"]],["lAD1",["mod","xAD1","yAD1",0]],["a1",["at2","yAD1","xAD1"]],["dxF1",["sin","lFD","a1"]],["dyF1",["cos","lFD","a1"]],["xF1",["+-","xD1","dxF1",0]],["yF1",["+-","yD1","dyF1",0]],["xE1",["+-","xA1",0,"dxF1"]],["yE1",["+-","yA1",0,"dyF1"]],["yC1t",["sin","th","a1"]],["xC1t",["cos","th","a1"]],["yC1",["+-","yF1","yC1t",0]],["xC1",["+-","xF1",0,"xC1t"]],["yB1",["+-","yE1","yC1t",0]],["xB1",["+-","xE1",0,"xC1t"]],["aD6",["+-","3cd4","ha",0]],["td61",["cos","rw","aD6"]],["td62",["sin","rh","aD6"]],["bD6",["at2","td61","td62"]],["ctd6",["cos","rh","bD6"]],["std6",["sin","rw","bD6"]],["md6",["mod","ctd6","std6",0]],["nd6",["*/","rw","rh","md6"]],["dxd6",["cos","nd6","bD6"]],["dyd6",["sin","nd6","bD6"]],["xD6",["+-","hc","dxd6",0]],["yD6",["+-","vc","dyd6",0]],["xA6",["+-","hc",0,"dxd6"]],["xF6",["+-","xD6",0,"lFD"]],["xE6",["+-","xA6","lFD",0]],["yC6",["+-","yD6",0,"th"]],["swAng1",["+-","bA1",0,"bD6"]],["aA2",["+-",1800000,0,"ha"]],["aD2",["+-",1800000,"ha",0]],["ta21",["cos","rw","aA2"]],["ta22",["sin","rh","aA2"]],["bA2",["at2","ta21","ta22"]],["yA2",["+-","h",0,"yD1"]],["td21",["cos","rw","aD2"]],["td22",["sin","rh","aD2"]],["bD2",["at2","td21","td22"]],["yD2",["+-","h",0,"yA1"]],["yC2",["+-","h",0,"yB1"]],["yB2",["+-","h",0,"yC1"]],["xB2",["val","xC1"]],["swAng2",["+-","bA2",0,"bD1"]],["aD3",["+-","cd4","ha",0]],["td31",["cos","rw","aD3"]],["td32",["sin","rh","aD3"]],["bD3",["at2","td31","td32"]],["yD3",["+-","h",0,"yD6"]],["yB3",["+-","h",0,"yC6"]],["aD4",["+-",9000000,"ha",0]],["td41",["cos","rw","aD4"]],["td42",["sin","rh","aD4"]],["bD4",["at2","td41","td42"]],["xD4",["+-","w",0,"xD1"]],["xC4",["+-","w",0,"xC1"]],["xB4",["+-","w",0,"xB1"]],["aD5",["+-",12600000,"ha",0]],["td51",["cos","rw","aD5"]],["td52",["sin","rh","aD5"]],["bD5",["at2","td51","td52"]],["xD5",["+-","w",0,"xA1"]],["xC5",["+-","w",0,"xB1"]],["xB5",["+-","w",0,"xC1"]],["xCxn1",["+/","xB1","xC1",2]],["yCxn1",["+/","yB1","yC1",2]],["yCxn2",["+-","b",0,"yCxn1"]],["xCxn4",["+/","r",0,"xCxn1"]]],"rect":["xD5","yA1","xA1","yD2"],"paths":[{"d":[["M","xA1","yA1"],["L","xB1","yB1"],["L","xC1","yC1"],["L","xD1","yD1"],["A","rw","rh","bD1","swAng2"],["L","xC1","yB2"],["L","xB1","yC2"],["L","xA1","yD2"],["A","rw","rh","bD2","swAng1"],["L","xF6","yB3"],["L","xE6","yB3"],["L","xA6","yD3"],["A","rw","rh","bD3","swAng1"],["L","xB4","yC2"],["L","xC4","yB2"],["L","xD4","yA2"],["A","rw","rh","bD4","swAng2"],["L","xB5","yC1"],["L","xC5","yB1"],["L","xD5","yA1"],["A","rw","rh","bD5","swAng1"],["L","xE6","yC6"],["L","xF6","yC6"],["L","xD6","yD6"],["A","rw","rh","bD6","swAng1"],["Z"]]}]},"gear9":{"av":[["adj1",["val",10000]],["adj2",["val",1763]]],"gd":[["a1",["pin",0,"adj1",20000]],["a2",["pin",0,"adj2",2679]],["th",["*/","ss","a1",100000]],["lFD",["*/","ss","a2",100000]],["th2",["*/","th",1,2]],["l2",["*/","lFD",1,2]],["l3",["+-","th2","l2",0]],["rh",["+-","hd2",0,"th"]],["rw",["+-","wd2",0,"th"]],["dr",["+-","rw",0,"rh"]],["maxr",["?:","dr","rh","rw"]],["ha",["at2","maxr","l3"]],["aA1",["+-",18600000,0,"ha"]],["aD1",["+-",18600000,"ha",0]],["ta11",["cos","rw","aA1"]],["ta12",["sin","rh","aA1"]],["bA1",["at2","ta11","ta12"]],["cta1",["cos","rh","bA1"]],["sta1",["sin","rw","bA1"]],["ma1",["mod","cta1","sta1",0]],["na1",["*/","rw","rh","ma1"]],["dxa1",["cos","na1","bA1"]],["dya1",["sin","na1","bA1"]],["xA1",["+-","hc","dxa1",0]],["yA1",["+-","vc","dya1",0]],["td11",["cos","rw","aD1"]],["td12",["sin","rh","aD1"]],["bD1",["at2","td11","td12"]],["ctd1",["cos","rh","bD1"]],["std1",["sin","rw","bD1"]],["md1",["mod","ctd1","std1",0]],["nd1",["*/","rw","rh","md1"]],["dxd1",["cos","nd1","bD1"]],["dyd1",["sin","nd1","bD1"]],["xD1",["+-","hc","dxd1",0]],["yD1",["+-","vc","dyd1",0]],["xAD1",["+-","xA1",0,"xD1"]],["yAD1",["+-","yA1",0,"yD1"]],["lAD1",["mod","xAD1","yAD1",0]],["a1",["at2","yAD1","xAD1"]],["dxF1",["sin","lFD","a1"]],["dyF1",["cos","lFD","a1"]],["xF1",["+-","xD1","dxF1",0]],["yF1",["+-","yD1","dyF1",0]],["xE1",["+-","xA1",0,"dxF1"]],["yE1",["+-","yA1",0,"dyF1"]],["yC1t",["sin","th","a1"]],["xC1t",["cos","th","a1"]],["yC1",["+-","yF1","yC1t",0]],["xC1",["+-","xF1",0,"xC1t"]],["yB1",["+-","yE1","yC1t",0]],["xB1",["+-","xE1",0,"xC1t"]],["aA2",["+-",21000000,0,"ha"]],["aD2",["+-",21000000,"ha",0]],["ta21",["cos","rw","aA2"]],["ta22",["sin","rh","aA2"]],["bA2",["at2","ta21","ta22"]],["cta2",["cos","rh","bA2"]],["sta2",["sin","rw","bA2"]],["ma2",["mod","cta2","sta2",0]],["na2",["*/","rw","rh","ma2"]],["dxa2",["cos","na2","bA2"]],["dya2",["sin","na2","bA2"]],["xA2",["+-","hc","dxa2",0]],["yA2",["+-","vc","dya2",0]],["td21",["cos","rw","aD2"]],["td22",["sin","rh","aD2"]],["bD2",["at2","td21","td22"]],["ctd2",["cos","rh","bD2"]],["std2",["sin","rw","bD2"]],["md2",["mod","ctd2","std2",0]],["nd2",["*/","rw","rh","md2"]],["dxd2",["cos","nd2","bD2"]],["dyd2",["sin","nd2","bD2"]],["xD2",["+-","hc","dxd2",0]],["yD2",["+-","vc","dyd2",0]],["xAD2",["+-","xA2",0,"xD2"]],["yAD2",["+-","yA2",0,"yD2"]],["lAD2",["mod","xAD2","yAD2",0]],["a2",["at2","yAD2","xAD2"]],["dxF2",["sin","lFD","a2"]],["dyF2",["cos","lFD","a2"]],["xF2",["+-","xD2","dxF2",0]],["yF2",["+-","yD2","dyF2",0]],["xE2",["+-","xA2",0,"dxF2"]],["yE2",["+-","yA2",0,"dyF2"]],["yC2t",["sin","th","a2"]],["xC2t",["cos","th","a2"]],["yC2",["+-","yF2","yC2t",0]],["xC2",["+-","xF2",0,"xC2t"]],["yB2",["+-","yE2","yC2t",0]],["xB2",["+-","xE2",0,"xC2t"]],["swAng1",["+-","bA2",0,"bD1"]],["aA3",["+-",1800000,0,"ha"]],["aD3",["+-",1800000,"ha",0]],["ta31",["cos","rw","aA3"]],["ta32",["sin","rh","aA3"]],["bA3",["at2","ta31","ta32"]],["cta3",["cos","rh","bA3"]],["sta3",["sin","rw","bA3"]],["ma3",["mod","cta3","sta3",0]],["na3",["*/","rw","rh","ma3"]],["dxa3",["cos","na3","bA3"]],["dya3",["sin","na3","bA3"]],["xA3",["+-","hc","dxa3",0]],["yA3",["+-","vc","dya3",0]],["td31",["cos","rw","aD3"]],["td32",["sin","rh","aD3"]],["bD3",["at2","td31","td32"]],["ctd3",["cos","rh","bD3"]],["std3",["sin","rw","bD3"]],["md3",["mod","ctd3","std3",0]],["nd3",["*/","rw","rh","md3"]],["dxd3",["cos","nd3","bD3"]],["dyd3",["sin","nd3","bD3"]],["xD3",["+-","hc","dxd3",0]],["yD3",["+-","vc","dyd3",0]],["xAD3",["+-","xA3",0,"xD3"]],["yAD3",["+-","yA3",0,"yD3"]],["lAD3",["mod","xAD3","yAD3",0]],["a3",["at2","yAD3","xAD3"]],["dxF3",["sin","lFD","a3"]],["dyF3",["cos","lFD","a3"]],["xF3",["+-","xD3","dxF3",0]],["yF3",["+-","yD3","dyF3",0]],["xE3",["+-","xA3",0,"dxF3"]],["yE3",["+-","yA3",0,"dyF3"]],["yC3t",["sin","th","a3"]],["xC3t",["cos","th","a3"]],["yC3",["+-","yF3","yC3t",0]],["xC3",["+-","xF3",0,"xC3t"]],["yB3",["+-","yE3","yC3t",0]],["xB3",["+-","xE3",0,"xC3t"]],["swAng2",["+-","bA3",0,"bD2"]],["aA4",["+-",4200000,0,"ha"]],["aD4",["+-",4200000,"ha",0]],["ta41",["cos","rw","aA4"]],["ta42",["sin","rh","aA4"]],["bA4",["at2","ta41","ta42"]],["cta4",["cos","rh","bA4"]],["sta4",["sin","rw","bA4"]],["ma4",["mod","cta4","sta4",0]],["na4",["*/","rw","rh","ma4"]],["dxa4",["cos","na4","bA4"]],["dya4",["sin","na4","bA4"]],["xA4",["+-","hc","dxa4",0]],["yA4",["+-","vc","dya4",0]],["td41",["cos","rw","aD4"]],["td42",["sin","rh","aD4"]],["bD4",["at2","td41","td42"]],["ctd4",["cos","rh","bD4"]],["std4",["sin","rw","bD4"]],["md4",["mod","ctd4","std4",0]],["nd4",["*/","rw","rh","md4"]],["dxd4",["cos","nd4","bD4"]],["dyd4",["sin","nd4","bD4"]],["xD4",["+-","hc","dxd4",0]],["yD4",["+-","vc","dyd4",0]],["xAD4",["+-","xA4",0,"xD4"]],["yAD4",["+-","yA4",0,"yD4"]],["lAD4",["mod","xAD4","yAD4",0]],["a4",["at2","yAD4","xAD4"]],["dxF4",["sin","lFD","a4"]],["dyF4",["cos","lFD","a4"]],["xF4",["+-","xD4","dxF4",0]],["yF4",["+-","yD4","dyF4",0]],["xE4",["+-","xA4",0,"dxF4"]],["yE4",["+-","yA4",0,"dyF4"]],["yC4t",["sin","th","a4"]],["xC4t",["cos","th","a4"]],["yC4",["+-","yF4","yC4t",0]],["xC4",["+-","xF4",0,"xC4t"]],["yB4",["+-","yE4","yC4t",0]],["xB4",["+-","xE4",0,"xC4t"]],["swAng3",["+-","bA4",0,"bD3"]],["aA5",["+-",6600000,0,"ha"]],["aD5",["+-",6600000,"ha",0]],["ta51",["cos","rw","aA5"]],["ta52",["sin","rh","aA5"]],["bA5",["at2","ta51","ta52"]],["td51",["cos","rw","aD5"]],["td52",["sin","rh","aD5"]],["bD5",["at2","td51","td52"]],["xD5",["+-","w",0,"xA4"]],["xC5",["+-","w",0,"xB4"]],["xB5",["+-","w",0,"xC4"]],["swAng4",["+-","bA5",0,"bD4"]],["aD6",["+-",9000000,"ha",0]],["td61",["cos","rw","aD6"]],["td62",["sin","rh","aD6"]],["bD6",["at2","td61","td62"]],["xD6",["+-","w",0,"xA3"]],["xC6",["+-","w",0,"xB3"]],["xB6",["+-","w",0,"xC3"]],["aD7",["+-",11400000,"ha",0]],["td71",["cos","rw","aD7"]],["td72",["sin","rh","aD7"]],["bD7",["at2","td71","td72"]],["xD7",["+-","w",0,"xA2"]],["xC7",["+-","w",0,"xB2"]],["xB7",["+-","w",0,"xC2"]],["aD8",["+-",13800000,"ha",0]],["td81",["cos","rw","aD8"]],["td82",["sin","rh","aD8"]],["bD8",["at2","td81","td82"]],["xA8",["+-","w",0,"xD1"]],["xD8",["+-","w",0,"xA1"]],["xC8",["+-","w",0,"xB1"]],["xB8",["+-","w",0,"xC1"]],["aA9",["+-","3cd4",0,"ha"]],["aD9",["+-","3cd4","ha",0]],["td91",["cos","rw","aD9"]],["td92",["sin","rh","aD9"]],["bD9",["at2","td91","td92"]],["ctd9",["cos","rh","bD9"]],["std9",["sin","rw","bD9"]],["md9",["mod","ctd9","std9",0]],["nd9",["*/","rw","rh","md9"]],["dxd9",["cos","nd9","bD9"]],["dyd9",["sin","nd9","bD9"]],["xD9",["+-","hc","dxd9",0]],["yD9",["+-","vc","dyd9",0]],["ta91",["cos","rw","aA9"]],["ta92",["sin","rh","aA9"]],["bA9",["at2","ta91","ta92"]],["xA9",["+-","hc",0,"dxd9"]],["xF9",["+-","xD9",0,"lFD"]],["xE9",["+-","xA9","lFD",0]],["yC9",["+-","yD9",0,"th"]],["swAng5",["+-","bA9",0,"bD8"]],["xCxn1",["+/","xB1","xC1",2]],["yCxn1",["+/","yB1","yC1",2]],["xCxn2",["+/","xB2","xC2",2]],["yCxn2",["+/","yB2","yC2",2]],["xCxn3",["+/","xB3","xC3",2]],["yCxn3",["+/","yB3","yC3",2]],["xCxn4",["+/","xB4","xC4",2]],["yCxn4",["+/","yB4","yC4",2]],["xCxn5",["+/","r",0,"xCxn4"]],["xCxn6",["+/","r",0,"xCxn3"]],["xCxn7",["+/","r",0,"xCxn2"]],["xCxn8",["+/","r",0,"xCxn1"]]],"rect":["xA8","yD1","xD1","yD3"],"paths":[{"d":[["M","xA1","yA1"],["L","xB1","yB1"],["L","xC1","yC1"],["L","xD1","yD1"],["A","rw","rh","bD1","swAng1"],["L","xB2","yB2"],["L","xC2","yC2"],["L","xD2","yD2"],["A","rw","rh","bD2","swAng2"],["L","xB3","yB3"],["L","xC3","yC3"],["L","xD3","yD3"],["A","rw","rh","bD3","swAng3"],["L","xB4","yB4"],["L","xC4","yC4"],["L","xD4","yD4"],["A","rw","rh","bD4","swAng4"],["L","xB5","yC4"],["L","xC5","yB4"],["L","xD5","yA4"],["A","rw","rh","bD5","swAng3"],["L","xB6","yC3"],["L","xC6","yB3"],["L","xD6","yA3"],["A","rw","rh","bD6","swAng2"],["L","xB7","yC2"],["L","xC7","yB2"],["L","xD7","yA2"],["A","rw","rh","bD7","swAng1"],["L","xB8","yC1"],["L","xC8","yB1"],["L","xD8","yA1"],["A","rw","rh","bD8","swAng5"],["L","xE9","yC9"],["L","xF9","yC9"],["L","xD9","yD9"],["A","rw","rh","bD9","swAng5"],["Z"]]}]},"halfFrame":{"av":[["adj1",["val",33333]],["adj2",["val",33333]]],"gd":[["maxAdj2",["*/",100000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["x1",["*/","ss","a2",100000]],["g1",["*/","h","x1","w"]],["g2",["+-","h",0,"g1"]],["maxAdj1",["*/",100000,"g2","ss"]],["a1",["pin",0,"adj1","maxAdj1"]],["y1",["*/","ss","a1",100000]],["dx2",["*/","y1","w","h"]],["x2",["+-","r",0,"dx2"]],["dy2",["*/","x1","h","w"]],["y2",["+-","b",0,"dy2"]],["cx1",["*/","x1",1,2]],["cy1",["+/","y2","b",2]],["cx2",["+/","x2","r",2]],["cy2",["*/","y1",1,2]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","x2","y1"],["L","x1","y1"],["L","x1","y2"],["L","l","b"],["Z"]]}]},"heart":{"gd":[["dx1",["*/","w",49,48]],["dx2",["*/","w",10,48]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["+-","t",0,"hd3"]],["il",["*/","w",1,6]],["ir",["*/","w",5,6]],["ib",["*/","h",2,3]]],"rect":["il","hd4","ir","ib"],"paths":[{"d":[["M","hc","hd4"],["C","x3","y1","x4","hd4","hc","b"],["C","x1","hd4","x2","y1","hc","hd4"],["Z"]]}]},"heptagon":{"av":[["hf",["val",102572]],["vf",["val",105210]]],"gd":[["swd2",["*/","wd2","hf",100000]],["shd2",["*/","hd2","vf",100000]],["svc",["*/","vc","vf",100000]],["dx1",["*/","swd2",97493,100000]],["dx2",["*/","swd2",78183,100000]],["dx3",["*/","swd2",43388,100000]],["dy1",["*/","shd2",62349,100000]],["dy2",["*/","shd2",22252,100000]],["dy3",["*/","shd2",90097,100000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc","dx3",0]],["x5",["+-","hc","dx2",0]],["x6",["+-","hc","dx1",0]],["y1",["+-","svc",0,"dy1"]],["y2",["+-","svc","dy2",0]],["y3",["+-","svc","dy3",0]],["ib",["+-","b",0,"y1"]]],"rect":["x2","y1","x5","ib"],"paths":[{"d":[["M","x1","y2"],["L","x2","y1"],["L","hc","t"],["L","x5","y1"],["L","x6","y2"],["L","x4","y3"],["L","x3","y3"],["Z"]]}]},"hexagon":{"av":[["adj",["val",25000]],["vf",["val",115470]]],"gd":[["maxAdj",["*/",50000,"w","ss"]],["a",["pin",0,"adj","maxAdj"]],["shd2",["*/","hd2","vf",100000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["dy1",["sin","shd2",3600000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["q1",["*/","maxAdj",-1,2]],["q2",["+-","a","q1",0]],["q3",["?:","q2",4,2]],["q4",["?:","q2",3,2]],["q5",["?:","q2","q1",0]],["q6",["+/","a","q5","q1"]],["q7",["*/","q6","q4",-1]],["q8",["+-","q3","q7",0]],["il",["*/","w","q8",24]],["it",["*/","h","q8",24]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"it"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["L","x1","y1"],["L","x2","y1"],["L","r","vc"],["L","x2","y2"],["L","x1","y2"],["Z"]]}]},"homePlate":{"av":[["adj",["val",50000]]],"gd":[["maxAdj",["*/",100000,"w","ss"]],["a",["pin",0,"adj","maxAdj"]],["dx1",["*/","ss","a",100000]],["x1",["+-","r",0,"dx1"]],["ir",["+/","x1","r",2]],["x2",["*/","x1",1,2]]],"rect":["l","t","ir","b"],"paths":[{"d":[["M","l","t"],["L","x1","t"],["L","r","vc"],["L","x1","b"],["L","l","b"],["Z"]]}]},"horizontalScroll":{"av":[["adj",["val",12500]]],"gd":[["a",["pin",0,"adj",25000]],["ch",["*/","ss","a",100000]],["ch2",["*/","ch",1,2]],["ch4",["*/","ch",1,4]],["y3",["+-","ch","ch2",0]],["y4",["+-","ch","ch",0]],["y6",["+-","b",0,"ch"]],["y7",["+-","b",0,"ch2"]],["y5",["+-","y6",0,"ch2"]],["x3",["+-","r",0,"ch"]],["x4",["+-","r",0,"ch2"]]],"rect":["ch","ch","x4","y6"],"paths":[{"stroke":false,"d":[["M","r","ch2"],["A","ch2","ch2",0,"cd4"],["L","x4","ch2"],["A","ch4","ch4",0,"cd2"],["L","x3","ch"],["L","ch2","ch"],["A","ch2","ch2","3cd4",-5400000],["L","l","y7"],["A","ch2","ch2","cd2",-10800000],["L","ch","y6"],["L","x4","y6"],["A","ch2","ch2","cd4",-5400000],["Z"],["M","ch2","y4"],["A","ch2","ch2","cd4",-5400000],["A","ch4","ch4",0,-10800000],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","ch2","y4"],["A","ch2","ch2","cd4",-5400000],["A","ch4","ch4",0,-10800000],["Z"],["M","x4","ch"],["A","ch2","ch2","cd4",-16200000],["A","ch4","ch4","cd2",-10800000],["Z"]]},{"fill":"none","d":[["M","l","y3"],["A","ch2","ch2","cd2","cd4"],["L","x3","ch"],["L","x3","ch2"],["A","ch2","ch2","cd2","cd2"],["L","r","y5"],["A","ch2","ch2",0,"cd4"],["L","ch","y6"],["L","ch","y7"],["A","ch2","ch2",0,"cd2"],["Z"],["M","x3","ch"],["L","x4","ch"],["A","ch2","ch2","cd4",-5400000],["M","x4","ch"],["L","x4","ch2"],["A","ch4","ch4",0,"cd2"],["M","ch2","y4"],["L","ch2","y3"],["A","ch4","ch4","cd2","cd2"],["A","ch2","ch2",0,"cd2"],["M","ch","y3"],["L","ch","y6"]]}]},"irregularSeal1":{"gd":[["x5",["*/","w",4627,21600]],["x12",["*/","w",8485,21600]],["x21",["*/","w",16702,21600]],["x24",["*/","w",14522,21600]],["y3",["*/","h",6320,21600]],["y6",["*/","h",8615,21600]],["y9",["*/","h",13937,21600]],["y18",["*/","h",13290,21600]]],"rect":["x5","y3","x21","y9"],"paths":[{"w":21600,"h":21600,"d":[["M",10800,5800],["L",14522,0],["L",14155,5325],["L",18380,4457],["L",16702,7315],["L",21097,8137],["L",17607,10475],["L",21600,13290],["L",16837,12942],["L",18145,18095],["L",14020,14457],["L",13247,19737],["L",10532,14935],["L",8485,21600],["L",7715,15627],["L",4762,17617],["L",5667,13937],["L",135,14587],["L",3722,11775],["L",0,8615],["L",4627,7617],["L",370,2295],["L",7312,6320],["L",8352,2295],["Z"]]}]},"irregularSeal2":{"gd":[["x2",["*/","w",9722,21600]],["x5",["*/","w",5372,21600]],["x16",["*/","w",11612,21600]],["x19",["*/","w",14640,21600]],["y2",["*/","h",1887,21600]],["y3",["*/","h",6382,21600]],["y8",["*/","h",12877,21600]],["y14",["*/","h",19712,21600]],["y16",["*/","h",18842,21600]],["y17",["*/","h",15935,21600]],["y24",["*/","h",6645,21600]]],"rect":["x5","y3","x19","y17"],"paths":[{"w":21600,"h":21600,"d":[["M",11462,4342],["L",14790,0],["L",14525,5777],["L",18007,3172],["L",16380,6532],["L",21600,6645],["L",16985,9402],["L",18270,11290],["L",16380,12310],["L",18877,15632],["L",14640,14350],["L",14942,17370],["L",12180,15935],["L",11612,18842],["L",9872,17370],["L",8700,19712],["L",7527,18125],["L",4917,21600],["L",4805,18240],["L",1285,17825],["L",3330,15370],["L",0,12877],["L",3935,11592],["L",1172,8270],["L",5372,7817],["L",4502,3625],["L",8550,6382],["L",9722,1887],["Z"]]}]},"leftArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",100000,"w","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["dx2",["*/","ss","a2",100000]],["x2",["+-","l","dx2",0]],["dy1",["*/","h","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["dx1",["*/","y1","dx2","hd2"]],["x1",["+-","x2",0,"dx1"]]],"rect":["x1","y1","r","y2"],"paths":[{"d":[["M","l","vc"],["L","x2","t"],["L","x2","y1"],["L","r","y1"],["L","r","y2"],["L","x2","y2"],["L","x2","b"],["Z"]]}]},"leftArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",64977]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",100000,"w","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","w"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dy1",["*/","ss","a2",100000]],["dy2",["*/","ss","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y4",["+-","vc","dy1",0]],["x1",["*/","ss","a3",100000]],["dx2",["*/","w","a4",100000]],["x2",["+-","r",0,"dx2"]],["x3",["+/","x2","r",2]]],"rect":["x2","t","r","b"],"paths":[{"d":[["M","l","vc"],["L","x1","y1"],["L","x1","y2"],["L","x2","y2"],["L","x2","t"],["L","r","t"],["L","r","b"],["L","x2","b"],["L","x2","y3"],["L","x1","y3"],["L","x1","y4"],["Z"]]}]},"leftBrace":{"av":[["adj1",["val",8333]],["adj2",["val",50000]]],"gd":[["a2",["pin",0,"adj2",100000]],["q1",["+-",100000,0,"a2"]],["q2",["min","q1","a2"]],["q3",["*/","q2",1,2]],["maxAdj1",["*/","q3","h","ss"]],["a1",["pin",0,"adj1","maxAdj1"]],["y1",["*/","ss","a1",100000]],["y3",["*/","h","a2",100000]],["y4",["+-","y3","y1",0]],["dx1",["cos","wd2",2700000]],["dy1",["sin","y1",2700000]],["il",["+-","r",0,"dx1"]],["it",["+-","y1",0,"dy1"]],["ib",["+-","b","dy1","y1"]]],"rect":["il","it","r","ib"],"paths":[{"stroke":false,"d":[["M","r","b"],["A","wd2","y1","cd4","cd4"],["L","hc","y4"],["A","wd2","y1",0,-5400000],["A","wd2","y1","cd4",-5400000],["L","hc","y1"],["A","wd2","y1","cd2","cd4"],["Z"]]},{"fill":"none","d":[["M","r","b"],["A","wd2","y1","cd4","cd4"],["L","hc","y4"],["A","wd2","y1",0,-5400000],["A","wd2","y1","cd4",-5400000],["L","hc","y1"],["A","wd2","y1","cd2","cd4"]]}]},"leftBracket":{"av":[["adj",["val",8333]]],"gd":[["maxAdj",["*/",50000,"h","ss"]],["a",["pin",0,"adj","maxAdj"]],["y1",["*/","ss","a",100000]],["y2",["+-","b",0,"y1"]],["dx1",["cos","w",2700000]],["dy1",["sin","y1",2700000]],["il",["+-","r",0,"dx1"]],["it",["+-","y1",0,"dy1"]],["ib",["+-","b","dy1","y1"]]],"rect":["il","it","r","ib"],"paths":[{"stroke":false,"d":[["M","r","b"],["A","w","y1","cd4","cd4"],["L","l","y1"],["A","w","y1","cd2","cd4"],["Z"]]},{"fill":"none","d":[["M","r","b"],["A","w","y1","cd4","cd4"],["L","l","y1"],["A","w","y1","cd2","cd4"]]}]},"leftCircularArrow":{"av":[["adj1",["val",12500]],["adj2",["val",-1142319]],["adj3",["val",1142319]],["adj4",["val",10800000]],["adj5",["val",12500]]],"gd":[["a5",["pin",0,"adj5",25000]],["maxAdj1",["*/","a5",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["enAng",["pin",1,"adj3",21599999]],["stAng",["pin",0,"adj4",21599999]],["th",["*/","ss","a1",100000]],["thh",["*/","ss","a5",100000]],["th2",["*/","th",1,2]],["rw1",["+-","wd2","th2","thh"]],["rh1",["+-","hd2","th2","thh"]],["rw2",["+-","rw1",0,"th"]],["rh2",["+-","rh1",0,"th"]],["rw3",["+-","rw2","th2",0]],["rh3",["+-","rh2","th2",0]],["wtH",["sin","rw3","enAng"]],["htH",["cos","rh3","enAng"]],["dxH",["cat2","rw3","htH","wtH"]],["dyH",["sat2","rh3","htH","wtH"]],["xH",["+-","hc","dxH",0]],["yH",["+-","vc","dyH",0]],["rI",["min","rw2","rh2"]],["u1",["*/","dxH","dxH",1]],["u2",["*/","dyH","dyH",1]],["u3",["*/","rI","rI",1]],["u4",["+-","u1",0,"u3"]],["u5",["+-","u2",0,"u3"]],["u6",["*/","u4","u5","u1"]],["u7",["*/","u6",1,"u2"]],["u8",["+-",1,0,"u7"]],["u9",["sqrt","u8"]],["u10",["*/","u4",1,"dxH"]],["u11",["*/","u10",1,"dyH"]],["u12",["+/",1,"u9","u11"]],["u13",["at2",1,"u12"]],["u14",["+-","u13",21600000,0]],["u15",["?:","u13","u13","u14"]],["u16",["+-","u15",0,"enAng"]],["u17",["+-","u16",21600000,0]],["u18",["?:","u16","u16","u17"]],["u19",["+-","u18",0,"cd2"]],["u20",["+-","u18",0,21600000]],["u21",["?:","u19","u20","u18"]],["u22",["abs","u21"]],["minAng",["*/","u22",-1,1]],["u23",["abs","adj2"]],["a2",["*/","u23",-1,1]],["aAng",["pin","minAng","a2",0]],["ptAng",["+-","enAng","aAng",0]],["wtA",["sin","rw3","ptAng"]],["htA",["cos","rh3","ptAng"]],["dxA",["cat2","rw3","htA","wtA"]],["dyA",["sat2","rh3","htA","wtA"]],["xA",["+-","hc","dxA",0]],["yA",["+-","vc","dyA",0]],["wtE",["sin","rw1","stAng"]],["htE",["cos","rh1","stAng"]],["dxE",["cat2","rw1","htE","wtE"]],["dyE",["sat2","rh1","htE","wtE"]],["xE",["+-","hc","dxE",0]],["yE",["+-","vc","dyE",0]],["wtD",["sin","rw2","stAng"]],["htD",["cos","rh2","stAng"]],["dxD",["cat2","rw2","htD","wtD"]],["dyD",["sat2","rh2","htD","wtD"]],["xD",["+-","hc","dxD",0]],["yD",["+-","vc","dyD",0]],["dxG",["cos","thh","ptAng"]],["dyG",["sin","thh","ptAng"]],["xG",["+-","xH","dxG",0]],["yG",["+-","yH","dyG",0]],["dxB",["cos","thh","ptAng"]],["dyB",["sin","thh","ptAng"]],["xB",["+-","xH",0,"dxB",0]],["yB",["+-","yH",0,"dyB",0]],["sx1",["+-","xB",0,"hc"]],["sy1",["+-","yB",0,"vc"]],["sx2",["+-","xG",0,"hc"]],["sy2",["+-","yG",0,"vc"]],["rO",["min","rw1","rh1"]],["x1O",["*/","sx1","rO","rw1"]],["y1O",["*/","sy1","rO","rh1"]],["x2O",["*/","sx2","rO","rw1"]],["y2O",["*/","sy2","rO","rh1"]],["dxO",["+-","x2O",0,"x1O"]],["dyO",["+-","y2O",0,"y1O"]],["dO",["mod","dxO","dyO",0]],["q1",["*/","x1O","y2O",1]],["q2",["*/","x2O","y1O",1]],["DO",["+-","q1",0,"q2"]],["q3",["*/","rO","rO",1]],["q4",["*/","dO","dO",1]],["q5",["*/","q3","q4",1]],["q6",["*/","DO","DO",1]],["q7",["+-","q5",0,"q6"]],["q8",["max","q7",0]],["sdelO",["sqrt","q8"]],["ndyO",["*/","dyO",-1,1]],["sdyO",["?:","ndyO",-1,1]],["q9",["*/","sdyO","dxO",1]],["q10",["*/","q9","sdelO",1]],["q11",["*/","DO","dyO",1]],["dxF1",["+/","q11","q10","q4"]],["q12",["+-","q11",0,"q10"]],["dxF2",["*/","q12",1,"q4"]],["adyO",["abs","dyO"]],["q13",["*/","adyO","sdelO",1]],["q14",["*/","DO","dxO",-1]],["dyF1",["+/","q14","q13","q4"]],["q15",["+-","q14",0,"q13"]],["dyF2",["*/","q15",1,"q4"]],["q16",["+-","x2O",0,"dxF1"]],["q17",["+-","x2O",0,"dxF2"]],["q18",["+-","y2O",0,"dyF1"]],["q19",["+-","y2O",0,"dyF2"]],["q20",["mod","q16","q18",0]],["q21",["mod","q17","q19",0]],["q22",["+-","q21",0,"q20"]],["dxF",["?:","q22","dxF1","dxF2"]],["dyF",["?:","q22","dyF1","dyF2"]],["sdxF",["*/","dxF","rw1","rO"]],["sdyF",["*/","dyF","rh1","rO"]],["xF",["+-","hc","sdxF",0]],["yF",["+-","vc","sdyF",0]],["x1I",["*/","sx1","rI","rw2"]],["y1I",["*/","sy1","rI","rh2"]],["x2I",["*/","sx2","rI","rw2"]],["y2I",["*/","sy2","rI","rh2"]],["dxI",["+-","x2I",0,"x1I"]],["dyI",["+-","y2I",0,"y1I"]],["dI",["mod","dxI","dyI",0]],["v1",["*/","x1I","y2I",1]],["v2",["*/","x2I","y1I",1]],["DI",["+-","v1",0,"v2"]],["v3",["*/","rI","rI",1]],["v4",["*/","dI","dI",1]],["v5",["*/","v3","v4",1]],["v6",["*/","DI","DI",1]],["v7",["+-","v5",0,"v6"]],["v8",["max","v7",0]],["sdelI",["sqrt","v8"]],["v9",["*/","sdyO","dxI",1]],["v10",["*/","v9","sdelI",1]],["v11",["*/","DI","dyI",1]],["dxC1",["+/","v11","v10","v4"]],["v12",["+-","v11",0,"v10"]],["dxC2",["*/","v12",1,"v4"]],["adyI",["abs","dyI"]],["v13",["*/","adyI","sdelI",1]],["v14",["*/","DI","dxI",-1]],["dyC1",["+/","v14","v13","v4"]],["v15",["+-","v14",0,"v13"]],["dyC2",["*/","v15",1,"v4"]],["v16",["+-","x1I",0,"dxC1"]],["v17",["+-","x1I",0,"dxC2"]],["v18",["+-","y1I",0,"dyC1"]],["v19",["+-","y1I",0,"dyC2"]],["v20",["mod","v16","v18",0]],["v21",["mod","v17","v19",0]],["v22",["+-","v21",0,"v20"]],["dxC",["?:","v22","dxC1","dxC2"]],["dyC",["?:","v22","dyC1","dyC2"]],["sdxC",["*/","dxC","rw2","rI"]],["sdyC",["*/","dyC","rh2","rI"]],["xC",["+-","hc","sdxC",0]],["yC",["+-","vc","sdyC",0]],["ist0",["at2","sdxC","sdyC"]],["ist1",["+-","ist0",21600000,0]],["istAng0",["?:","ist0","ist0","ist1"]],["isw1",["+-","stAng",0,"istAng0"]],["isw2",["+-","isw1",21600000,0]],["iswAng0",["?:","isw1","isw1","isw2"]],["istAng",["+-","istAng0","iswAng0",0]],["iswAng",["+-",0,0,"iswAng0"]],["p1",["+-","xF",0,"xC"]],["p2",["+-","yF",0,"yC"]],["p3",["mod","p1","p2",0]],["p4",["*/","p3",1,2]],["p5",["+-","p4",0,"thh"]],["xGp",["?:","p5","xF","xG"]],["yGp",["?:","p5","yF","yG"]],["xBp",["?:","p5","xC","xB"]],["yBp",["?:","p5","yC","yB"]],["en0",["at2","sdxF","sdyF"]],["en1",["+-","en0",21600000,0]],["en2",["?:","en0","en0","en1"]],["sw0",["+-","en2",0,"stAng"]],["sw1",["+-","sw0",0,21600000]],["swAng",["?:","sw0","sw1","sw0"]],["stAng0",["+-","stAng","swAng",0]],["swAng0",["+-",0,0,"swAng"]],["wtI",["sin","rw3","stAng"]],["htI",["cos","rh3","stAng"]],["dxI",["cat2","rw3","htI","wtI"]],["dyI",["sat2","rh3","htI","wtI"]],["xI",["+-","hc","dxI",0]],["yI",["+-","vc","dyI",0]],["aI",["+-","stAng","cd4",0]],["aA",["+-","ptAng",0,"cd4"]],["aB",["+-","ptAng","cd2",0]],["idx",["cos","rw1",2700000]],["idy",["sin","rh1",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","xE","yE"],["L","xD","yD"],["A","rw2","rh2","istAng","iswAng"],["L","xBp","yBp"],["L","xA","yA"],["L","xGp","yGp"],["L","xF","yF"],["A","rw1","rh1","stAng0","swAng0"],["Z"]]}]},"leftRightArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["x2",["*/","ss","a2",100000]],["x3",["+-","r",0,"x2"]],["dy",["*/","h","a1",200000]],["y1",["+-","vc",0,"dy"]],["y2",["+-","vc","dy",0]],["dx1",["*/","y1","x2","hd2"]],["x1",["+-","x2",0,"dx1"]],["x4",["+-","x3","dx1",0]]],"rect":["x1","y1","x4","y2"],"paths":[{"d":[["M","l","vc"],["L","x2","t"],["L","x2","y1"],["L","x3","y1"],["L","x3","t"],["L","r","vc"],["L","x3","b"],["L","x3","y2"],["L","x2","y2"],["L","x2","b"],["Z"]]}]},"leftRightArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",48123]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",50000,"w","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","wd2"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dy1",["*/","ss","a2",100000]],["dy2",["*/","ss","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y4",["+-","vc","dy1",0]],["x1",["*/","ss","a3",100000]],["x4",["+-","r",0,"x1"]],["dx2",["*/","w","a4",200000]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]]],"rect":["x2","t","x3","b"],"paths":[{"d":[["M","l","vc"],["L","x1","y1"],["L","x1","y2"],["L","x2","y2"],["L","x2","t"],["L","x3","t"],["L","x3","y2"],["L","x4","y2"],["L","x4","y1"],["L","r","vc"],["L","x4","y4"],["L","x4","y3"],["L","x3","y3"],["L","x3","b"],["L","x2","b"],["L","x2","y3"],["L","x1","y3"],["L","x1","y4"],["Z"]]}]},"leftRightCircularArrow":{"av":[["adj1",["val",12500]],["adj2",["val",1142319]],["adj3",["val",20457681]],["adj4",["val",11942319]],["adj5",["val",12500]]],"gd":[["a5",["pin",0,"adj5",25000]],["maxAdj1",["*/","a5",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["enAng",["pin",1,"adj3",21599999]],["stAng",["pin",0,"adj4",21599999]],["th",["*/","ss","a1",100000]],["thh",["*/","ss","a5",100000]],["th2",["*/","th",1,2]],["rw1",["+-","wd2","th2","thh"]],["rh1",["+-","hd2","th2","thh"]],["rw2",["+-","rw1",0,"th"]],["rh2",["+-","rh1",0,"th"]],["rw3",["+-","rw2","th2",0]],["rh3",["+-","rh2","th2",0]],["wtH",["sin","rw3","enAng"]],["htH",["cos","rh3","enAng"]],["dxH",["cat2","rw3","htH","wtH"]],["dyH",["sat2","rh3","htH","wtH"]],["xH",["+-","hc","dxH",0]],["yH",["+-","vc","dyH",0]],["rI",["min","rw2","rh2"]],["u1",["*/","dxH","dxH",1]],["u2",["*/","dyH","dyH",1]],["u3",["*/","rI","rI",1]],["u4",["+-","u1",0,"u3"]],["u5",["+-","u2",0,"u3"]],["u6",["*/","u4","u5","u1"]],["u7",["*/","u6",1,"u2"]],["u8",["+-",1,0,"u7"]],["u9",["sqrt","u8"]],["u10",["*/","u4",1,"dxH"]],["u11",["*/","u10",1,"dyH"]],["u12",["+/",1,"u9","u11"]],["u13",["at2",1,"u12"]],["u14",["+-","u13",21600000,0]],["u15",["?:","u13","u13","u14"]],["u16",["+-","u15",0,"enAng"]],["u17",["+-","u16",21600000,0]],["u18",["?:","u16","u16","u17"]],["u19",["+-","u18",0,"cd2"]],["u20",["+-","u18",0,21600000]],["u21",["?:","u19","u20","u18"]],["maxAng",["abs","u21"]],["aAng",["pin",0,"adj2","maxAng"]],["ptAng",["+-","enAng","aAng",0]],["wtA",["sin","rw3","ptAng"]],["htA",["cos","rh3","ptAng"]],["dxA",["cat2","rw3","htA","wtA"]],["dyA",["sat2","rh3","htA","wtA"]],["xA",["+-","hc","dxA",0]],["yA",["+-","vc","dyA",0]],["dxG",["cos","thh","ptAng"]],["dyG",["sin","thh","ptAng"]],["xG",["+-","xH","dxG",0]],["yG",["+-","yH","dyG",0]],["dxB",["cos","thh","ptAng"]],["dyB",["sin","thh","ptAng"]],["xB",["+-","xH",0,"dxB",0]],["yB",["+-","yH",0,"dyB",0]],["sx1",["+-","xB",0,"hc"]],["sy1",["+-","yB",0,"vc"]],["sx2",["+-","xG",0,"hc"]],["sy2",["+-","yG",0,"vc"]],["rO",["min","rw1","rh1"]],["x1O",["*/","sx1","rO","rw1"]],["y1O",["*/","sy1","rO","rh1"]],["x2O",["*/","sx2","rO","rw1"]],["y2O",["*/","sy2","rO","rh1"]],["dxO",["+-","x2O",0,"x1O"]],["dyO",["+-","y2O",0,"y1O"]],["dO",["mod","dxO","dyO",0]],["q1",["*/","x1O","y2O",1]],["q2",["*/","x2O","y1O",1]],["DO",["+-","q1",0,"q2"]],["q3",["*/","rO","rO",1]],["q4",["*/","dO","dO",1]],["q5",["*/","q3","q4",1]],["q6",["*/","DO","DO",1]],["q7",["+-","q5",0,"q6"]],["q8",["max","q7",0]],["sdelO",["sqrt","q8"]],["ndyO",["*/","dyO",-1,1]],["sdyO",["?:","ndyO",-1,1]],["q9",["*/","sdyO","dxO",1]],["q10",["*/","q9","sdelO",1]],["q11",["*/","DO","dyO",1]],["dxF1",["+/","q11","q10","q4"]],["q12",["+-","q11",0,"q10"]],["dxF2",["*/","q12",1,"q4"]],["adyO",["abs","dyO"]],["q13",["*/","adyO","sdelO",1]],["q14",["*/","DO","dxO",-1]],["dyF1",["+/","q14","q13","q4"]],["q15",["+-","q14",0,"q13"]],["dyF2",["*/","q15",1,"q4"]],["q16",["+-","x2O",0,"dxF1"]],["q17",["+-","x2O",0,"dxF2"]],["q18",["+-","y2O",0,"dyF1"]],["q19",["+-","y2O",0,"dyF2"]],["q20",["mod","q16","q18",0]],["q21",["mod","q17","q19",0]],["q22",["+-","q21",0,"q20"]],["dxF",["?:","q22","dxF1","dxF2"]],["dyF",["?:","q22","dyF1","dyF2"]],["sdxF",["*/","dxF","rw1","rO"]],["sdyF",["*/","dyF","rh1","rO"]],["xF",["+-","hc","sdxF",0]],["yF",["+-","vc","sdyF",0]],["x1I",["*/","sx1","rI","rw2"]],["y1I",["*/","sy1","rI","rh2"]],["x2I",["*/","sx2","rI","rw2"]],["y2I",["*/","sy2","rI","rh2"]],["dxI",["+-","x2I",0,"x1I"]],["dyI",["+-","y2I",0,"y1I"]],["dI",["mod","dxI","dyI",0]],["v1",["*/","x1I","y2I",1]],["v2",["*/","x2I","y1I",1]],["DI",["+-","v1",0,"v2"]],["v3",["*/","rI","rI",1]],["v4",["*/","dI","dI",1]],["v5",["*/","v3","v4",1]],["v6",["*/","DI","DI",1]],["v7",["+-","v5",0,"v6"]],["v8",["max","v7",0]],["sdelI",["sqrt","v8"]],["v9",["*/","sdyO","dxI",1]],["v10",["*/","v9","sdelI",1]],["v11",["*/","DI","dyI",1]],["dxC1",["+/","v11","v10","v4"]],["v12",["+-","v11",0,"v10"]],["dxC2",["*/","v12",1,"v4"]],["adyI",["abs","dyI"]],["v13",["*/","adyI","sdelI",1]],["v14",["*/","DI","dxI",-1]],["dyC1",["+/","v14","v13","v4"]],["v15",["+-","v14",0,"v13"]],["dyC2",["*/","v15",1,"v4"]],["v16",["+-","x1I",0,"dxC1"]],["v17",["+-","x1I",0,"dxC2"]],["v18",["+-","y1I",0,"dyC1"]],["v19",["+-","y1I",0,"dyC2"]],["v20",["mod","v16","v18",0]],["v21",["mod","v17","v19",0]],["v22",["+-","v21",0,"v20"]],["dxC",["?:","v22","dxC1","dxC2"]],["dyC",["?:","v22","dyC1","dyC2"]],["sdxC",["*/","dxC","rw2","rI"]],["sdyC",["*/","dyC","rh2","rI"]],["xC",["+-","hc","sdxC",0]],["yC",["+-","vc","sdyC",0]],["wtI",["sin","rw3","stAng"]],["htI",["cos","rh3","stAng"]],["dxI",["cat2","rw3","htI","wtI"]],["dyI",["sat2","rh3","htI","wtI"]],["xI",["+-","hc","dxI",0]],["yI",["+-","vc","dyI",0]],["lptAng",["+-","stAng",0,"aAng"]],["wtL",["sin","rw3","lptAng"]],["htL",["cos","rh3","lptAng"]],["dxL",["cat2","rw3","htL","wtL"]],["dyL",["sat2","rh3","htL","wtL"]],["xL",["+-","hc","dxL",0]],["yL",["+-","vc","dyL",0]],["dxK",["cos","thh","lptAng"]],["dyK",["sin","thh","lptAng"]],["xK",["+-","xI","dxK",0]],["yK",["+-","yI","dyK",0]],["dxJ",["cos","thh","lptAng"]],["dyJ",["sin","thh","lptAng"]],["xJ",["+-","xI",0,"dxJ",0]],["yJ",["+-","yI",0,"dyJ",0]],["p1",["+-","xF",0,"xC"]],["p2",["+-","yF",0,"yC"]],["p3",["mod","p1","p2",0]],["p4",["*/","p3",1,2]],["p5",["+-","p4",0,"thh"]],["xGp",["?:","p5","xF","xG"]],["yGp",["?:","p5","yF","yG"]],["xBp",["?:","p5","xC","xB"]],["yBp",["?:","p5","yC","yB"]],["en0",["at2","sdxF","sdyF"]],["en1",["+-","en0",21600000,0]],["en2",["?:","en0","en0","en1"]],["od0",["+-","en2",0,"enAng"]],["od1",["+-","od0",21600000,0]],["od2",["?:","od0","od0","od1"]],["st0",["+-","stAng",0,"od2"]],["st1",["+-","st0",21600000,0]],["st2",["?:","st0","st0","st1"]],["sw0",["+-","en2",0,"st2"]],["sw1",["+-","sw0",21600000,0]],["swAng",["?:","sw0","sw0","sw1"]],["ist0",["at2","sdxC","sdyC"]],["ist1",["+-","ist0",21600000,0]],["istAng",["?:","ist0","ist0","ist1"]],["id0",["+-","istAng",0,"enAng"]],["id1",["+-","id0",0,21600000]],["id2",["?:","id0","id1","id0"]],["ien0",["+-","stAng",0,"id2"]],["ien1",["+-","ien0",0,21600000]],["ien2",["?:","ien1","ien1","ien0"]],["isw1",["+-","ien2",0,"istAng"]],["isw2",["+-","isw1",0,21600000]],["iswAng",["?:","isw1","isw2","isw1"]],["wtE",["sin","rw1","st2"]],["htE",["cos","rh1","st2"]],["dxE",["cat2","rw1","htE","wtE"]],["dyE",["sat2","rh1","htE","wtE"]],["xE",["+-","hc","dxE",0]],["yE",["+-","vc","dyE",0]],["wtD",["sin","rw2","ien2"]],["htD",["cos","rh2","ien2"]],["dxD",["cat2","rw2","htD","wtD"]],["dyD",["sat2","rh2","htD","wtD"]],["xD",["+-","hc","dxD",0]],["yD",["+-","vc","dyD",0]],["xKp",["?:","p5","xE","xK"]],["yKp",["?:","p5","yE","yK"]],["xJp",["?:","p5","xD","xJ"]],["yJp",["?:","p5","yD","yJ"]],["aL",["+-","lptAng",0,"cd4"]],["aA",["+-","ptAng","cd4",0]],["aB",["+-","ptAng","cd2",0]],["aJ",["+-","lptAng","cd2",0]],["idx",["cos","rw1",2700000]],["idy",["sin","rh1",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","xL","yL"],["L","xKp","yKp"],["L","xE","yE"],["A","rw1","rh1","st2","swAng"],["L","xGp","yGp"],["L","xA","yA"],["L","xBp","yBp"],["L","xC","yC"],["A","rw2","rh2","istAng","iswAng"],["L","xJp","yJp"],["Z"]]}]},"leftRightRibbon":{"av":[["adj1",["val",50000]],["adj2",["val",50000]],["adj3",["val",16667]]],"gd":[["a3",["pin",0,"adj3",33333]],["maxAdj1",["+-",100000,0,"a3"]],["a1",["pin",0,"adj1","maxAdj1"]],["w1",["+-","wd2",0,"wd32"]],["maxAdj2",["*/",100000,"w1","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["x1",["*/","ss","a2",100000]],["x4",["+-","r",0,"x1"]],["dy1",["*/","h","a1",200000]],["dy2",["*/","h","a3",-200000]],["ly1",["+-","vc","dy2","dy1"]],["ry4",["+-","vc","dy1","dy2"]],["ly2",["+-","ly1","dy1",0]],["ry3",["+-","b",0,"ly2"]],["ly4",["*/","ly2",2,1]],["ry1",["+-","b",0,"ly4"]],["ly3",["+-","ly4",0,"ly1"]],["ry2",["+-","b",0,"ly3"]],["hR",["*/","a3","ss",400000]],["x2",["+-","hc",0,"wd32"]],["x3",["+-","hc","wd32",0]],["y1",["+-","ly1","hR",0]],["y2",["+-","ry2",0,"hR"]]],"rect":["x1","ly1","x4","ry4"],"paths":[{"stroke":false,"d":[["M","l","ly2"],["L","x1","t"],["L","x1","ly1"],["L","hc","ly1"],["A","wd32","hR","3cd4","cd2"],["A","wd32","hR","3cd4",-10800000],["L","x4","ry2"],["L","x4","ry1"],["L","r","ry3"],["L","x4","b"],["L","x4","ry4"],["L","hc","ry4"],["A","wd32","hR","cd4","cd4"],["L","x2","ly3"],["L","x1","ly3"],["L","x1","ly4"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x3","y1"],["A","wd32","hR",0,"cd4"],["A","wd32","hR","3cd4",-10800000],["L","x3","ry2"],["Z"]]},{"fill":"none","d":[["M","l","ly2"],["L","x1","t"],["L","x1","ly1"],["L","hc","ly1"],["A","wd32","hR","3cd4","cd2"],["A","wd32","hR","3cd4",-10800000],["L","x4","ry2"],["L","x4","ry1"],["L","r","ry3"],["L","x4","b"],["L","x4","ry4"],["L","hc","ry4"],["A","wd32","hR","cd4","cd4"],["L","x2","ly3"],["L","x1","ly3"],["L","x1","ly4"],["Z"],["M","x3","y1"],["L","x3","ry2"],["M","x2","y2"],["L","x2","ly3"]]}]},"leftRightUpArrow":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]]],"gd":[["a2",["pin",0,"adj2",50000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["q1",["+-",100000,0,"maxAdj1"]],["maxAdj3",["*/","q1",1,2]],["a3",["pin",0,"adj3","maxAdj3"]],["x1",["*/","ss","a3",100000]],["dx2",["*/","ss","a2",100000]],["x2",["+-","hc",0,"dx2"]],["x5",["+-","hc","dx2",0]],["dx3",["*/","ss","a1",200000]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc","dx3",0]],["x6",["+-","r",0,"x1"]],["dy2",["*/","ss","a2",50000]],["y2",["+-","b",0,"dy2"]],["y4",["+-","b",0,"dx2"]],["y3",["+-","y4",0,"dx3"]],["y5",["+-","y4","dx3",0]],["il",["*/","dx3","x1","dx2"]],["ir",["+-","r",0,"il"]]],"rect":["il","y3","ir","y5"],"paths":[{"d":[["M","l","y4"],["L","x1","y2"],["L","x1","y3"],["L","x3","y3"],["L","x3","x1"],["L","x2","x1"],["L","hc","t"],["L","x5","x1"],["L","x4","x1"],["L","x4","y3"],["L","x6","y3"],["L","x6","y2"],["L","r","y4"],["L","x6","b"],["L","x6","y5"],["L","x1","y5"],["L","x1","b"],["Z"]]}]},"leftUpArrow":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]]],"gd":[["a2",["pin",0,"adj2",50000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["+-",100000,0,"maxAdj1"]],["a3",["pin",0,"adj3","maxAdj3"]],["x1",["*/","ss","a3",100000]],["dx2",["*/","ss","a2",50000]],["x2",["+-","r",0,"dx2"]],["y2",["+-","b",0,"dx2"]],["dx4",["*/","ss","a2",100000]],["x4",["+-","r",0,"dx4"]],["y4",["+-","b",0,"dx4"]],["dx3",["*/","ss","a1",200000]],["x3",["+-","x4",0,"dx3"]],["x5",["+-","x4","dx3",0]],["y3",["+-","y4",0,"dx3"]],["y5",["+-","y4","dx3",0]],["il",["*/","dx3","x1","dx4"]],["cx1",["+/","x1","x5",2]],["cy1",["+/","x1","y5",2]]],"rect":["il","y3","x4","y5"],"paths":[{"d":[["M","l","y4"],["L","x1","y2"],["L","x1","y3"],["L","x3","y3"],["L","x3","x1"],["L","x2","x1"],["L","x4","t"],["L","r","x1"],["L","x5","x1"],["L","x5","y5"],["L","x1","y5"],["L","x1","b"],["Z"]]}]},"lightningBolt":{"gd":[["x1",["*/","w",5022,21600]],["x3",["*/","w",8472,21600]],["x4",["*/","w",8757,21600]],["x5",["*/","w",10012,21600]],["x8",["*/","w",12860,21600]],["x9",["*/","w",13917,21600]],["x11",["*/","w",16577,21600]],["y1",["*/","h",3890,21600]],["y2",["*/","h",6080,21600]],["y4",["*/","h",7437,21600]],["y6",["*/","h",9705,21600]],["y7",["*/","h",12007,21600]],["y10",["*/","h",14277,21600]],["y11",["*/","h",14915,21600]]],"rect":["x4","y4","x9","y10"],"paths":[{"w":21600,"h":21600,"d":[["M",8472,0],["L",12860,6080],["L",11050,6797],["L",16577,12007],["L",14767,12877],["L",21600,21600],["L",10012,14915],["L",12222,13987],["L",5022,9705],["L",7602,8382],["L",0,3890],["Z"]]}]},"line":{"paths":[{"d":[["M","l","t"],["L","r","b"]]}]},"lineInv":{"paths":[{"d":[["M","l","b"],["L","r","t"]]}]},"mathDivide":{"av":[["adj1",["val",23520]],["adj2",["val",5880]],["adj3",["val",11760]]],"gd":[["a1",["pin",1000,"adj1",36745]],["ma1",["+-",0,0,"a1"]],["ma3h",["+/",73490,"ma1",4]],["ma3w",["*/",36745,"w","h"]],["maxAdj3",["min","ma3h","ma3w"]],["a3",["pin",1000,"adj3","maxAdj3"]],["m4a3",["*/",-4,"a3",1]],["maxAdj2",["+-",73490,"m4a3","a1"]],["a2",["pin",0,"adj2","maxAdj2"]],["dy1",["*/","h","a1",200000]],["yg",["*/","h","a2",100000]],["rad",["*/","h","a3",100000]],["dx1",["*/","w",73490,200000]],["y3",["+-","vc",0,"dy1"]],["y4",["+-","vc","dy1",0]],["a",["+-","yg","rad",0]],["y2",["+-","y3",0,"a"]],["y1",["+-","y2",0,"rad"]],["y5",["+-","b",0,"y1"]],["x1",["+-","hc",0,"dx1"]],["x3",["+-","hc","dx1",0]],["x2",["+-","hc",0,"rad"]]],"rect":["x1","y3","x3","y4"],"paths":[{"d":[["M","hc","y1"],["A","rad","rad","3cd4",21600000],["Z"],["M","hc","y5"],["A","rad","rad","cd4",21600000],["Z"],["M","x1","y3"],["L","x3","y3"],["L","x3","y4"],["L","x1","y4"],["Z"]]}]},"mathEqual":{"av":[["adj1",["val",23520]],["adj2",["val",11760]]],"gd":[["a1",["pin",0,"adj1",36745]],["2a1",["*/","a1",2,1]],["mAdj2",["+-",100000,0,"2a1"]],["a2",["pin",0,"adj2","mAdj2"]],["dy1",["*/","h","a1",100000]],["dy2",["*/","h","a2",200000]],["dx1",["*/","w",73490,200000]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y1",["+-","y2",0,"dy1"]],["y4",["+-","y3","dy1",0]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["yC1",["+/","y1","y2",2]],["yC2",["+/","y3","y4",2]]],"rect":["x1","y1","x2","y4"],"paths":[{"d":[["M","x1","y1"],["L","x2","y1"],["L","x2","y2"],["L","x1","y2"],["Z"],["M","x1","y3"],["L","x2","y3"],["L","x2","y4"],["L","x1","y4"],["Z"]]}]},"mathMinus":{"av":[["adj1",["val",23520]]],"gd":[["a1",["pin",0,"adj1",100000]],["dy1",["*/","h","a1",200000]],["dx1",["*/","w",73490,200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]]],"rect":["x1","y1","x2","y2"],"paths":[{"d":[["M","x1","y1"],["L","x2","y1"],["L","x2","y2"],["L","x1","y2"],["Z"]]}]},"mathMultiply":{"av":[["adj1",["val",23520]]],"gd":[["a1",["pin",0,"adj1",51965]],["th",["*/","ss","a1",100000]],["a",["at2","w","h"]],["sa",["sin",1,"a"]],["ca",["cos",1,"a"]],["ta",["tan",1,"a"]],["dl",["mod","w","h",0]],["rw",["*/","dl",51965,100000]],["lM",["+-","dl",0,"rw"]],["xM",["*/","ca","lM",2]],["yM",["*/","sa","lM",2]],["dxAM",["*/","sa","th",2]],["dyAM",["*/","ca","th",2]],["xA",["+-","xM",0,"dxAM"]],["yA",["+-","yM","dyAM",0]],["xB",["+-","xM","dxAM",0]],["yB",["+-","yM",0,"dyAM"]],["xBC",["+-","hc",0,"xB"]],["yBC",["*/","xBC","ta",1]],["yC",["+-","yBC","yB",0]],["xD",["+-","r",0,"xB"]],["xE",["+-","r",0,"xA"]],["yFE",["+-","vc",0,"yA"]],["xFE",["*/","yFE",1,"ta"]],["xF",["+-","xE",0,"xFE"]],["xL",["+-","xA","xFE",0]],["yG",["+-","b",0,"yA"]],["yH",["+-","b",0,"yB"]],["yI",["+-","b",0,"yC"]],["xC2",["+-","r",0,"xM"]],["yC3",["+-","b",0,"yM"]]],"rect":["xA","yB","xE","yH"],"paths":[{"d":[["M","xA","yA"],["L","xB","yB"],["L","hc","yC"],["L","xD","yB"],["L","xE","yA"],["L","xF","vc"],["L","xE","yG"],["L","xD","yH"],["L","hc","yI"],["L","xB","yH"],["L","xA","yG"],["L","xL","vc"],["Z"]]}]},"mathNotEqual":{"av":[["adj1",["val",23520]],["adj2",["val",6600000]],["adj3",["val",11760]]],"gd":[["a1",["pin",0,"adj1",50000]],["crAng",["pin",4200000,"adj2",6600000]],["2a1",["*/","a1",2,1]],["maxAdj3",["+-",100000,0,"2a1"]],["a3",["pin",0,"adj3","maxAdj3"]],["dy1",["*/","h","a1",100000]],["dy2",["*/","h","a3",200000]],["dx1",["*/","w",73490,200000]],["x1",["+-","hc",0,"dx1"]],["x8",["+-","hc","dx1",0]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y1",["+-","y2",0,"dy1"]],["y4",["+-","y3","dy1",0]],["cadj2",["+-","crAng",0,"cd4"]],["xadj2",["tan","hd2","cadj2"]],["len",["mod","xadj2","hd2",0]],["bhw",["*/","len","dy1","hd2"]],["bhw2",["*/","bhw",1,2]],["x7",["+-","hc","xadj2","bhw2"]],["dx67",["*/","xadj2","y1","hd2"]],["x6",["+-","x7",0,"dx67"]],["dx57",["*/","xadj2","y2","hd2"]],["x5",["+-","x7",0,"dx57"]],["dx47",["*/","xadj2","y3","hd2"]],["x4",["+-","x7",0,"dx47"]],["dx37",["*/","xadj2","y4","hd2"]],["x3",["+-","x7",0,"dx37"]],["dx27",["*/","xadj2",2,1]],["x2",["+-","x7",0,"dx27"]],["rx7",["+-","x7","bhw",0]],["rx6",["+-","x6","bhw",0]],["rx5",["+-","x5","bhw",0]],["rx4",["+-","x4","bhw",0]],["rx3",["+-","x3","bhw",0]],["rx2",["+-","x2","bhw",0]],["dx7",["*/","dy1","hd2","len"]],["rxt",["+-","x7","dx7",0]],["lxt",["+-","rx7",0,"dx7"]],["rx",["?:","cadj2","rxt","rx7"]],["lx",["?:","cadj2","x7","lxt"]],["dy3",["*/","dy1","xadj2","len"]],["dy4",["+-",0,0,"dy3"]],["ry",["?:","cadj2","dy3","t"]],["ly",["?:","cadj2","t","dy4"]],["dlx",["+-","w",0,"rx"]],["drx",["+-","w",0,"lx"]],["dly",["+-","h",0,"ry"]],["dry",["+-","h",0,"ly"]],["xC1",["+/","rx","lx",2]],["xC2",["+/","drx","dlx",2]],["yC1",["+/","ry","ly",2]],["yC2",["+/","y1","y2",2]],["yC3",["+/","y3","y4",2]],["yC4",["+/","dry","dly",2]]],"rect":["x1","y1","x8","y4"],"paths":[{"d":[["M","x1","y1"],["L","x6","y1"],["L","lx","ly"],["L","rx","ry"],["L","rx6","y1"],["L","x8","y1"],["L","x8","y2"],["L","rx5","y2"],["L","rx4","y3"],["L","x8","y3"],["L","x8","y4"],["L","rx3","y4"],["L","drx","dry"],["L","dlx","dly"],["L","x3","y4"],["L","x1","y4"],["L","x1","y3"],["L","x4","y3"],["L","x5","y2"],["L","x1","y2"],["Z"]]}]},"mathPlus":{"av":[["adj1",["val",23520]]],"gd":[["a1",["pin",0,"adj1",73490]],["dx1",["*/","w",73490,200000]],["dy1",["*/","h",73490,200000]],["dx2",["*/","ss","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dx2"]],["y3",["+-","vc","dx2",0]],["y4",["+-","vc","dy1",0]]],"rect":["x1","y2","x4","y3"],"paths":[{"d":[["M","x1","y2"],["L","x2","y2"],["L","x2","y1"],["L","x3","y1"],["L","x3","y2"],["L","x4","y2"],["L","x4","y3"],["L","x3","y3"],["L","x3","y4"],["L","x2","y4"],["L","x2","y3"],["L","x1","y3"],["Z"]]}]},"moon":{"av":[["adj",["val",50000]]],"gd":[["a",["pin",0,"adj",87500]],["g0",["*/","ss","a",100000]],["g0w",["*/","g0","w","ss"]],["g1",["+-","ss",0,"g0"]],["g2",["*/","g0","g0","g1"]],["g3",["*/","ss","ss","g1"]],["g4",["*/","g3",2,1]],["g5",["+-","g4",0,"g2"]],["g6",["+-","g5",0,"g0"]],["g6w",["*/","g6","w","ss"]],["g7",["*/","g5",1,2]],["g8",["+-","g7",0,"g0"]],["dy1",["*/","g8","hd2","ss"]],["g10h",["+-","vc",0,"dy1"]],["g11h",["+-","vc","dy1",0]],["g12",["*/","g0",9598,32768]],["g12w",["*/","g12","w","ss"]],["g13",["+-","ss",0,"g12"]],["q1",["*/","ss","ss",1]],["q2",["*/","g13","g13",1]],["q3",["+-","q1",0,"q2"]],["q4",["sqrt","q3"]],["dy4",["*/","q4","hd2","ss"]],["g15h",["+-","vc",0,"dy4"]],["g16h",["+-","vc","dy4",0]],["g17w",["+-","g6w",0,"g0w"]],["g18w",["*/","g17w",1,2]],["dx2p",["+-","g0w","g18w","w"]],["dx2",["*/","dx2p",-1,1]],["dy2",["*/","hd2",-1,1]],["stAng1",["at2","dx2","dy2"]],["enAngp1",["at2","dx2","hd2"]],["enAng1",["+-","enAngp1",0,21600000]],["swAng1",["+-","enAng1",0,"stAng1"]]],"rect":["g12w","g15h","g0w","g16h"],"paths":[{"d":[["M","r","b"],["A","w","hd2","cd4","cd2"],["A","g18w","dy1","stAng1","swAng1"],["Z"]]}]},"nonIsoscelesTrapezoid":{"av":[["adj1",["val",25000]],["adj2",["val",25000]]],"gd":[["maxAdj",["*/",50000,"w","ss"]],["a1",["pin",0,"adj1","maxAdj"]],["a2",["pin",0,"adj2","maxAdj"]],["x1",["*/","ss","a1",200000]],["x2",["*/","ss","a1",100000]],["dx3",["*/","ss","a2",100000]],["x3",["+-","r",0,"dx3"]],["x4",["+/","r","x3",2]],["il",["*/","wd3","a1","maxAdj"]],["adjm",["max","a1","a2"]],["it",["*/","hd3","adjm","maxAdj"]],["irt",["*/","wd3","a2","maxAdj"]],["ir",["+-","r",0,"irt"]]],"rect":["il","it","ir","b"],"paths":[{"d":[["M","l","b"],["L","x2","t"],["L","x3","t"],["L","r","b"],["Z"]]}]},"noSmoking":{"av":[["adj",["val",18750]]],"gd":[["a",["pin",0,"adj",50000]],["dr",["*/","ss","a",100000]],["iwd2",["+-","wd2",0,"dr"]],["ihd2",["+-","hd2",0,"dr"]],["ang",["at2","w","h"]],["ct",["cos","ihd2","ang"]],["st",["sin","iwd2","ang"]],["m",["mod","ct","st",0]],["n",["*/","iwd2","ihd2","m"]],["drd2",["*/","dr",1,2]],["dang",["at2","n","drd2"]],["dang2",["*/","dang",2,1]],["swAng",["+-",-10800000,"dang2",0]],["t3",["at2","w","h"]],["stAng1",["+-","t3",0,"dang"]],["stAng2",["+-","stAng1",0,"cd2"]],["ct1",["cos","ihd2","stAng1"]],["st1",["sin","iwd2","stAng1"]],["m1",["mod","ct1","st1",0]],["n1",["*/","iwd2","ihd2","m1"]],["dx1",["cos","n1","stAng1"]],["dy1",["sin","n1","stAng1"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["x2",["+-","hc",0,"dx1"]],["y2",["+-","vc",0,"dy1"]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["A","wd2","hd2","3cd4","cd4"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"],["M","x1","y1"],["A","iwd2","ihd2","stAng1","swAng"],["Z"],["M","x2","y2"],["A","iwd2","ihd2","stAng2","swAng"],["Z"]]}]},"notchedRightArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",100000,"w","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["dx2",["*/","ss","a2",100000]],["x2",["+-","r",0,"dx2"]],["dy1",["*/","h","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["x1",["*/","dy1","dx2","hd2"]],["x3",["+-","r",0,"x1"]]],"rect":["x1","y1","x3","y2"],"paths":[{"d":[["M","l","y1"],["L","x2","y1"],["L","x2","t"],["L","r","vc"],["L","x2","b"],["L","x2","y2"],["L","l","y2"],["L","x1","vc"],["Z"]]}]},"octagon":{"av":[["adj",["val",29289]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]],["il",["*/","x1",1,2]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","l","x1"],["L","x1","t"],["L","x2","t"],["L","r","x1"],["L","r","y2"],["L","x2","b"],["L","x1","b"],["L","l","y2"],["Z"]]}]},"parallelogram":{"av":[["adj",["val",25000]]],"gd":[["maxAdj",["*/",100000,"w","ss"]],["a",["pin",0,"adj","maxAdj"]],["x1",["*/","ss","a",200000]],["x2",["*/","ss","a",100000]],["x6",["+-","r",0,"x1"]],["x5",["+-","r",0,"x2"]],["x3",["*/","x5",1,2]],["x4",["+-","r",0,"x3"]],["il",["*/","wd2","a","maxAdj"]],["q1",["*/",5,"a","maxAdj"]],["q2",["+/",1,"q1",12]],["il",["*/","q2","w",1]],["it",["*/","q2","h",1]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"it"]],["q3",["*/","h","hc","x2"]],["y1",["pin",0,"q3","h"]],["y2",["+-","b",0,"y1"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","b"],["L","x2","t"],["L","r","t"],["L","x5","b"],["Z"]]}]},"pentagon":{"av":[["hf",["val",105146]],["vf",["val",110557]]],"gd":[["swd2",["*/","wd2","hf",100000]],["shd2",["*/","hd2","vf",100000]],["svc",["*/","vc","vf",100000]],["dx1",["cos","swd2",1080000]],["dx2",["cos","swd2",18360000]],["dy1",["sin","shd2",1080000]],["dy2",["sin","shd2",18360000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["+-","svc",0,"dy1"]],["y2",["+-","svc",0,"dy2"]],["it",["*/","y1","dx2","dx1"]]],"rect":["x2","it","x3","y2"],"paths":[{"d":[["M","x1","y1"],["L","hc","t"],["L","x4","y1"],["L","x3","y2"],["L","x2","y2"],["Z"]]}]},"pie":{"av":[["adj1",["val",0]],["adj2",["val",16200000]]],"gd":[["stAng",["pin",0,"adj1",21599999]],["enAng",["pin",0,"adj2",21599999]],["sw1",["+-","enAng",0,"stAng"]],["sw2",["+-","sw1",21600000,0]],["swAng",["?:","sw1","sw1","sw2"]],["wt1",["sin","wd2","stAng"]],["ht1",["cos","hd2","stAng"]],["dx1",["cat2","wd2","ht1","wt1"]],["dy1",["sat2","hd2","ht1","wt1"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["wt2",["sin","wd2","enAng"]],["ht2",["cos","hd2","enAng"]],["dx2",["cat2","wd2","ht2","wt2"]],["dy2",["sat2","hd2","ht2","wt2"]],["x2",["+-","hc","dx2",0]],["y2",["+-","vc","dy2",0]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","ir","it","ib"],"paths":[{"d":[["M","x1","y1"],["A","wd2","hd2","stAng","swAng"],["L","hc","vc"],["Z"]]}]},"pieWedge":{"gd":[["g1",["cos","w",13500000]],["g2",["sin","h",13500000]],["x1",["+-","r","g1",0]],["y1",["+-","b","g2",0]]],"rect":["x1","y1","r","b"],"paths":[{"d":[["M","l","b"],["A","w","h","cd2","cd4"],["L","r","b"],["Z"]]}]},"plaque":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]],["il",["*/","x1",70711,100000]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","l","x1"],["A","x1","x1","cd4",-5400000],["L","x2","t"],["A","x1","x1","cd2",-5400000],["L","r","y2"],["A","x1","x1","3cd4",-5400000],["L","x1","b"],["A","x1","x1",0,-5400000],["Z"]]}]},"plaqueTabs":{"gd":[["md",["mod","w","h",0]],["dx",["*/",1,"md",20]],["y1",["+-",0,"b","dx"]],["x1",["+-",0,"r","dx"]]],"rect":["dx","dx","x1","y1"],"paths":[{"d":[["M","l","t"],["L","dx","t"],["A","dx","dx",0,"cd4"],["Z"]]},{"d":[["M","l","y1"],["A","dx","dx","3cd4","cd4"],["L","l","b"],["Z"]]},{"d":[["M","r","t"],["L","r","dx"],["A","dx","dx","cd4","cd4"],["Z"]]},{"d":[["M","x1","b"],["A","dx","dx","cd2","cd4"],["L","r","b"],["Z"]]}]},"plus":{"av":[["adj",["val",25000]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]],["d",["+-","w",0,"h"]],["il",["?:","d","l","x1"]],["ir",["?:","d","r","x2"]],["it",["?:","d","x1","t"]],["ib",["?:","d","y2","b"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","x1"],["L","x1","x1"],["L","x1","t"],["L","x2","t"],["L","x2","x1"],["L","r","x1"],["L","r","y2"],["L","x2","y2"],["L","x2","b"],["L","x1","b"],["L","x1","y2"],["L","l","y2"],["Z"]]}]},"quadArrow":{"av":[["adj1",["val",22500]],["adj2",["val",22500]],["adj3",["val",22500]]],"gd":[["a2",["pin",0,"adj2",50000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["q1",["+-",100000,0,"maxAdj1"]],["maxAdj3",["*/","q1",1,2]],["a3",["pin",0,"adj3","maxAdj3"]],["x1",["*/","ss","a3",100000]],["dx2",["*/","ss","a2",100000]],["x2",["+-","hc",0,"dx2"]],["x5",["+-","hc","dx2",0]],["dx3",["*/","ss","a1",200000]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc","dx3",0]],["x6",["+-","r",0,"x1"]],["y2",["+-","vc",0,"dx2"]],["y5",["+-","vc","dx2",0]],["y3",["+-","vc",0,"dx3"]],["y4",["+-","vc","dx3",0]],["y6",["+-","b",0,"x1"]],["il",["*/","dx3","x1","dx2"]],["ir",["+-","r",0,"il"]]],"rect":["il","y3","ir","y4"],"paths":[{"d":[["M","l","vc"],["L","x1","y2"],["L","x1","y3"],["L","x3","y3"],["L","x3","x1"],["L","x2","x1"],["L","hc","t"],["L","x5","x1"],["L","x4","x1"],["L","x4","y3"],["L","x6","y3"],["L","x6","y2"],["L","r","vc"],["L","x6","y5"],["L","x6","y4"],["L","x4","y4"],["L","x4","y6"],["L","x5","y6"],["L","hc","b"],["L","x2","y6"],["L","x3","y6"],["L","x3","y4"],["L","x1","y4"],["L","x1","y5"],["Z"]]}]},"quadArrowCallout":{"av":[["adj1",["val",18515]],["adj2",["val",18515]],["adj3",["val",18515]],["adj4",["val",48123]]],"gd":[["a2",["pin",0,"adj2",50000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["+-",50000,0,"a2"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3",2,1]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin","a1","adj4","maxAdj4"]],["dx2",["*/","ss","a2",100000]],["dx3",["*/","ss","a1",200000]],["ah",["*/","ss","a3",100000]],["dx1",["*/","w","a4",200000]],["dy1",["*/","h","a4",200000]],["x8",["+-","r",0,"ah"]],["x2",["+-","hc",0,"dx1"]],["x7",["+-","hc","dx1",0]],["x3",["+-","hc",0,"dx2"]],["x6",["+-","hc","dx2",0]],["x4",["+-","hc",0,"dx3"]],["x5",["+-","hc","dx3",0]],["y8",["+-","b",0,"ah"]],["y2",["+-","vc",0,"dy1"]],["y7",["+-","vc","dy1",0]],["y3",["+-","vc",0,"dx2"]],["y6",["+-","vc","dx2",0]],["y4",["+-","vc",0,"dx3"]],["y5",["+-","vc","dx3",0]]],"rect":["x2","y2","x7","y7"],"paths":[{"d":[["M","l","vc"],["L","ah","y3"],["L","ah","y4"],["L","x2","y4"],["L","x2","y2"],["L","x4","y2"],["L","x4","ah"],["L","x3","ah"],["L","hc","t"],["L","x6","ah"],["L","x5","ah"],["L","x5","y2"],["L","x7","y2"],["L","x7","y4"],["L","x8","y4"],["L","x8","y3"],["L","r","vc"],["L","x8","y6"],["L","x8","y5"],["L","x7","y5"],["L","x7","y7"],["L","x5","y7"],["L","x5","y8"],["L","x6","y8"],["L","hc","b"],["L","x3","y8"],["L","x4","y8"],["L","x4","y7"],["L","x2","y7"],["L","x2","y5"],["L","ah","y5"],["L","ah","y6"],["Z"]]}]},"rect":{"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","r","t"],["L","r","b"],["L","l","b"],["Z"]]}]},"ribbon":{"av":[["adj1",["val",16667]],["adj2",["val",50000]]],"gd":[["a1",["pin",0,"adj1",33333]],["a2",["pin",25000,"adj2",75000]],["x10",["+-","r",0,"wd8"]],["dx2",["*/","w","a2",200000]],["x2",["+-","hc",0,"dx2"]],["x9",["+-","hc","dx2",0]],["x3",["+-","x2","wd32",0]],["x8",["+-","x9",0,"wd32"]],["x5",["+-","x2","wd8",0]],["x6",["+-","x9",0,"wd8"]],["x4",["+-","x5",0,"wd32"]],["x7",["+-","x6","wd32",0]],["y1",["*/","h","a1",200000]],["y2",["*/","h","a1",100000]],["y4",["+-","b",0,"y2"]],["y3",["*/","y4",1,2]],["hR",["*/","h","a1",400000]],["y5",["+-","b",0,"hR"]],["y6",["+-","y2",0,"hR"]]],"rect":["x2","y2","x9","b"],"paths":[{"stroke":false,"d":[["M","l","t"],["L","x4","t"],["A","wd32","hR","3cd4","cd2"],["L","x3","y1"],["A","wd32","hR","3cd4",-10800000],["L","x8","y2"],["A","wd32","hR","cd4",-10800000],["L","x7","y1"],["A","wd32","hR","cd4","cd2"],["L","r","t"],["L","x10","y3"],["L","r","y4"],["L","x9","y4"],["L","x9","y5"],["A","wd32","hR",0,"cd4"],["L","x3","b"],["A","wd32","hR","cd4","cd4"],["L","x2","y4"],["L","l","y4"],["L","wd8","y3"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x5","hR"],["A","wd32","hR",0,"cd4"],["L","x3","y1"],["A","wd32","hR","3cd4",-10800000],["L","x5","y2"],["Z"],["M","x6","hR"],["A","wd32","hR","cd2",-5400000],["L","x8","y1"],["A","wd32","hR","3cd4","cd2"],["L","x6","y2"],["Z"]]},{"fill":"none","d":[["M","l","t"],["L","x4","t"],["A","wd32","hR","3cd4","cd2"],["L","x3","y1"],["A","wd32","hR","3cd4",-10800000],["L","x8","y2"],["A","wd32","hR","cd4",-10800000],["L","x7","y1"],["A","wd32","hR","cd4","cd2"],["L","r","t"],["L","x10","y3"],["L","r","y4"],["L","x9","y4"],["L","x9","y5"],["A","wd32","hR",0,"cd4"],["L","x3","b"],["A","wd32","hR","cd4","cd4"],["L","x2","y4"],["L","l","y4"],["L","wd8","y3"],["Z"],["M","x5","hR"],["L","x5","y2"],["M","x6","y2"],["L","x6","hR"],["M","x2","y4"],["L","x2","y6"],["M","x9","y6"],["L","x9","y4"]]}]},"ribbon2":{"av":[["adj1",["val",16667]],["adj2",["val",50000]]],"gd":[["a1",["pin",0,"adj1",33333]],["a2",["pin",25000,"adj2",75000]],["x10",["+-","r",0,"wd8"]],["dx2",["*/","w","a2",200000]],["x2",["+-","hc",0,"dx2"]],["x9",["+-","hc","dx2",0]],["x3",["+-","x2","wd32",0]],["x8",["+-","x9",0,"wd32"]],["x5",["+-","x2","wd8",0]],["x6",["+-","x9",0,"wd8"]],["x4",["+-","x5",0,"wd32"]],["x7",["+-","x6","wd32",0]],["dy1",["*/","h","a1",200000]],["y1",["+-","b",0,"dy1"]],["dy2",["*/","h","a1",100000]],["y2",["+-","b",0,"dy2"]],["y4",["+-","t","dy2",0]],["y3",["+/","y4","b",2]],["hR",["*/","h","a1",400000]],["y6",["+-","b",0,"hR"]],["y7",["+-","y1",0,"hR"]]],"rect":["x2","t","x9","y2"],"paths":[{"stroke":false,"d":[["M","l","b"],["L","x4","b"],["A","wd32","hR","cd4",-10800000],["L","x3","y1"],["A","wd32","hR","cd4","cd2"],["L","x8","y2"],["A","wd32","hR","3cd4","cd2"],["L","x7","y1"],["A","wd32","hR","3cd4",-10800000],["L","r","b"],["L","x10","y3"],["L","r","y4"],["L","x9","y4"],["L","x9","hR"],["A","wd32","hR",0,-5400000],["L","x3","t"],["A","wd32","hR","3cd4",-5400000],["L","x2","y4"],["L","l","y4"],["L","wd8","y3"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x5","y6"],["A","wd32","hR",0,-5400000],["L","x3","y1"],["A","wd32","hR","cd4","cd2"],["L","x5","y2"],["Z"],["M","x6","y6"],["A","wd32","hR","cd2","cd4"],["L","x8","y1"],["A","wd32","hR","cd4",-10800000],["L","x6","y2"],["Z"]]},{"fill":"none","d":[["M","l","b"],["L","wd8","y3"],["L","l","y4"],["L","x2","y4"],["L","x2","hR"],["A","wd32","hR","cd2","cd4"],["L","x8","t"],["A","wd32","hR","3cd4","cd4"],["L","x9","y4"],["L","x9","y4"],["L","r","y4"],["L","x10","y3"],["L","r","b"],["L","x7","b"],["A","wd32","hR","cd4","cd2"],["L","x8","y1"],["A","wd32","hR","cd4",-10800000],["L","x3","y2"],["A","wd32","hR","3cd4",-10800000],["L","x4","y1"],["A","wd32","hR","3cd4","cd2"],["Z"],["M","x5","y2"],["L","x5","y6"],["M","x6","y6"],["L","x6","y2"],["M","x2","y7"],["L","x2","y4"],["M","x9","y4"],["L","x9","y7"]]}]},"rightArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",100000,"w","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["dx1",["*/","ss","a2",100000]],["x1",["+-","r",0,"dx1"]],["dy1",["*/","h","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["dx2",["*/","y1","dx1","hd2"]],["x2",["+-","x1","dx2",0]]],"rect":["l","y1","x2","y2"],"paths":[{"d":[["M","l","y1"],["L","x1","y1"],["L","x1","t"],["L","r","vc"],["L","x1","b"],["L","x1","y2"],["L","l","y2"],["Z"]]}]},"rightArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",64977]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",100000,"w","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","w"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dy1",["*/","ss","a2",100000]],["dy2",["*/","ss","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y4",["+-","vc","dy1",0]],["dx3",["*/","ss","a3",100000]],["x3",["+-","r",0,"dx3"]],["x2",["*/","w","a4",100000]],["x1",["*/","x2",1,2]]],"rect":["l","t","x2","b"],"paths":[{"d":[["M","l","t"],["L","x2","t"],["L","x2","y2"],["L","x3","y2"],["L","x3","y1"],["L","r","vc"],["L","x3","y4"],["L","x3","y3"],["L","x2","y3"],["L","x2","b"],["L","l","b"],["Z"]]}]},"rightBrace":{"av":[["adj1",["val",8333]],["adj2",["val",50000]]],"gd":[["a2",["pin",0,"adj2",100000]],["q1",["+-",100000,0,"a2"]],["q2",["min","q1","a2"]],["q3",["*/","q2",1,2]],["maxAdj1",["*/","q3","h","ss"]],["a1",["pin",0,"adj1","maxAdj1"]],["y1",["*/","ss","a1",100000]],["y3",["*/","h","a2",100000]],["y2",["+-","y3",0,"y1"]],["y4",["+-","b",0,"y1"]],["dx1",["cos","wd2",2700000]],["dy1",["sin","y1",2700000]],["ir",["+-","l","dx1",0]],["it",["+-","y1",0,"dy1"]],["ib",["+-","b","dy1","y1"]]],"rect":["l","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","t"],["A","wd2","y1","3cd4","cd4"],["L","hc","y2"],["A","wd2","y1","cd2",-5400000],["A","wd2","y1","3cd4",-5400000],["L","hc","y4"],["A","wd2","y1",0,"cd4"],["Z"]]},{"fill":"none","d":[["M","l","t"],["A","wd2","y1","3cd4","cd4"],["L","hc","y2"],["A","wd2","y1","cd2",-5400000],["A","wd2","y1","3cd4",-5400000],["L","hc","y4"],["A","wd2","y1",0,"cd4"]]}]},"rightBracket":{"av":[["adj",["val",8333]]],"gd":[["maxAdj",["*/",50000,"h","ss"]],["a",["pin",0,"adj","maxAdj"]],["y1",["*/","ss","a",100000]],["y2",["+-","b",0,"y1"]],["dx1",["cos","w",2700000]],["dy1",["sin","y1",2700000]],["ir",["+-","l","dx1",0]],["it",["+-","y1",0,"dy1"]],["ib",["+-","b","dy1","y1"]]],"rect":["l","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","t"],["A","w","y1","3cd4","cd4"],["L","r","y2"],["A","w","y1",0,"cd4"],["Z"]]},{"fill":"none","d":[["M","l","t"],["A","w","y1","3cd4","cd4"],["L","r","y2"],["A","w","y1",0,"cd4"]]}]},"round1Rect":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["*/","ss","a",100000]],["x1",["+-","r",0,"dx1"]],["idx",["*/","dx1",29289,100000]],["ir",["+-","r",0,"idx"]]],"rect":["l","t","ir","b"],"paths":[{"d":[["M","l","t"],["L","x1","t"],["A","dx1","dx1","3cd4","cd4"],["L","r","b"],["L","l","b"],["Z"]]}]},"round2DiagRect":{"av":[["adj1",["val",16667]],["adj2",["val",0]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["x1",["*/","ss","a1",100000]],["y1",["+-","b",0,"x1"]],["a",["*/","ss","a2",100000]],["x2",["+-","r",0,"a"]],["y2",["+-","b",0,"a"]],["dx1",["*/","x1",29289,100000]],["dx2",["*/","a",29289,100000]],["d",["+-","dx1",0,"dx2"]],["dx",["?:","d","dx1","dx2"]],["ir",["+-","r",0,"dx"]],["ib",["+-","b",0,"dx"]]],"rect":["dx","dx","ir","ib"],"paths":[{"d":[["M","x1","t"],["L","x2","t"],["A","a","a","3cd4","cd4"],["L","r","y1"],["A","x1","x1",0,"cd4"],["L","a","b"],["A","a","a","cd4","cd4"],["L","l","x1"],["A","x1","x1","cd2","cd4"],["Z"]]}]},"round2SameRect":{"av":[["adj1",["val",16667]],["adj2",["val",0]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["tx1",["*/","ss","a1",100000]],["tx2",["+-","r",0,"tx1"]],["bx1",["*/","ss","a2",100000]],["bx2",["+-","r",0,"bx1"]],["by1",["+-","b",0,"bx1"]],["d",["+-","tx1",0,"bx1"]],["tdx",["*/","tx1",29289,100000]],["bdx",["*/","bx1",29289,100000]],["il",["?:","d","tdx","bdx"]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"bdx"]]],"rect":["il","tdx","ir","ib"],"paths":[{"d":[["M","tx1","t"],["L","tx2","t"],["A","tx1","tx1","3cd4","cd4"],["L","r","by1"],["A","bx1","bx1",0,"cd4"],["L","bx1","b"],["A","bx1","bx1","cd4","cd4"],["L","l","tx1"],["A","tx1","tx1","cd2","cd4"],["Z"]]}]},"roundRect":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["x1",["*/","ss","a",100000]],["x2",["+-","r",0,"x1"]],["y2",["+-","b",0,"x1"]],["il",["*/","x1",29289,100000]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","l","x1"],["A","x1","x1","cd2","cd4"],["L","x2","t"],["A","x1","x1","3cd4","cd4"],["L","r","y2"],["A","x1","x1",0,"cd4"],["L","x1","b"],["A","x1","x1","cd4","cd4"],["Z"]]}]},"rtTriangle":{"gd":[["it",["*/","h",7,12]],["ir",["*/","w",7,12]],["ib",["*/","h",11,12]]],"rect":["wd12","it","ir","ib"],"paths":[{"d":[["M","l","b"],["L","l","t"],["L","r","b"],["Z"]]}]},"smileyFace":{"av":[["adj",["val",4653]]],"gd":[["a",["pin",-4653,"adj",4653]],["x1",["*/","w",4969,21699]],["x2",["*/","w",6215,21600]],["x3",["*/","w",13135,21600]],["x4",["*/","w",16640,21600]],["y1",["*/","h",7570,21600]],["y3",["*/","h",16515,21600]],["dy2",["*/","h","a",100000]],["y2",["+-","y3",0,"dy2"]],["y4",["+-","y3","dy2",0]],["dy3",["*/","h","a",50000]],["y5",["+-","y4","dy3",0]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]],["wR",["*/","w",1125,21600]],["hR",["*/","h",1125,21600]]],"rect":["il","it","ir","ib"],"paths":[{"stroke":false,"d":[["M","l","vc"],["A","wd2","hd2","cd2",21600000],["Z"]]},{"fill":"darkenLess","d":[["M","x2","y1"],["A","wR","hR","cd2",21600000],["M","x3","y1"],["A","wR","hR","cd2",21600000]]},{"fill":"none","d":[["M","x1","y2"],["Q","hc","y5","x4","y2"]]},{"fill":"none","d":[["M","l","vc"],["A","wd2","hd2","cd2",21600000],["Z"]]}]},"snip1Rect":{"av":[["adj",["val",16667]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["*/","ss","a",100000]],["x1",["+-","r",0,"dx1"]],["it",["*/","dx1",1,2]],["ir",["+/","x1","r",2]]],"rect":["l","it","ir","b"],"paths":[{"d":[["M","l","t"],["L","x1","t"],["L","r","dx1"],["L","r","b"],["L","l","b"],["Z"]]}]},"snip2DiagRect":{"av":[["adj1",["val",0]],["adj2",["val",16667]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["lx1",["*/","ss","a1",100000]],["lx2",["+-","r",0,"lx1"]],["ly1",["+-","b",0,"lx1"]],["rx1",["*/","ss","a2",100000]],["rx2",["+-","r",0,"rx1"]],["ry1",["+-","b",0,"rx1"]],["d",["+-","lx1",0,"rx1"]],["dx",["?:","d","lx1","rx1"]],["il",["*/","dx",1,2]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","lx1","t"],["L","rx2","t"],["L","r","rx1"],["L","r","ly1"],["L","lx2","b"],["L","rx1","b"],["L","l","ry1"],["L","l","lx1"],["Z"]]}]},"snip2SameRect":{"av":[["adj1",["val",16667]],["adj2",["val",0]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["tx1",["*/","ss","a1",100000]],["tx2",["+-","r",0,"tx1"]],["bx1",["*/","ss","a2",100000]],["bx2",["+-","r",0,"bx1"]],["by1",["+-","b",0,"bx1"]],["d",["+-","tx1",0,"bx1"]],["dx",["?:","d","tx1","bx1"]],["il",["*/","dx",1,2]],["ir",["+-","r",0,"il"]],["it",["*/","tx1",1,2]],["ib",["+/","by1","b",2]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","tx1","t"],["L","tx2","t"],["L","r","tx1"],["L","r","by1"],["L","bx2","b"],["L","bx1","b"],["L","l","by1"],["L","l","tx1"],["Z"]]}]},"snipRoundRect":{"av":[["adj1",["val",16667]],["adj2",["val",16667]]],"gd":[["a1",["pin",0,"adj1",50000]],["a2",["pin",0,"adj2",50000]],["x1",["*/","ss","a1",100000]],["dx2",["*/","ss","a2",100000]],["x2",["+-","r",0,"dx2"]],["il",["*/","x1",29289,100000]],["ir",["+/","x2","r",2]]],"rect":["il","il","ir","b"],"paths":[{"d":[["M","x1","t"],["L","x2","t"],["L","r","dx2"],["L","r","b"],["L","l","b"],["L","l","x1"],["A","x1","x1","cd2","cd4"],["Z"]]}]},"squareTabs":{"gd":[["md",["mod","w","h",0]],["dx",["*/",1,"md",20]],["y1",["+-",0,"b","dx"]],["x1",["+-",0,"r","dx"]]],"rect":["dx","dx","x1","y1"],"paths":[{"d":[["M","l","t"],["L","dx","t"],["L","dx","dx"],["L","l","dx"],["Z"]]},{"d":[["M","l","y1"],["L","dx","y1"],["L","dx","b"],["L","l","b"],["Z"]]},{"d":[["M","x1","t"],["L","r","t"],["L","r","dx"],["L","x1","dx"],["Z"]]},{"d":[["M","x1","y1"],["L","r","y1"],["L","r","b"],["L","x1","b"],["Z"]]}]},"star10":{"av":[["adj",["val",42533]],["hf",["val",105146]]],"gd":[["a",["pin",0,"adj",50000]],["swd2",["*/","wd2","hf",100000]],["dx1",["*/","swd2",95106,100000]],["dx2",["*/","swd2",58779,100000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["dy1",["*/","hd2",80902,100000]],["dy2",["*/","hd2",30902,100000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]],["y4",["+-","vc","dy1",0]],["iwd2",["*/","swd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["*/","iwd2",80902,100000]],["sdx2",["*/","iwd2",30902,100000]],["sdy1",["*/","ihd2",95106,100000]],["sdy2",["*/","ihd2",58779,100000]],["sx1",["+-","hc",0,"iwd2"]],["sx2",["+-","hc",0,"sdx1"]],["sx3",["+-","hc",0,"sdx2"]],["sx4",["+-","hc","sdx2",0]],["sx5",["+-","hc","sdx1",0]],["sx6",["+-","hc","iwd2",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc","sdy2",0]],["sy4",["+-","vc","sdy1",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["sx2","sy2","sx5","sy3"],"paths":[{"d":[["M","x1","y2"],["L","sx2","sy2"],["L","x2","y1"],["L","sx3","sy1"],["L","hc","t"],["L","sx4","sy1"],["L","x3","y1"],["L","sx5","sy2"],["L","x4","y2"],["L","sx6","vc"],["L","x4","y3"],["L","sx5","sy3"],["L","x3","y4"],["L","sx4","sy4"],["L","hc","b"],["L","sx3","sy4"],["L","x2","y4"],["L","sx2","sy3"],["L","x1","y3"],["L","sx1","vc"],["Z"]]}]},"star12":{"av":[["adj",["val",37500]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["cos","wd2",1800000]],["dy1",["sin","hd2",3600000]],["x1",["+-","hc",0,"dx1"]],["x3",["*/","w",3,4]],["x4",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["y3",["*/","h",3,4]],["y4",["+-","vc","dy1",0]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["cos","iwd2",900000]],["sdx2",["cos","iwd2",2700000]],["sdx3",["cos","iwd2",4500000]],["sdy1",["sin","ihd2",4500000]],["sdy2",["sin","ihd2",2700000]],["sdy3",["sin","ihd2",900000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc",0,"sdx3"]],["sx4",["+-","hc","sdx3",0]],["sx5",["+-","hc","sdx2",0]],["sx6",["+-","hc","sdx1",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc",0,"sdy3"]],["sy4",["+-","vc","sdy3",0]],["sy5",["+-","vc","sdy2",0]],["sy6",["+-","vc","sdy1",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["sx2","sy2","sx5","sy5"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy3"],["L","x1","hd4"],["L","sx2","sy2"],["L","wd4","y1"],["L","sx3","sy1"],["L","hc","t"],["L","sx4","sy1"],["L","x3","y1"],["L","sx5","sy2"],["L","x4","hd4"],["L","sx6","sy3"],["L","r","vc"],["L","sx6","sy4"],["L","x4","y3"],["L","sx5","sy5"],["L","x3","y4"],["L","sx4","sy6"],["L","hc","b"],["L","sx3","sy6"],["L","wd4","y4"],["L","sx2","sy5"],["L","x1","y3"],["L","sx1","sy4"],["Z"]]}]},"star16":{"av":[["adj",["val",37500]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["*/","wd2",92388,100000]],["dx2",["*/","wd2",70711,100000]],["dx3",["*/","wd2",38268,100000]],["dy1",["*/","hd2",92388,100000]],["dy2",["*/","hd2",70711,100000]],["dy3",["*/","hd2",38268,100000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc","dx3",0]],["x5",["+-","hc","dx2",0]],["x6",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc",0,"dy3"]],["y4",["+-","vc","dy3",0]],["y5",["+-","vc","dy2",0]],["y6",["+-","vc","dy1",0]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["*/","iwd2",98079,100000]],["sdx2",["*/","iwd2",83147,100000]],["sdx3",["*/","iwd2",55557,100000]],["sdx4",["*/","iwd2",19509,100000]],["sdy1",["*/","ihd2",98079,100000]],["sdy2",["*/","ihd2",83147,100000]],["sdy3",["*/","ihd2",55557,100000]],["sdy4",["*/","ihd2",19509,100000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc",0,"sdx3"]],["sx4",["+-","hc",0,"sdx4"]],["sx5",["+-","hc","sdx4",0]],["sx6",["+-","hc","sdx3",0]],["sx7",["+-","hc","sdx2",0]],["sx8",["+-","hc","sdx1",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc",0,"sdy3"]],["sy4",["+-","vc",0,"sdy4"]],["sy5",["+-","vc","sdy4",0]],["sy6",["+-","vc","sdy3",0]],["sy7",["+-","vc","sdy2",0]],["sy8",["+-","vc","sdy1",0]],["idx",["cos","iwd2",2700000]],["idy",["sin","ihd2",2700000]],["il",["+-","hc",0,"idx"]],["it",["+-","vc",0,"idy"]],["ir",["+-","hc","idx",0]],["ib",["+-","vc","idy",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy4"],["L","x1","y3"],["L","sx2","sy3"],["L","x2","y2"],["L","sx3","sy2"],["L","x3","y1"],["L","sx4","sy1"],["L","hc","t"],["L","sx5","sy1"],["L","x4","y1"],["L","sx6","sy2"],["L","x5","y2"],["L","sx7","sy3"],["L","x6","y3"],["L","sx8","sy4"],["L","r","vc"],["L","sx8","sy5"],["L","x6","y4"],["L","sx7","sy6"],["L","x5","y5"],["L","sx6","sy7"],["L","x4","y6"],["L","sx5","sy8"],["L","hc","b"],["L","sx4","sy8"],["L","x3","y6"],["L","sx3","sy7"],["L","x2","y5"],["L","sx2","sy6"],["L","x1","y4"],["L","sx1","sy5"],["Z"]]}]},"star24":{"av":[["adj",["val",37500]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["cos","wd2",900000]],["dx2",["cos","wd2",1800000]],["dx3",["cos","wd2",2700000]],["dx4",["val","wd4"]],["dx5",["cos","wd2",4500000]],["dy1",["sin","hd2",4500000]],["dy2",["sin","hd2",3600000]],["dy3",["sin","hd2",2700000]],["dy4",["val","hd4"]],["dy5",["sin","hd2",900000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc",0,"dx4"]],["x5",["+-","hc",0,"dx5"]],["x6",["+-","hc","dx5",0]],["x7",["+-","hc","dx4",0]],["x8",["+-","hc","dx3",0]],["x9",["+-","hc","dx2",0]],["x10",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc",0,"dy3"]],["y4",["+-","vc",0,"dy4"]],["y5",["+-","vc",0,"dy5"]],["y6",["+-","vc","dy5",0]],["y7",["+-","vc","dy4",0]],["y8",["+-","vc","dy3",0]],["y9",["+-","vc","dy2",0]],["y10",["+-","vc","dy1",0]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["*/","iwd2",99144,100000]],["sdx2",["*/","iwd2",92388,100000]],["sdx3",["*/","iwd2",79335,100000]],["sdx4",["*/","iwd2",60876,100000]],["sdx5",["*/","iwd2",38268,100000]],["sdx6",["*/","iwd2",13053,100000]],["sdy1",["*/","ihd2",99144,100000]],["sdy2",["*/","ihd2",92388,100000]],["sdy3",["*/","ihd2",79335,100000]],["sdy4",["*/","ihd2",60876,100000]],["sdy5",["*/","ihd2",38268,100000]],["sdy6",["*/","ihd2",13053,100000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc",0,"sdx3"]],["sx4",["+-","hc",0,"sdx4"]],["sx5",["+-","hc",0,"sdx5"]],["sx6",["+-","hc",0,"sdx6"]],["sx7",["+-","hc","sdx6",0]],["sx8",["+-","hc","sdx5",0]],["sx9",["+-","hc","sdx4",0]],["sx10",["+-","hc","sdx3",0]],["sx11",["+-","hc","sdx2",0]],["sx12",["+-","hc","sdx1",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc",0,"sdy3"]],["sy4",["+-","vc",0,"sdy4"]],["sy5",["+-","vc",0,"sdy5"]],["sy6",["+-","vc",0,"sdy6"]],["sy7",["+-","vc","sdy6",0]],["sy8",["+-","vc","sdy5",0]],["sy9",["+-","vc","sdy4",0]],["sy10",["+-","vc","sdy3",0]],["sy11",["+-","vc","sdy2",0]],["sy12",["+-","vc","sdy1",0]],["idx",["cos","iwd2",2700000]],["idy",["sin","ihd2",2700000]],["il",["+-","hc",0,"idx"]],["it",["+-","vc",0,"idy"]],["ir",["+-","hc","idx",0]],["ib",["+-","vc","idy",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy6"],["L","x1","y5"],["L","sx2","sy5"],["L","x2","y4"],["L","sx3","sy4"],["L","x3","y3"],["L","sx4","sy3"],["L","x4","y2"],["L","sx5","sy2"],["L","x5","y1"],["L","sx6","sy1"],["L","hc","t"],["L","sx7","sy1"],["L","x6","y1"],["L","sx8","sy2"],["L","x7","y2"],["L","sx9","sy3"],["L","x8","y3"],["L","sx10","sy4"],["L","x9","y4"],["L","sx11","sy5"],["L","x10","y5"],["L","sx12","sy6"],["L","r","vc"],["L","sx12","sy7"],["L","x10","y6"],["L","sx11","sy8"],["L","x9","y7"],["L","sx10","sy9"],["L","x8","y8"],["L","sx9","sy10"],["L","x7","y9"],["L","sx8","sy11"],["L","x6","y10"],["L","sx7","sy12"],["L","hc","b"],["L","sx6","sy12"],["L","x5","y10"],["L","sx5","sy11"],["L","x4","y9"],["L","sx4","sy10"],["L","x3","y8"],["L","sx3","sy9"],["L","x2","y7"],["L","sx2","sy8"],["L","x1","y6"],["L","sx1","sy7"],["Z"]]}]},"star32":{"av":[["adj",["val",37500]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["*/","wd2",98079,100000]],["dx2",["*/","wd2",92388,100000]],["dx3",["*/","wd2",83147,100000]],["dx4",["cos","wd2",2700000]],["dx5",["*/","wd2",55557,100000]],["dx6",["*/","wd2",38268,100000]],["dx7",["*/","wd2",19509,100000]],["dy1",["*/","hd2",98079,100000]],["dy2",["*/","hd2",92388,100000]],["dy3",["*/","hd2",83147,100000]],["dy4",["sin","hd2",2700000]],["dy5",["*/","hd2",55557,100000]],["dy6",["*/","hd2",38268,100000]],["dy7",["*/","hd2",19509,100000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc",0,"dx4"]],["x5",["+-","hc",0,"dx5"]],["x6",["+-","hc",0,"dx6"]],["x7",["+-","hc",0,"dx7"]],["x8",["+-","hc","dx7",0]],["x9",["+-","hc","dx6",0]],["x10",["+-","hc","dx5",0]],["x11",["+-","hc","dx4",0]],["x12",["+-","hc","dx3",0]],["x13",["+-","hc","dx2",0]],["x14",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc",0,"dy3"]],["y4",["+-","vc",0,"dy4"]],["y5",["+-","vc",0,"dy5"]],["y6",["+-","vc",0,"dy6"]],["y7",["+-","vc",0,"dy7"]],["y8",["+-","vc","dy7",0]],["y9",["+-","vc","dy6",0]],["y10",["+-","vc","dy5",0]],["y11",["+-","vc","dy4",0]],["y12",["+-","vc","dy3",0]],["y13",["+-","vc","dy2",0]],["y14",["+-","vc","dy1",0]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["*/","iwd2",99518,100000]],["sdx2",["*/","iwd2",95694,100000]],["sdx3",["*/","iwd2",88192,100000]],["sdx4",["*/","iwd2",77301,100000]],["sdx5",["*/","iwd2",63439,100000]],["sdx6",["*/","iwd2",47140,100000]],["sdx7",["*/","iwd2",29028,100000]],["sdx8",["*/","iwd2",9802,100000]],["sdy1",["*/","ihd2",99518,100000]],["sdy2",["*/","ihd2",95694,100000]],["sdy3",["*/","ihd2",88192,100000]],["sdy4",["*/","ihd2",77301,100000]],["sdy5",["*/","ihd2",63439,100000]],["sdy6",["*/","ihd2",47140,100000]],["sdy7",["*/","ihd2",29028,100000]],["sdy8",["*/","ihd2",9802,100000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc",0,"sdx3"]],["sx4",["+-","hc",0,"sdx4"]],["sx5",["+-","hc",0,"sdx5"]],["sx6",["+-","hc",0,"sdx6"]],["sx7",["+-","hc",0,"sdx7"]],["sx8",["+-","hc",0,"sdx8"]],["sx9",["+-","hc","sdx8",0]],["sx10",["+-","hc","sdx7",0]],["sx11",["+-","hc","sdx6",0]],["sx12",["+-","hc","sdx5",0]],["sx13",["+-","hc","sdx4",0]],["sx14",["+-","hc","sdx3",0]],["sx15",["+-","hc","sdx2",0]],["sx16",["+-","hc","sdx1",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc",0,"sdy3"]],["sy4",["+-","vc",0,"sdy4"]],["sy5",["+-","vc",0,"sdy5"]],["sy6",["+-","vc",0,"sdy6"]],["sy7",["+-","vc",0,"sdy7"]],["sy8",["+-","vc",0,"sdy8"]],["sy9",["+-","vc","sdy8",0]],["sy10",["+-","vc","sdy7",0]],["sy11",["+-","vc","sdy6",0]],["sy12",["+-","vc","sdy5",0]],["sy13",["+-","vc","sdy4",0]],["sy14",["+-","vc","sdy3",0]],["sy15",["+-","vc","sdy2",0]],["sy16",["+-","vc","sdy1",0]],["idx",["cos","iwd2",2700000]],["idy",["sin","ihd2",2700000]],["il",["+-","hc",0,"idx"]],["it",["+-","vc",0,"idy"]],["ir",["+-","hc","idx",0]],["ib",["+-","vc","idy",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy8"],["L","x1","y7"],["L","sx2","sy7"],["L","x2","y6"],["L","sx3","sy6"],["L","x3","y5"],["L","sx4","sy5"],["L","x4","y4"],["L","sx5","sy4"],["L","x5","y3"],["L","sx6","sy3"],["L","x6","y2"],["L","sx7","sy2"],["L","x7","y1"],["L","sx8","sy1"],["L","hc","t"],["L","sx9","sy1"],["L","x8","y1"],["L","sx10","sy2"],["L","x9","y2"],["L","sx11","sy3"],["L","x10","y3"],["L","sx12","sy4"],["L","x11","y4"],["L","sx13","sy5"],["L","x12","y5"],["L","sx14","sy6"],["L","x13","y6"],["L","sx15","sy7"],["L","x14","y7"],["L","sx16","sy8"],["L","r","vc"],["L","sx16","sy9"],["L","x14","y8"],["L","sx15","sy10"],["L","x13","y9"],["L","sx14","sy11"],["L","x12","y10"],["L","sx13","sy12"],["L","x11","y11"],["L","sx12","sy13"],["L","x10","y12"],["L","sx11","sy14"],["L","x9","y13"],["L","sx10","sy15"],["L","x8","y14"],["L","sx9","sy16"],["L","hc","b"],["L","sx8","sy16"],["L","x7","y14"],["L","sx7","sy15"],["L","x6","y13"],["L","sx6","sy14"],["L","x5","y12"],["L","sx5","sy13"],["L","x4","y11"],["L","sx4","sy12"],["L","x3","y10"],["L","sx3","sy11"],["L","x2","y9"],["L","sx2","sy10"],["L","x1","y8"],["L","sx1","sy9"],["Z"]]}]},"star4":{"av":[["adj",["val",12500]]],"gd":[["a",["pin",0,"adj",50000]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx",["cos","iwd2",2700000]],["sdy",["sin","ihd2",2700000]],["sx1",["+-","hc",0,"sdx"]],["sx2",["+-","hc","sdx",0]],["sy1",["+-","vc",0,"sdy"]],["sy2",["+-","vc","sdy",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["sx1","sy1","sx2","sy2"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy1"],["L","hc","t"],["L","sx2","sy1"],["L","r","vc"],["L","sx2","sy2"],["L","hc","b"],["L","sx1","sy2"],["Z"]]}]},"star5":{"av":[["adj",["val",19098]],["hf",["val",105146]],["vf",["val",110557]]],"gd":[["a",["pin",0,"adj",50000]],["swd2",["*/","wd2","hf",100000]],["shd2",["*/","hd2","vf",100000]],["svc",["*/","vc","vf",100000]],["dx1",["cos","swd2",1080000]],["dx2",["cos","swd2",18360000]],["dy1",["sin","shd2",1080000]],["dy2",["sin","shd2",18360000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["+-","svc",0,"dy1"]],["y2",["+-","svc",0,"dy2"]],["iwd2",["*/","swd2","a",50000]],["ihd2",["*/","shd2","a",50000]],["sdx1",["cos","iwd2",20520000]],["sdx2",["cos","iwd2",3240000]],["sdy1",["sin","ihd2",3240000]],["sdy2",["sin","ihd2",20520000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc","sdx2",0]],["sx4",["+-","hc","sdx1",0]],["sy1",["+-","svc",0,"sdy1"]],["sy2",["+-","svc",0,"sdy2"]],["sy3",["+-","svc","ihd2",0]],["yAdj",["+-","svc",0,"ihd2"]]],"rect":["sx1","sy1","sx4","sy3"],"paths":[{"d":[["M","x1","y1"],["L","sx2","sy1"],["L","hc","t"],["L","sx3","sy1"],["L","x4","y1"],["L","sx4","sy2"],["L","x3","y2"],["L","hc","sy3"],["L","x2","y2"],["L","sx1","sy2"],["Z"]]}]},"star6":{"av":[["adj",["val",28868]],["hf",["val",115470]]],"gd":[["a",["pin",0,"adj",50000]],["swd2",["*/","wd2","hf",100000]],["dx1",["cos","swd2",1800000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["y2",["+-","vc","hd4",0]],["iwd2",["*/","swd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx2",["*/","iwd2",1,2]],["sx1",["+-","hc",0,"iwd2"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc","sdx2",0]],["sx4",["+-","hc","iwd2",0]],["sdy1",["sin","ihd2",3600000]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc","sdy1",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["sx1","sy1","sx4","sy2"],"paths":[{"d":[["M","x1","hd4"],["L","sx2","sy1"],["L","hc","t"],["L","sx3","sy1"],["L","x2","hd4"],["L","sx4","vc"],["L","x2","y2"],["L","sx3","sy2"],["L","hc","b"],["L","sx2","sy2"],["L","x1","y2"],["L","sx1","vc"],["Z"]]}]},"star7":{"av":[["adj",["val",34601]],["hf",["val",102572]],["vf",["val",105210]]],"gd":[["a",["pin",0,"adj",50000]],["swd2",["*/","wd2","hf",100000]],["shd2",["*/","hd2","vf",100000]],["svc",["*/","vc","vf",100000]],["dx1",["*/","swd2",97493,100000]],["dx2",["*/","swd2",78183,100000]],["dx3",["*/","swd2",43388,100000]],["dy1",["*/","shd2",62349,100000]],["dy2",["*/","shd2",22252,100000]],["dy3",["*/","shd2",90097,100000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc",0,"dx3"]],["x4",["+-","hc","dx3",0]],["x5",["+-","hc","dx2",0]],["x6",["+-","hc","dx1",0]],["y1",["+-","svc",0,"dy1"]],["y2",["+-","svc","dy2",0]],["y3",["+-","svc","dy3",0]],["iwd2",["*/","swd2","a",50000]],["ihd2",["*/","shd2","a",50000]],["sdx1",["*/","iwd2",97493,100000]],["sdx2",["*/","iwd2",78183,100000]],["sdx3",["*/","iwd2",43388,100000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc",0,"sdx3"]],["sx4",["+-","hc","sdx3",0]],["sx5",["+-","hc","sdx2",0]],["sx6",["+-","hc","sdx1",0]],["sdy1",["*/","ihd2",90097,100000]],["sdy2",["*/","ihd2",22252,100000]],["sdy3",["*/","ihd2",62349,100000]],["sy1",["+-","svc",0,"sdy1"]],["sy2",["+-","svc",0,"sdy2"]],["sy3",["+-","svc","sdy3",0]],["sy4",["+-","svc","ihd2",0]],["yAdj",["+-","svc",0,"ihd2"]]],"rect":["sx2","sy1","sx5","sy3"],"paths":[{"d":[["M","x1","y2"],["L","sx1","sy2"],["L","x2","y1"],["L","sx3","sy1"],["L","hc","t"],["L","sx4","sy1"],["L","x5","y1"],["L","sx6","sy2"],["L","x6","y2"],["L","sx5","sy3"],["L","x4","y3"],["L","hc","sy4"],["L","x3","y3"],["L","sx2","sy3"],["Z"]]}]},"star8":{"av":[["adj",["val",37500]]],"gd":[["a",["pin",0,"adj",50000]],["dx1",["cos","wd2",2700000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["dy1",["sin","hd2",2700000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["iwd2",["*/","wd2","a",50000]],["ihd2",["*/","hd2","a",50000]],["sdx1",["*/","iwd2",92388,100000]],["sdx2",["*/","iwd2",38268,100000]],["sdy1",["*/","ihd2",92388,100000]],["sdy2",["*/","ihd2",38268,100000]],["sx1",["+-","hc",0,"sdx1"]],["sx2",["+-","hc",0,"sdx2"]],["sx3",["+-","hc","sdx2",0]],["sx4",["+-","hc","sdx1",0]],["sy1",["+-","vc",0,"sdy1"]],["sy2",["+-","vc",0,"sdy2"]],["sy3",["+-","vc","sdy2",0]],["sy4",["+-","vc","sdy1",0]],["yAdj",["+-","vc",0,"ihd2"]]],"rect":["sx1","sy1","sx4","sy4"],"paths":[{"d":[["M","l","vc"],["L","sx1","sy2"],["L","x1","y1"],["L","sx2","sy1"],["L","hc","t"],["L","sx3","sy1"],["L","x2","y1"],["L","sx4","sy2"],["L","r","vc"],["L","sx4","sy3"],["L","x2","y2"],["L","sx3","sy4"],["L","hc","b"],["L","sx2","sy4"],["L","x1","y2"],["L","sx1","sy3"],["Z"]]}]},"straightConnector1":{"rect":["l","t","r","b"],"paths":[{"fill":"none","d":[["M","l","t"],["L","r","b"]]}]},"stripedRightArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",84375,"w","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["x4",["*/","ss",5,32]],["dx5",["*/","ss","a2",100000]],["x5",["+-","r",0,"dx5"]],["dy1",["*/","h","a1",200000]],["y1",["+-","vc",0,"dy1"]],["y2",["+-","vc","dy1",0]],["dx6",["*/","dy1","dx5","hd2"]],["x6",["+-","r",0,"dx6"]]],"rect":["x4","y1","x6","y2"],"paths":[{"d":[["M","l","y1"],["L","ssd32","y1"],["L","ssd32","y2"],["L","l","y2"],["Z"],["M","ssd16","y1"],["L","ssd8","y1"],["L","ssd8","y2"],["L","ssd16","y2"],["Z"],["M","x4","y1"],["L","x5","y1"],["L","x5","t"],["L","r","vc"],["L","x5","b"],["L","x5","y2"],["L","x4","y2"],["Z"]]}]},"sun":{"av":[["adj",["val",25000]]],"gd":[["a",["pin",12500,"adj",46875]],["g0",["+-",50000,0,"a"]],["g1",["*/","g0",30274,32768]],["g2",["*/","g0",12540,32768]],["g3",["+-","g1",50000,0]],["g4",["+-","g2",50000,0]],["g5",["+-",50000,0,"g1"]],["g6",["+-",50000,0,"g2"]],["g7",["*/","g0",23170,32768]],["g8",["+-",50000,"g7",0]],["g9",["+-",50000,0,"g7"]],["g10",["*/","g5",3,4]],["g11",["*/","g6",3,4]],["g12",["+-","g10",3662,0]],["g13",["+-","g11",3662,0]],["g14",["+-","g11",12500,0]],["g15",["+-",100000,0,"g10"]],["g16",["+-",100000,0,"g12"]],["g17",["+-",100000,0,"g13"]],["g18",["+-",100000,0,"g14"]],["ox1",["*/","w",18436,21600]],["oy1",["*/","h",3163,21600]],["ox2",["*/","w",3163,21600]],["oy2",["*/","h",18436,21600]],["x8",["*/","w","g8",100000]],["x9",["*/","w","g9",100000]],["x10",["*/","w","g10",100000]],["x12",["*/","w","g12",100000]],["x13",["*/","w","g13",100000]],["x14",["*/","w","g14",100000]],["x15",["*/","w","g15",100000]],["x16",["*/","w","g16",100000]],["x17",["*/","w","g17",100000]],["x18",["*/","w","g18",100000]],["x19",["*/","w","a",100000]],["wR",["*/","w","g0",100000]],["hR",["*/","h","g0",100000]],["y8",["*/","h","g8",100000]],["y9",["*/","h","g9",100000]],["y10",["*/","h","g10",100000]],["y12",["*/","h","g12",100000]],["y13",["*/","h","g13",100000]],["y14",["*/","h","g14",100000]],["y15",["*/","h","g15",100000]],["y16",["*/","h","g16",100000]],["y17",["*/","h","g17",100000]],["y18",["*/","h","g18",100000]]],"rect":["x9","y9","x8","y8"],"paths":[{"d":[["M","r","vc"],["L","x15","y18"],["L","x15","y14"],["Z"],["M","ox1","oy1"],["L","x16","y13"],["L","x17","y12"],["Z"],["M","hc","t"],["L","x18","y10"],["L","x14","y10"],["Z"],["M","ox2","oy1"],["L","x13","y12"],["L","x12","y13"],["Z"],["M","l","vc"],["L","x10","y14"],["L","x10","y18"],["Z"],["M","ox2","oy2"],["L","x12","y17"],["L","x13","y16"],["Z"],["M","hc","b"],["L","x14","y15"],["L","x18","y15"],["Z"],["M","ox1","oy2"],["L","x17","y16"],["L","x16","y17"],["Z"],["M","x19","vc"],["A","wR","hR","cd2",21600000],["Z"]]}]},"swooshArrow":{"av":[["adj1",["val",25000]],["adj2",["val",16667]]],"gd":[["a1",["pin",1,"adj1",75000]],["maxAdj2",["*/",70000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["ad1",["*/","h","a1",100000]],["ad2",["*/","ss","a2",100000]],["xB",["+-","r",0,"ad2"]],["yB",["+-","t","ssd8",0]],["alfa",["*/","cd4",1,14]],["dx0",["tan","ssd8","alfa"]],["xC",["+-","xB",0,"dx0"]],["dx1",["tan","ad1","alfa"]],["yF",["+-","yB","ad1",0]],["xF",["+-","xB","dx1",0]],["xE",["+-","xF","dx0",0]],["yE",["+-","yF","ssd8",0]],["dy2",["+-","yE",0,"t"]],["dy22",["*/","dy2",1,2]],["dy3",["*/","h",1,20]],["yD",["+-","t","dy22","dy3"]],["dy4",["*/","hd6",1,1]],["yP1",["+-","hd6","dy4",0]],["xP1",["val","wd6"]],["dy5",["*/","hd6",1,2]],["yP2",["+-","yF","dy5",0]],["xP2",["val","wd4"]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","b"],["Q","xP1","yP1","xB","yB"],["L","xC","t"],["L","r","yD"],["L","xE","yE"],["L","xF","yF"],["Q","xP2","yP2","l","b"],["Z"]]}]},"teardrop":{"av":[["adj",["val",100000]]],"gd":[["a",["pin",0,"adj",200000]],["r2",["sqrt",2]],["tw",["*/","wd2","r2",1]],["th",["*/","hd2","r2",1]],["sw",["*/","tw","a",100000]],["sh",["*/","th","a",100000]],["dx1",["cos","sw",2700000]],["dy1",["sin","sh",2700000]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc",0,"dy1"]],["x2",["+/","hc","x1",2]],["y2",["+/","vc","y1",2]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","l","vc"],["A","wd2","hd2","cd2","cd4"],["Q","x2","t","x1","y1"],["Q","r","y2","r","vc"],["A","wd2","hd2",0,"cd4"],["A","wd2","hd2","cd4","cd4"],["Z"]]}]},"trapezoid":{"av":[["adj",["val",25000]]],"gd":[["maxAdj",["*/",50000,"w","ss"]],["a",["pin",0,"adj","maxAdj"]],["x1",["*/","ss","a",200000]],["x2",["*/","ss","a",100000]],["x3",["+-","r",0,"x2"]],["x4",["+-","r",0,"x1"]],["il",["*/","wd3","a","maxAdj"]],["it",["*/","hd3","a","maxAdj"]],["ir",["+-","r",0,"il"]]],"rect":["il","it","ir","b"],"paths":[{"d":[["M","l","b"],["L","x2","t"],["L","x3","t"],["L","r","b"],["Z"]]}]},"triangle":{"av":[["adj",["val",50000]]],"gd":[["a",["pin",0,"adj",100000]],["x1",["*/","w","a",200000]],["x2",["*/","w","a",100000]],["x3",["+-","x1","wd2",0]]],"rect":["x1","vc","x3","b"],"paths":[{"d":[["M","l","b"],["L","x2","t"],["L","r","b"],["Z"]]}]},"upArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",64977]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",100000,"h","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","h"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dx1",["*/","ss","a2",100000]],["dx2",["*/","ss","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["*/","ss","a3",100000]],["dy2",["*/","h","a4",100000]],["y2",["+-","b",0,"dy2"]],["y3",["+/","y2","b",2]]],"rect":["l","y2","r","b"],"paths":[{"d":[["M","l","y2"],["L","x2","y2"],["L","x2","y1"],["L","x1","y1"],["L","hc","t"],["L","x4","y1"],["L","x3","y1"],["L","x3","y2"],["L","r","y2"],["L","r","b"],["L","l","b"],["Z"]]}]},"upDownArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",50000,"h","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["y2",["*/","ss","a2",100000]],["y3",["+-","b",0,"y2"]],["dx1",["*/","w","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["dy1",["*/","x1","y2","wd2"]],["y1",["+-","y2",0,"dy1"]],["y4",["+-","y3","dy1",0]]],"rect":["x1","y1","x2","y4"],"paths":[{"d":[["M","l","y2"],["L","hc","t"],["L","r","y2"],["L","x2","y2"],["L","x2","y3"],["L","r","y3"],["L","hc","b"],["L","l","y3"],["L","x1","y3"],["L","x1","y2"],["Z"]]}]},"upArrow":{"av":[["adj1",["val",50000]],["adj2",["val",50000]]],"gd":[["maxAdj2",["*/",100000,"h","ss"]],["a1",["pin",0,"adj1",100000]],["a2",["pin",0,"adj2","maxAdj2"]],["dy2",["*/","ss","a2",100000]],["y2",["+-","t","dy2",0]],["dx1",["*/","w","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc","dx1",0]],["dy1",["*/","x1","dy2","wd2"]],["y1",["+-","y2",0,"dy1"]]],"rect":["x1","y1","x2","b"],"paths":[{"d":[["M","l","y2"],["L","hc","t"],["L","r","y2"],["L","x2","y2"],["L","x2","b"],["L","x1","b"],["L","x1","y2"],["Z"]]}]},"upDownArrowCallout":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",48123]]],"gd":[["maxAdj2",["*/",50000,"w","ss"]],["a2",["pin",0,"adj2","maxAdj2"]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["maxAdj3",["*/",50000,"h","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q2",["*/","a3","ss","hd2"]],["maxAdj4",["+-",100000,0,"q2"]],["a4",["pin",0,"adj4","maxAdj4"]],["dx1",["*/","ss","a2",100000]],["dx2",["*/","ss","a1",200000]],["x1",["+-","hc",0,"dx1"]],["x2",["+-","hc",0,"dx2"]],["x3",["+-","hc","dx2",0]],["x4",["+-","hc","dx1",0]],["y1",["*/","ss","a3",100000]],["y4",["+-","b",0,"y1"]],["dy2",["*/","h","a4",200000]],["y2",["+-","vc",0,"dy2"]],["y3",["+-","vc","dy2",0]]],"rect":["l","y2","r","y3"],"paths":[{"d":[["M","l","y2"],["L","x2","y2"],["L","x2","y1"],["L","x1","y1"],["L","hc","t"],["L","x4","y1"],["L","x3","y1"],["L","x3","y2"],["L","r","y2"],["L","r","y3"],["L","x3","y3"],["L","x3","y4"],["L","x4","y4"],["L","hc","b"],["L","x1","y4"],["L","x2","y4"],["L","x2","y3"],["L","l","y3"],["Z"]]}]},"uturnArrow":{"av":[["adj1",["val",25000]],["adj2",["val",25000]],["adj3",["val",25000]],["adj4",["val",43750]],["adj5",["val",75000]]],"gd":[["a2",["pin",0,"adj2",25000]],["maxAdj1",["*/","a2",2,1]],["a1",["pin",0,"adj1","maxAdj1"]],["q2",["*/","a1","ss","h"]],["q3",["+-",100000,0,"q2"]],["maxAdj3",["*/","q3","h","ss"]],["a3",["pin",0,"adj3","maxAdj3"]],["q1",["+-","a3","a1",0]],["minAdj5",["*/","q1","ss","h"]],["a5",["pin","minAdj5","adj5",100000]],["th",["*/","ss","a1",100000]],["aw2",["*/","ss","a2",100000]],["th2",["*/","th",1,2]],["dh2",["+-","aw2",0,"th2"]],["y5",["*/","h","a5",100000]],["ah",["*/","ss","a3",100000]],["y4",["+-","y5",0,"ah"]],["x9",["+-","r",0,"dh2"]],["bw",["*/","x9",1,2]],["bs",["min","bw","y4"]],["maxAdj4",["*/","bs",100000,"ss"]],["a4",["pin",0,"adj4","maxAdj4"]],["bd",["*/","ss","a4",100000]],["bd3",["+-","bd",0,"th"]],["bd2",["max","bd3",0]],["x3",["+-","th","bd2",0]],["x8",["+-","r",0,"aw2"]],["x6",["+-","x8",0,"aw2"]],["x7",["+-","x6","dh2",0]],["x4",["+-","x9",0,"bd"]],["x5",["+-","x7",0,"bd2"]],["cx",["+/","th","x7",2]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","b"],["L","l","bd"],["A","bd","bd","cd2","cd4"],["L","x4","t"],["A","bd","bd","3cd4","cd4"],["L","x9","y4"],["L","r","y4"],["L","x8","y5"],["L","x6","y4"],["L","x7","y4"],["L","x7","x3"],["A","bd2","bd2",0,-5400000],["L","x3","th"],["A","bd2","bd2","3cd4",-5400000],["L","th","b"],["Z"]]}]},"verticalScroll":{"av":[["adj",["val",12500]]],"gd":[["a",["pin",0,"adj",25000]],["ch",["*/","ss","a",100000]],["ch2",["*/","ch",1,2]],["ch4",["*/","ch",1,4]],["x3",["+-","ch","ch2",0]],["x4",["+-","ch","ch",0]],["x6",["+-","r",0,"ch"]],["x7",["+-","r",0,"ch2"]],["x5",["+-","x6",0,"ch2"]],["y3",["+-","b",0,"ch"]],["y4",["+-","b",0,"ch2"]]],"rect":["ch","ch","x6","y4"],"paths":[{"stroke":false,"d":[["M","ch2","b"],["A","ch2","ch2","cd4",-5400000],["L","ch2","y4"],["A","ch4","ch4","cd4",-10800000],["L","ch","y3"],["L","ch","ch2"],["A","ch2","ch2","cd2","cd4"],["L","x7","t"],["A","ch2","ch2","3cd4","cd2"],["L","x6","ch"],["L","x6","y4"],["A","ch2","ch2",0,"cd4"],["Z"],["M","x4","ch2"],["A","ch2","ch2",0,"cd4"],["A","ch4","ch4","cd4","cd2"],["Z"]]},{"fill":"darkenLess","stroke":false,"d":[["M","x4","ch2"],["A","ch2","ch2",0,"cd4"],["A","ch4","ch4","cd4","cd2"],["Z"],["M","ch","y4"],["A","ch2","ch2",0,"3cd4"],["A","ch4","ch4","3cd4","cd2"],["Z"]]},{"fill":"none","d":[["M","ch","y3"],["L","ch","ch2"],["A","ch2","ch2","cd2","cd4"],["L","x7","t"],["A","ch2","ch2","3cd4","cd2"],["L","x6","ch"],["L","x6","y4"],["A","ch2","ch2",0,"cd4"],["L","ch2","b"],["A","ch2","ch2","cd4","cd2"],["Z"],["M","x3","t"],["A","ch2","ch2","3cd4","cd2"],["A","ch4","ch4","cd4","cd2"],["L","x4","ch2"],["M","x6","ch"],["L","x3","ch"],["M","ch2","y3"],["A","ch4","ch4","3cd4","cd2"],["L","ch","y4"],["M","ch2","b"],["A","ch2","ch2","cd4",-5400000],["L","ch","y3"]]}]},"wave":{"av":[["adj1",["val",12500]],["adj2",["val",0]]],"gd":[["a1",["pin",0,"adj1",20000]],["a2",["pin",-10000,"adj2",10000]],["y1",["*/","h","a1",100000]],["dy2",["*/","y1",10,3]],["y2",["+-","y1",0,"dy2"]],["y3",["+-","y1","dy2",0]],["y4",["+-","b",0,"y1"]],["y5",["+-","y4",0,"dy2"]],["y6",["+-","y4","dy2",0]],["dx1",["*/","w","a2",100000]],["of2",["*/","w","a2",50000]],["x1",["abs","dx1"]],["dx2",["?:","of2",0,"of2"]],["x2",["+-","l",0,"dx2"]],["dx5",["?:","of2","of2",0]],["x5",["+-","r",0,"dx5"]],["dx3",["+/","dx2","x5",3]],["x3",["+-","x2","dx3",0]],["x4",["+/","x3","x5",2]],["x6",["+-","l","dx5",0]],["x10",["+-","r","dx2",0]],["x7",["+-","x6","dx3",0]],["x8",["+/","x7","x10",2]],["x9",["+-","r",0,"x1"]],["xAdj",["+-","hc","dx1",0]],["xAdj2",["+-","hc",0,"dx1"]],["il",["max","x2","x6"]],["ir",["min","x5","x10"]],["it",["*/","h","a1",50000]],["ib",["+-","b",0,"it"]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","x2","y1"],["C","x3","y2","x4","y3","x5","y1"],["L","x10","y4"],["C","x8","y6","x7","y5","x6","y4"],["Z"]]}]},"wedgeEllipseCallout":{"av":[["adj1",["val",-20833]],["adj2",["val",62500]]],"gd":[["dxPos",["*/","w","adj1",100000]],["dyPos",["*/","h","adj2",100000]],["xPos",["+-","hc","dxPos",0]],["yPos",["+-","vc","dyPos",0]],["sdx",["*/","dxPos","h",1]],["sdy",["*/","dyPos","w",1]],["pang",["at2","sdx","sdy"]],["stAng",["+-","pang",660000,0]],["enAng",["+-","pang",0,660000]],["dx1",["cos","wd2","stAng"]],["dy1",["sin","hd2","stAng"]],["x1",["+-","hc","dx1",0]],["y1",["+-","vc","dy1",0]],["dx2",["cos","wd2","enAng"]],["dy2",["sin","hd2","enAng"]],["x2",["+-","hc","dx2",0]],["y2",["+-","vc","dy2",0]],["stAng1",["at2","dx1","dy1"]],["enAng1",["at2","dx2","dy2"]],["swAng1",["+-","enAng1",0,"stAng1"]],["swAng2",["+-","swAng1",21600000,0]],["swAng",["?:","swAng1","swAng1","swAng2"]],["idx",["cos","wd2",2700000]],["idy",["sin","hd2",2700000]],["il",["+-","hc",0,"idx"]],["ir",["+-","hc","idx",0]],["it",["+-","vc",0,"idy"]],["ib",["+-","vc","idy",0]]],"rect":["il","it","ir","ib"],"paths":[{"d":[["M","xPos","yPos"],["L","x1","y1"],["A","wd2","hd2","stAng1","swAng"],["Z"]]}]},"wedgeRectCallout":{"av":[["adj1",["val",-20833]],["adj2",["val",62500]]],"gd":[["dxPos",["*/","w","adj1",100000]],["dyPos",["*/","h","adj2",100000]],["xPos",["+-","hc","dxPos",0]],["yPos",["+-","vc","dyPos",0]],["dx",["+-","xPos",0,"hc"]],["dy",["+-","yPos",0,"vc"]],["dq",["*/","dxPos","h","w"]],["ady",["abs","dyPos"]],["adq",["abs","dq"]],["dz",["+-","ady",0,"adq"]],["xg1",["?:","dxPos",7,2]],["xg2",["?:","dxPos",10,5]],["x1",["*/","w","xg1",12]],["x2",["*/","w","xg2",12]],["yg1",["?:","dyPos",7,2]],["yg2",["?:","dyPos",10,5]],["y1",["*/","h","yg1",12]],["y2",["*/","h","yg2",12]],["t1",["?:","dxPos","l","xPos"]],["xl",["?:","dz","l","t1"]],["t2",["?:","dyPos","x1","xPos"]],["xt",["?:","dz","t2","x1"]],["t3",["?:","dxPos","xPos","r"]],["xr",["?:","dz","r","t3"]],["t4",["?:","dyPos","xPos","x1"]],["xb",["?:","dz","t4","x1"]],["t5",["?:","dxPos","y1","yPos"]],["yl",["?:","dz","y1","t5"]],["t6",["?:","dyPos","t","yPos"]],["yt",["?:","dz","t6","t"]],["t7",["?:","dxPos","yPos","y1"]],["yr",["?:","dz","y1","t7"]],["t8",["?:","dyPos","yPos","b"]],["yb",["?:","dz","t8","b"]]],"rect":["l","t","r","b"],"paths":[{"d":[["M","l","t"],["L","x1","t"],["L","xt","yt"],["L","x2","t"],["L","r","t"],["L","r","y1"],["L","xr","yr"],["L","r","y2"],["L","r","b"],["L","x2","b"],["L","xb","yb"],["L","x1","b"],["L","l","b"],["L","l","y2"],["L","xl","yl"],["L","l","y1"],["Z"]]}]},"wedgeRoundRectCallout":{"av":[["adj1",["val",-20833]],["adj2",["val",62500]],["adj3",["val",16667]]],"gd":[["dxPos",["*/","w","adj1",100000]],["dyPos",["*/","h","adj2",100000]],["xPos",["+-","hc","dxPos",0]],["yPos",["+-","vc","dyPos",0]],["dq",["*/","dxPos","h","w"]],["ady",["abs","dyPos"]],["adq",["abs","dq"]],["dz",["+-","ady",0,"adq"]],["xg1",["?:","dxPos",7,2]],["xg2",["?:","dxPos",10,5]],["x1",["*/","w","xg1",12]],["x2",["*/","w","xg2",12]],["yg1",["?:","dyPos",7,2]],["yg2",["?:","dyPos",10,5]],["y1",["*/","h","yg1",12]],["y2",["*/","h","yg2",12]],["t1",["?:","dxPos","l","xPos"]],["xl",["?:","dz","l","t1"]],["t2",["?:","dyPos","x1","xPos"]],["xt",["?:","dz","t2","x1"]],["t3",["?:","dxPos","xPos","r"]],["xr",["?:","dz","r","t3"]],["t4",["?:","dyPos","xPos","x1"]],["xb",["?:","dz","t4","x1"]],["t5",["?:","dxPos","y1","yPos"]],["yl",["?:","dz","y1","t5"]],["t6",["?:","dyPos","t","yPos"]],["yt",["?:","dz","t6","t"]],["t7",["?:","dxPos","yPos","y1"]],["yr",["?:","dz","y1","t7"]],["t8",["?:","dyPos","yPos","b"]],["yb",["?:","dz","t8","b"]],["u1",["*/","ss","adj3",100000]],["u2",["+-","r",0,"u1"]],["v2",["+-","b",0,"u1"]],["il",["*/","u1",29289,100000]],["ir",["+-","r",0,"il"]],["ib",["+-","b",0,"il"]]],"rect":["il","il","ir","ib"],"paths":[{"d":[["M","l","u1"],["A","u1","u1","cd2","cd4"],["L","x1","t"],["L","xt","yt"],["L","x2","t"],["L","u2","t"],["A","u1","u1","3cd4","cd4"],["L","r","y1"],["L","xr","yr"],["L","r","y2"],["L","r","v2"],["A","u1","u1",0,"cd4"],["L","x2","b"],["L","xb","yb"],["L","x1","b"],["L","u1","b"],["A","u1","u1","cd4","cd4"],["L","l","y2"],["L","xl","yl"],["L","l","y1"],["Z"]]}]}},"animations":{"enter.appear":{"dur":1,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"}],"bg":1},"exit.appear":{"dur":1,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.fly.down":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.up":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.up_left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.up_right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.down_right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.fly.down_left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"exit.fly.down":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.up":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.right":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.left":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.up_left":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.up_right":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.down_right":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fly.down_left":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.blinds.horizontal":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"blinds(horizontal)"}],"bg":1},"enter.blinds.vertical":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"blinds(vertical)"}],"bg":1},"exit.blinds.horizontal":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"blinds(horizontal)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.blinds.vertical":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"blinds(vertical)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.box.in":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"box(in)"}],"bg":1},"enter.box.out":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"box(out)"}],"bg":1},"exit.box.in":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"box(in)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.box.out":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"box(out)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.checkerboard.horizontal":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"checkerboard(across)"}],"bg":1},"enter.checkerboard.vertical":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"checkerboard(down)"}],"bg":1},"exit.checkerboard.horizontal":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"checkerboard(across)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.checkerboard.vertical":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"checkerboard(down)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.circle.in":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"circle(in)"}],"bg":1},"enter.circle.out":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"circle(out)"}],"bg":1},"exit.circle.in":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"circle(in)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.circle.out":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"circle(out)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.crawl.down":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.up":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.right":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.left":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.up_left":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.up_right":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.down_right":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"enter.crawl.down_left":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"0-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":5000,"fill":"hold","attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"exit.crawl.down":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.up":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.right":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.left":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.up_left":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.up_right":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"0-ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.down_right":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"1+ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.crawl.down_left":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"0-ppt_w/2"]]},{"dur":5000,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+ppt_h/2"]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.diamond.in":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"diamond(in)"}],"bg":1},"enter.diamond.out":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"diamond(out)"}],"bg":1},"exit.diamond.in":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"diamond(in)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.diamond.out":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"diamond(out)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.dissolve":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"dissolve"}],"bg":1},"exit.dissolve":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"dissolve"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.fade":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.fade":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.flashOnce":{"dur":1000,"c":[{"dur":1000,"attr":["style.visibility"],"t":"set","to":"visible"}],"bg":1},"exit.flashOnce":{"dur":1000,"c":[{"dur":1000,"attr":["style.visibility"],"t":"anim","calc":"discrete","vt":"str","tav":[[0.0,"hidden"],[0.5,"visible"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.peek.down":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+#ppt_h*1.125000"],[1.0,"#ppt_y"]]},{"dur":500,"t":"effect","tr":"in","filter":"wipe(up)"}],"bg":1},"enter.peek.up":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-#ppt_h*1.125000"],[1.0,"#ppt_y"]]},{"dur":500,"t":"effect","tr":"in","filter":"wipe(down)"}],"bg":1},"enter.peek.right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x+#ppt_w*1.125000"],[1.0,"#ppt_x"]]},{"dur":500,"t":"effect","tr":"in","filter":"wipe(left)"}],"bg":1},"enter.peek.left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-#ppt_w*1.125000"],[1.0,"#ppt_x"]]},{"dur":500,"t":"effect","tr":"in","filter":"wipe(right)"}],"bg":1},"exit.peek.down":{"dur":500,"c":[{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y+#ppt_h*1.125000"]]},{"dur":500,"t":"effect","tr":"out","filter":"wipe(down)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.peek.up":{"dur":500,"c":[{"dur":500,"attr":["ppt_y"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y-#ppt_h*1.125000"]]},{"dur":500,"t":"effect","tr":"out","filter":"wipe(up)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.peek.right":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x+#ppt_w*1.125000"]]},{"dur":500,"t":"effect","tr":"out","filter":"wipe(right)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.peek.left":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"additive":"base","t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x-#ppt_w*1.125000"]]},{"dur":500,"t":"effect","tr":"out","filter":"wipe(left)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.plus.in":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"plus(in)"}],"bg":1},"enter.plus.out":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"plus(out)"}],"bg":1},"exit.plus.in":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"plus(in)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.plus.out":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"plus(out)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.randomBars.horizontal":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"randombar(horizontal)"}],"bg":1},"enter.randomBars.vertical":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"randombar(vertical)"}],"bg":1},"exit.randomBars.horizontal":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"randombar(horizontal)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.randomBars.vertical":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"randombar(vertical)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.spiral":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":1000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_x+(cos(-2*pi*(1-$))*-#ppt_x-sin(-2*pi*(1-$))*(1-#ppt_y))*(1-$)"],[1.0,1.0]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_y+(sin(-2*pi*(1-$))*-#ppt_x+cos(-2*pi*(1-$))*(1-#ppt_y))*(1-$)"],[1.0,1.0]]}],"bg":1},"exit.spiral":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":1000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[0.05,"ppt_x+-0.0500*(ppt_x*0.9511+(1-ppt_y)*0.3090)"],[0.1,"ppt_x+-0.1000*(ppt_x*0.8090+(1-ppt_y)*0.5878)"],[0.15,"ppt_x+-0.1500*(ppt_x*0.5878+(1-ppt_y)*0.8090)"],[0.2,"ppt_x+-0.2000*(ppt_x*0.3090+(1-ppt_y)*0.9511)"],[0.25,"ppt_x+-0.2500*(ppt_x*-0.0000+(1-ppt_y)*1.0000)"],[0.3,"ppt_x+-0.3000*(ppt_x*-0.3090+(1-ppt_y)*0.9511)"],[0.35,"ppt_x+-0.3500*(ppt_x*-0.5878+(1-ppt_y)*0.8090)"],[0.4,"ppt_x+-0.4000*(ppt_x*-0.8090+(1-ppt_y)*0.5878)"],[0.45,"ppt_x+-0.4500*(ppt_x*-0.9511+(1-ppt_y)*0.3090)"],[0.5,"ppt_x+-0.5000*(ppt_x*-1.0000+(1-ppt_y)*-0.0000)"],[0.55,"ppt_x+-0.5500*(ppt_x*-0.9511+(1-ppt_y)*-0.3090)"],[0.6,"ppt_x+-0.6000*(ppt_x*-0.8090+(1-ppt_y)*-0.5878)"],[0.65,"ppt_x+-0.6500*(ppt_x*-0.5878+(1-ppt_y)*-0.8090)"],[0.7,"ppt_x+-0.7000*(ppt_x*-0.3090+(1-ppt_y)*-0.9511)"],[0.75,"ppt_x+-0.7500*(ppt_x*0.0000+(1-ppt_y)*-1.0000)"],[0.8,"ppt_x+-0.8000*(ppt_x*0.3090+(1-ppt_y)*-0.9511)"],[0.85,"ppt_x+-0.8500*(ppt_x*0.5878+(1-ppt_y)*-0.8090)"],[0.9,"ppt_x+-0.9000*(ppt_x*0.8090+(1-ppt_y)*-0.5878)"],[0.95,"ppt_x+-0.9500*(ppt_x*0.9511+(1-ppt_y)*-0.3090)"],[1.0,"ppt_x+-1.0000*(ppt_x*1.0000+(1-ppt_y)*0.0000)"]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.05,"ppt_y+-0.0500*(ppt_x*0.3090-(1-ppt_y)*0.9511)"],[0.1,"ppt_y+-0.1000*(ppt_x*0.5878-(1-ppt_y)*0.8090)"],[0.15,"ppt_y+-0.1500*(ppt_x*0.8090-(1-ppt_y)*0.5878)"],[0.2,"ppt_y+-0.2000*(ppt_x*0.9511-(1-ppt_y)*0.3090)"],[0.25,"ppt_y+-0.2500*(ppt_x*1.0000-(1-ppt_y)*-0.0000)"],[0.3,"ppt_y+-0.3000*(ppt_x*0.9511-(1-ppt_y)*-0.3090)"],[0.35,"ppt_y+-0.3500*(ppt_x*0.8090-(1-ppt_y)*-0.5878)"],[0.4,"ppt_y+-0.4000*(ppt_x*0.5878-(1-ppt_y)*-0.8090)"],[0.45,"ppt_y+-0.4500*(ppt_x*0.3090-(1-ppt_y)*-0.9511)"],[0.5,"ppt_y+-0.5000*(ppt_x*-0.0000-(1-ppt_y)*-1.0000)"],[0.55,"ppt_y+-0.5500*(ppt_x*-0.3090-(1-ppt_y)*-0.9511)"],[0.6,"ppt_y+-0.6000*(ppt_x*-0.5878-(1-ppt_y)*-0.8090)"],[0.65,"ppt_y+-0.6500*(ppt_x*-0.8090-(1-ppt_y)*-0.5878)"],[0.7,"ppt_y+-0.7000*(ppt_x*-0.9511-(1-ppt_y)*-0.3090)"],[0.75,"ppt_y+-0.7500*(ppt_x*-1.0000-(1-ppt_y)*0.0000)"],[0.8,"ppt_y+-0.8000*(ppt_x*-0.9511-(1-ppt_y)*0.3090)"],[0.85,"ppt_y+-0.8500*(ppt_x*-0.8090-(1-ppt_y)*0.5878)"],[0.9,"ppt_y+-0.9000*(ppt_x*-0.5878-(1-ppt_y)*0.8090)"],[0.95,"ppt_y+-0.9500*(ppt_x*-0.3090-(1-ppt_y)*0.9511)"],[1.0,"ppt_y+-1.0000*(ppt_x*0.0000-(1-ppt_y)*1.0000)"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.split.vertical_in":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"barn(inVertical)"}],"bg":1},"enter.split.horizontal_in":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"barn(inHorizontal)"}],"bg":1},"enter.split.horizontal_out":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"barn(outHorizontal)"}],"bg":1},"enter.split.vertical_out":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"barn(outVertical)"}],"bg":1},"exit.split.vertical_in":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"barn(inVertical)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.split.horizontal_in":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"barn(inHorizontal)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.split.horizontal_out":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"barn(outHorizontal)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.split.vertical_out":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"barn(outVertical)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.stretch.horizontal":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.stretch.up":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-#ppt_h/2"],[1.0,"#ppt_y"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]}],"bg":1},"enter.stretch.right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x+#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.stretch.down":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+#ppt_h/2"],[1.0,"#ppt_y"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]}],"bg":1},"enter.stretch.left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-#ppt_w/2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"exit.stretch.horizontal":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.stretch.up":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y-ppt_h/2"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.stretch.right":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x+ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.stretch.down":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+ppt_h/2"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.stretch.left":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x-ppt_w/2"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.strips.down_left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"strips(downLeft)"}],"bg":1},"enter.strips.up_left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"strips(upLeft)"}],"bg":1},"enter.strips.up_right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"strips(upRight)"}],"bg":1},"enter.strips.down_right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"strips(downRight)"}],"bg":1},"exit.strips.down_left":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"strips(downLeft)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.strips.up_left":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"strips(upLeft)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.strips.up_right":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"strips(upRight)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.strips.down_right":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"strips(downRight)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.swivel.horizontal":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_w*sin(2.5*pi*$)"],[1.0,1.0]]},{"dur":5000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.swivel.vertical":{"dur":5000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":5000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w"],[1.0,"#ppt_w"]]},{"dur":5000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_h*sin(2.5*pi*$)"],[1.0,1.0]]}],"bg":1},"exit.swivel.horizontal":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":5000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[0.05,"0.92*ppt_w"],[0.1,"0.71*ppt_w"],[0.15,"0.38*ppt_w"],[0.2,0.0],[0.25,"-0.38*ppt_w"],[0.3,"-0.71*ppt_w"],[0.35,"-0.92*ppt_w"],[0.4,"-ppt_w"],[0.45,"-0.92*ppt_w"],[0.5,"-0.71*ppt_w"],[0.55,"-0.38*ppt_w"],[0.6,0.0],[0.65,"0.38*ppt_w"],[0.7,"0.71*ppt_w"],[0.75,"0.92*ppt_w"],[0.8,"ppt_w"],[0.85,"0.92*ppt_w"],[0.9,"0.71*ppt_w"],[0.95,"0.38*ppt_w"],[1.0,0.0]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.swivel.vertical":{"dur":5000,"c":[{"dur":5000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w"]]},{"dur":5000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[0.05,"0.92*ppt_h"],[0.1,"0.71*ppt_h"],[0.15,"0.38*ppt_h"],[0.2,0.0],[0.25,"-0.38*ppt_h"],[0.3,"-0.71*ppt_h"],[0.35,"-0.92*ppt_h"],[0.4,"-ppt_h"],[0.45,"-0.92*ppt_h"],[0.5,"-0.71*ppt_h"],[0.55,"-0.38*ppt_h"],[0.6,0.0],[0.65,"0.38*ppt_h"],[0.7,"0.71*ppt_h"],[0.75,"0.92*ppt_h"],[0.8,"ppt_h"],[0.85,"0.92*ppt_h"],[0.9,"0.71*ppt_h"],[0.95,"0.38*ppt_h"],[1.0,0.0]]},{"dur":1,"delay":4999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.wedge":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"wedge"}],"bg":1},"exit.wedge":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"wedge"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.wheel":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"wheel(1)"}],"bg":1},"exit.wheel":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"wheel(1)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.wipe.down":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"wipe(down)"}],"bg":1},"enter.wipe.up":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"wipe(up)"}],"bg":1},"enter.wipe.right":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"wipe(right)"}],"bg":1},"enter.wipe.left":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"t":"effect","tr":"in","filter":"wipe(left)"}],"bg":1},"exit.wipe.down":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"wipe(down)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.wipe.up":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"wipe(up)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.wipe.right":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"wipe(right)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.wipe.left":{"dur":500,"c":[{"dur":500,"t":"effect","tr":"out","filter":"wipe(left)"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.zoom.in":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]}],"bg":1},"enter.zoom.out":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"4*#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"4*#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.zoom.in_slightly":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"2/3*#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"2/3*#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.zoom.in_center":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5],[1.0,"#ppt_y"]]}],"bg":1},"enter.zoom.out_slightly":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"4/3*#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"4/3*#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"enter.zoom.out_bottom":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"(6*min(max(#ppt_w*#ppt_h,.3),1)-7.4)/-.7*#ppt_w"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"(6*min(max(#ppt_w*#ppt_h,.3),1)-7.4)/-.7*#ppt_h"],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"1+(6*min(max(#ppt_w*#ppt_h,.3),1)-7.4)/-.7*#ppt_h/2"],[1.0,"#ppt_y"]]}],"bg":1},"exit.zoom.in":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"4*ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"4*ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.zoom.out":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.zoom.in_slightly":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"4/3*ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"4/3*ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.zoom.in_bottom":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"(6*min(max(ppt_w*ppt_h,.3),1)-7.4)/-.7*ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"(6*min(max(ppt_w*ppt_h,.3),1)-7.4)/-.7*ppt_h"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"1+(6*min(max(ppt_w*ppt_h,.3),1)-7.4)/-.7*ppt_h/2"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.zoom.out_slightly":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"2/3*ppt_w"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"2/3*ppt_h"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.zoom.out_center":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,0.5]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,0.5]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.randomEffects":{"dur":1,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1,"fill":"hold","attr":[null],"t":"anim","calc":"lin","vt":"num","to":""}],"bg":1},"exit.randomEffects":{"dur":1,"c":[{"dur":1,"attr":[null],"t":"anim","calc":"lin","vt":"num","to":""},{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.boomerang":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"decel":0.5,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,-90.0],[1.0,0.0]]},{"dur":500,"decel":0.5,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w"],[1.0,"#ppt_w*.05"]]},{"dur":500,"delay":500,"accel":0.5,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w*.05"],[1.0,"#ppt_w"]]},{"dur":1000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]},{"dur":500,"decel":0.5,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x+.4"],[1.0,"#ppt_x"]]},{"dur":500,"decel":0.5,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-.2"],[1.0,"#ppt_y+.1"]]},{"dur":500,"delay":500,"accel":0.5,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+.1"],[1.0,"#ppt_y"]]},{"dur":1000,"decel":0.5,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.boomerang":{"dur":1000,"c":[{"dur":1000,"accel":0.5,"t":"effect","tr":"out","filter":"fade"},{"dur":500,"accel":0.5,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+.1"]]},{"dur":500,"delay":500,"decel":0.5,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y-.1"]]},{"dur":500,"delay":500,"accel":0.5,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x+.4"]]},{"dur":1000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":500,"accel":0.5,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w*.05"]]},{"dur":500,"delay":500,"decel":0.5,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w/.05"]]},{"dur":500,"delay":500,"accel":0.5,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,-90.0]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.bounce":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":580,"t":"effect","tr":"in","filter":"wipe(down)"},{"dur":1822,"tmFilter":[[0.0,0.0],[0.14,0.36],[0.43,0.73],[0.71,0.91],[1.0,1.0]],"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-0.25"],[1.0,"#ppt_x"]]},{"dur":664,"tmFilter":[[0.0,0.0],[0.25,0.07],[0.5,0.2],[0.75,0.467],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5,"#ppt_y-sin(pi*$)/3"],[1.0,1.0]]},{"dur":664,"delay":664,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_y-sin(pi*$)/9"],[1.0,1.0]]},{"dur":332,"delay":1324,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_y-sin(pi*$)/27"],[1.0,1.0]]},{"dur":164,"delay":1656,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_y-sin(pi*$)/81"],[1.0,1.0]]},{"dur":26,"delay":650,"t":"scale","to":[1.0,0.6]},{"dur":166,"delay":676,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1312,"t":"scale","to":[1.0,0.8]},{"dur":166,"delay":1338,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1642,"t":"scale","to":[1.0,0.9]},{"dur":166,"delay":1668,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1808,"t":"scale","to":[1.0,0.95]},{"dur":166,"delay":1834,"decel":0.5,"t":"scale","to":[1.0,1.0]}],"bg":1},"exit.bounce":{"dur":2000,"c":[{"dur":180,"delay":1820,"accel":0.5,"t":"effect","tr":"out","filter":"wipe(down)"},{"dur":1822,"tmFilter":[[0.0,0.0],[0.14,0.31],[0.43,0.73],[0.71,0.91],[1.0,1.0]],"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"#ppt_x+0.25"]]},{"dur":178,"delay":1822,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":664,"tmFilter":[[0.0,0.0],[0.25,0.07],[0.5,0.2],[0.75,0.467],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.05,"ppt_y+0.026"],[0.1,"ppt_y+0.052"],[0.15,"ppt_y+0.078"],[0.2,"ppt_y+0.103"],[0.3,"ppt_y+0.151"],[0.4,"ppt_y+0.196"],[0.5,"ppt_y+0.236"],[0.6,"ppt_y+0.270"],[0.7,"ppt_y+0.297"],[0.8,"ppt_y+0.317"],[0.9,"ppt_y+0.329"],[1.0,"ppt_y+0.333"]]},{"dur":664,"delay":664,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.1,"ppt_y-0.034"],[0.2,"ppt_y-0.065"],[0.3,"ppt_y-0.090"],[0.4,"ppt_y-0.106"],[0.5,"ppt_y-0.111"],[0.6,"ppt_y-0.106"],[0.7,"ppt_y-0.090"],[0.8,"ppt_y-0.065"],[0.9,"ppt_y-0.034"],[1.0,"ppt_y"]]},{"dur":332,"delay":1324,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.1,"ppt_y-0.011"],[0.2,"ppt_y-0.022"],[0.3,"ppt_y-0.030"],[0.4,"ppt_y-0.035"],[0.5,"ppt_y-0.037"],[0.6,"ppt_y-0.035"],[0.7,"ppt_y-0.030"],[0.8,"ppt_y-0.022"],[0.9,"ppt_y-0.011"],[1.0,"ppt_y"]]},{"dur":164,"delay":1656,"tmFilter":[[0.0,0.0],[0.125,0.2665],[0.25,0.4],[0.375,0.465],[0.5,0.5],[0.625,0.535],[0.75,0.6],[0.875,0.7335],[1.0,1.0]],"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.1,"ppt_y-0.004"],[0.2,"ppt_y-0.007"],[0.3,"ppt_y-0.010"],[0.4,"ppt_y-0.012"],[0.5,"ppt_y-0.0123"],[0.6,"ppt_y-0.012"],[0.7,"ppt_y-0.010"],[0.8,"ppt_y-0.007"],[0.9,"ppt_y-0.004"],[1.0,"ppt_y"]]},{"dur":180,"delay":1820,"accel":0.5,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+ppt_h"]]},{"dur":26,"delay":620,"t":"scale","to":[1.0,0.6]},{"dur":166,"delay":646,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1312,"t":"scale","to":[1.0,0.8]},{"dur":166,"delay":1338,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1642,"t":"scale","to":[1.0,0.9]},{"dur":166,"delay":1668,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":26,"delay":1808,"t":"scale","to":[1.0,0.95]},{"dur":166,"delay":1834,"decel":0.5,"t":"scale","to":[1.0,1.0]},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.colorReveal":{"dur":80,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":80,"attr":["style.color"],"override":"childStyle","t":"anim","calc":"discrete","vt":"clr","tav":[[0.0,{"scheme":"accent2"}],[0.5,{"scheme":"hlink"}]]},{"dur":80,"attr":["fillcolor"],"t":"anim","calc":"discrete","vt":"clr","tav":[[0.0,{"scheme":"accent2"}],[0.5,{"scheme":"hlink"}]]},{"dur":80,"attr":["fill.type"],"t":"set","to":"solid"}],"it":{"type":"lt","pct":0.5},"bg":1},"exit.colorReveal":{"dur":80,"c":[{"dur":80,"attr":["style.color"],"override":"childStyle","t":"anim","calc":"discrete","vt":"clr","tav":[[0.0,{"scheme":"hlink"}],[0.5,{"scheme":"accent2"}]]},{"dur":80,"attr":["fillcolor"],"t":"anim","calc":"discrete","vt":"clr","tav":[[0.0,{"scheme":"hlink"}],[0.5,{"scheme":"accent2"}]]},{"dur":80,"attr":["fill.type"],"t":"set","to":"solid"},{"dur":1,"delay":79,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"it":{"type":"lt","pct":0.5},"bg":1},"enter.credits":{"dur":15000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":15000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":15000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+1"],[1.0,"#ppt_y-1"]]}],"bg":1},"exit.credits":{"dur":15000,"c":[{"dur":15000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":15000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y-1"],[1.0,"ppt_y+1"]]},{"dur":1,"delay":14999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.easeIn":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-.2"],[1.0,"#ppt_x"]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":1000,"t":"effect","tr":"in","filter":"wipe(right)","pr":"gradientSize: 0.1"}],"bg":1},"exit.easeIn":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x-.2"]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.float":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":800,"decel":1.0,"t":"effect","tr":"in","filter":"fade"},{"dur":800,"decel":1.0,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,-90.0],[1.0,0.0]]},{"dur":800,"decel":1.0,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x+0.4"],[1.0,"#ppt_x-0.05"]]},{"dur":800,"decel":1.0,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-0.4"],[1.0,"#ppt_y+0.1"]]},{"dur":200,"delay":800,"accel":1.0,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-0.05"],[1.0,"#ppt_x"]]},{"dur":200,"delay":800,"accel":1.0,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+0.1"],[1.0,"#ppt_y"]]}],"bg":1},"exit.float":{"dur":1000,"c":[{"dur":800,"delay":200,"accel":1.0,"t":"effect","tr":"out","filter":"fade"},{"dur":800,"delay":200,"accel":1.0,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,-90.0]]},{"dur":200,"decel":1.0,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x-0.05"]]},{"dur":200,"decel":1.0,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+0.1"]]},{"dur":800,"delay":200,"accel":1.0,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x+0.4+0.05"]]},{"dur":800,"delay":200,"accel":1.0,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y-0.4-0.1"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.growAndTurn":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":1000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":1000,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,90.0],[1.0,0.0]]},{"dur":1000,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.growAndTurn":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":1000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":1000,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,90.0]]},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.lightSpeed":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":600,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","from":"(-#ppt_w/2)","to":"(#ppt_x)"},{"dur":200,"delay":600,"decel":0.5,"autoRev":true,"fill":"hold","attr":["xshear"],"t":"anim","calc":"lin","vt":"num","from":"0","to":"-1.0"},{"dur":200,"delay":600,"decel":1.0,"autoRev":true,"fill":"hold","t":"scale","from":[1.0,1.0],"to":[0.8,1.0]},{"dur":200,"delay":600,"decel":1.0,"autoRev":true,"fill":"hold","attr":["ppt_x"],"additive":"sum","t":"anim","calc":"lin","vt":"num","by":"(#ppt_h/3+#ppt_w*0.1)"}],"bg":1},"exit.lightSpeed":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_x)","to":"(ppt_x+1)"},{"dur":200,"accel":0.5,"attr":["xshear"],"t":"anim","calc":"lin","vt":"num","from":"0","to":"-1.0"},{"dur":800,"delay":200,"attr":["xshear"],"t":"set","to":"-1.0"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.pinwheel":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"fade"},{"dur":2000,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,720.0],[1.0,0.0]]},{"dur":2000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":2000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]}],"bg":1},"exit.pinwheel":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"fade"},{"dur":2000,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,720.0]]},{"dur":2000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":2000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.riseUp":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"t":"effect","tr":"in","filter":"fade"},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":900,"decel":1.0,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+1"],[1.0,"#ppt_y-.03"]]},{"dur":100,"delay":900,"accel":1.0,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-.03"],[1.0,"#ppt_y"]]}],"bg":1},"exit.riseUp":{"dur":1000,"c":[{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":100,"decel":1.0,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y-.03"]]},{"dur":900,"delay":100,"accel":1.0,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+1"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.swish":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":455,"fill":"hold","attr":["style.rotation"],"t":"set","to":"-45.0"},{"dur":455,"delay":455,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,-45.0],[0.699,45.0],[1.0,0.0]]},{"dur":455,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-1"],[1.0,"#ppt_y-(0.354*#ppt_w-0.172*#ppt_h)"]]},{"dur":156,"delay":455,"decel":0.5,"autoRev":true,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-(0.354*#ppt_w-0.172*#ppt_h)"],[1.0,"#ppt_y-(0.354*#ppt_w-0.172*#ppt_h)-#ppt_h/2"]]},{"dur":136,"delay":864,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-(0.354*#ppt_w-0.172*#ppt_h)"],[1.0,"#ppt_y"]]}],"it":{"type":"lt","pct":0.5},"bg":1},"exit.swish":{"dur":1000,"c":[{"dur":1000,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,45.0]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+1"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"it":{"type":"lt","pct":0.5},"bg":1},"enter.thinLine":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h/20"],[0.5,"#ppt_h/20"],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w+.3"],[0.5,"#ppt_w+.3"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-.3"],[0.5,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"bg":1},"exit.thinLine":{"dur":500,"c":[{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[0.5,"ppt_h/20"],[1.0,"ppt_h/20"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[0.5,"ppt_w+.3"],[1.0,"ppt_w+.3"]]},{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[0.5,"ppt_x"],[1.0,"ppt_x-.3"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.unfold":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"t":"effect","tr":"in","filter":"fade"},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-.1"],[1.0,"#ppt_x"]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]}],"it":{"type":"lt","pct":0.1},"bg":1},"exit.unfold":{"dur":1000,"c":[{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x-.1"]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"it":{"type":"lt","pct":0.1},"bg":1},"enter.whip":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[0.5,"#ppt_x+.1"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h/10"],[0.5,"#ppt_h+.01"],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w/10"],[0.5,"#ppt_w+.01"],[1.0,"#ppt_w"]]},{"dur":500,"tmFilter":[[0.0,0.0],[0.5,1.0],[1.0,1.0]],"t":"effect","tr":"in","filter":"fade"}],"it":{"type":"lt","pct":0.1},"bg":1},"exit.whip":{"dur":500,"c":[{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[0.5,"ppt_x+.1"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[0.5,"ppt_h+.01"],[1.0,"ppt_h/10"]]},{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[0.5,"ppt_w+.01"],[1.0,"ppt_w/10"]]},{"dur":500,"tmFilter":[[0.0,0.0],[0.5,0.0],[1.0,1.0]],"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"it":{"type":"lt","pct":0.1},"bg":1},"enter.ascend":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"t":"effect","tr":"in","filter":"fade"},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+.1"],[1.0,"#ppt_y"]]}],"bg":1},"exit.ascend":{"dur":1000,"c":[{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y+.1"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.centerRevolve":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":100,"t":"effect","tr":"in","filter":"fade"},{"dur":400,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":400,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+0.31"],[1.0,"#ppt_y+0.31"]]},{"dur":600,"delay":400,"decel":0.5,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[0.05,"#ppt_x+0.0242"],[0.1,"#ppt_x+0.0479"],[0.15,"#ppt_x+0.0704"],[0.2,"#ppt_x+0.0911"],[0.25,"#ppt_x+0.1096"],[0.3,"#ppt_x+0.1254"],[0.35,"#ppt_x+0.1381"],[0.4,"#ppt_x+0.1474"],[0.45,"#ppt_x+0.1531"],[0.5,"#ppt_x+0.1550"],[0.55,"#ppt_x+0.1531"],[0.6,"#ppt_x+0.1474"],[0.65,"#ppt_x+0.1381"],[0.7,"#ppt_x+0.1254"],[0.75,"#ppt_x+0.1096"],[0.8,"#ppt_x+0.0911"],[0.85,"#ppt_x+0.0704"],[0.9,"#ppt_x+0.0479"],[0.95,"#ppt_x+0.0242"],[1.0,"#ppt_x"]]},{"dur":600,"delay":400,"decel":0.5,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y+0.31"],[0.05,"#ppt_y+0.308"],[0.1,"#ppt_y+0.3024"],[0.15,"#ppt_y+0.2931"],[0.2,"#ppt_y+0.2804"],[0.25,"#ppt_y+0.2646"],[0.3,"#ppt_y+0.2461"],[0.35,"#ppt_y+0.2253"],[0.4,"#ppt_y+0.2029"],[0.45,"#ppt_y+0.1792"],[0.5,"#ppt_y+0.155"],[0.55,"#ppt_y+0.1307"],[0.6,"#ppt_y+0.1071"],[0.65,"#ppt_y+0.0846"],[0.7,"#ppt_y+0.0639"],[0.75,"#ppt_y+0.0454"],[0.8,"#ppt_y+0.0296"],[0.85,"#ppt_y+0.0169"],[0.9,"#ppt_y+0.0076"],[0.95,"#ppt_y+0.0019"],[1.0,"#ppt_y"]]}],"bg":1},"exit.centerRevolve":{"dur":1000,"c":[{"dur":600,"decel":0.5,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[0.05,"ppt_x+0.0242"],[0.1,"ppt_x+0.0479"],[0.15,"ppt_x+0.0704"],[0.2,"ppt_x+0.0911"],[0.25,"ppt_x+0.1096"],[0.3,"ppt_x+0.1254"],[0.35,"ppt_x+0.1381"],[0.4,"ppt_x+0.1474"],[0.45,"ppt_x+0.1531"],[0.5,"ppt_x+0.155"],[0.55,"ppt_x+0.1531"],[0.6,"ppt_x+0.1474"],[0.65,"ppt_x+0.1381"],[0.7,"ppt_x+0.1254"],[0.75,"ppt_x+0.1096"],[0.8,"ppt_x+0.0911"],[0.85,"ppt_x+0.0704"],[0.9,"ppt_x+0.0479"],[0.95,"ppt_x+0.0242"],[1.0,"ppt_x"]]},{"dur":400,"delay":600,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":600,"decel":0.5,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[0.05,"ppt_y+0.0019"],[0.1,"ppt_y+0.0076"],[0.15,"ppt_y+0.0169"],[0.2,"ppt_y+0.0296"],[0.25,"ppt_y+0.0454"],[0.3,"ppt_y+0.0639"],[0.35,"ppt_y+0.0846"],[0.4,"ppt_y+0.1071"],[0.45,"ppt_y+0.1307"],[0.5,"ppt_y+0.155"],[0.55,"ppt_y+0.1792"],[0.6,"ppt_y+0.2029"],[0.65,"ppt_y+0.2253"],[0.7,"ppt_y+0.2461"],[0.75,"ppt_y+0.2646"],[0.8,"ppt_y+0.2804"],[0.85,"ppt_y+0.2931"],[0.9,"ppt_y+0.3024"],[0.95,"ppt_y+0.308"],[1.0,"ppt_y+0.31"]]},{"dur":400,"delay":600,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":100,"delay":900,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.fadedSwivel":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":2000,"t":"effect","tr":"in","filter":"fade"},{"dur":2000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0,"#ppt_w*sin(2.5*pi*$)"],[1.0,1.0]]},{"dur":2000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]}],"bg":1},"exit.fadedSwivel":{"dur":2000,"c":[{"dur":2000,"t":"effect","tr":"out","filter":"fade"},{"dur":2000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[0.05,"0.92*ppt_w"],[0.1,"0.71*ppt_w"],[0.15,"0.38*ppt_w"],[0.2,0.0],[0.25,"-0.38*ppt_w"],[0.3,"-0.71*ppt_w"],[0.35,"-0.92*ppt_w"],[0.4,"-ppt_w"],[0.45,"-0.92*ppt_w"],[0.5,"-0.71*ppt_w"],[0.55,"-0.38*ppt_w"],[0.6,0.0],[0.65,"0.38*ppt_w"],[0.7,"0.71*ppt_w"],[0.75,"0.92*ppt_w"],[0.8,"ppt_w"],[0.85,"0.92*ppt_w"],[0.9,"0.71*ppt_w"],[0.95,"0.38*ppt_w"],[1.0,0.0]]},{"dur":2000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.descend":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"t":"effect","tr":"in","filter":"fade"},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y-.1"],[1.0,"#ppt_y"]]}],"bg":1},"exit.descend":{"dur":1000,"c":[{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y-.1"]]},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.sling":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,90.0],[0.8,90.0],[0.8,90.0],[1.0,0.0]]},{"dur":1000,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,-1.0],[0.5,0.95],[1.0,"#ppt_x"]]},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":1000,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.sling":{"dur":1000,"c":[{"dur":1000,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[0.2,90.0],[0.2,90.0],[1.0,90.0]]},{"dur":1000,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[0.5,0.95],[1.0,-1.0]]},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.spinner":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,360.0],[1.0,0.0]]},{"dur":500,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.spinner":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":500,"attr":["style.rotation"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,360.0]]},{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.stretchy":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w+.3"],[1.0,"#ppt_w"]]},{"dur":1000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]},{"dur":1000,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.stretchy":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w+.3"]]},{"dur":1000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.zip":{"dur":2000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":770,"decel":1.0,"t":"effect","tr":"in","filter":"fade"},{"dur":770,"decel":1.0,"t":"scale","from":[0.1,0.1],"to":[2.0,4.5]},{"dur":1230,"delay":770,"accel":1.0,"fill":"hold","t":"scale","from":[2.0,4.5],"to":[1.0,1.0]},{"dur":770,"fill":"hold","attr":["ppt_x"],"t":"set","to":"(0.5)"},{"dur":1230,"delay":770,"accel":1.0,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","from":"(0.5)","to":"(#ppt_x)"},{"dur":770,"fill":"hold","attr":["ppt_y"],"t":"set","to":"(#ppt_y+0.4)"},{"dur":1230,"delay":770,"accel":1.0,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","from":"(#ppt_y+0.4)","to":"(#ppt_y)"}],"bg":1},"exit.zip":{"dur":2000,"c":[{"dur":770,"delay":1230,"accel":1.0,"t":"effect","tr":"out","filter":"fade"},{"dur":770,"delay":1230,"accel":1.0,"t":"scale","from":[2.0,4.5],"to":[0.1,0.1]},{"dur":1230,"decel":1.0,"t":"scale","from":[1.0,1.0],"to":[2.0,4.5]},{"dur":1230,"decel":1.0,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_x)","to":"(0.5)"},{"dur":770,"delay":1230,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","from":"(0.5)","to":"(0.5)"},{"dur":1230,"decel":1.0,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_y)","to":"(ppt_y+0.4)"},{"dur":770,"delay":1230,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_y)","to":"(ppt_y)"},{"dur":1,"delay":1999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.arcUp":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"decel":0.5,"fill":"hold","t":"scale","from":[2.5,2.5],"to":[1.0,1.0]},{"dur":1000,"decel":0.5,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M -0.46736 0.92887  C -0.37517 0.88508  -0.02552 0.75279  0.0908 0.66613  C  0.20747 0.57948  0.21649 0.50394  0.23177 0.40825  C 0.24705 0.31256  0.22118 0.15964   0.18264 0.09152  C 0.1441 0.02341  0.03802 0.0  0.0 0.0  ","origin":"layout"},{"dur":1000,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.arcUp":{"dur":1000,"c":[{"dur":1000,"accel":0.5,"t":"scale","from":[1.0,1.0],"to":[2.5,2.5]},{"dur":1000,"accel":0.5,"attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0.0000 0.0000 C 0.03802 0.0 0.1441 0.02341 0.1826 0.0915 C 0.22118 0.15964 0.24705 0.31256 0.2318 0.4083 C 0.21649 0.50394 0.20747 0.57948 0.0908 0.6661 C -0.02552 0.75279 -0.37517 0.88508 -0.4674 0.9289","origin":"layout"},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.fadedZoom.in":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":500,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"enter.fadedZoom.in_center":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.0],[1.0,"#ppt_h"]]},{"dur":500,"t":"effect","tr":"in","filter":"fade"},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,0.5],[1.0,"#ppt_y"]]}],"bg":1},"exit.fadedZoom.out":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"exit.fadedZoom.out_center":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,0.0]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,0.0]]},{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,0.5]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,0.5]]},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.glide":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w*0.05"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x-.2"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_y"],[1.0,"#ppt_y"]]},{"dur":500,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.glide":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w*0.05"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x-.2"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_y"]]},{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.expand":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":1000,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w*0.70"],[1.0,"#ppt_w"]]},{"dur":1000,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h"],[1.0,"#ppt_h"]]},{"dur":1000,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.expand":{"dur":1000,"c":[{"dur":1000,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w*0.70"]]},{"dur":1000,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h"]]},{"dur":1000,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"enter.flip":{"dur":1000,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"autoRev":true,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","by":"(-#ppt_w*2)"},{"dur":500,"decel":0.5,"autoRev":true,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","by":"(#ppt_w*0.50)"},{"dur":1000,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","from":"(-#ppt_h/2)","to":"(#ppt_y)"},{"dur":1000,"fill":"hold","attr":["r"],"t":"rot","by":360.0}],"it":{"type":"lt","pct":0.1},"bg":1},"exit.flip":{"dur":1000,"c":[{"dur":500,"autoRev":true,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_w)","to":"(-ppt_w*2)"},{"dur":500,"decel":0.5,"autoRev":true,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","by":"(ppt_w*0.50)"},{"dur":1000,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","from":"(ppt_y)","to":"(1+ppt_h/2)"},{"dur":1000,"attr":["r"],"t":"rot","by":360.0},{"dur":1,"delay":999,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"it":{"type":"lt","pct":0.1},"bg":1},"emphasis.shimmer":{"dur":500,"c":[{"dur":250,"autoRev":true,"fill":"hold","t":"scale","to":[0.8,1.0]},{"dur":250,"autoRev":true,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","by":"(#ppt_w*0.10)"},{"dur":250,"autoRev":true,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","by":"(-#ppt_w*0.10)"},{"dur":250,"autoRev":true,"fill":"hold","attr":["r"],"t":"rot","by":-8.0}],"it":{"type":"lt","pct":0.1},"bg":1},"enter.fold":{"dur":500,"c":[{"dur":1,"fill":"hold","attr":["style.visibility"],"t":"set","to":"visible"},{"dur":500,"fill":"hold","attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_w*2.5"],[1.0,"#ppt_w"]]},{"dur":500,"fill":"hold","attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h*0.01"],[1.0,"#ppt_h"]]},{"dur":500,"fill":"hold","attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_x"],[1.0,"#ppt_x"]]},{"dur":500,"fill":"hold","attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"#ppt_h+1"],[1.0,"#ppt_y"]]},{"dur":500,"t":"effect","tr":"in","filter":"fade"}],"bg":1},"exit.fold":{"dur":500,"c":[{"dur":500,"attr":["ppt_w"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_w"],[1.0,"ppt_w*2.5"]]},{"dur":500,"attr":["ppt_h"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_h"],[1.0,"ppt_h*0.01"]]},{"dur":500,"attr":["ppt_x"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_x"],[1.0,"ppt_x"]]},{"dur":500,"attr":["ppt_y"],"t":"anim","calc":"lin","vt":"num","tav":[[0.0,"ppt_y"],[1.0,"ppt_h+1"]]},{"dur":500,"t":"effect","tr":"out","filter":"fade"},{"dur":1,"delay":499,"fill":"hold","attr":["style.visibility"],"t":"set","to":"hidden"}],"bg":1},"emphasis.changeFillColor":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":2000,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"},{"dur":2000,"fill":"hold","attr":["fill.on"],"t":"set","to":"true"}]},"emphasis.changeFont":{"dur":-1,"c":[{"dur":null,"attr":["style.fontFamily"],"override":"childStyle","t":"set","to":"바탕"}]},"emphasis.changeFontColor":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}}]},"emphasis.changeFontSize":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["style.fontSize"],"override":"childStyle","t":"anim","calc":"lin","vt":"num","to":"1.5"}]},"emphasis.changeFontStyle":{"dur":-1,"c":[{"dur":null,"attr":["style.fontStyle"],"override":"childStyle","t":"set","to":"normal"},{"dur":null,"attr":["style.fontWeight"],"override":"childStyle","t":"set","to":"bold"},{"dur":null,"attr":["style.textDecorationUnderline"],"override":"childStyle","t":"set","to":"false"}]},"emphasis.growShrink":{"dur":2000,"c":[{"dur":2000,"fill":"hold","t":"scale","by":[1.5,1.5]}],"bg":1},"emphasis.changeLineColor":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":2000,"fill":"hold","attr":["stroke.on"],"t":"set","to":"true"}]},"emphasis.spin":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["r"],"t":"rot","by":360.0}],"bg":1},"emphasis.transparency":{"dur":-1,"c":[{"dur":null,"attr":["style.opacity"],"t":"set","to":"0.5"},{"dur":null,"t":"effect","tr":"in","filter":"image","pr":"opacity: 0.5"}],"bg":1},"emphasis.boldFlash":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["style.fontWeight"],"override":"childStyle","t":"anim","calc":"discrete","vt":"str","tav":[[0.0,"normal"],[0.5,"bold"],[0.6,"normal"],[1.0,"normal"]]}]},"emphasis.blast":{"dur":2000,"c":[{"dur":1900,"delay":100,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":1900,"delay":100,"fill":"hold","attr":["fillColor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":1900,"delay":100,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"},{"dur":1900,"delay":100,"fill":"hold","attr":["fill.on"],"t":"set","to":"true"},{"dur":200,"fill":"hold","t":"scale","from":[1.0,1.0],"to":[1.0,0.05]},{"dur":200,"delay":200,"fill":"hold","t":"scale","from":[1.0,0.05],"to":[1.2,1.5]},{"dur":600,"delay":1400,"fill":"hold","t":"scale","to":[1.2,1.5]}],"bg":1},"emphasis.boldReveal":{"dur":-1,"c":[{"dur":null,"attr":["style.fontWeight"],"override":"childStyle","t":"set","to":"bold"}],"it":{"type":"lt","ms":25}},"emphasis.brushOnColor":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"set","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"set","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"it":{"type":"lt","pct":0.04},"bg":1},"emphasis.brushOnUnderline":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.textDecorationUnderline"],"override":"childStyle","t":"set","to":"true"}],"it":{"type":"lt","pct":0.04}},"emphasis.colorBlend":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"},{"dur":500,"fill":"hold","attr":["fill.on"],"t":"set","to":"true"}],"bg":1},"emphasis.colorWave":{"dur":1000,"c":[{"dur":500,"autoRev":true,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"set","to":{"scheme":"accent2"}},{"dur":500,"autoRev":true,"fill":"hold","attr":["fillcolor"],"t":"set","to":{"scheme":"accent2"}},{"dur":500,"autoRev":true,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"it":{"type":"lt","pct":0.1},"bg":1},"emphasis.complementaryColor":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.complementaryColor2":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":-120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":-120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":-120.0,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.contrastingColor":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":180.70588333333333,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":180.70588333333333,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":180.70588333333333,"s":0.0,"l":0.0}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.darken":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-12.549,"l":-25.098}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-12.549,"l":-25.098}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-12.549,"l":-25.098}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.desaturate":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-70.588,"l":0.0}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-70.588,"l":0.0}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":-70.588,"l":0.0}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.flashBulb":{"dur":500,"c":[{"dur":500,"tmFilter":[[0.0,0.0],[0.2,0.5],[0.8,0.5],[1.0,0.0]],"t":"effect","tr":"out","filter":"fade"},{"dur":250,"autoRev":true,"fill":"hold","t":"scale","by":[1.05,1.05]}],"bg":1},"emphasis.flicker":{"dur":500,"c":[{"dur":250,"autoRev":true,"fill":"remove","attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"bg1"}},{"dur":250,"autoRev":true,"fill":"remove","attr":["fillcolor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"bg1"}},{"dur":250,"autoRev":true,"fill":"remove","attr":["fill.type"],"t":"set","to":"solid"},{"dur":250,"autoRev":true,"fill":"remove","attr":["fill.on"],"t":"set","to":"true"}],"bg":1},"emphasis.growWithColor":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"},{"dur":500,"fill":"hold","attr":["style.fontSize"],"override":"childStyle","t":"anim","calc":"lin","vt":"num","to":"1.5"}],"it":{"type":"lt","pct":0.1}},"emphasis.lighten":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":12.549,"l":25.098}},{"dur":500,"fill":"hold","attr":["fillcolor"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":12.549,"l":25.098}},{"dur":500,"fill":"hold","attr":["stroke.color"],"t":"clr","space":"hsl","dir":"cw","by":{"h":0.0,"s":12.549,"l":25.098}},{"dur":500,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"}],"bg":1},"emphasis.styleEmphasis":{"dur":500,"c":[{"dur":500,"fill":"hold","attr":["style.color"],"override":"childStyle","t":"set","to":{"scheme":"accent2"}},{"dur":500,"fill":"hold","attr":["style.fontStyle"],"override":"childStyle","t":"set","to":"italic"},{"dur":500,"fill":"hold","attr":["style.fontWeight"],"t":"set","to":"bold"},{"dur":500,"fill":"hold","attr":["style.textDecorationUnderline"],"t":"set","to":"true"}]},"emphasis.teeter":{"dur":1000,"c":[{"dur":100,"fill":"hold","attr":["r"],"t":"rot","by":2.0},{"dur":200,"delay":200,"fill":"hold","attr":["r"],"t":"rot","by":-4.0},{"dur":200,"delay":400,"fill":"hold","attr":["r"],"t":"rot","by":4.0},{"dur":200,"delay":600,"fill":"hold","attr":["r"],"t":"rot","by":-4.0},{"dur":200,"delay":800,"fill":"hold","attr":["r"],"t":"rot","by":2.0}],"bg":1},"emphasis.verticalGrow":{"dur":3000,"c":[{"dur":1500,"accel":0.5,"autoRev":true,"fill":"hold","tmFilter":[[0.0,0.0],[0.33333,1.0],[1.0,1.0]],"attr":["style.color"],"override":"childStyle","t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":1500,"accel":0.5,"autoRev":true,"fill":"hold","tmFilter":[[0.0,0.0],[0.33333,1.0],[1.0,1.0]],"attr":["fillcolor"],"t":"clr","space":"rgb","dir":"cw","to":{"scheme":"accent2"}},{"dur":3000,"fill":"hold","attr":["fill.type"],"t":"set","to":"solid"},{"dur":3000,"fill":"hold","attr":["fill.on"],"t":"set","to":"true"},{"dur":1500,"accel":0.5,"autoRev":true,"fill":"hold","tmFilter":[[0.0,0.0],[0.33333,1.0],[1.0,1.0]],"t":"scale","from":[1.0,1.0],"to":[1.0,1.4]}],"bg":1},"emphasis.wave":{"dur":500,"c":[{"dur":250,"accel":0.5,"decel":0.5,"autoRev":true,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0.0 0.0 L 0.0 -0.07213","origin":"layout"},{"dur":125,"fill":"hold","attr":["r"],"t":"rot","by":25.0},{"dur":125,"delay":125,"fill":"hold","attr":["r"],"t":"rot","by":-25.0},{"dur":125,"delay":250,"fill":"hold","attr":["r"],"t":"rot","by":-25.0},{"dur":125,"delay":375,"fill":"hold","attr":["r"],"t":"rot","by":25.0}],"it":{"type":"lt","pct":0.1},"bg":1},"move.circle":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.069 0 0.125 0.056 0.125 0.125 C 0.125 0.194 0.069 0.25 0 0.25 C -0.069 0.25 -0.125 0.194 -0.125 0.125 C -0.125 0.056 -0.069 0 0 0 Z","origin":"layout"}],"bg":1},"move.rightTriangle":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0 -0.147 L 0.25 0 L 0 0 Z","origin":"layout"}],"bg":1},"move.diamond":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 -0.084 L 0.25 0 L 0.125 0.084 L 0 0 Z","origin":"layout"}],"bg":1},"move.hexagon":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 0 L 0.188 0.109 L 0.125 0.217 L 0 0.217 L -0.063 0.109 L 0 0 Z","origin":"layout"}],"bg":1},"move.fivePointStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.029 0.091 L 0.125 0.091 L 0.048 0.147 L 0.077 0.238 L 0 0.182 L -0.077 0.238 L -0.048 0.147 L -0.125 0.091 L -0.029 0.091 L 0 0 Z","origin":"layout"}],"bg":1},"move.crescentMoon":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.014 -0.005 -0.029 -0.009 -0.044 -0.009 C -0.114 -0.009 -0.169 0.048 -0.169 0.117 C -0.169 0.185 -0.114 0.241 -0.044 0.241 C -0.029 0.241 -0.014 0.238 0 0.233 C -0.047 0.215 -0.08 0.17 -0.08 0.117 C -0.08 0.063 -0.047 0.018 0 0 Z","origin":"layout"}],"bg":1},"move.square":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.25 0 L 0.25 0.25 L 0 0.25 L 0 0 Z","origin":"layout"}],"bg":1},"move.trapezoid":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.167 0 L 0.21 0.167 L -0.04 0.167 L 0 0 Z","origin":"layout"}],"bg":1},"move.heart":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.012 -0.018 0.033 -0.044 0.058 -0.044 C 0.095 -0.044 0.125 -0.017 0.125 0.017 C 0.125 0.028 0.122 0.038 0.116 0.047 C 0.117 0.047 0 0.182 0 0.183 C 0 0.182 -0.117 0.047 -0.116 0.047 C -0.122 0.038 -0.125 0.028 -0.125 0.017 C -0.125 -0.017 -0.095 -0.044 -0.057 -0.044 C -0.033 -0.044 -0.012 -0.018 0 0 Z","origin":"layout"}],"bg":1},"move.octagon":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.073 -0.073 L 0.177 -0.073 L 0.25 0 L 0.25 0.104 L 0.177 0.177 L 0.073 0.177 L 0 0.104 L 0 0 Z","origin":"layout"}],"bg":1},"move.sixPointStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.036 0.062 L 0.108 0.062 L 0.072 0.125 L 0.108 0.187 L 0.036 0.187 L 0 0.25 L -0.036 0.187 L -0.108 0.187 L -0.072 0.125 L -0.108 0.062 L -0.036 0.062 L 0 0 Z","origin":"layout"}],"bg":1},"move.football":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.03 -0.038 0.075 -0.062 0.125 -0.062 C 0.175 -0.062 0.22 -0.038 0.25 0 C 0.22 0.038 0.175 0.062 0.125 0.062 C 0.075 0.062 0.03 0.038 0 0 Z","origin":"layout"}],"bg":1},"move.equalTriangle":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 0.216 L -0.125 0.216 L 0 0 Z","origin":"layout"}],"bg":1},"move.parallelogram":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.178 0 L 0.25 0.121 L 0.072 0.121 L 0 0 Z","origin":"layout"}],"bg":1},"move.pentagon":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 0.091 L 0.077 0.238 L -0.077 0.238 L -0.125 0.091 L 0 0 Z","origin":"layout"}],"bg":1},"move.fourPointStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.091 -0.034 L 0.125 -0.125 L 0.158 -0.034 L 0.249 0 L 0.158 0.034 L 0.125 0.125 L 0.091 0.034 L 0 0 Z","origin":"layout"}],"bg":1},"move.eightPointStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.052 0 L 0.089 -0.037 L 0.125 0 L 0.177 0 L 0.177 0.052 L 0.213 0.089 L 0.177 0.125 L 0.177 0.177 L 0.125 0.177 L 0.089 0.213 L 0.052 0.177 L 0 0.177 L 0 0.125 L -0.037 0.089 L 0 0.052 L 0 0 Z","origin":"layout"}],"bg":1},"move.teardrop":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.001 0.034 0.011 0.065 0.028 0.085 C 0.028 0.086 0.055 0.113 0.055 0.112 C 0.07 0.127 0.079 0.148 0.079 0.17 C 0.079 0.214 0.044 0.249 0 0.25 C -0.044 0.249 -0.079 0.214 -0.079 0.17 C -0.079 0.148 -0.07 0.127 -0.055 0.112 C -0.055 0.113 -0.028 0.086 -0.028 0.085 C -0.011 0.065 -0.001 0.034 0 0 Z","origin":"layout"}],"bg":1},"move.pointyStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.069 0 0.124 -0.056 0.124 -0.125 C 0.124 -0.056 0.179 -0.001 0.248 -0.001 C 0.179 -0.001 0.125 0.056 0.125 0.125 C 0.125 0.056 0.069 0 0 0 Z","origin":"layout"}],"bg":1},"move.curvedSquare":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0 -0.032 0.026 -0.058 0.058 -0.058 L 0.192 -0.058 C 0.224 -0.058 0.25 -0.032 0.25 0 L 0.25 0.132 C 0.25 0.164 0.224 0.191 0.192 0.191 L 0.058 0.191 C 0.026 0.191 0 0.164 0 0.132 Z","origin":"layout"}],"bg":1},"move.curvedX":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.006 0.006 0.011 0.011 0.015 0.017 C 0.02 0.011 0.024 0.006 0.03 0 C 0.065 -0.035 0.107 -0.05 0.124 -0.034 C 0.14 -0.017 0.125 0.025 0.09 0.06 C 0.084 0.065 0.079 0.07 0.073 0.075 C 0.079 0.079 0.084 0.084 0.09 0.09 C 0.125 0.125 0.14 0.167 0.124 0.183 C 0.107 0.2 0.065 0.185 0.03 0.15 C 0.024 0.144 0.02 0.139 0.015 0.133 C 0.011 0.139 0.006 0.144 0 0.15 C -0.035 0.185 -0.077 0.2 -0.094 0.183 C -0.11 0.167 -0.095 0.125 -0.06 0.09 C -0.054 0.084 -0.049 0.079 -0.043 0.075 C -0.049 0.07 -0.054 0.065 -0.06 0.06 C -0.095 0.025 -0.11 -0.017 -0.094 -0.034 C -0.077 -0.05 -0.035 -0.035 0 0 Z","origin":"layout"}],"bg":1},"move.verticalFigure8":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.033 0 0.06 0.027 0.06 0.06 C 0.06 0.099 0.03 0.113 0.012 0.119 L -0.012 0.125 C -0.03 0.131 -0.06 0.146 -0.06 0.19 C -0.06 0.218 -0.033 0.25 0 0.25 C 0.033 0.25 0.06 0.218 0.06 0.19 C 0.06 0.146 0.03 0.131 0.012 0.125 L -0.012 0.119 C -0.03 0.113 -0.06 0.099 -0.06 0.06 C -0.06 0.027 -0.033 0 0 0 Z","origin":"layout"}],"bg":1},"move.curvyStar":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.072 0.058 0.1 0.152 0.077 0.238 C -0.015 0.233 -0.093 0.173 -0.125 0.091 C -0.047 0.04 0.051 0.043 0.125 0.091 C 0.092 0.178 0.011 0.233 -0.077 0.238 C -0.101 0.148 -0.068 0.056 0 0 Z","origin":"layout"}],"bg":1},"move.loopdeLoop":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.023 0.001 0.042 0.009 0.052 0.021 L 0.075 0.049 C 0.08 0.055 0.088 0.058 0.098 0.058 C 0.112 0.058 0.124 0.05 0.125 0.038 C 0.124 0.028 0.112 0.019 0.098 0.019 C 0.088 0.019 0.08 0.023 0.075 0.028 L 0.052 0.056 C 0.042 0.068 0.023 0.076 0 0.077 C -0.023 0.076 -0.042 0.068 -0.052 0.056 L -0.075 0.028 C -0.08 0.023 -0.088 0.019 -0.098 0.019 C -0.112 0.019 -0.124 0.028 -0.125 0.038 C -0.124 0.05 -0.112 0.058 -0.098 0.058 C -0.088 0.058 -0.08 0.055 -0.075 0.049 L -0.052 0.021 C -0.042 0.009 -0.023 0.001 0 0 Z","origin":"layout"}],"bg":1},"move.buzzsaw":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.022 -0.017 -0.033 -0.046 -0.027 -0.075 C -0.024 -0.085 -0.02 -0.095 -0.014 -0.103 C -0.01 -0.08 0.004 -0.059 0.025 -0.046 C 0.025 -0.074 0.041 -0.101 0.068 -0.113 C 0.077 -0.118 0.087 -0.12 0.097 -0.121 C 0.082 -0.104 0.074 -0.08 0.077 -0.055 C 0.099 -0.073 0.13 -0.077 0.157 -0.064 C 0.166 -0.06 0.175 -0.053 0.181 -0.046 C 0.158 -0.048 0.134 -0.039 0.117 -0.021 C 0.144 -0.015 0.167 0.006 0.174 0.035 C 0.176 0.045 0.176 0.055 0.174 0.065 C 0.161 0.046 0.139 0.033 0.115 0.031 C 0.127 0.056 0.124 0.087 0.106 0.11 C 0.099 0.118 0.091 0.125 0.082 0.129 C 0.089 0.107 0.085 0.082 0.072 0.062 C 0.06 0.087 0.034 0.104 0.004 0.104 C -0.007 0.104 -0.017 0.102 -0.026 0.098 C -0.004 0.09 0.013 0.071 0.021 0.048 C -0.007 0.054 -0.036 0.045 -0.055 0.022 C -0.062 0.013 -0.066 0.004 -0.069 -0.006 C -0.049 0.007 -0.023 0.009 0 0 Z","origin":"layout"}],"bg":1},"move.horizontalFigure8":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0 0.033 0.027 0.06 0.06 0.06 C 0.099 0.06 0.113 0.03 0.119 0.012 L 0.125 -0.012 C 0.131 -0.03 0.146 -0.06 0.19 -0.06 C 0.218 -0.06 0.25 -0.033 0.25 0 C 0.25 0.033 0.218 0.06 0.19 0.06 C 0.146 0.06 0.131 0.03 0.125 0.012 L 0.119 -0.012 C 0.113 -0.03 0.099 -0.06 0.06 -0.06 C 0.027 -0.06 0 -0.033 0 0 Z","origin":"layout"}],"bg":1},"move.peanut":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.038 0 0.069 0.031 0.069 0.069 C 0.069 0.094 0.056 0.116 0.037 0.129 C 0.037 0.129 0.036 0.129 0.036 0.129 C 0.029 0.134 0.025 0.142 0.025 0.151 C 0.025 0.159 0.029 0.166 0.034 0.171 C 0.042 0.179 0.047 0.191 0.047 0.203 C 0.047 0.229 0.026 0.25 0 0.25 C -0.026 0.25 -0.047 0.229 -0.047 0.203 C -0.047 0.191 -0.042 0.179 -0.034 0.171 C -0.029 0.166 -0.026 0.159 -0.026 0.151 C -0.026 0.142 -0.03 0.134 -0.036 0.129 C -0.036 0.129 -0.037 0.129 -0.037 0.129 C -0.057 0.116 -0.07 0.094 -0.07 0.069 C -0.07 0.031 -0.039 0 0 0 C 0 0 0 0 0 0 Z","origin":"layout"}],"bg":1},"move.figure8Four":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.017 0 0.031 0.014 0.031 0.031 C 0.031 0.049 0.017 0.063 0 0.063 C -0.017 0.063 -0.031 0.077 -0.031 0.094 C -0.031 0.111 -0.017 0.125 0 0.125 C 0.017 0.125 0.031 0.139 0.031 0.156 C 0.031 0.173 0.017 0.187 0 0.187 C -0.017 0.187 -0.031 0.201 -0.031 0.219 C -0.031 0.236 -0.017 0.25 0 0.25 C 0.017 0.25 0.031 0.236 0.031 0.219 C 0.031 0.201 0.017 0.187 0 0.187 C -0.017 0.187 -0.031 0.173 -0.031 0.156 C -0.031 0.139 -0.017 0.125 0 0.125 C 0.017 0.125 0.031 0.111 0.031 0.094 C 0.031 0.077 0.017 0.063 0 0.063 C -0.017 0.063 -0.031 0.049 -0.031 0.031 C -0.031 0.014 -0.017 0 0 0 Z","origin":"layout"}],"bg":1},"move.neutron":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.007 -0.01 0.014 -0.021 0.021 -0.035 C 0.04 -0.075 0.045 -0.114 0.031 -0.12 C 0.017 -0.127 -0.01 -0.099 -0.029 -0.059 C -0.039 -0.038 -0.045 -0.018 -0.047 -0.003 C -0.05 0.009 -0.051 0.021 -0.051 0.035 C -0.051 0.08 -0.038 0.117 -0.023 0.117 C -0.008 0.117 0.005 0.08 0.005 0.035 C 0.005 0.014 0.002 -0.006 -0.003 -0.02 C -0.005 -0.032 -0.01 -0.045 -0.016 -0.058 C -0.036 -0.099 -0.063 -0.127 -0.077 -0.12 C -0.091 -0.113 -0.086 -0.075 -0.066 -0.034 C -0.058 -0.015 -0.047 0.001 -0.036 0.012 C -0.028 0.022 -0.019 0.031 -0.007 0.04 C 0.029 0.069 0.065 0.082 0.075 0.07 C 0.084 0.058 0.064 0.025 0.028 -0.003 C 0.013 -0.015 -0.003 -0.024 -0.016 -0.03 C -0.028 -0.036 -0.043 -0.041 -0.059 -0.044 C -0.103 -0.054 -0.141 -0.051 -0.144 -0.035 C -0.148 -0.02 -0.115 0 -0.071 0.01 C -0.051 0.014 -0.032 0.016 -0.017 0.015 C -0.004 0.015 0.01 0.013 0.025 0.01 C 0.069 0 0.102 -0.021 0.098 -0.036 C 0.095 -0.051 0.057 -0.055 0.013 -0.045 C -0.008 -0.04 -0.027 -0.033 -0.04 -0.025 C -0.051 -0.019 -0.062 -0.012 -0.074 -0.003 C -0.109 0.026 -0.13 0.058 -0.12 0.07 C -0.111 0.082 -0.074 0.069 -0.039 0.041 C -0.022 0.027 -0.008 0.013 0 0 Z","origin":"layout"}],"bg":1},"move.swoosh":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0 0 0.017 -0.065 0.017 -0.065 C 0.034 -0.118 0.061 -0.139 0.1 -0.139 C 0.12 -0.139 0.138 -0.131 0.152 -0.118 C 0.162 -0.109 0.174 -0.104 0.187 -0.104 C 0.212 -0.104 0.233 -0.122 0.241 -0.148 C 0.241 -0.148 0.25 -0.179 0.25 -0.179 C 0.25 -0.179 0.232 -0.113 0.232 -0.113 C 0.215 -0.061 0.188 -0.04 0.15 -0.04 C 0.13 -0.04 0.111 -0.048 0.096 -0.062 C 0.087 -0.07 0.075 -0.075 0.063 -0.075 C 0.038 -0.075 0.017 -0.057 0.009 -0.031 C 0.009 -0.031 0 0 0 0 Z","origin":"layout"}],"bg":1},"move.bean":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.002 -0.003 0.012 -0.034 0.037 -0.032 C 0.075 -0.029 0.09 -0.007 0.125 -0.029 C 0.147 -0.042 0.173 -0.075 0.192 -0.074 C 0.235 -0.073 0.244 -0.039 0.244 -0.008 C 0.245 0.036 0.189 0.073 0.121 0.077 C 0.052 0.08 -0.005 0.033 0 0 Z","origin":"layout"}],"bg":1},"move.plus":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.118 -0.118 0.132 -0.118 0.011 0 C 0.132 -0.118 0.132 0.132 0.011 0.011 C 0.132 0.132 -0.118 0.132 0 0.011 C -0.118 0.132 -0.118 -0.118 0 0 Z","origin":"layout"}],"bg":1},"move.invertedTriangle":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.015 0.024 0.037 0.049 0.055 0.059 C 0.082 0.075 0.108 0.081 0.113 0.073 C 0.117 0.065 0.099 0.045 0.072 0.029 C 0.054 0.019 0.021 0.012 -0.008 0.011 C -0.036 0.012 -0.07 0.019 -0.088 0.029 C -0.115 0.045 -0.133 0.065 -0.128 0.073 C -0.123 0.081 -0.097 0.075 -0.071 0.059 C -0.053 0.049 -0.03 0.024 -0.016 0 C -0.001 -0.025 0.009 -0.058 0.009 -0.079 C 0.009 -0.111 0.002 -0.136 -0.008 -0.136 C -0.017 -0.136 -0.025 -0.111 -0.025 -0.079 C -0.025 -0.058 -0.014 -0.025 0 0 Z","origin":"layout"}],"bg":1},"move.invertedSquare":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.004 -0.004 0.01 -0.006 0.015 -0.006 C 0.022 -0.006 0.029 -0.003 0.033 0.002 C 0.05 0.022 0.063 0.066 0.063 0.118 C 0.063 0.118 0.063 0.119 0.063 0.119 C 0.063 0.119 0.063 0.12 0.063 0.12 C 0.063 0.172 0.05 0.217 0.033 0.237 C 0.029 0.241 0.022 0.244 0.015 0.244 C 0.01 0.244 0.004 0.242 0 0.238 C -0.004 0.234 -0.006 0.229 -0.006 0.223 C -0.006 0.216 -0.003 0.21 0.002 0.206 C 0.022 0.188 0.066 0.175 0.118 0.175 C 0.118 0.175 0.119 0.175 0.119 0.175 C 0.119 0.175 0.12 0.175 0.12 0.175 C 0.172 0.175 0.217 0.188 0.237 0.206 C 0.241 0.21 0.244 0.216 0.244 0.223 C 0.244 0.229 0.242 0.234 0.238 0.238 C 0.234 0.242 0.229 0.244 0.223 0.244 C 0.216 0.244 0.21 0.241 0.206 0.237 C 0.188 0.217 0.175 0.172 0.175 0.12 C 0.175 0.12 0.175 0.119 0.175 0.119 C 0.175 0.119 0.175 0.118 0.175 0.118 C 0.175 0.066 0.188 0.022 0.206 0.001 C 0.21 -0.003 0.216 -0.006 0.223 -0.006 C 0.229 -0.006 0.234 -0.004 0.238 0 C 0.242 0.004 0.244 0.01 0.244 0.015 C 0.244 0.022 0.241 0.028 0.237 0.033 C 0.217 0.05 0.172 0.063 0.12 0.063 C 0.12 0.063 0.12 0.063 0.119 0.063 C 0.119 0.063 0.118 0.063 0.118 0.063 C 0.066 0.063 0.022 0.05 0.002 0.033 C -0.003 0.028 -0.006 0.022 -0.006 0.015 C -0.006 0.01 -0.004 0.004 0 0 Z","origin":"layout"}],"bg":1},"move.left":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L -0.25 0 E","origin":"layout"}],"bg":1},"move.turnRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0 0.125 C 0 0.181 0.069 0.25 0.125 0.25 L 0.25 0.25 E","origin":"layout"}],"bg":1},"move.arcDown":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.067 0.04 C 0.081 0.049 0.102 0.054 0.124 0.054 C 0.149 0.054 0.169 0.049 0.183 0.04 L 0.25 0 E","origin":"layout"}],"bg":1},"move.zigzag":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.016 0.099 L 0.031 0 L 0.047 0.099 L 0.063 0 L 0.078 0.099 L 0.094 0 L 0.109 0.099 L 0.125 0 L 0.141 0.099 L 0.156 0 L 0.172 0.099 L 0.187 0 L 0.203 0.099 L 0.219 0 L 0.234 0.099 L 0.25 0 E","origin":"layout"}],"bg":1},"move.sCurve2":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0 0.035 0.028 0.062 0.062 0.062 C 0.097 0.062 0.125 0.035 0.125 0 C 0.125 -0.035 0.153 -0.062 0.188 -0.062 C 0.222 -0.062 0.25 -0.035 0.25 0 E","origin":"layout"}],"bg":1},"move.sineWave":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.003 -0.019 0.007 -0.037 0.015 -0.037 C 0.024 -0.037 0.027 -0.019 0.03 0 C 0.034 0.021 0.037 0.042 0.047 0.042 C 0.056 0.042 0.059 0.021 0.063 0 C 0.065 -0.019 0.069 -0.037 0.078 -0.037 C 0.086 -0.037 0.09 -0.019 0.093 0 C 0.096 0.021 0.1 0.042 0.109 0.042 C 0.118 0.042 0.125 0 0.125 0 C 0.128 -0.019 0.131 -0.037 0.14 -0.037 C 0.149 -0.037 0.152 -0.019 0.155 0 C 0.159 0.021 0.162 0.042 0.172 0.042 C 0.181 0.042 0.184 0.021 0.187 0 C 0.191 -0.019 0.194 -0.037 0.203 -0.037 C 0.211 -0.037 0.215 -0.019 0.218 0 C 0.221 0.021 0.225 0.042 0.234 0.042 C 0.243 0.042 0.246 0.021 0.25 0 E","origin":"layout"}],"bg":1},"move.bounceLeft":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 c -0.004 -0.008 -0.018 -0.016 -0.023 -0.016 c -0.031 0 -0.063 0.125 -0.063 0.25 c 0 -0.063 -0.016 -0.125 -0.031 -0.125 c -0.016 0 -0.031 0.063 -0.031 0.125 c 0 -0.031 -0.008 -0.063 -0.016 -0.063 c -0.008 0 -0.016 0.031 -0.016 0.063 c 0 -0.016 -0.004 -0.031 -0.008 -0.031 c -0.004 0 -0.008 0.016 -0.008 0.031 c 0 -0.008 -0.002 -0.016 -0.004 -0.016 c -0.001 0 -0.004 0.008 -0.004 0.016 c 0 -0.004 -0.001 -0.008 -0.002 -0.008 c 0 -0.001 -0.002 0.004 -0.002 0.008 c 0 -0.002 0 -0.004 -0.001 -0.004 c 0 0.001 -0.001 0.002 -0.001 0.004 c 0 -0.001 0 -0.002 0 -0.003 c -0.001 0 -0.001 0.001 -0.001 0.002 c -0.001 0 -0.001 -0.001 -0.001 -0.002 c -0.001 0 -0.001 0.001 -0.001 0.002 E","origin":"layout"}],"bg":1},"move.down":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0 0.25 E","origin":"layout"}],"bg":1},"move.turnUp":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 0 C 0.181 0 0.25 -0.069 0.25 -0.125 L 0.25 -0.25 E","origin":"layout"}],"bg":1},"move.arcUp":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.067 -0.04 C 0.081 -0.049 0.102 -0.054 0.124 -0.054 C 0.149 -0.054 0.169 -0.049 0.183 -0.04 L 0.25 0 E","origin":"layout"}],"bg":1},"move.heartbeat":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.017 0 C 0.025 0 0.034 -0.014 0.042 -0.016 C 0.048 -0.016 0.059 -0.003 0.064 -0.003 C 0.071 -0.003 0.078 -0.007 0.091 -0.007 L 0.1 -0.162 L 0.11 0.025 L 0.122 0 L 0.132 -0.007 L 0.156 -0.001 C 0.167 -0.004 0.176 -0.017 0.187 -0.022 C 0.191 -0.023 0.2 -0.024 0.206 -0.022 C 0.212 -0.02 0.217 -0.006 0.219 -0.005 C 0.222 -0.001 0.229 -0.005 0.233 -0.003 L 0.239 0 L 0.25 0 E","origin":"layout"}],"bg":1},"move.spiralRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.004 -0.067 0.046 -0.125 0.113 -0.129 C 0.177 -0.134 0.237 -0.089 0.241 -0.024 C 0.246 0.036 0.204 0.092 0.144 0.096 C 0.089 0.099 0.037 0.062 0.033 0.006 C 0.029 -0.045 0.064 -0.093 0.115 -0.097 C 0.162 -0.1 0.206 -0.069 0.209 -0.022 C 0.212 0.02 0.184 0.061 0.142 0.063 C 0.104 0.066 0.068 0.042 0.065 0.004 C 0.063 -0.03 0.084 -0.063 0.117 -0.065 C 0.146 -0.067 0.175 -0.049 0.177 -0.02 C 0.179 0.005 0.164 0.029 0.14 0.031 C 0.12 0.033 0.099 0.022 0.098 0.002 C 0.096 -0.014 0.104 -0.031 0.119 -0.033 C 0.131 -0.033 0.143 -0.029 0.145 -0.018 C 0.146 -0.011 0.144 -0.004 0.138 -0.001 C 0.135 0 0.133 0 0.13 -0.001 E","origin":"layout"}],"bg":1},"move.wave":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.002 0.063 0.009 0.108 0.016 0.108 C 0.023 0.108 0.029 0.063 0.031 0 C 0.034 0.063 0.04 0.108 0.047 0.108 C 0.054 0.108 0.06 0.063 0.062 0 C 0.065 0.063 0.071 0.108 0.078 0.108 C 0.085 0.108 0.092 0.063 0.094 0 C 0.096 0.063 0.102 0.108 0.11 0.108 C 0.116 0.108 0.123 0.063 0.125 0 C 0.127 0.063 0.134 0.108 0.141 0.108 C 0.148 0.108 0.154 0.063 0.156 0 C 0.159 0.063 0.165 0.108 0.172 0.108 C 0.179 0.108 0.185 0.063 0.188 0 C 0.19 0.063 0.196 0.108 0.203 0.108 C 0.21 0.108 0.217 0.063 0.219 0 C 0.221 0.063 0.227 0.108 0.235 0.108 C 0.242 0.108 0.248 0.063 0.25 0 E","origin":"layout"}],"bg":1},"move.curvyLeft":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.008 0.008 0.017 0.016 0.021 0.026 C 0.025 0.037 0.027 0.05 0.029 0.063 C 0.031 0.076 0.029 0.087 0.027 0.099 C 0.025 0.11 0.022 0.122 0.015 0.132 C 0.009 0.142 -0.001 0.15 -0.012 0.156 C -0.022 0.162 -0.034 0.166 -0.046 0.168 C -0.058 0.17 -0.07 0.17 -0.081 0.168 C -0.093 0.166 -0.104 0.161 -0.113 0.153 C -0.122 0.146 -0.13 0.137 -0.134 0.126 C -0.139 0.116 -0.141 0.102 -0.141 0.091 C -0.142 0.08 -0.141 0.067 -0.136 0.056 C -0.131 0.046 -0.122 0.038 -0.11 0.034 C -0.098 0.031 -0.086 0.035 -0.078 0.042 C -0.071 0.049 -0.066 0.06 -0.065 0.073 C -0.065 0.086 -0.066 0.098 -0.071 0.108 C -0.076 0.118 -0.075 0.12 -0.095 0.133 C -0.113 0.147 -0.131 0.143 -0.142 0.144 C -0.153 0.144 -0.162 0.14 -0.173 0.136 C -0.185 0.131 -0.195 0.122 -0.202 0.114 C -0.209 0.106 -0.212 0.096 -0.216 0.08 C -0.219 0.064 -0.219 0.056 -0.219 0.044 C -0.219 0.032 -0.219 0.02 -0.219 0.008 E","origin":"layout"}],"bg":1},"move.diagonalDownRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.25 0.25 E","origin":"layout"}],"bg":1},"move.turnDown":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.125 0 C 0.181 0 0.25 0.069 0.25 0.125 L 0.25 0.25 E","origin":"layout"}],"bg":1},"move.arcLeft":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L -0.04 0.067 C -0.049 0.081 -0.054 0.102 -0.054 0.124 C -0.054 0.149 -0.049 0.169 -0.04 0.183 L 0 0.25 E","origin":"layout"}],"bg":1},"move.funnel":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.001 0.025 0.06 0.047 0.137 0.048 C 0.198 0.05 0.248 0.038 0.249 0.023 C 0.249 0.008 0.2 -0.006 0.138 -0.007 C 0.107 -0.007 0.079 -0.005 0.059 0 C 0.03 0.007 0.013 0.018 0.013 0.031 C 0.013 0.038 0.018 0.045 0.027 0.051 C 0.048 0.064 0.089 0.073 0.136 0.074 C 0.191 0.076 0.236 0.065 0.236 0.052 C 0.237 0.038 0.192 0.026 0.137 0.024 C 0.109 0.024 0.084 0.026 0.065 0.03 C 0.04 0.037 0.024 0.048 0.024 0.059 C 0.024 0.065 0.029 0.071 0.037 0.077 C 0.056 0.088 0.092 0.097 0.135 0.098 C 0.185 0.099 0.225 0.089 0.225 0.077 C 0.226 0.065 0.186 0.054 0.136 0.053 C 0.111 0.052 0.088 0.054 0.071 0.058 C 0.048 0.064 0.035 0.073 0.035 0.084 C 0.035 0.089 0.039 0.095 0.046 0.1 C 0.063 0.11 0.096 0.118 0.134 0.119 C 0.179 0.119 0.215 0.111 0.215 0.1 C 0.215 0.089 0.18 0.079 0.135 0.078 C 0.113 0.078 0.092 0.08 0.077 0.083 C 0.056 0.088 0.044 0.097 0.043 0.106 C 0.043 0.111 0.048 0.116 0.054 0.12 C 0.069 0.13 0.099 0.137 0.133 0.137 C 0.173 0.138 0.206 0.131 0.206 0.121 C 0.207 0.111 0.174 0.102 0.134 0.101 C 0.114 0.101 0.095 0.102 0.082 0.106 C 0.063 0.11 0.052 0.118 0.052 0.126 C 0.052 0.131 0.055 0.135 0.061 0.139 C 0.075 0.148 0.101 0.154 0.132 0.155 C 0.169 0.155 0.198 0.149 0.198 0.14 C 0.199 0.131 0.17 0.123 0.133 0.122 C 0.115 0.122 0.099 0.123 0.087 0.126 C 0.07 0.13 0.06 0.137 0.06 0.145 C 0.06 0.149 0.063 0.152 0.068 0.156 C 0.08 0.164 0.104 0.169 0.132 0.17 C 0.165 0.171 0.191 0.165 0.191 0.156 C 0.191 0.149 0.166 0.141 0.133 0.141 C 0.116 0.14 0.101 0.142 0.09 0.144 C 0.075 0.148 0.066 0.154 0.066 0.161 C 0.066 0.165 0.069 0.168 0.074 0.171 C 0.085 0.178 0.107 0.183 0.131 0.184 C 0.161 0.185 0.185 0.179 0.185 0.172 C 0.185 0.164 0.161 0.158 0.132 0.157 C 0.118 0.157 0.104 0.158 0.094 0.161 C 0.08 0.164 0.072 0.169 0.072 0.176 C 0.072 0.179 0.075 0.182 0.079 0.185 C 0.089 0.191 0.108 0.196 0.131 0.196 C 0.157 0.197 0.179 0.192 0.179 0.185 C 0.179 0.179 0.158 0.173 0.131 0.173 C 0.119 0.172 0.106 0.173 0.097 0.175 C 0.085 0.179 0.078 0.184 0.078 0.189 C 0.078 0.192 0.08 0.195 0.084 0.197 C 0.093 0.203 0.11 0.207 0.131 0.208 C 0.155 0.208 0.174 0.203 0.174 0.198 C 0.174 0.192 0.155 0.186 0.131 0.186 C 0.119 0.186 0.108 0.187 0.101 0.189 C 0.089 0.191 0.083 0.196 0.083 0.201 C 0.083 0.203 0.085 0.206 0.088 0.208 C 0.096 0.214 0.112 0.217 0.13 0.218 C 0.152 0.218 0.169 0.214 0.169 0.209 C 0.169 0.203 0.152 0.199 0.131 0.198 C 0.12 0.198 0.11 0.199 0.103 0.201 C 0.093 0.203 0.087 0.207 0.087 0.212 C 0.087 0.214 0.089 0.216 0.092 0.218 E","origin":"layout"}],"bg":1},"move.spring":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.066 0.006 -0.115 0.021 -0.115 0.033 C -0.115 0.044 -0.067 0.052 -0.003 0.052 C 0.061 0.052 0.115 0.044 0.115 0.033 C 0.115 0.021 0.059 0.018 -0.005 0.026 C -0.068 0.035 -0.115 0.05 -0.115 0.061 C -0.115 0.072 -0.066 0.081 -0.003 0.081 C 0.061 0.081 0.115 0.072 0.115 0.061 C 0.115 0.05 0.059 0.047 -0.004 0.055 C -0.068 0.063 -0.115 0.078 -0.115 0.089 C -0.115 0.101 -0.066 0.11 -0.002 0.11 C 0.061 0.11 0.115 0.101 0.115 0.089 C 0.115 0.079 0.059 0.076 -0.004 0.083 C -0.067 0.091 -0.115 0.107 -0.115 0.118 C -0.115 0.129 -0.065 0.138 -0.002 0.138 C 0.063 0.138 0.115 0.129 0.115 0.118 C 0.115 0.107 0.06 0.104 -0.003 0.112 C -0.066 0.12 -0.115 0.135 -0.115 0.146 C -0.115 0.158 -0.065 0.166 -0.001 0.166 C 0.063 0.166 0.115 0.157 0.115 0.146 C 0.115 0.135 0.06 0.132 -0.003 0.14 C -0.066 0.148 -0.115 0.164 -0.115 0.174 C -0.115 0.185 -0.064 0.194 -0.001 0.194 C 0.063 0.194 0.115 0.185 0.115 0.174 C 0.115 0.164 0.061 0.161 -0.003 0.168 C -0.066 0.176 -0.115 0.192 -0.115 0.203 C -0.115 0.213 -0.064 0.223 0 0.223 C 0.064 0.223 0.115 0.214 0.115 0.203 C 0.115 0.192 0.061 0.189 -0.002 0.197 C -0.065 0.205 -0.116 0.22 -0.115 0.231 C -0.114 0.242 -0.064 0.25 0 0.25 C 0.064 0.25 0.115 0.241 0.115 0.23 C 0.115 0.22 0.063 0.217 0 0.226 E","origin":"layout"}],"bg":1},"move.bounceRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 c 0.004 -0.008 0.018 -0.016 0.023 -0.016 c 0.031 0 0.063 0.125 0.063 0.25 c 0 -0.063 0.016 -0.125 0.031 -0.125 c 0.016 0 0.031 0.063 0.031 0.125 c 0 -0.031 0.008 -0.063 0.016 -0.063 c 0.008 0 0.016 0.031 0.016 0.063 c 0 -0.016 0.004 -0.031 0.008 -0.031 c 0.004 0 0.008 0.016 0.008 0.031 c 0 -0.008 0.002 -0.016 0.004 -0.016 c 0.001 0 0.004 0.008 0.004 0.016 c 0 -0.004 0.001 -0.008 0.002 -0.008 c 0 0.001 0.002 0.004 0.002 0.008 c 0 -0.002 0 -0.004 0.001 -0.004 c 0 0.001 0.001 0.002 0.001 0.004 c 0 -0.001 0 -0.002 0 -0.003 c 0.001 0 0.001 0.001 0.001 0.002 c 0.001 0 0.001 -0.001 0.001 -0.002 c 0.001 0 0.001 0.001 0.001 0.002 E","origin":"layout"}],"bg":1},"move.spiralLeft":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.004 -0.067 -0.046 -0.125 -0.113 -0.129 C -0.177 -0.134 -0.237 -0.089 -0.241 -0.024 C -0.246 0.036 -0.204 0.092 -0.144 0.096 C -0.089 0.099 -0.037 0.062 -0.033 0.006 C -0.029 -0.045 -0.064 -0.093 -0.115 -0.097 C -0.162 -0.1 -0.206 -0.069 -0.209 -0.022 C -0.212 0.02 -0.184 0.061 -0.142 0.063 C -0.104 0.066 -0.068 0.042 -0.065 0.004 C -0.063 -0.03 -0.084 -0.063 -0.117 -0.065 C -0.146 -0.067 -0.175 -0.049 -0.177 -0.02 C -0.179 0.005 -0.164 0.029 -0.14 0.031 C -0.12 0.033 -0.099 0.022 -0.098 0.002 C -0.096 -0.014 -0.104 -0.031 -0.119 -0.033 C -0.131 -0.033 -0.143 -0.029 -0.145 -0.018 C -0.146 -0.011 -0.144 -0.004 -0.138 -0.001 C -0.135 0 -0.133 0 -0.13 -0.001 E","origin":"layout"}],"bg":1},"move.diagonalUpRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.25 -0.25 E","origin":"layout"}],"bg":1},"move.turnUpRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0 -0.125 C 0 -0.181 0.069 -0.25 0.125 -0.25 L 0.25 -0.25 E","origin":"layout"}],"bg":1},"move.arcRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.04 0.067 C 0.049 0.081 0.054 0.102 0.054 0.124 C 0.054 0.149 0.049 0.169 0.04 0.183 L 0 0.25 E","origin":"layout"}],"bg":1},"move.sCurve1":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0 -0.035 0.028 -0.062 0.062 -0.062 C 0.097 -0.062 0.125 -0.035 0.125 0 C 0.125 0.035 0.153 0.062 0.188 0.062 C 0.222 0.062 0.25 0.035 0.25 0 E","origin":"layout"}],"bg":1},"move.decayingWave":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C 0.002 0.053 0.007 0.127 0.025 0.126 C 0.051 0.126 0.053 -0.122 0.084 -0.123 C 0.112 -0.123 0.097 0.094 0.124 0.093 C 0.152 0.093 0.137 -0.064 0.167 -0.064 C 0.194 -0.064 0.179 0.042 0.203 0.042 C 0.226 0.042 0.214 -0.039 0.235 -0.039 C 0.247 -0.039 0.248 -0.017 0.249 0 E","origin":"layout"}],"bg":1},"move.curvyRight":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 C -0.008 0.008 -0.017 0.016 -0.021 0.026 C -0.025 0.037 -0.027 0.05 -0.029 0.063 C -0.031 0.076 -0.029 0.087 -0.027 0.099 C -0.025 0.11 -0.022 0.122 -0.015 0.132 C -0.009 0.142 0.001 0.15 0.012 0.156 C 0.022 0.162 0.034 0.166 0.046 0.168 C 0.058 0.17 0.07 0.17 0.081 0.168 C 0.093 0.166 0.104 0.161 0.113 0.153 C 0.122 0.146 0.13 0.137 0.134 0.126 C 0.139 0.116 0.141 0.102 0.141 0.091 C 0.142 0.08 0.141 0.067 0.136 0.056 C 0.131 0.046 0.122 0.038 0.11 0.034 C 0.098 0.031 0.086 0.035 0.078 0.042 C 0.071 0.049 0.066 0.06 0.065 0.073 C 0.065 0.086 0.066 0.098 0.071 0.108 C 0.076 0.118 0.075 0.12 0.095 0.133 C 0.113 0.147 0.131 0.143 0.142 0.144 C 0.153 0.144 0.162 0.14 0.173 0.136 C 0.185 0.131 0.195 0.122 0.202 0.114 C 0.209 0.106 0.212 0.096 0.216 0.08 C 0.219 0.064 0.219 0.056 0.219 0.044 C 0.219 0.032 0.219 0.02 0.219 0.008 E","origin":"layout"}],"bg":1},"move.stairsDown":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 l 0.036 0 l 0 0.036 E","origin":"layout"}],"bg":1},"move.up":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0 -0.25 E","origin":"layout"}],"bg":1},"move.right":{"dur":2000,"c":[{"dur":2000,"fill":"hold","attr":["ppt_x","ppt_y"],"t":"motion","path":"M 0 0 L 0.25 0 E","origin":"layout"}],"bg":1}},"patterns":{"percent_5":[128,0,0,0,8,0,0,0],"percent_10":[128,0,8,0,128,0,8,0],"percent_20":[136,0,34,0,136,0,34,0],"percent_25":[136,34,136,34,136,34,136,34],"percent_30":[170,68,170,17,170,68,170,17],"percent_40":[170,85,170,81,170,85,170,21],"percent_50":[170,85,170,85,170,85,170,85],"percent_60":[238,85,187,85,238,85,187,85],"percent_70":[119,221,119,221,119,221,119,221],"percent_75":[119,255,221,255,119,255,221,255],"percent_80":[239,255,254,255,239,255,254,255],"percent_90":[255,255,255,247,255,255,255,127],"horizontal":[255,0,0,0,0,0,0,0],"vertical":[128,128,128,128,128,128,128,128],"light_horizontal":[255,0,0,0,255,0,0,0],"light_vertical":[136,136,136,136,136,136,136,136],"dark_horizontal":[255,255,0,0,255,255,0,0],"dark_vertical":[204,204,204,204,204,204,204,204],"narrow_horizontal":[255,0,255,0,255,0,255,0],"narrow_vertical":[85,85,85,85,85,85,85,85],"dashed_horizontal":[240,0,0,0,15,0,0,0],"dashed_vertical":[128,128,128,128,8,8,8,8],"cross":[255,128,128,128,128,128,128,128],"downward_diagonal":[128,64,32,16,8,4,2,1],"upward_diagonal":[1,2,4,8,16,32,64,128],"light_downward_diagonal":[136,68,34,17,136,68,34,17],"light_upward_diagonal":[17,34,68,136,17,34,68,136],"dark_downward_diagonal":[204,102,51,153,204,102,51,153],"dark_upward_diagonal":[51,102,204,153,51,102,204,153],"wide_downward_diagonal":[193,224,112,56,28,14,7,131],"wide_upward_diagonal":[131,7,14,28,56,112,224,193],"dashed_downward_diagonal":[0,0,136,68,34,17,0,0],"dashed_upward_diagonal":[0,0,17,34,68,136,0,0],"diagonal_cross":[129,66,36,24,24,36,66,129],"small_checker":[153,102,102,153,153,102,102,153],"large_checker":[240,240,240,240,15,15,15,15],"small_grid":[255,136,136,136,255,136,136,136],"large_grid":[255,128,128,128,128,128,128,128],"dotted_grid":[170,0,128,0,128,0,128,0],"small_confetti":[128,8,64,2,16,1,32,4],"large_confetti":[177,48,3,27,216,192,12,141],"horizontal_brick":[255,128,128,128,255,8,8,8],"diagonal_brick":[1,2,4,8,24,36,66,129],"solid_diamond":[16,56,124,254,124,56,16,0],"outlined_diamond":[130,68,40,16,40,68,130,1],"dotted_diamond":[128,0,34,0,8,0,34,0],"plaid":[170,85,170,85,240,240,240,240],"sphere":[119,137,143,143,119,152,248,248],"weave":[136,84,34,69,136,20,34,81],"divot":[0,16,8,16,0,128,1,128],"shingle":[3,132,72,48,12,2,1,1],"wave":[0,24,37,192,0,24,37,192],"trellis":[255,102,255,153,255,102,255,153],"zigzag":[129,66,36,24,129,66,36,24]}}/*@generated-end*/;
})(typeof window !== 'undefined' ? window : this);

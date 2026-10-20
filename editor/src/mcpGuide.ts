// AI 에이전트에게 주는 .tlide 언어 설명. get_language_guide 도구가 쓴다.
// 토큰을 아끼려고 주제별로 짧게 쓰고, 컴파일러가 아는 목록(개체, 속성, enum, 전환, 애니메이션)은 스키마에서 만든다.
// 문법이 바뀌면 여기의 글도 고친다 (영어: 에이전트가 읽는 글이다)
import type { Schema, SchemaVar } from './lsp';

const TOPICS: Record<string, string> = {
    syntax: `Statements end with ';' and blocks use { }. Comments: /* ... */. Top level: #include, style, template, object, enum, master, theme, slide, section "name";, target, asset, image/video/audio NAME = ...;.
#include <std/stddef> (standard objects), #include <std/styles> (opt-in: bold italic underline strike uppercase superscript align_* reset brgap(n) and CSS color names red navy ..., see topic style), #include <std/widget/flat> (widget templates, see topic widgets), #include "file" (relative to this file).
slide { background = hex(FFFFFF); put text_box { text = "Hi"; x = 80px; y = 40px; width = 600px; height = 80px; } transition fade 0.5s; comment "speaker notes"; }
put NAME [as alias] { prop = value; ... }  puts an object or template. 'group [as alias] { put ...; }' groups. 'put' of a template expands it.
Control flow: if (cond) {} else {}, for (int i in 1..3) {}, for (string s in ["a", "b"]) {} - only inside template, group or master case (not directly in slide). Expressions: + - *, ==, f"text {expr}", @slide.number, @slide.page, @slide.count, @date.
Every 'var' without a default must be given at each put. Types: int float string text color bool font ref image video audio fill, enum names, list<T>.
Lists: [a, b] is a list<T> value (list<list<text>> for table cells). bullets [...] / numbers [...] / dashes [...] / paragraphs [...] only make text.`,

    text: `text = sequence of [style name | style(...) | string | text var | list]: big "Title" " tail". A style applies to the end of the enclosing text; wrap with ( ) to scope: (style(color = hex(F00), font-size = 20pt) "red") " normal".
Named style: style big { font-size = 32pt; font-weight = bold; color = hex(C0392B); }  style with params: style tint(color c) { color = c; } used as tint(hex(F00)) "x".
bullets/numbers/dashes/paragraphs [item, item]; nested level = separate item: bullets ["a", bullets ["a-1"]]. Item text may contain styles. A bare [..] in text is an error.
Strings: \\n = line break inside a paragraph, \\" \\\\ \\t \\{ \\}. paragraphs ["p1", "p2"] = separate paragraphs. f"{n} items".
link("https://x") "text", link(slide(3)) "text", action(...) / hover_action(...) in text like link. Fonts: font("맑은 고딕") only (never a plain string); theme.heading_font / theme.body_font.
Style properties: see topic style.`,

    units: `No unit = px. Units: px pt in cm mm (converted to px), % (of slide size, backends resolve), deg, s, ms. Negative literals: -10px (no general unary minus). width/height must be >= 0.
Angles follow PowerPoint by default (0 = right, clockwise; linear(90, a, b) = top to bottom); target angles = css switches to CSS meaning.
Colors: hex(RGB|RRGGBB|RRGGBBAA), rgb(r, g, b), rgba(r, g, b, a), hsl(160deg, 40%, 59%), hwb, lab, lch, oklab, oklch (CSS Color 4, out-of-sRGB gives a warning), theme.accent1 (dark1 light1 dark2 light2 accent1..6 hyperlink followed_hyperlink), 'color name hex(..);' declares a named color style.
Fills: color | linear(angle, c1, c2) | radial(c1, c2) | pattern(kind, fg, bg) | image("path") | asset image. Line style: line_color line_width line_dash(solid dot dash ...).`,

    template: `template card { var text title; var int x; var int y; var color tone = hex(3498DB); put shape { kind = roundRect; x = x; y = y; width = 300; height = 120; fill = tone; text = title; } }
put card { title = "A"; x = 10; y = 10; }
Templates expand to built-in objects (shape, text_box, ...). Defaults must be constants. A named put/group becomes a PowerPoint group. Values written inside a template are locked in the visual editor.`,

    master: `master brand { var color ink = hex(222); theme = main; case title { background = hex(FFF); put placeholder { role = title; text = "Title"; x = 80; y = 60; width = 1100; height = 100; } } case body { ... } }
Master parameters are 'var' lines like a template's (defaults must be constants; a var without a default must be given by every slide).
slide { layout = brand(hex(F00)).title; title = "My title"; subtitle = "..."; body = bullets ["a"]; }   (layout is master.case, or master(args).case; args may be omitted from the end when they have defaults, or named like Python: brand(ink = hex(F00)).title)
theme main { dark1 = hex(222222); accent1 = hex(3498DB); heading_font = font("Arial"); body_font = font("Arial"); }
Slide properties: see topic slide. Each slide picks its own layout; a target has no master.`,

    slide: `Slide statements: background = fill; hidden = true; advance_after = 5s; transition_sound = "x.wav"; layout = master.case; title/subtitle/body = text (fills layout placeholders); comment "notes"; review { text = "..."; author = "..."; x = 0; y = 0; }; transition KIND[.OPTION] [duration]; animate ...; section "Name"; (before slides).`,

    animation: `animate [NAME] enter|emphasis|exit|move EFFECT[.OPTION] [duration] [on_click|with_previous|after_previous] [delay d] [order N];
Slide level: animate title enter fade 0.5s; (NAME = 'put ... as name'). Inside a put/group block the NAME is omitted: put shape as box { ...; animate enter fly.left 0.4s after_previous order 2; }
'order N' sets the play order slide-wide (numbered first, then written order). media: animate media play|pause|stop (video/audio). move path "M0 0 L100 0" uses an SVG path. Effect names and options: topic animations. Transitions: topic transitions.`,

    target: `target out { path = "out.pptx"; type = pptx; }  types: pptx, html (one self-contained file), web (folder), pdf (Chrome/Edge headless), png (one image per visible slide: path "s.png" -> s-1.png, s-2.png; scale = 2; transparent = true).
Other properties: width/height (slide size, default 1280x720 px), title, author, loop, angles = powerpoint|css, script = "app.js" (html/web run(...) actions). bevel and depth (3D) are compile errors for html/web/pdf/png; some particle/mesh transitions (vortex, ripple, shred, ...) and randomBars are errors for html/web only. run(...) actions need html/web; program(...) and macro(...) need pptx (other targets warn and ignore them).`,

    assets: `Pictures, video and audio live in a .tasset bundle next to the .tlide (a zip). asset "pics.tasset"; asset("logo") ; or file("pics.tasset/logo.png"); file("x.png") = disk file only.
Name a file: image logo = asset("logo"); then put image { data = logo; ... }. 'asset "x.tasset" default;' = bundle the editor adds inserted files to. Image property is 'data' (not path). Formats: images png jpg gif bmp; video mp4 webm; audio mp3 wav m4a (pptx sounds for actions: wav).
get_resources lists the bundles and their files.`,

    table: `put table { x; y; width; height; cells = [["a", "b"], ["c", ""]]; ... }  cells: rows of cell text (list<list<text>>), every row same length, text may use styles: bold "x" (needs #include <std/styles>).
col_widths / row_heights = [2, 1, 1] ratios scaled to width/height (empty = equal); rows grow with text. header_rows / header_cols (bold + header_fill), banded_rows / banded_cols (band_fill), fill = base cell fill, header_fill, band_fill, line_color/line_width/line_dash = all borders (default 1px gray), padding (+padding_left/top/right/bottom), anchor (top|middle|bottom), align (left|center|right|justify).
cell(row, col) { col_span = 2; row_span = 2; fill = ...; align = ...; anchor = ...; padding = ...; line_color/line_width/line_dash and line_color_left|top|right|bottom, line_width_left|... } - row/col are 1-based. Cells covered by a merge must be "". At most one cell block per cell.
Not allowed on tables: rotation, flip, radius, opacity, shadows, glow, bevel, depth, 3D.`,

    chart: `put chart { kind = column|bar|line|area|pie|scatter; values = [[1, 2], [3, 4]]; categories = ["a", "b"]; names = ["s1", "s2"]; x; y; width; height; title; x_title; y_title; legend = none|top|bottom|left|right; stacked; data_labels; smooth; markers; gridlines; colors = [hex(2E86AB)]; range = [0, 100]; font_size; text_color; alt = "..."; }
values = one row per series, each row as long as categories (empty categories = 1, 2, 3 ...). x_title labels the category axis, y_title the value axis. pie draws the first series only (categories = slice names). scatter: xs = [[x values]] per series instead of categories, values = y values. stacked: column/bar/area. smooth/markers: line, scatter. range = [min, max] of the value axis (empty = auto). colors repeat when short; empty = theme accent1..6.
Data lives inline. pptx gets a native, data-editable chart; html/web/pdf/png draw the same chart as SVG. Give it alt text (accessibility).`,

    math: `put math { latex = "\\\\frac{a}{b} + \\\\sqrt{x}"; x; y; width; height; font_size = 32px; text_color = hex(000000); align = center; anchor = middle; alt; }
latex is LaTeX math; write each backslash as \\\\ inside the string. pptx: becomes a PowerPoint equation (editable); html/web/pdf/png: KaTeX. Broken LaTeX syntax is always an error. Commands PowerPoint cannot take (\\\\xrightarrow, aligned/array environments, & or a line break outside a matrix/cases ...) are an error only when a pptx target exists in the document; html/pdf/png still draw them.
Supported for pptx: fractions, roots, sub/superscripts, \\\\sum \\\\int \\\\lim with limits, \\\\left \\\\right, matrices (matrix pmatrix bmatrix vmatrix ...) and cases, accents (\\\\vec \\\\hat \\\\bar), \\\\mathbb \\\\text, Greek letters, common operators and functions (\\\\sin \\\\log ...).`,

    format: `Formatting: a file is formatted only if it has #fmt lines (one option per line, anywhere): #fmt indent_style space; #fmt indent_size 4; #fmt brace_style same_line|next_line; #fmt short_blocks keep|expand; #fmt hex_case keep|upper|lower; ... Unknown options and bad values are warnings (W2002, W2003).
The editor formats with the toolbar wand button (Alt+Shift+F); CLI: templide fmt [--check|--stdout] file.tlide. The formatter re-parses its result and refuses to write if anything but layout would change.`,

    quality: `Diagnostics have codes (E1xxx parse, E2xxx analysis, W warnings, A accessibility) and often a 'help: did you mean ...' line. In the editor, get_accessibility lists the accessibility problems with their lines and fixes. CLI: templide file.tlide --check [--fmt] [--access] [--strict] [--format text|json|github]; exit code 0 ok, 1 errors (or warnings with --strict), 2 usage/IO error. templide tool access file.tlide runs only the accessibility check.
Accessibility (A0001 error: image/video/audio/chart without alt; alt = "" marks decoration), contrast (A0002), text under 12pt (A0003), slide without title (A0004), reading order (A0005), many animations (A0006), table without header (A0007), shape without alt (A0008).`,

    widgets: `#include <std/widget/NAME> with NAME = flat | material | skeuomorphism | neumorphism | glassmorphism | claymorphism | neobrutalism gives templates prefixed flat_, material_, skeuo_, neu_, glass_, clay_, brutal_ (e.g. flat_card { title; content; x; y; width; height }, flat_button { text; ... }, flat_rect, flat_round_rect, flat_circle, flat_pill). Files: get_resources shows their paths; read one to see the exact templates.`,

    workflow: `1) get_editor_state (open files, current slide, selection, errors). 2) get_selection when the user says "this"/"selected". 3) get_language_guide for unfamiliar syntax. 4) read_document, then edit_document with a unique old_string; the reply lists compile errors - fix them. 5) render_slide to look at the result. 6) build only when asked.
Edit the editor's text with edit_document, never the file on disk (unsaved changes live in the editor; the user can Ctrl+Z). Keep the user's formatting; strings are UTF-8 (Korean is fine).`,
};

// 타입 이름을 줄인다 (fill 자리의 긴 이름)
const short = (type: string) => type === 'color, gradient, pattern or image' ? 'fill' : type === 'object name' ? 'ref' : type;

// 속성 하나: 이름:타입, 기본값이 있으면 =기본값. 필수는 이름 뒤에 !를 붙이지 않고 기본값이 없는 것으로 알 수 있다
function variable(v: SchemaVar): string {
    const fallback = v.default ? (v.default.length > 26 ? '...' : v.default.replace(/\s+/g, ' ')) : '';
    return `${v.name}:${short(v.type)}${fallback ? '=' + fallback : ''}`;
}

const signature = (name: string, vars: SchemaVar[]) => `${name}(${vars.map(variable).join(', ')})`;

// object의 속성에서 모든 object에 자동으로 붙는 것(공통, 글 속성)을 뺀다. 기본값이 있으면 그 object가 선언한 것이라 남긴다 (표의 padding)
function ownVars(vars: SchemaVar[], schema: Schema): SchemaVar[] {
    const automatic = new Set([...schema.commonProperties, ...schema.textProperties].map((v) => v.name));
    return vars.filter((v) => !(automatic.has(v.name) && !v.default && !v.required));
}

// object별로 따로 일러 줄 것
const NOTES: Record<string, string> = {
    text_box: 'text box; text style props via style/inline style. Also text props: padding(+_left/_top/_right/_bottom), autofit(none|shrink|resize), wrap, text_direction, columns, column_gap.',
    shape: 'preset shape; kind = a PowerPoint preset name (rect, roundRect, ellipse, triangle, star5, rightArrow ...; see enum shape_kind). radius for rounded kinds, adj1..adj8 for other adjustments. Has text like text_box.',
    image: "picture; data = file(\"x.png\") | asset(\"name\") | named image. fit = stretch|cover|contain, crop_left/top/right/bottom in %, kind clips to a shape.",
    placeholder: 'only inside a master case; a slide fills it with title/subtitle/body. role = title|subtitle|body.',
    backdrop: 'blurs the slide/layout background behind it (glass); put a translucent shape on top. No fill.',
    connector: "joins two named objects ('put x as a'): from = a; to = b; kind straight|elbow|curved.",
    line: 'x1,y1 -> x2,y2; start_arrow/end_arrow.',
    freeform: 'SVG path string (M L H V C S Q T A Z), coordinates in px from (x, y).',
    video: 'mp4/webm. start = click_sequence|auto|when_clicked; poster, loop, rewind, volume, trim_*, fade_*.',
    audio: 'mp3/wav/m4a, shows a speaker icon (width/height default 48). start, across_slides, loop, hide_icon.',
    table: 'see topic table.',
    chart: 'see topic chart.',
    math: 'see topic math.',
};

function objectsText(schema: Schema): string {
    const lines = Object.entries(schema.objects).map(([name, vars]) => signature(name, ownVars(vars, schema)));
    return `${lines.join('\n')}\nEvery object also accepts the optional common properties, and objects with text the text properties (topic common). Templates the document defines or includes: topic templates.`;
}

function commonText(schema: Schema): string {
    return `Any object: ${schema.commonProperties.map(variable).join(' ')}\nObjects with a text property also: ${schema.textProperties.map(variable).join(' ')}`;
}

function enumsText(schema: Schema): string {
    return Object.entries(schema.enums).map(([name, members]) => `${name}: ${members.length > 24 ? members.slice(0, 24).join(' ') + ` ... (${members.length})` : members.join(' ')}`).join('\n');
}

function transitionsText(schema: Schema): string {
    return Object.entries(schema.transitions).map(([kind, options]) => kind + (options.length ? '.' + options.join('|') : '')).join(' ');
}

function animationsText(schema: Schema): string {
    // enter.fly와 exit.fly처럼 종류만 다른 짝은 하나로 합친다
    const merged = new Map<string, Set<string>>();
    for (const [key, options] of Object.entries(schema.animations)) {
        const [category, effect] = key.split('.');
        const id = effect + '|' + options.join('|');
        if (!merged.has(id)) {
            merged.set(id, new Set());
        }
        merged.get(id)!.add(category);
    }
    return Array.from(merged.entries()).map(([id, categories]) => {
        const [effect, ...options] = id.split('|');
        return `${effect}${options.length ? '.' + options.join('|') : ''} [${Array.from(categories).join(',')}]`;
    }).join(' ');
}

const INDEX = `templide .tlide guide. Call get_language_guide with a topic:
syntax, text, units (also colors/fills), template, master, slide, animation, target, assets, table, chart, math, format (#fmt), quality (diagnostics, --check, accessibility), widgets, workflow (static)
objects, common, enums, style, transitions, animations, cell, templates (generated from the compiler for the open document)
or an object/template/style/enum/master name for its signature (e.g. "table", "text_box").
Core: slide { put text_box { text = "Hi"; x = 80px; y = 40px; width = 600px; height = 80px; } } target out { path = "out.pptx"; type = pptx; } with #include <std/stddef> first. No unit = px.`;

export function guide(topic: string | undefined, schema: Schema | null): string {
    const name = (topic ?? '').trim();
    if (!name || name === 'index') {
        return INDEX;
    }
    const lower = name.toLowerCase();
    const aliases: Record<string, string> = { colors: 'units', color: 'units', fills: 'units', fill: 'units', transition: 'transitions', animate: 'animations', object: 'objects', types: 'syntax', list: 'syntax', lists: 'syntax', charts: 'chart', equation: 'math', latex: 'math', fmt: 'format', formatter: 'format', accessibility: 'quality', a11y: 'quality', diagnostics: 'quality', check: 'quality' };
    const key = aliases[lower] ?? lower;
    if (key in TOPICS) {
        return TOPICS[key];
    }
    if (key === 'animation') {
        return TOPICS.animation + (schema ? '\n' + animationsText(schema) : '');
    }
    if (!schema) {
        return 'The compiler has not analyzed the document yet (it may have errors). Fix compile errors, or ask for a static topic: ' + Object.keys(TOPICS).join(', ');
    }
    switch (key) {
        case 'objects': return objectsText(schema);
        case 'common': return commonText(schema);
        case 'enums': return enumsText(schema);
        case 'style': return 'Properties of style { ... } and style(...): ' + schema.styleProperties.map(variable).join(' ') + '\nStyles and colors available in this document (bold, italic, red, ... need #include <std/styles>): ' + Object.keys(schema.styles).join(' ');
        case 'transitions': return 'transition KIND[.OPTION] [duration];  ' + transitionsText(schema);
        case 'animations': return animationsText(schema);
        case 'cell': return 'cell(row, col) { ... } in put table (1-based): ' + schema.cellProperties.map(variable).join(' ');
        case 'templates': return Object.keys(schema.templates).length ? Object.entries(schema.templates).map(([n, vars]) => signature(n, vars)).join('\n') : 'No templates are defined (include <std/widget/...> or write template).';
        case 'slide': return TOPICS.slide;
    }
    // 이름 하나: object, template, style, enum, master
    for (const [kind, table] of [['object', schema.objects], ['template', schema.templates]] as const) {
        const vars = table[name];
        if (vars) {
            return `${kind} ${signature(name, kind === 'object' ? ownVars(vars, schema) : vars)}${kind === 'object' ? '\n' + (NOTES[name] ?? '') + '\n(plus the common properties: topic common)' : ''}`.trim();
        }
    }
    if (schema.styles[name]) {
        return `style ${name}(${schema.styles[name].join(', ')})`;
    }
    if (schema.enums[name]) {
        return `enum ${name}: ${schema.enums[name].join(' ')}`;
    }
    if (schema.masters[name]) {
        const master = schema.masters[name];
        return `master ${name} { ${master.parameters.map((each) => `var ${variable(each)};`).join(' ')} } cases: ${master.cases.join(' ')}`;
    }
    return `Unknown topic "${name}". ${INDEX.split('\n')[1]}`;
}

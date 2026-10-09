// labels.ko.ts의 영어 이름. 키는 같고, 효과 이름은 PowerPoint 영어판의 이름을 쓴다

export const objectNames: Record<string, string> = {
    text_box: 'Text box', shape: 'Shape', image: 'Picture', line: 'Line', group: 'Group', backdrop: 'Backdrop blur', connector: 'Connector',
    freeform: 'Freeform', placeholder: 'Placeholder', video: 'Video', audio: 'Audio',
};

export const propertyNames: Record<string, string> = {
    x: 'X', y: 'Y', width: 'Width', height: 'Height', x1: 'X1', y1: 'Y1', x2: 'X2', y2: 'Y2', rotation: 'Rotation',
    kind: 'Shape', anchor: 'Vertical alignment', text: 'Text', data: 'File', path: 'Path', fit: 'Fit', poster: 'Poster image', blur: 'Blur',
    crop_left: 'Crop left', crop_top: 'Crop top', crop_right: 'Crop right', crop_bottom: 'Crop bottom',
    start_arrow: 'Start arrow', end_arrow: 'End arrow', from: 'Start object', to: 'End object', from_side: 'Start side', to_side: 'End side', role: 'Role',
    fill: 'Fill', opacity: 'Opacity', line_color: 'Line color', line_width: 'Line width', line_dash: 'Dash type', line_cap: 'Line cap', line_join: 'Line join',
    line_compound: 'Compound type', shadow: 'Shadow', shadow_blur: 'Shadow blur', shadow_distance: 'Shadow distance', shadow_angle: 'Shadow angle',
    inner_shadow: 'Inner shadow', inner_shadow_blur: 'Inner shadow blur', inner_shadow_distance: 'Inner shadow distance', inner_shadow_angle: 'Inner shadow angle',
    glow: 'Glow', glow_size: 'Glow size', soft_edge: 'Soft edges', reflection: 'Reflection', reflection_size: 'Reflection size',
    reflection_distance: 'Reflection distance', reflection_blur: 'Reflection blur', bevel: 'Bevel', bevel_width: 'Bevel width', bevel_height: 'Bevel height',
    depth: 'Depth', depth_color: 'Depth color', rotation_x: 'X rotation', rotation_y: 'Y rotation', perspective: 'Perspective', radius: 'Corner radius', flip: 'Flip',
    link: 'Link', action: 'On click', hover_action: 'On hover', action_sound: 'Click sound', hover_sound: 'Hover sound',
    action_highlight: 'Highlight on click', hover_highlight: 'Highlight on hover',
    padding: 'Padding', padding_left: 'Left padding', padding_top: 'Top padding', padding_right: 'Right padding', padding_bottom: 'Bottom padding',
    autofit: 'Autofit', wrap: 'Wrap text', text_direction: 'Text direction', columns: 'Columns', column_gap: 'Column spacing',
    start: 'Start playback', fullscreen: 'Full screen', loop: 'Loop', rewind: 'Rewind after playing', hide_when_stopped: 'Hide when not playing',
    hide_icon: 'Hide icon during show', across_slides: 'Play across slides', volume: 'Volume', trim_start: 'Trim start', trim_end: 'Trim end',
    fade_in: 'Fade in', fade_out: 'Fade out', background: 'Background', hidden: 'Hide slide', advance_after: 'Advance automatically',
    transition_sound: 'Transition sound', title: 'Title', author: 'Author',
};

export const enumNames: Record<string, string> = {
    // vertical alignment, fit
    top: 'Top', middle: 'Middle', bottom: 'Bottom', stretch: 'Stretch', cover: 'Fill (crop)', contain: 'Fit (show all)',
    // line
    none: 'None', triangle: 'Triangle', stealth: 'Stealth arrow', diamond: 'Diamond', oval: 'Oval', arrow: 'Arrow',
    solid: 'Solid', dot: 'Round dot', dash: 'Dash', lgDash: 'Long dash', dashDot: 'Dash dot', lgDashDot: 'Long dash dot', lgDashDotDot: 'Long dash dot dot',
    sysDash: 'Square dash', sysDot: 'Square dot', sysDashDot: 'Square dash dot', sysDashDotDot: 'Square dash dot dot',
    flat: 'Flat', round: 'Round', square: 'Square', bevel: 'Bevel', miter: 'Miter',
    single: 'Single', double: 'Double', thick_thin: 'Thick-thin', thin_thick: 'Thin-thick', triple: 'Triple',
    straight: 'Straight', elbow: 'Elbow', curved: 'Curved', auto: 'Auto', left: 'Left', right: 'Right', up: 'Up', down: 'Down',
    horizontal: 'Horizontal', vertical: 'Vertical', both: 'Both', vertical270: 'Vertical (270°)', stacked: 'Stacked', east_asian: 'Vertical (East Asian)',
    shrink: 'Shrink text on overflow', resize: 'Resize shape to fit text',
    circle: 'Circle', relaxed_inset: 'Relaxed inset', cross: 'Cross', cool_slant: 'Cool slant', angle: 'Angle', soft_round: 'Soft round',
    convex: 'Convex', slope: 'Slope', divot: 'Divot', riblet: 'Riblet', hard_edge: 'Hard edge', art_deco: 'Art deco',
    // media
    click_sequence: 'On click (in sequence)', when_clicked: 'When the object is clicked',
    // placeholder
    title: 'Title', subtitle: 'Subtitle', body: 'Body',
    // text
    normal: 'Normal', bold: 'Bold', italic: 'Italic', underline: 'Underline', double_underline: 'Double underline', wavy_underline: 'Wavy underline',
    line_through: 'Strikethrough', double_line_through: 'Double strikethrough', baseline: 'Baseline', super: 'Superscript', sub: 'Subscript',
    uppercase: 'All caps', small_caps: 'Small caps', center: 'Center', justify: 'Justify',
};

export const shapeKindNames: Record<string, string> = {
    rect: 'Rectangle', roundRect: 'Rounded rectangle', ellipse: 'Ellipse', triangle: 'Triangle', rtTriangle: 'Right triangle', diamond: 'Diamond',
    parallelogram: 'Parallelogram', trapezoid: 'Trapezoid', pentagon: 'Pentagon', hexagon: 'Hexagon', heptagon: 'Heptagon', octagon: 'Octagon',
    decagon: 'Decagon', dodecagon: 'Dodecagon', star4: '4-point star', star5: '5-point star', star6: '6-point star', star7: '7-point star', star8: '8-point star',
    star10: '10-point star', star12: '12-point star', star16: '16-point star', star24: '24-point star', star32: '32-point star',
    rightArrow: 'Right arrow', leftArrow: 'Left arrow', upArrow: 'Up arrow', downArrow: 'Down arrow', leftRightArrow: 'Left-right arrow',
    chevron: 'Chevron', homePlate: 'Pentagon arrow', cloud: 'Cloud', heart: 'Heart', lightningBolt: 'Lightning bolt', sun: 'Sun', moon: 'Moon',
    smileyFace: 'Smiley face', donut: 'Donut', noSmoking: '"No" symbol', plus: 'Plus', can: 'Cylinder', cube: 'Cube', frame: 'Frame', wave: 'Wave',
    doubleWave: 'Double wave', plaque: 'Plaque', wedgeRectCallout: 'Rectangular callout', wedgeRoundRectCallout: 'Rounded rectangular callout',
    wedgeEllipseCallout: 'Oval callout', cloudCallout: 'Cloud callout', flowChartProcess: 'Flowchart: Process', flowChartDecision: 'Flowchart: Decision',
    flowChartTerminator: 'Flowchart: Terminator', teardrop: 'Teardrop', pie: 'Pie', arc: 'Arc', chord: 'Chord', blockArc: 'Block arc',
    snip1Rect: 'Snip single corner rectangle', round1Rect: 'Round single corner rectangle',
};

// animation categories
export const animationCategories: [string, string][] = [['enter', 'Entrance'], ['emphasis', 'Emphasis'], ['exit', 'Exit'], ['move', 'Motion paths'], ['media', 'Media']];

export const effectNames: Record<string, string> = {
    appear: 'Appear', fade: 'Fade', fly: 'Fly In', blinds: 'Blinds', box: 'Box', checkerboard: 'Checkerboard', circle: 'Circle',
    crawl: 'Crawl In', diamond: 'Diamond', dissolve: 'Dissolve In', flashOnce: 'Flash Once', peek: 'Peek In', plus: 'Plus',
    randomBars: 'Random Bars', spiral: 'Spiral In', split: 'Split', stretch: 'Stretch', strips: 'Strips', swivel: 'Swivel', wedge: 'Wedge', wheel: 'Wheel',
    wipe: 'Wipe', zoom: 'Zoom', randomEffects: 'Random Effects', boomerang: 'Boomerang', bounce: 'Bounce', colorReveal: 'Color Reveal',
    credits: 'Credits', easeIn: 'Ease In', float: 'Float In', growAndTurn: 'Grow & Turn', lightSpeed: 'Light Speed', pinwheel: 'Pinwheel',
    riseUp: 'Rise Up', swish: 'Swish', thinLine: 'Thin Line', unfold: 'Unfold', whip: 'Whip', ascend: 'Ascend', centerRevolve: 'Center Revolve',
    fadedSwivel: 'Faded Swivel', descend: 'Descend', sling: 'Sling', spinner: 'Spinner', stretchy: 'Stretchy', zip: 'Zip', arcUp: 'Arc Up',
    fadedZoom: 'Faded Zoom', glide: 'Glide', expand: 'Expand', flip: 'Flip', fold: 'Fold', shimmer: 'Shimmer',
    changeFillColor: 'Fill Color', changeFont: 'Font', changeFontColor: 'Font Color', changeFontSize: 'Font Size', changeFontStyle: 'Font Style',
    growShrink: 'Grow/Shrink', changeLineColor: 'Line Color', spin: 'Spin', transparency: 'Transparency', boldFlash: 'Bold Flash', blast: 'Blast',
    boldReveal: 'Bold Reveal', brushOnColor: 'Brush On Color', brushOnUnderline: 'Brush On Underline', colorBlend: 'Color Blend', colorWave: 'Color Wave',
    complementaryColor: 'Complementary Color', complementaryColor2: 'Complementary Color 2', contrastingColor: 'Contrasting Color', darken: 'Darken',
    desaturate: 'Desaturate', flashBulb: 'Flash Bulb', flicker: 'Flicker', growWithColor: 'Grow With Color', lighten: 'Lighten',
    styleEmphasis: 'Style Emphasis', teeter: 'Teeter', verticalGrow: 'Vertical Grow', path: 'Custom Path', play: 'Play', pause: 'Pause', stop: 'Stop',
    right: 'Right', left: 'Left', up: 'Up', down: 'Down', rightTriangle: 'Right Triangle', hexagon: 'Hexagon', fivePointStar: '5 Point Star',
    crescentMoon: 'Crescent Moon', square: 'Square', trapezoid: 'Trapezoid', heart: 'Heart', octagon: 'Octagon', sixPointStar: '6 Point Star', football: 'Football',
    equalTriangle: 'Equal Triangle', parallelogram: 'Parallelogram', pentagon: 'Pentagon', fourPointStar: '4 Point Star', eightPointStar: '8 Point Star',
    teardrop: 'Teardrop', pointyStar: 'Pointy Star', curvedSquare: 'Curved Square', curvedX: 'Curved X', verticalFigure8: 'Vertical Figure 8',
    curvyStar: 'Curvy Star', loopdeLoop: 'Loop de Loop', buzzsaw: 'Buzzsaw', horizontalFigure8: 'Horizontal Figure 8', peanut: 'Peanut',
    figure8Four: 'Figure 8 Four', neutron: 'Neutron', swoosh: 'Swoosh', bean: 'Bean', invertedTriangle: 'Inverted Triangle', invertedSquare: 'Inverted Square',
    turnRight: 'Turn Right', arcDown: 'Arc Down', zigzag: 'Zigzag', sCurve2: 'S Curve 2', sineWave: 'Sine Wave', bounceLeft: 'Bounce Left', turnUp: 'Turn Up',
    heartbeat: 'Heartbeat', spiralRight: 'Spiral Right', wave: 'Wave', curvyLeft: 'Curvy Left', diagonalDownRight: 'Diagonal Down Right',
    turnDown: 'Turn Down', arcLeft: 'Arc Left', funnel: 'Funnel', spring: 'Spring', bounceRight: 'Bounce Right', spiralLeft: 'Spiral Left',
    diagonalUpRight: 'Diagonal Up Right', turnUpRight: 'Turn Up Right', arcRight: 'Arc Right',
};

export const optionNames: Record<string, string> = {
    down: 'Down', up: 'Up', right: 'Right', left: 'Left', up_left: 'Up left', up_right: 'Up right', down_right: 'Down right', down_left: 'Down left',
    left_up: 'Left up', right_up: 'Right up', left_down: 'Left down', right_down: 'Right down',
    horizontal: 'Horizontal', vertical: 'Vertical', in: 'In', out: 'Out', in_slightly: 'In slightly', in_center: 'In from center',
    out_slightly: 'Out slightly', out_bottom: 'Out from bottom', in_bottom: 'In from bottom', out_center: 'Out from center',
    vertical_in: 'Vertical in', horizontal_in: 'Horizontal in', horizontal_out: 'Horizontal out', vertical_out: 'Vertical out',
    smoothly: 'Smoothly', through_black: 'Through black', across: 'Across', spokes1: '1 spoke', spokes2: '2 spokes', spokes3: '3 spokes',
    spokes4: '4 spokes', spokes8: '8 spokes', center: 'From center', diamond_right: 'Diamond right', diamond_left: 'Diamond left',
    diamond_up: 'Diamond up', diamond_down: 'Diamond down', hexagon_right: 'Hexagon right', hexagon_left: 'Hexagon left',
    hexagon_up: 'Hexagon up', hexagon_down: 'Hexagon down', in_bounce: 'In (bounce)', out_bounce: 'Out (bounce)',
    smooth_left: 'Smooth left', smooth_right: 'Smooth right', black_left: 'Left through black', black_right: 'Right through black',
    strips_in: 'Strips in', strips_out: 'Strips out', rectangle_in: 'Rectangle in', rectangle_out: 'Rectangle out',
    by_object: 'By object', by_word: 'By word', by_char: 'By letter',
};

export const transitionNames: Record<string, string> = {
    cut: 'Cut', fade: 'Fade', random: 'Random', blinds: 'Blinds', checkerboard: 'Checkerboard', cover: 'Cover', uncover: 'Uncover',
    dissolve: 'Dissolve', randomBars: 'Random Bars', strips: 'Strips', wipe: 'Wipe', push: 'Push', box: 'Box', split: 'Split',
    circle: 'Circle', diamond: 'Diamond', plus: 'Plus', comb: 'Comb', newsflash: 'Newsflash', wedge: 'Wedge', wheel: 'Wheel',
    wheelReverse: 'Wheel (counterclockwise)', vortex: 'Vortex', ripple: 'Ripple', glitter: 'Glitter', gallery: 'Gallery', conveyor: 'Conveyor',
    doors: 'Doors', window: 'Window', warp: 'Warp', flyThrough: 'Fly Through', reveal: 'Reveal', honeycomb: 'Honeycomb', ferrisWheel: 'Ferris Wheel',
    switch: 'Switch', flip: 'Flip', flashbulb: 'Flashbulb', shred: 'Shred', cube: 'Cube', rotate: 'Rotate', orbit: 'Orbit', pan: 'Pan',
    fallOver: 'Fall Over', drape: 'Drape', curtains: 'Curtains', wind: 'Wind', prestige: 'Prestige', fracture: 'Fracture', crush: 'Crush',
    peelOff: 'Peel Off', pageCurlSingle: 'Page Curl', pageCurlDouble: 'Page Curl (double)', airplane: 'Airplane', origami: 'Origami',
    morph: 'Morph',
};

export const startNames: Record<string, string> = { on_click: 'On click', with_previous: 'With previous', after_previous: 'After previous' };

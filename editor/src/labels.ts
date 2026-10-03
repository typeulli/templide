// 화면에 보이는 한국어 이름. 코드의 이름은 그대로 두고 여기서만 바꾼다. 없는 이름은 코드의 이름을 그대로 보여 준다

export const objectNames: Record<string, string> = {
    text_box: '글상자', shape: '도형', image: '그림', line: '선', group: '그룹', backdrop: '배경 흐림', connector: '연결선',
    freeform: '자유형', placeholder: '개체 틀', video: '비디오', audio: '오디오',
};

export const propertyNames: Record<string, string> = {
    x: 'X', y: 'Y', width: '너비', height: '높이', x1: 'X1', y1: 'Y1', x2: 'X2', y2: 'Y2', rotation: '회전',
    kind: '모양', anchor: '세로 맞춤', text: '글자', path: '파일', fit: '맞추기', poster: '표지 그림', blur: '흐림',
    crop_left: '왼쪽 자르기', crop_top: '위 자르기', crop_right: '오른쪽 자르기', crop_bottom: '아래 자르기',
    start_arrow: '시작 화살표', end_arrow: '끝 화살표', from: '시작 개체', to: '끝 개체', from_side: '시작 쪽', to_side: '끝 쪽', role: '역할',
    fill: '채우기', opacity: '불투명도', line_color: '선 색', line_width: '선 두께', line_dash: '선 종류', line_cap: '선 끝', line_join: '선 연결',
    line_compound: '겹선', shadow: '그림자', shadow_blur: '그림자 흐림', shadow_distance: '그림자 거리', shadow_angle: '그림자 각도',
    inner_shadow: '안쪽 그림자', inner_shadow_blur: '안쪽 그림자 흐림', inner_shadow_distance: '안쪽 그림자 거리', inner_shadow_angle: '안쪽 그림자 각도',
    glow: '네온', glow_size: '네온 크기', soft_edge: '부드러운 가장자리', reflection: '반사', reflection_size: '반사 크기',
    reflection_distance: '반사 거리', reflection_blur: '반사 흐림', bevel: '입체 테두리', bevel_width: '테두리 너비', bevel_height: '테두리 높이',
    depth: '깊이', depth_color: '깊이 색', rotation_x: 'X 회전', rotation_y: 'Y 회전', perspective: '원근', radius: '모서리 반지름', flip: '뒤집기',
    link: '링크', action: '누를 때', hover_action: '마우스를 올릴 때', action_sound: '누를 때 소리', hover_sound: '올릴 때 소리',
    action_highlight: '누를 때 강조', hover_highlight: '올릴 때 강조',
    padding: '안쪽 여백', padding_left: '왼쪽 여백', padding_top: '위 여백', padding_right: '오른쪽 여백', padding_bottom: '아래 여백',
    autofit: '자동 맞춤', wrap: '줄 바꿈', text_direction: '글자 방향', columns: '단 수', column_gap: '단 간격',
    start: '재생 시작', fullscreen: '전체 화면', loop: '반복', rewind: '끝나면 되감기', hide_when_stopped: '재생하지 않을 때 숨기기',
    hide_icon: '쇼 동안 아이콘 숨기기', across_slides: '여러 슬라이드에 걸쳐 재생', volume: '음량', trim_start: '앞 자르기', trim_end: '뒤 자르기',
    fade_in: '소리 키우며 시작', fade_out: '소리 줄이며 끝', background: '배경', hidden: '슬라이드 숨기기', advance_after: '자동으로 넘기기',
    transition_sound: '전환 소리', title: '제목', author: '작성자',
};

export const enumNames: Record<string, string> = {
    // 세로 맞춤, 맞추기
    top: '위', middle: '가운데', bottom: '아래', stretch: '늘이기', cover: '채우기(자르기)', contain: '맞추기(다 보이게)',
    // 선
    none: '없음', triangle: '삼각형', stealth: '날카로운 화살표', diamond: '마름모', oval: '원', arrow: '화살표',
    solid: '실선', dot: '점선(둥근)', dash: '파선', lgDash: '긴 파선', dashDot: '파선-점선', lgDashDot: '긴 파선-점선', lgDashDotDot: '긴 파선-점선-점선',
    sysDash: '사각 점선', sysDot: '사각 점선(촘촘)', sysDashDot: '사각 파선-점선', sysDashDotDot: '사각 파선-점선-점선',
    flat: '평면', round: '둥글게', square: '사각', bevel: '빗각', miter: '각지게',
    single: '단일', double: '이중', thick_thin: '굵게-얇게', thin_thick: '얇게-굵게', triple: '삼중',
    straight: '직선', elbow: '꺾인 선', curved: '곡선', auto: '자동', left: '왼쪽', right: '오른쪽', up: '위', down: '아래',
    horizontal: '가로', vertical: '세로', both: '둘 다', vertical270: '세로(270°)', stacked: '스택', east_asian: '세로(동아시아)',
    shrink: '넘치면 글자 줄이기', resize: '글자에 맞게 크기 조정',
    circle: '원', relaxed_inset: '부드러운 안쪽', cross: '십자', cool_slant: '비스듬히', angle: '각', soft_round: '부드럽게 둥글게',
    convex: '볼록', slope: '경사', divot: '오목', riblet: '골', hard_edge: '단단한 가장자리', art_deco: '아르 데코',
    // 미디어
    click_sequence: '클릭할 때(순서대로)', when_clicked: '개체를 누를 때',
    // 개체 틀
    title: '제목', subtitle: '부제목', body: '본문',
    // 글자
    normal: '보통', bold: '굵게', italic: '기울임', underline: '밑줄', double_underline: '이중 밑줄', wavy_underline: '물결 밑줄',
    line_through: '취소선', double_line_through: '이중 취소선', baseline: '기준선', super: '위 첨자', sub: '아래 첨자',
    uppercase: '대문자', small_caps: '작은 대문자', center: '가운데', justify: '양쪽',
};

export const shapeKindNames: Record<string, string> = {
    rect: '사각형', roundRect: '둥근 사각형', ellipse: '타원', triangle: '삼각형', rtTriangle: '직각 삼각형', diamond: '마름모',
    parallelogram: '평행 사변형', trapezoid: '사다리꼴', pentagon: '오각형', hexagon: '육각형', heptagon: '칠각형', octagon: '팔각형',
    decagon: '십각형', dodecagon: '십이각형', star4: '별 4', star5: '별 5', star6: '별 6', star7: '별 7', star8: '별 8', star10: '별 10',
    star12: '별 12', star16: '별 16', star24: '별 24', star32: '별 32', rightArrow: '오른쪽 화살표', leftArrow: '왼쪽 화살표',
    upArrow: '위쪽 화살표', downArrow: '아래쪽 화살표', leftRightArrow: '양쪽 화살표', chevron: '갈매기', homePlate: '오각형 화살표',
    cloud: '구름', heart: '하트', lightningBolt: '번개', sun: '해', moon: '달', smileyFace: '웃는 얼굴', donut: '도넛', noSmoking: '금지',
    plus: '더하기', can: '원통', cube: '정육면체', frame: '액자', wave: '물결', doubleWave: '이중 물결', plaque: '배지',
    wedgeRectCallout: '사각 설명선', wedgeRoundRectCallout: '둥근 사각 설명선', wedgeEllipseCallout: '타원 설명선', cloudCallout: '구름 설명선',
    flowChartProcess: '순서도: 처리', flowChartDecision: '순서도: 판단', flowChartTerminator: '순서도: 단말', teardrop: '눈물 방울',
    pie: '원형', arc: '호', chord: '현', blockArc: '막힌 원호', snip1Rect: '한쪽 잘린 사각형', round1Rect: '한쪽 둥근 사각형',
};

// 애니메이션 종류
export const animationCategories: [string, string][] = [['enter', '나타내기'], ['emphasis', '강조'], ['exit', '끝내기'], ['move', '이동 경로'], ['media', '미디어']];

export const effectNames: Record<string, string> = {
    appear: '나타내기', fade: '밝기 변화', fly: '날아오기', blinds: '블라인드', box: '상자', checkerboard: '바둑판 무늬', circle: '원',
    crawl: '살짝 날아오기', diamond: '다이아몬드', dissolve: '디졸브', flashOnce: '한 번 깜박이기', peek: '엿보기', plus: '더하기',
    randomBars: '임의 막대', spiral: '나선', split: '분할', stretch: '늘이기', strips: '줄무늬', swivel: '회전', wedge: '쐐기', wheel: '시계 방향 회전',
    wipe: '닦아내기', zoom: '확대/축소', randomEffects: '임의 효과', boomerang: '부메랑', bounce: '바운드', colorReveal: '색 나타내기',
    credits: '크레딧', easeIn: '완만하게', float: '떠오르기', growAndTurn: '확대하며 회전', lightSpeed: '광속', pinwheel: '바람개비',
    riseUp: '솟아오르기', swish: '휘익', thinLine: '얇은 선', unfold: '펼치기', whip: '채찍', ascend: '올라가기', centerRevolve: '가운데 회전',
    fadedSwivel: '밝기 변화 회전', descend: '내려가기', sling: '새총', spinner: '스피너', stretchy: '늘어나기', zip: '지퍼', arcUp: '위로 호',
    fadedZoom: '밝기 변화 확대/축소', glide: '미끄러지기', expand: '확장', flip: '뒤집기', fold: '접기', shimmer: '반짝임',
    changeFillColor: '채우기 색', changeFont: '글꼴', changeFontColor: '글꼴 색', changeFontSize: '글꼴 크기', changeFontStyle: '글꼴 스타일',
    growShrink: '크게/작게', changeLineColor: '선 색', spin: '회전', transparency: '투명', boldFlash: '굵게 깜박이기', blast: '터뜨리기',
    boldReveal: '굵게 나타내기', brushOnColor: '붓으로 색 칠하기', brushOnUnderline: '붓으로 밑줄', colorBlend: '색 섞기', colorWave: '색 물결',
    complementaryColor: '보색', complementaryColor2: '보색 2', contrastingColor: '대비 색', darken: '어둡게', desaturate: '채도 낮추기',
    flashBulb: '플래시', flicker: '깜박임', growWithColor: '색과 함께 확대', lighten: '밝게', styleEmphasis: '스타일 강조', teeter: '흔들기',
    verticalGrow: '세로로 늘이기', path: '직접 그린 경로', play: '재생', pause: '일시 중지', stop: '중지',
    right: '오른쪽', left: '왼쪽', up: '위', down: '아래', rightTriangle: '직각 삼각형', hexagon: '육각형', fivePointStar: '별 5',
    crescentMoon: '초승달', square: '사각형', trapezoid: '사다리꼴', heart: '하트', octagon: '팔각형', sixPointStar: '별 6', football: '럭비공',
    equalTriangle: '정삼각형', parallelogram: '평행 사변형', pentagon: '오각형', fourPointStar: '별 4', eightPointStar: '별 8', teardrop: '눈물 방울',
    pointyStar: '뾰족한 별', curvedSquare: '둥근 사각형', curvedX: '둥근 X', verticalFigure8: '세로 8자', curvyStar: '곡선 별', loopdeLoop: '고리',
    buzzsaw: '톱니', horizontalFigure8: '가로 8자', peanut: '땅콩', figure8Four: '8자 넷', neutron: '중성자', swoosh: '휙', bean: '콩',
    invertedTriangle: '역삼각형', invertedSquare: '역사각형', turnRight: '오른쪽으로 돌기', arcDown: '아래로 호', zigzag: '지그재그',
    sCurve2: 'S 곡선', sineWave: '사인파', bounceLeft: '왼쪽으로 튀기', turnUp: '위로 돌기', heartbeat: '심장 박동', spiralRight: '오른쪽 나선',
    wave: '물결', curvyLeft: '왼쪽 곡선', diagonalDownRight: '오른쪽 아래 대각선', turnDown: '아래로 돌기', arcLeft: '왼쪽으로 호', funnel: '깔때기',
    spring: '용수철', bounceRight: '오른쪽으로 튀기', spiralLeft: '왼쪽 나선', diagonalUpRight: '오른쪽 위 대각선', turnUpRight: '오른쪽 위로 돌기',
    arcRight: '오른쪽으로 호',
};

export const optionNames: Record<string, string> = {
    down: '아래로', up: '위로', right: '오른쪽', left: '왼쪽', up_left: '왼쪽 위', up_right: '오른쪽 위', down_right: '오른쪽 아래', down_left: '왼쪽 아래',
    left_up: '왼쪽 위', right_up: '오른쪽 위', left_down: '왼쪽 아래', right_down: '오른쪽 아래',
    horizontal: '가로', vertical: '세로', in: '안으로', out: '밖으로', in_slightly: '살짝 안으로', in_center: '가운데에서 안으로',
    out_slightly: '살짝 밖으로', out_bottom: '아래에서 밖으로', in_bottom: '아래에서 안으로', out_center: '가운데에서 밖으로',
    vertical_in: '세로 안쪽으로', horizontal_in: '가로 안쪽으로', horizontal_out: '가로 바깥쪽으로', vertical_out: '세로 바깥쪽으로',
    smoothly: '부드럽게', through_black: '검정 화면을 지나', across: '가로로', spokes1: '살 1개', spokes2: '살 2개', spokes3: '살 3개',
    spokes4: '살 4개', spokes8: '살 8개', center: '가운데에서', diamond_right: '다이아몬드 오른쪽', diamond_left: '다이아몬드 왼쪽',
    diamond_up: '다이아몬드 위', diamond_down: '다이아몬드 아래', hexagon_right: '육각형 오른쪽', hexagon_left: '육각형 왼쪽',
    hexagon_up: '육각형 위', hexagon_down: '육각형 아래', in_bounce: '안으로(튕기기)', out_bounce: '밖으로(튕기기)',
    smooth_left: '부드럽게 왼쪽', smooth_right: '부드럽게 오른쪽', black_left: '검정으로 왼쪽', black_right: '검정으로 오른쪽',
    strips_in: '줄무늬 안으로', strips_out: '줄무늬 밖으로', rectangle_in: '사각형 안으로', rectangle_out: '사각형 밖으로',
    by_object: '개체', by_word: '단어', by_char: '글자',
};

export const transitionNames: Record<string, string> = {
    cut: '잘라내기', fade: '페이드', random: '임의', blinds: '블라인드', checkerboard: '바둑판 무늬', cover: '덮기', uncover: '당기기',
    dissolve: '디졸브', randomBars: '임의 막대', strips: '줄무늬', wipe: '닦아내기', push: '밀어내기', box: '상자', split: '나누기',
    circle: '원', diamond: '다이아몬드', plus: '더하기', comb: '빗질', newsflash: '뉴스 속보', wedge: '쐐기', wheel: '시계 방향 회전',
    wheelReverse: '시계 반대 방향 회전', vortex: '소용돌이', ripple: '물결', glitter: '반짝이', gallery: '갤러리', conveyor: '컨베이어',
    doors: '문', window: '창', warp: '휘어지기', flyThrough: '날기', reveal: '나타내기', honeycomb: '벌집', ferrisWheel: '관람차',
    switch: '전환', flip: '젖히기', flashbulb: '플래시', shred: '조각', cube: '큐브', rotate: '회전', orbit: '궤도', pan: '이동',
    fallOver: '넘어지기', drape: '드레이프', curtains: '커튼', wind: '바람', prestige: '프레스티지', fracture: '균열', crush: '구기기',
    peelOff: '벗기기', pageCurlSingle: '페이지 말아 넘기기', pageCurlDouble: '페이지 말아 넘기기(양쪽)', airplane: '비행기', origami: '종이 접기',
    morph: '모핑',
};

// PowerPoint의 전환 갤러리 순서 (은은한 효과, 화려한 효과, 동적 콘텐츠)
export const transitionOrder = ['morph', 'fade', 'push', 'wipe', 'split', 'reveal', 'cut', 'randomBars', 'circle', 'diamond', 'plus', 'uncover', 'cover',
    'flashbulb', 'fallOver', 'drape', 'curtains', 'wind', 'prestige', 'fracture', 'crush', 'peelOff', 'pageCurlSingle', 'pageCurlDouble', 'airplane', 'origami',
    'dissolve', 'checkerboard', 'blinds', 'wheel', 'wheelReverse', 'wedge', 'ripple', 'honeycomb', 'glitter', 'vortex', 'shred', 'switch', 'flip', 'gallery',
    'cube', 'doors', 'box', 'comb', 'warp', 'newsflash', 'strips', 'random', 'pan', 'ferrisWheel', 'conveyor', 'rotate', 'window', 'orbit', 'flyThrough'];

// html, web에서 쓸 수 없는 전환 (pptx에서만 된다)
export const pptxOnlyTransitions = new Set(['vortex', 'ripple', 'glitter', 'honeycomb', 'shred', 'warp', 'drape', 'curtains', 'wind', 'prestige',
    'fracture', 'crush', 'peelOff', 'pageCurlSingle', 'pageCurlDouble', 'airplane', 'origami', 'morph', 'fallOver', 'randomBars']);

export const startNames: Record<string, string> = { on_click: '클릭할 때', with_previous: '이전 효과와 함께', after_previous: '이전 효과 다음에' };

// 이름이 없으면 camelCase, snake_case를 띄어 쓴다
export function label(table: Record<string, string>, name: string): string {
    return table[name] ?? name.replace(/_/g, ' ').replace(/([a-z])([A-Z0-9])/g, '$1 $2');
}

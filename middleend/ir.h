#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// middle end가 만들어 backend에 넘기는 결과. 이름은 모두 해석됐고 계산할 수 있는 값은 모두 계산돼 있다
namespace templide::ir {
    // 단위별 계수의 합. 50%-200px -> [(%, 50), (px, -200)]
    // 단위가 없는 항은 unit이 빈 문자열이며 backend에서 px로 본다.
    // pt, in, cm, mm는 미들 엔드가 px로 바꾸므로 unit은 "", px, %, deg, s, ms 중 하나다.
    // 단위와 범위는 미들 엔드가 검사했고, %는 backend가 슬라이드 크기로 푼다.
    // int 값(is_float가 아님)의 %는 int(슬라이드 크기 * % / 100)px로 소수를 버린다
    struct Number {
        std::vector<std::pair<std::string, double>> terms;
        bool is_float = false;
    };

    struct Color {
        int r = 0;
        int g = 0;
        int b = 0;
        double a = 1;
        // theme.accent1 같은 테마 색이면 그 이름(dk1, lt1, dk2, lt2, accent1~6, hlink, folHlink).
        // r, g, b는 기본 테마의 값이고, backend가 실제 테마로 바꿔 쓴다
        std::string scheme;
    };

    struct EnumValue {
        std::string type;
        std::string member;
    };

    // linear(각도, 색, 색, ...) 또는 radial(색, 색, ...). 색 뒤에 위치(%)를 쓸 수 있다
    // 각도는 0이 왼쪽 -> 오른쪽이고 시계 방향으로 커진다 (90이면 위 -> 아래). radial은 가운데 -> 바깥
    struct Gradient {
        Number angle;
        std::vector<Color> colors;
        std::vector<std::optional<Number>> positions; // colors와 같은 길이. 없는 위치는 앞뒤 사이에 고르게 놓는다
        bool radial = false;
    };

    // pattern(종류, 앞색, 뒷색). kind는 pattern_kind 값(diagonal_cross 등)이고 backend가 자기 무늬 이름으로 바꾼다
    struct Pattern {
        std::string kind;
        Color foreground;
        Color background;
    };

    // 링크 대상. url, slide(번호), next_slide 같은 이동 중 하나만 있다
    struct Link {
        std::string url;
        int slide = 0;
        std::string jump; // next_slide, previous_slide, first_slide, last_slide, last_viewed_slide, end_show
    };

    // 개체나 글자를 누르거나(action) 마우스를 올렸을 때(hover_action) 하는 일. PowerPoint의 실행 설정이다.
    // kind가 link면 link로 가고, run은 JS 함수를(html, web), program은 프로그램을(pptx), macro는 매크로를(pptx) 실행하고, file은 파일을 연다
    struct Action {
        std::string kind;   // link, run, program, macro, file
        Link link;          // kind가 link일 때
        std::string target; // run은 함수 이름, program과 file은 경로(.tlide 기준), macro는 매크로 이름
        std::vector<std::variant<bool, double, std::string>> arguments; // run의 함수에 넘기는 값
        std::string where;  // 적은 곳(파일:줄:칸). backend가 이 동작을 무시할 때 경고에 쓴다
    };

    // image("경로")
    struct Image {
        std::string path;
    };

    // 값이 없는 속성은 style로 지정되지 않은 것
    struct TextStyle {
        std::optional<Color> color;
        std::optional<EnumValue> font_weight;
        std::optional<Number> line_height;
        std::optional<std::string> font_family;
        std::optional<Number> font_size;
        std::optional<EnumValue> text_align; // 문단 단위
        std::optional<EnumValue> font_style;
        std::optional<EnumValue> text_decoration;
        std::optional<EnumValue> vertical_align;
        std::optional<Number> letter_spacing;
        std::optional<Color> highlight;
        std::optional<EnumValue> text_transform;
        // 여기부터 list_start까지 문단 단위
        std::optional<Number> space_before;
        std::optional<Number> space_after;
        std::optional<Number> margin_left;
        std::optional<Number> text_indent;
        std::optional<std::string> list_marker;
        std::optional<Color> list_marker_color;
        std::optional<EnumValue> list_style;
        std::optional<Number> list_start;
        std::optional<Link> link;
        std::optional<Action> action;
        std::optional<Action> hover_action;
    };

    // 원문 안의 범위. begin, end는 파일 안의 바이트 위치
    struct SourceRange {
        std::string path;
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    // 값이 원문의 어디에서 왔는지. 편집기가 화면에서 바꾼 값을 원문에 되돌릴 때 쓴다
    struct Origin {
        enum class Kind {
            LOCKED,  // 화면에서 바꿀 수 없다. reason이 이유 (template, computed, for, package, layout)
            LITERAL, // range에 적힌 리터럴을 바꾸면 된다. unit은 리터럴에 적힌 단위 (단위가 없으면 빈 문자열)
            BLOCK,   // 값을 적지 않았다. range의 블록({ ... }를 가진 문장)에 'name = 값;'을 넣으면 된다
        };
        Kind kind = Kind::LOCKED;
        SourceRange range;
        std::string unit;
        std::string name;
        std::string reason;
        bool integer = false; // 정수만 받는 값 (int var)
        bool text = false;    // text 자리에 적은 문자열 리터럴. 일부를 style(...)로 감쌀 수 있다
        // 리터럴이 (style(...) "글자")처럼 인라인 style 하나와 함께 괄호로 감싸여 있으면 그 style(...)과 괄호 전체. 아니면 path가 비어 있다
        SourceRange style;
        SourceRange group;
        // LITERAL이 'name = 값;' 대입에 적혀 있으면 그 문장 전체. 편집기가 속성을 지울(기본값으로 되돌릴) 때 쓴다. 아니면 path가 비어 있다
        SourceRange statement;
    };

    struct Run {
        std::string text; // '\n'은 같은 문단 안의 줄바꿈
        TextStyle style;
        std::string field; // 자동으로 바뀌는 글자(slidenum, datetime1)면 그 종류이고 text는 지금의 값
        Origin origin;     // 이 글자를 적은 문자열 리터럴
    };

    enum class ListKind {
        NONE,
        BULLETS,
        NUMBERS,
        DASHES,
    };

    struct Paragraph {
        ListKind list = ListKind::NONE;
        int level = 0; // 목록의 깊이. 가장 바깥 목록이 0
        std::vector<Run> runs;
    };

    struct Text {
        std::vector<Paragraph> paragraphs;
    };

    using Value = std::variant<bool, Number, std::string, Color, EnumValue, Text, Gradient, Image, Pattern, Link, Action>;

    struct Property {
        std::string name;
        Value value;
        Origin origin;
    };

    // stddef에 선언된 object(text_box, image 등) 하나. object가 "group"이면 children을 묶는다
    struct Element {
        std::string object;
        std::vector<Property> properties;
        std::string name; // put ... as NAME의 이름. 없으면 빈 문자열
        bool generated_name = false; // 이름 없는 개체에 animate를 걸어 컴파일러가 붙인 이름이다. backend는 보여 주지 않는다
        std::vector<Element> children;
        // 편집기가 element를 가리키는 이름. slide 번호와 이 element까지 거친 put 문장의 위치로 만든다. layout의 element는 비어 있다
        std::string id;
        // slide에 적은 put 문장(BLOCK). template이 만든 element는 그 template을 넣은 바깥쪽 put이다.
        // 적지 않은 속성은 from_template이 아닐 때 여기에 넣는다
        Origin source;
        bool from_template = false;
        // from_template일 때 바깥쪽 put에 적은 x, y, width, height. 화면에서 끌거나 크기를 바꾸면 이 값을 바꿔 template 전체를 바꾼다
        std::vector<Property> instance;
    };

    struct Layout {
        std::string name;
        std::vector<Element> elements;
        std::optional<Value> background; // Color, Gradient 또는 Image
    };

    // 테마 색(dk1, lt1, dk2, lt2, accent1~6, hlink, folHlink)과 제목, 본문 글꼴
    struct Theme {
        std::string name;
        std::map<std::string, Color> colors;
        std::string heading_font;
        std::string body_font;
    };

    struct Master {
        std::string name;
        std::vector<Layout> layouts;
        std::optional<Theme> theme;
    };

    // Document::masters[master].layouts[layout]
    struct LayoutRef {
        std::size_t master;
        std::size_t layout;
    };

    // 종류와 옵션은 PowerPoint의 전환 효과 이름을 따른다. 옵션은 기본값이 채워져 있다
    struct Transition {
        std::string kind;
        std::string option; // 옵션이 없는 전환은 빈 문자열
        std::optional<Number> duration;
        Origin source;      // transition 문장(BLOCK)
    };

    // animate 문장 하나. category는 enter, emphasis, exit, move, media(play, pause, stop)
    // Slide::animations는 재생하는 차례대로다. order가 있는 것을 번호 순으로, 그다음 없는 것을 적은 순서로 놓는다
    struct Animation {
        std::string target; // 대상 element의 이름. 이름 없는 개체에 걸었으면 컴파일러가 붙인 이름이다
        std::string category;
        std::string effect;
        std::string option; // 옵션이 없는 효과는 빈 문자열. 기본값이 채워져 있다
        std::string path;   // move path "..."의 경로. 좌표는 px
        std::optional<Number> duration;
        std::string start = "on_click"; // on_click, with_previous, after_previous
        std::optional<Number> delay;
        std::optional<int> order;
        // 편집기를 위한 것. element_id는 대상 element의 id, source는 animate 문장(BLOCK, 고칠 수 없으면 LOCKED)이다.
        // inside면 put이나 group 블록 안에 대상 없이 적은 문장이다.
        // implicit이면 video, audio의 start(click_sequence, auto)가 만든 재생이고 source는 그 put 블록이다
        std::string element_id;
        Origin source;
        bool inside = false;
        bool implicit = false;
    };

    // 검토 메모
    struct Review {
        std::string text;
        std::string author;
        Number x;
        Number y;
        Origin source; // review 문장(BLOCK). 편집기가 고치고 지울 때 쓴다
    };

    struct Slide {
        int page = 0;
        std::optional<LayoutRef> layout;
        std::vector<Element> elements;
        Text notes; // 발표자 메모. 없으면 문단이 없다
        std::optional<Transition> transition;
        std::optional<Value> background; // 없으면 layout의 배경을 쓴다
        std::map<std::string, Text> placeholders; // layout 개체 틀의 역할(title, subtitle, body) -> 내용
        bool hidden = false;
        std::optional<Number> advance_after;
        std::optional<std::string> transition_sound;
        std::optional<std::string> section; // 속한 구역의 이름
        std::vector<Animation> animations;
        std::vector<Review> reviews;
        Origin source; // slide 문장(BLOCK). 화면에서 만든 element를 이 블록 끝에 넣는다
        // 편집기를 위한 것. slide 블록에 적은 속성(background, hidden, layout 등)의 값과 comment 문장들(BLOCK)
        std::map<std::string, Origin> origins;
        std::vector<Origin> note_sources;
    };

    struct Target {
        std::string name;
        std::string path;
        std::string type;
        std::vector<std::size_t> masters; // 이 target에 넣을 Document::masters의 index
        // 슬라이드 크기. 둘 다 있거나 둘 다 없다
        std::optional<Number> width;
        std::optional<Number> height;
        std::string title;  // 문서 속성. 비어 있으면 target 이름
        std::string author; // 비어 있으면 templide
        bool loop = false;  // 슬라이드 쇼를 끝까지 보면 처음부터 다시
        // IR의 각도(그라데이션, 그림자, rotation_x/y)를 읽는 방식. powerpoint는 0이 오른쪽이고,
        // css는 0이 위쪽이고 rotation_x/y가 CSS의 rotateX/rotateY다. 둘 다 시계 방향이다
        std::string angles = "powerpoint";
        // width, height를 적은 곳. 적지 않았으면 target 블록(BLOCK)
        Origin width_origin;
        Origin height_origin;
        // run(...) 동작이 부르는 JS 함수가 든 파일(.tlide 기준). 없으면 빈 문자열. html, web만 쓴다
        std::string script;
        std::string script_where; // script를 적은 곳(파일:줄:칸)
        // 편집기를 위한 것. target 블록(BLOCK)과 적은 속성(title, author, loop)의 값
        Origin source;
        std::map<std::string, Origin> origins;
    };

    struct Document {
        std::vector<Master> masters;
        std::vector<Slide> slides;
        std::vector<Target> targets;
    };

    // 기본 테마(Office)의 색에 테마 색 이름을 붙인 것. scheme은 dk1, lt1, dk2, lt2, accent1~6, hlink, folHlink
    inline Color default_theme_color(const std::string& scheme) {
        static const std::map<std::string, Color> defaults = {
            {"dk1", {0x00, 0x00, 0x00, 1, ""}}, {"lt1", {0xFF, 0xFF, 0xFF, 1, ""}}, {"dk2", {0x44, 0x54, 0x6A, 1, ""}}, {"lt2", {0xE7, 0xE6, 0xE6, 1, ""}},
            {"accent1", {0x44, 0x72, 0xC4, 1, ""}}, {"accent2", {0xED, 0x7D, 0x31, 1, ""}}, {"accent3", {0xA5, 0xA5, 0xA5, 1, ""}},
            {"accent4", {0xFF, 0xC0, 0x00, 1, ""}}, {"accent5", {0x5B, 0x9B, 0xD5, 1, ""}}, {"accent6", {0x70, 0xAD, 0x47, 1, ""}},
            {"hlink", {0x05, 0x63, 0xC1, 1, ""}}, {"folHlink", {0x95, 0x4F, 0x72, 1, ""}},
        };
        Color color = defaults.at(scheme);
        color.scheme = scheme;
        return color;
    }

    inline std::string format_scalar(double value) {
        std::ostringstream out;
        out << std::setprecision(15) << value;
        return out.str();
    }

    inline std::string format_number(const Number& number) {
        if (number.terms.empty()) {
            return "0";
        }
        std::string result;
        for (std::size_t i = 0; i < number.terms.size(); ++i) {
            const auto& [unit, value] = number.terms[i];
            if (i > 0) {
                result += value < 0 ? "-" : "+";
            }
            result += format_scalar(i > 0 ? std::abs(value) : value) + unit;
        }
        return result;
    }

    inline std::string format_color(const Color& color) {
        if (!color.scheme.empty()) {
            return "theme." + color.scheme + (color.a < 1 ? "(alpha " + format_scalar(color.a) + ")" : "");
        }
        return "rgba(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " + std::to_string(color.b) + ", " + format_scalar(color.a) + ")";
    }

    inline std::string format_gradient(const Gradient& gradient) {
        std::string result = gradient.radial ? "radial(" : "linear(" + format_number(gradient.angle) + ", ";
        for (std::size_t i = 0; i < gradient.colors.size(); ++i) {
            result += (i > 0 ? ", " : "") + format_color(gradient.colors[i]);
            if (i < gradient.positions.size() && gradient.positions[i]) {
                result += " " + format_number(*gradient.positions[i]);
            }
        }
        return result + ")";
    }
}

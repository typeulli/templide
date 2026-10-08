#pragma once

#include "pptx_model.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// DrawingML의 색, 채우기, 선, 효과, 글자 서식을 테마와 상속까지 풀어서 읽는다
namespace templide::importer {
    // 색을 풀 때 쓰는 테마와 색 이름 짝 (tx1 -> dk1 등)
    struct ColorContext {
        const Theme* theme = nullptr;
        const std::map<std::string, std::string>* color_map = nullptr;
    };

    // node 안의 색 요소(srgbClr, schemeClr 등)를 읽는다. placeholder는 테마 서식의 phClr 자리에 들어갈 색
    std::optional<Color> read_color(const xml::Node* node, const ColorContext& context, const std::optional<Color>& placeholder = std::nullopt);
    // 색 요소 자신
    std::optional<Color> color_element(const xml::Node& element, const ColorContext& context, const std::optional<Color>& placeholder);
    bool same_color(const Color& a, const Color& b);

    // 그림에 건 색 바꾸기 (a:blip 안의 duotone, grayscl, lum). templide에는 없으므로 불러올 때 그림에 미리 적용한다
    struct ImageEffects {
        std::optional<std::pair<Color, Color>> duotone;
        bool grayscale = false;
        double bright = 0;   // -1 ~ 1
        double contrast = 0; // -1 ~ 1
        std::vector<std::string> dropped; // 적용하지 못한 효과
        bool any() const { return duotone.has_value() || grayscale || bright != 0 || contrast != 0; }
    };

    ImageEffects read_image_effects(const xml::Node* blip, const ColorContext& context, const std::optional<Color>& placeholder = std::nullopt);

    struct GradientStop {
        Color color;
        double position; // 0 ~ 100
    };

    struct Fill {
        enum class Kind { None, Solid, Gradient, Pattern, Image, Group } kind = Kind::None;
        Color color;                     // Solid
        std::vector<GradientStop> stops; // Gradient
        double angle = 0;                // 선형 그라데이션의 각도 (도)
        bool radial = false;
        std::string pattern;             // Pattern: PowerPoint의 무늬 이름 (pct50 등)
        Color foreground;
        Color background;
        std::string image;               // Image: 그림 파트 이름
        ImageEffects image_effects;
        const xml::Node* blip = nullptr;
        const Part* part = nullptr;
    };

    // properties(spPr, bgPr 등)의 바로 아래 채우기. 없으면 nullopt
    std::optional<Fill> read_fill(const xml::Node* properties, const Part& part, const ColorContext& context, const std::optional<Color>& placeholder = std::nullopt);
    // 채우기 요소 자신 (noFill, solidFill, gradFill, pattFill, blipFill, grpFill)
    std::optional<Fill> fill_element(const xml::Node& element, const Part& part, const ColorContext& context, const std::optional<Color>& placeholder);
    // p:style의 fillRef나 bgRef가 가리키는 테마 채우기
    std::optional<Fill> theme_fill(const xml::Node* reference, const Part& part, const ColorContext& context);

    struct Line {
        bool none = false;
        std::optional<Color> color;
        std::optional<Emu> width;
        std::string dash;     // dash_style 값 (solid, dot ...)
        std::string cap;      // flat, round, square
        std::string join;     // round, bevel, miter
        std::string compound; // single, double ...
        std::string head;     // line_arrow 값
        std::string tail;
        bool present = false; // 선 정보가 하나라도 있다
    };

    // ln 요소를 base 위에 덮어쓴다
    Line read_line(const xml::Node* ln, const ColorContext& context, Line base, const std::optional<Color>& placeholder = std::nullopt);
    // p:style의 lnRef가 가리키는 테마 선
    Line theme_line(const xml::Node* reference, const ColorContext& context);

    struct Shadow {
        Color color;
        double blur = 4;     // pt
        double distance = 3; // pt
        double angle = 45;   // 도
    };

    struct Effects {
        std::optional<Shadow> shadow;
        std::optional<Shadow> inner_shadow;
        std::optional<Color> glow;
        double glow_size = 8; // pt
        std::optional<double> soft_edge; // pt
        std::optional<double> reflection; // 시작 불투명도 0 ~ 1
        double reflection_size = 0.35;
        double reflection_distance = 0;  // pt
        double reflection_blur = 0.5;    // pt
        // 3차원
        std::optional<double> rotation_x;
        std::optional<double> rotation_y;
        std::optional<double> perspective;
        std::string bevel; // bevel_kind 값
        double bevel_width = 6;  // pt
        double bevel_height = 6; // pt
        std::optional<double> depth; // pt
        std::optional<Color> depth_color;
        std::vector<std::string> dropped; // 옮기지 못한 효과
    };

    // spPr의 effectLst, scene3d, sp3d. 없으면 effectRef가 가리키는 테마 효과
    Effects read_effects(const xml::Node* properties, const xml::Node* reference, const ColorContext& context);

    // ---- 글자

    // 글자 하나의 모양. 값이 없으면 정하지 않은 것
    struct RunStyle {
        std::optional<double> size; // pt
        std::optional<bool> bold;
        std::optional<bool> italic;
        std::optional<std::string> underline; // none, sng, dbl, wavy ...
        std::optional<std::string> strike;    // noStrike, sngStrike, dblStrike
        std::optional<std::string> cap;       // none, all, small
        std::optional<double> spacing;        // pt
        std::optional<double> baseline;       // %
        std::optional<Color> color;
        std::optional<Color> highlight;
        std::optional<std::string> latin;
        std::optional<std::string> ea;
    };

    // 문단 하나의 모양
    struct ParagraphStyle {
        std::optional<std::string> align; // l, ctr, r, just, dist
        std::optional<double> line_percent; // 줄 간격 (1 = 한 줄)
        std::optional<double> line_points;
        std::optional<double> before_points;
        std::optional<double> before_percent;
        std::optional<double> after_points;
        std::optional<double> after_percent;
        std::optional<Emu> margin;
        std::optional<Emu> indent;
        std::optional<std::string> bullet; // none, char, auto, blip
        std::string bullet_char;
        std::string auto_type;
        std::optional<long long> start_at;
        std::optional<Color> bullet_color;
    };

    // 상속 순서대로(가까운 것부터) 놓은 lstStyle 비슷한 노드들 (a:lstStyle, p:titleStyle, p:defaultTextStyle ...)
    using StyleChain = std::vector<const xml::Node*>;

    // rPr(또는 defRPr)의 값으로 아직 정하지 않은 것을 채운다
    void merge_run(RunStyle& style, const xml::Node* properties, const ColorContext& context);
    // pPr(또는 lvlNpPr)의 값으로 아직 정하지 않은 것을 채운다
    void merge_paragraph(ParagraphStyle& style, const xml::Node* properties, const ColorContext& context);
    // level(0부터) 문단의 모양: pPr, 그다음 chain의 lvl(level+1)pPr
    ParagraphStyle resolve_paragraph(const xml::Node* paragraph_properties, const StyleChain& chain, int level, const ColorContext& context);
    // 글자 모양: rPr, 그다음 chain의 lvl(level+1)pPr의 defRPr
    RunStyle resolve_run(const xml::Node* run_properties, const StyleChain& chain, int level, const ColorContext& context);
}

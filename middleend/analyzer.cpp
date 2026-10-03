#include "analyzer.h"
#include "geometry.h"

#include "../frontend/lexor.h"
#include "../frontend/parser.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace templide::middleend {
    namespace {
        namespace ast = parser::ast;
        using lexor::Token;

        constexpr int max_expansion_depth = 64;

        struct EnumInfo {
            std::string name;
            std::vector<std::string> members;

            bool has(const std::string& member) const {
                return std::find(members.begin(), members.end(), member) != members.end();
            }
        };

        struct Type {
            // Fill과 Background는 색, 그라데이션, 무늬, 그림을 받는 속성 타입이다.
            // Link는 url 문자열, slide(번호), slide_jump 값을 받고, Ref는 put ... as로 붙인 개체 이름이다.
            // Action은 Link가 받는 값과 run(...), program(...), macro(...), file(...)을 받는다
            enum class Kind { Any, Error, Bool, Int, Float, String, Text, Color, Enum, Gradient, Image, Fill, Background, Pattern, Link, SlideRef, Ref, Action };
            Kind kind = Kind::Any;
            const EnumInfo* enumeration = nullptr;
        };
        using Kind = Type::Kind;

        bool same_type(const Type& a, const Type& b) {
            return a.kind == b.kind && a.enumeration == b.enumeration;
        }

        // from 타입의 값을 to 자리에 넣을 수 있는지. 에러 타입은 이미 보고했으므로 통과시킨다
        bool assignable(const Type& from, const Type& to) {
            if (to.kind == Kind::Any || from.kind == Kind::Error || to.kind == Kind::Error) {
                return true;
            }
            if (same_type(from, to)) {
                return true;
            }
            const bool paint = from.kind == Kind::Color || from.kind == Kind::Gradient || from.kind == Kind::Pattern || from.kind == Kind::Image;
            const bool link = from.kind == Kind::String || from.kind == Kind::SlideRef || (from.kind == Kind::Enum && from.enumeration->name == "slide_jump");
            return (from.kind == Kind::String && to.kind == Kind::Text) || (from.kind == Kind::Int && to.kind == Kind::Float)
                || (paint && (to.kind == Kind::Fill || to.kind == Kind::Background)) || (link && (to.kind == Kind::Link || to.kind == Kind::Action));
        }

        bool comparable(const Type& a, const Type& b) {
            return assignable(a, b) || assignable(b, a);
        }

        bool is_numeric(const Type& type) {
            return type.kind == Kind::Int || type.kind == Kind::Float;
        }

        std::string type_name(const Type& type) {
            switch (type.kind) {
                case Kind::Any: return "any";
                case Kind::Error: return "error";
                case Kind::Bool: return "bool";
                case Kind::Int: return "int";
                case Kind::Float: return "float";
                case Kind::String: return "string";
                case Kind::Text: return "text";
                case Kind::Color: return "color";
                case Kind::Enum: return type.enumeration->name;
                case Kind::Gradient: return "gradient";
                case Kind::Image: return "image";
                case Kind::Fill:
                case Kind::Background: return "color, gradient, pattern or image";
                case Kind::Pattern: return "pattern";
                case Kind::Link: return "link";
                case Kind::SlideRef: return "slide";
                case Kind::Ref: return "object name";
                case Kind::Action: return "action";
            }
            return "unknown";
        }

        struct VarInfo {
            std::string name;
            Type type;
            const ast::ASTNode* default_value = nullptr;
            bool optional = false; // 값을 넣지 않아도 되고, 넣지 않으면 결과에 없다
        };

        const VarInfo* find_var(const std::vector<VarInfo>& vars, const std::string& name) {
            for (const auto& var : vars) {
                if (var.name == name) {
                    return &var;
                }
            }
            return nullptr;
        }

        // 컴파일러에 내장된 style 속성
        struct PropertyInfo {
            std::string name;
            Type type;
            void (*apply)(ir::TextStyle&, const ir::Value&);
        };

        // 검사할 때 보이는 이름들
        struct Scope {
            const std::vector<VarInfo>* vars = nullptr;
            bool in_master = false; // master 안에서는 @slide를 쓸 수 없다
            bool in_slide = false;  // slide에 바로 적은 put, group이라 animate를 쓸 수 있다

            const VarInfo* find(const std::string& name) const {
                return vars == nullptr ? nullptr : find_var(*vars, name);
            }
        };

        // 계산할 때 보이는 값들
        struct Env {
            std::map<std::string, ir::Value> values;
            std::optional<int> page; // master를 펼칠 때는 비어 있다
            // 편집기를 위한 출처. origins는 values의 값이 원문의 어디에서 왔는지
            std::map<std::string, ir::Origin> origins;
            std::string lock;                      // 지금 실행하는 문장에 적힌 값을 화면에서 바꿀 수 없는 이유. 바꿀 수 있으면 빈 문자열
            std::string id;                        // 지금까지 거친 put 문장의 위치. element id의 앞부분
            std::optional<ir::Origin> instance;    // template을 펼치는 중이면 slide에 적은 바깥쪽 put
            std::vector<ir::Property> instance_xy; // 그 put에 적은 x, y, width, height
            // @self.name의 값. put의 속성 값을 계산할 때 그 put의 이름(없으면 빈 문자열)이고, 그 밖에서는 없다
            std::optional<std::string> self;
        };

        struct LayoutInfo {
            const ast::ASTMaster* master;
            const ast::ASTMasterCase* layout;
            const ast::ASTNode* expression;
            std::vector<ast::ASTNode*> arguments; // master의 매개변수에 넘기는 값
        };

        constexpr int max_loop_iterations = 10000;

        // PowerPoint의 전환 효과와 그 옵션. 첫 옵션이 기본값이고, 옵션이 없는 전환은 빈 문자열 하나를 가진다
        const std::map<std::string, std::vector<std::string>>& transition_options() {
            static const std::vector<std::string> none = {""};
            static const std::vector<std::string> directions4 = {"left", "up", "right", "down"};
            static const std::vector<std::string> directions8 = {"left", "up", "right", "down", "left_up", "right_up", "left_down", "right_down"};
            static const std::vector<std::string> left_right = {"left", "right"};
            static const std::vector<std::string> right_left = {"right", "left"};
            static const std::vector<std::string> orientations = {"horizontal", "vertical"};
            static const std::vector<std::string> black = {"smoothly", "through_black"};
            static const std::map<std::string, std::vector<std::string>> table = {
                {"cut", black},
                {"fade", black},
                {"random", none},
                {"blinds", orientations},
                {"checkerboard", {"across", "down"}},
                {"cover", directions8},
                {"uncover", directions8},
                {"dissolve", none},
                {"randomBars", orientations},
                {"strips", {"left_up", "right_up", "left_down", "right_down"}},
                {"wipe", directions4},
                {"push", directions4},
                {"box", {"out", "in", "left", "up", "right", "down"}},
                {"split", {"horizontal_out", "horizontal_in", "vertical_out", "vertical_in"}},
                {"circle", {"out"}},
                {"diamond", {"out"}},
                {"plus", {"out"}},
                {"comb", orientations},
                {"newsflash", none},
                {"wedge", none},
                {"wheel", {"spokes4", "spokes1", "spokes2", "spokes3", "spokes8"}},
                {"wheelReverse", {"spokes1"}},
                {"vortex", directions4},
                {"ripple", {"center", "left_up", "right_up", "left_down", "right_down"}},
                {"glitter", {"diamond_right", "diamond_left", "diamond_up", "diamond_down", "hexagon_right", "hexagon_left", "hexagon_up", "hexagon_down"}},
                {"gallery", left_right},
                {"conveyor", left_right},
                {"doors", orientations},
                {"window", orientations},
                {"warp", {"out", "in"}},
                {"flyThrough", {"in", "out", "in_bounce", "out_bounce"}},
                {"reveal", {"smooth_left", "smooth_right", "black_left", "black_right"}},
                {"honeycomb", none},
                {"ferrisWheel", left_right},
                {"switch", left_right},
                {"flip", left_right},
                {"flashbulb", none},
                {"shred", {"strips_in", "strips_out", "rectangle_in", "rectangle_out"}},
                {"cube", directions4},
                {"rotate", directions4},
                {"orbit", directions4},
                {"pan", directions4},
                {"fallOver", left_right},
                {"drape", left_right},
                {"curtains", none},
                {"wind", right_left},
                {"prestige", none},
                {"fracture", none},
                {"crush", none},
                {"peelOff", left_right},
                {"pageCurlSingle", left_right},
                {"pageCurlDouble", left_right},
                {"airplane", right_left},
                {"origami", right_left},
                {"morph", {"by_object", "by_word", "by_char"}},
            };
            return table;
        }

        struct AnimationInfo {
            std::vector<std::string> options; // 첫 옵션이 기본값. 옵션이 없는 효과는 빈 문자열 하나
            bool fixed = false;               // 끝나는 시간이 없어 길이를 정할 수 없는 효과
        };

        // "종류.효과" -> 옵션. 이름은 PowerPoint VBA의 MsoAnimEffect, 옵션은 MsoAnimDirection에서 왔고,
        // PowerPoint에서 결과가 같은 옵션은 하나로 합쳤다
        const std::map<std::string, AnimationInfo>& animation_options() {
            static const std::map<std::string, AnimationInfo> table = {
                {"enter.appear", {{""}, false}},
                {"exit.appear", {{""}, false}},
                {"enter.fly", {{"down", "up", "right", "left", "up_left", "up_right", "down_right", "down_left"}, false}},
                {"exit.fly", {{"down", "up", "right", "left", "up_left", "up_right", "down_right", "down_left"}, false}},
                {"enter.blinds", {{"horizontal", "vertical"}, false}},
                {"exit.blinds", {{"horizontal", "vertical"}, false}},
                {"enter.box", {{"in", "out"}, false}},
                {"exit.box", {{"in", "out"}, false}},
                {"enter.checkerboard", {{"horizontal", "vertical"}, false}},
                {"exit.checkerboard", {{"horizontal", "vertical"}, false}},
                {"enter.circle", {{"in", "out"}, false}},
                {"exit.circle", {{"in", "out"}, false}},
                {"enter.crawl", {{"down", "up", "right", "left", "up_left", "up_right", "down_right", "down_left"}, false}},
                {"exit.crawl", {{"down", "up", "right", "left", "up_left", "up_right", "down_right", "down_left"}, false}},
                {"enter.diamond", {{"in", "out"}, false}},
                {"exit.diamond", {{"in", "out"}, false}},
                {"enter.dissolve", {{""}, false}},
                {"exit.dissolve", {{""}, false}},
                {"enter.fade", {{""}, false}},
                {"exit.fade", {{""}, false}},
                {"enter.flashOnce", {{""}, false}},
                {"exit.flashOnce", {{""}, false}},
                {"enter.peek", {{"down", "up", "right", "left"}, false}},
                {"exit.peek", {{"down", "up", "right", "left"}, false}},
                {"enter.plus", {{"in", "out"}, false}},
                {"exit.plus", {{"in", "out"}, false}},
                {"enter.randomBars", {{"horizontal", "vertical"}, false}},
                {"exit.randomBars", {{"horizontal", "vertical"}, false}},
                {"enter.spiral", {{""}, false}},
                {"exit.spiral", {{""}, false}},
                {"enter.split", {{"vertical_in", "horizontal_in", "horizontal_out", "vertical_out"}, false}},
                {"exit.split", {{"vertical_in", "horizontal_in", "horizontal_out", "vertical_out"}, false}},
                {"enter.stretch", {{"horizontal", "up", "right", "down", "left"}, false}},
                {"exit.stretch", {{"horizontal", "up", "right", "down", "left"}, false}},
                {"enter.strips", {{"down_left", "up_left", "up_right", "down_right"}, false}},
                {"exit.strips", {{"down_left", "up_left", "up_right", "down_right"}, false}},
                {"enter.swivel", {{"horizontal", "vertical"}, false}},
                {"exit.swivel", {{"horizontal", "vertical"}, false}},
                {"enter.wedge", {{""}, false}},
                {"exit.wedge", {{""}, false}},
                {"enter.wheel", {{""}, false}},
                {"exit.wheel", {{""}, false}},
                {"enter.wipe", {{"down", "up", "right", "left"}, false}},
                {"exit.wipe", {{"down", "up", "right", "left"}, false}},
                {"enter.zoom", {{"in", "out", "in_slightly", "in_center", "out_slightly", "out_bottom"}, false}},
                {"exit.zoom", {{"in", "out", "in_slightly", "in_bottom", "out_slightly", "out_center"}, false}},
                {"enter.randomEffects", {{""}, false}},
                {"exit.randomEffects", {{""}, false}},
                {"enter.boomerang", {{""}, false}},
                {"exit.boomerang", {{""}, false}},
                {"enter.bounce", {{""}, false}},
                {"exit.bounce", {{""}, false}},
                {"enter.colorReveal", {{""}, false}},
                {"exit.colorReveal", {{""}, false}},
                {"enter.credits", {{""}, false}},
                {"exit.credits", {{""}, false}},
                {"enter.easeIn", {{""}, false}},
                {"exit.easeIn", {{""}, false}},
                {"enter.float", {{""}, false}},
                {"exit.float", {{""}, false}},
                {"enter.growAndTurn", {{""}, false}},
                {"exit.growAndTurn", {{""}, false}},
                {"enter.lightSpeed", {{""}, false}},
                {"exit.lightSpeed", {{""}, false}},
                {"enter.pinwheel", {{""}, false}},
                {"exit.pinwheel", {{""}, false}},
                {"enter.riseUp", {{""}, false}},
                {"exit.riseUp", {{""}, false}},
                {"enter.swish", {{""}, false}},
                {"exit.swish", {{""}, false}},
                {"enter.thinLine", {{""}, false}},
                {"exit.thinLine", {{""}, false}},
                {"enter.unfold", {{""}, false}},
                {"exit.unfold", {{""}, false}},
                {"enter.whip", {{""}, false}},
                {"exit.whip", {{""}, false}},
                {"enter.ascend", {{""}, false}},
                {"exit.ascend", {{""}, false}},
                {"enter.centerRevolve", {{""}, false}},
                {"exit.centerRevolve", {{""}, false}},
                {"enter.fadedSwivel", {{""}, false}},
                {"exit.fadedSwivel", {{""}, false}},
                {"enter.descend", {{""}, false}},
                {"exit.descend", {{""}, false}},
                {"enter.sling", {{""}, false}},
                {"exit.sling", {{""}, false}},
                {"enter.spinner", {{""}, false}},
                {"exit.spinner", {{""}, false}},
                {"enter.stretchy", {{""}, false}},
                {"exit.stretchy", {{""}, false}},
                {"enter.zip", {{""}, false}},
                {"exit.zip", {{""}, false}},
                {"enter.arcUp", {{""}, false}},
                {"exit.arcUp", {{""}, false}},
                {"enter.fadedZoom", {{"in", "in_center"}, false}},
                {"exit.fadedZoom", {{"out", "out_center"}, false}},
                {"enter.glide", {{""}, false}},
                {"exit.glide", {{""}, false}},
                {"enter.expand", {{""}, false}},
                {"exit.expand", {{""}, false}},
                {"enter.flip", {{""}, false}},
                {"exit.flip", {{""}, false}},
                {"emphasis.shimmer", {{""}, false}},
                {"enter.fold", {{""}, false}},
                {"exit.fold", {{""}, false}},
                {"emphasis.changeFillColor", {{""}, false}},
                {"emphasis.changeFont", {{""}, true}},
                {"emphasis.changeFontColor", {{""}, false}},
                {"emphasis.changeFontSize", {{""}, false}},
                {"emphasis.changeFontStyle", {{""}, true}},
                {"emphasis.growShrink", {{""}, false}},
                {"emphasis.changeLineColor", {{""}, false}},
                {"emphasis.spin", {{""}, false}},
                {"emphasis.transparency", {{""}, true}},
                {"emphasis.boldFlash", {{""}, false}},
                {"emphasis.blast", {{""}, false}},
                {"emphasis.boldReveal", {{""}, true}},
                {"emphasis.brushOnColor", {{""}, false}},
                {"emphasis.brushOnUnderline", {{""}, false}},
                {"emphasis.colorBlend", {{""}, false}},
                {"emphasis.colorWave", {{""}, false}},
                {"emphasis.complementaryColor", {{""}, false}},
                {"emphasis.complementaryColor2", {{""}, false}},
                {"emphasis.contrastingColor", {{""}, false}},
                {"emphasis.darken", {{""}, false}},
                {"emphasis.desaturate", {{""}, false}},
                {"emphasis.flashBulb", {{""}, false}},
                {"emphasis.flicker", {{""}, false}},
                {"emphasis.growWithColor", {{""}, false}},
                {"emphasis.lighten", {{""}, false}},
                {"emphasis.styleEmphasis", {{""}, false}},
                {"emphasis.teeter", {{""}, false}},
                {"emphasis.verticalGrow", {{""}, false}},
                {"emphasis.wave", {{""}, false}},
                {"move.circle", {{""}, false}},
                {"move.rightTriangle", {{""}, false}},
                {"move.diamond", {{""}, false}},
                {"move.hexagon", {{""}, false}},
                {"move.fivePointStar", {{""}, false}},
                {"move.crescentMoon", {{""}, false}},
                {"move.square", {{""}, false}},
                {"move.trapezoid", {{""}, false}},
                {"move.heart", {{""}, false}},
                {"move.octagon", {{""}, false}},
                {"move.sixPointStar", {{""}, false}},
                {"move.football", {{""}, false}},
                {"move.equalTriangle", {{""}, false}},
                {"move.parallelogram", {{""}, false}},
                {"move.pentagon", {{""}, false}},
                {"move.fourPointStar", {{""}, false}},
                {"move.eightPointStar", {{""}, false}},
                {"move.teardrop", {{""}, false}},
                {"move.pointyStar", {{""}, false}},
                {"move.curvedSquare", {{""}, false}},
                {"move.curvedX", {{""}, false}},
                {"move.verticalFigure8", {{""}, false}},
                {"move.curvyStar", {{""}, false}},
                {"move.loopdeLoop", {{""}, false}},
                {"move.buzzsaw", {{""}, false}},
                {"move.horizontalFigure8", {{""}, false}},
                {"move.peanut", {{""}, false}},
                {"move.figure8Four", {{""}, false}},
                {"move.neutron", {{""}, false}},
                {"move.swoosh", {{""}, false}},
                {"move.bean", {{""}, false}},
                {"move.plus", {{""}, false}},
                {"move.invertedTriangle", {{""}, false}},
                {"move.invertedSquare", {{""}, false}},
                {"move.left", {{""}, false}},
                {"move.turnRight", {{""}, false}},
                {"move.arcDown", {{""}, false}},
                {"move.zigzag", {{""}, false}},
                {"move.sCurve2", {{""}, false}},
                {"move.sineWave", {{""}, false}},
                {"move.bounceLeft", {{""}, false}},
                {"move.down", {{""}, false}},
                {"move.turnUp", {{""}, false}},
                {"move.arcUp", {{""}, false}},
                {"move.heartbeat", {{""}, false}},
                {"move.spiralRight", {{""}, false}},
                {"move.wave", {{""}, false}},
                {"move.curvyLeft", {{""}, false}},
                {"move.diagonalDownRight", {{""}, false}},
                {"move.turnDown", {{""}, false}},
                {"move.arcLeft", {{""}, false}},
                {"move.funnel", {{""}, false}},
                {"move.spring", {{""}, false}},
                {"move.bounceRight", {{""}, false}},
                {"move.spiralLeft", {{""}, false}},
                {"move.diagonalUpRight", {{""}, false}},
                {"move.turnUpRight", {{""}, false}},
                {"move.arcRight", {{""}, false}},
                {"move.sCurve1", {{""}, false}},
                {"move.decayingWave", {{""}, false}},
                {"move.curvyRight", {{""}, false}},
                {"move.stairsDown", {{""}, false}},
                {"move.up", {{""}, false}},
                {"move.right", {{""}, false}},
            };
            return table;
        }

        std::string join(const std::vector<std::string>& items) {
            std::string result;
            for (std::size_t i = 0; i < items.size(); ++i) {
                result += (i > 0 ? ", " : "") + items[i];
            }
            return result;
        }

        struct ParagraphContext {
            ir::ListKind list;
            int level;
        };

        struct TextBuilder {
            ir::Text text;
            bool open = false; // 마지막 문단에 run을 이어 붙일 수 있는지
        };

        std::vector<const ast::ASTNode*> parts_of(const ast::ASTNode* expr) {
            if (expr->type == ast::TEXT) {
                const auto& parts = static_cast<const ast::ASTText*>(expr)->parts;
                return {parts.begin(), parts.end()};
            }
            return {expr};
        }

        // 에러 위치로 쓸 토큰. 이항 연산과 멤버 접근은 식의 왼쪽 끝을 가리킨다
        const Token& location(const ast::ASTNode* expr) {
            switch (expr->type) {
                case ast::ADD: return location(static_cast<const ast::ASTAdd*>(expr)->left);
                case ast::MINUS: return location(static_cast<const ast::ASTMinus*>(expr)->left);
                case ast::MULTIPLY: return location(static_cast<const ast::ASTMultiply*>(expr)->left);
                case ast::EQUAL: return location(static_cast<const ast::ASTEqual*>(expr)->left);
                case ast::MEMBER: return location(static_cast<const ast::ASTMember*>(expr)->object);
                default: return expr->token;
            }
        }

        // PowerPoint가 보여 줄 때 값을 정하는 글자(@slide.number, @date)면 그 필드 종류
        std::string field_of(const ast::ASTNode* expr) {
            if (expr->type == ast::CONTEXT && static_cast<const ast::ASTContext*>(expr)->name->name == "date") {
                return "datetime1";
            }
            if (expr->type == ast::MEMBER) {
                const auto* member = static_cast<const ast::ASTMember*>(expr);
                if (member->object->type == ast::CONTEXT && static_cast<const ast::ASTContext*>(member->object)->name->name == "slide" && member->member->name == "number") {
                    return "slidenum";
                }
            }
            return "";
        }

        // 수의 단위 종류. 미들 엔드가 단위와 범위를 검사하고, backend는 %만 슬라이드 크기로 푼다
        enum class Measure {
            Position, // px 또는 %(슬라이드 크기에 대한 비율)
            Length,   // px
            Angle,    // 단위 없음 또는 deg
            Duration, // s 또는 ms
            Ratio,    // 단위 없는 수
            Percent,  // %
        };

        struct Rule {
            Measure measure;
            double min;
            double max;
            bool integer;
            std::string range; // 범위를 벗어났을 때 보여 줄 범위. 비어 있으면 min, max로 만든다

            Rule(Measure measure, double min = -std::numeric_limits<double>::infinity(), double max = std::numeric_limits<double>::infinity(),
                 bool integer = false, std::string range = "")
                : measure(measure), min(min), max(max), integer(integer), range(std::move(range)) {}
        };

        constexpr double pt = 4.0 / 3.0; // 1pt의 px

        // 절대 단위를 px로 바꾸는 배수. %, deg, s, ms는 그대로 두므로 0이다
        std::optional<double> px_per_unit(const std::string& unit) {
            if (unit == "px") {
                return 1.0;
            }
            if (unit == "pt") {
                return pt;
            }
            if (unit == "in") {
                return 96.0;
            }
            if (unit == "cm") {
                return 96.0 / 2.54;
            }
            if (unit == "mm") {
                return 96.0 / 25.4;
            }
            if (unit == "%" || unit == "deg" || unit == "s" || unit == "ms") {
                return 0.0;
            }
            return std::nullopt;
        }

        std::string measure_name(Measure measure) {
            switch (measure) {
                case Measure::Position: return "a length (px, pt, in, cm, mm) or a percentage of the slide";
                case Measure::Length: return "a length such as 10px or 2pt";
                case Measure::Angle: return "an angle without a unit or with deg";
                case Measure::Duration: return "a duration in s or ms";
                case Measure::Ratio: return "a number without a unit";
                case Measure::Percent: return "a percentage such as 30%";
            }
            return "";
        }

        bool unit_allowed(Measure measure, const std::string& unit) {
            switch (measure) {
                case Measure::Position: return unit.empty() || unit == "px" || unit == "%";
                case Measure::Length: return unit.empty() || unit == "px";
                case Measure::Angle: return unit.empty() || unit == "deg";
                case Measure::Duration: return unit == "s" || unit == "ms";
                case Measure::Ratio: return unit.empty();
                case Measure::Percent: return unit == "%";
            }
            return false;
        }

        std::string format_bound(double value) {
            std::ostringstream out;
            out << value;
            return out.str();
        }

        // 모든 object의 속성 중 수인 것의 규칙
        const std::map<std::string, Rule>& property_rules() {
            static const std::map<std::string, Rule> rules = [] {
                std::map<std::string, Rule> result = {
                    {"x", {Measure::Position}}, {"y", {Measure::Position}}, {"width", {Measure::Position, 0}}, {"height", {Measure::Position, 0}},
                    {"x1", {Measure::Position}}, {"y1", {Measure::Position}}, {"x2", {Measure::Position}}, {"y2", {Measure::Position}},
                    {"rotation", {Measure::Angle}}, {"shadow_angle", {Measure::Angle}}, {"inner_shadow_angle", {Measure::Angle}},
                    {"rotation_x", {Measure::Angle}}, {"rotation_y", {Measure::Angle}}, {"perspective", {Measure::Angle, 0, 180}},
                    {"opacity", {Measure::Ratio, 0, 1}}, {"reflection", {Measure::Ratio, 0, 1}}, {"reflection_size", {Measure::Ratio, 0, 1}},
                    {"columns", {Measure::Ratio, 1, 16, true}},
                };
                for (const char* name : {"line_width", "shadow_blur", "shadow_distance", "inner_shadow_blur", "inner_shadow_distance", "glow_size", "soft_edge",
                                         "reflection_distance", "reflection_blur", "bevel_width", "bevel_height", "depth", "radius", "padding", "padding_left",
                                         "padding_top", "padding_right", "padding_bottom", "column_gap", "blur"}) {
                    result.emplace(name, Rule{Measure::Length, 0});
                }
                for (const char* name : {"crop_left", "crop_top", "crop_right", "crop_bottom", "volume"}) {
                    result.emplace(name, Rule{Measure::Percent, 0, 100});
                }
                for (const char* name : {"trim_start", "trim_end", "fade_in", "fade_out"}) {
                    result.emplace(name, Rule{Measure::Duration, 0});
                }
                for (int i = 1; i <= 8; ++i) {
                    result.emplace("adj" + std::to_string(i), Rule{Measure::Ratio});
                }
                return result;
            }();
            return rules;
        }

        // style 속성 중 수인 것의 규칙
        const std::map<std::string, Rule>& style_rules() {
            static const std::map<std::string, Rule> rules = {
                {"font-size", {Measure::Length, pt, 4000 * pt, false, "1pt and 4000pt"}},
                {"line-height", {Measure::Ratio, 0}},
                {"letter-spacing", {Measure::Length}},
                {"space-before", {Measure::Length, 0}},
                {"space-after", {Measure::Length, 0}},
                {"margin-left", {Measure::Length, 0}},
                {"text-indent", {Measure::Length}},
                {"list-start", {Measure::Ratio, 1, 32767, true}},
            };
            return rules;
        }

        ir::ListKind to_ir(ast::ListKind kind) {
            switch (kind) {
                case ast::ListKind::BULLETS: return ir::ListKind::BULLETS;
                case ast::ListKind::NUMBERS: return ir::ListKind::NUMBERS;
                case ast::ListKind::DASHES: return ir::ListKind::DASHES;
                case ast::ListKind::PARAGRAPHS: return ir::ListKind::NONE;
            }
            return ir::ListKind::NONE;
        }

        ir::Number make_number(const std::string& unit, double value, bool is_float) {
            return ir::Number{{{unit, value}}, is_float};
        }

        ir::Number as_number(const ir::Value& value) {
            const auto* number = std::get_if<ir::Number>(&value);
            return number == nullptr ? ir::Number{} : *number;
        }

        // 단위가 없는 수이면 그 값
        std::optional<double> scalar(const ir::Number& number) {
            if (number.terms.empty()) {
                return 0.0;
            }
            if (number.terms.size() == 1 && number.terms[0].first.empty()) {
                return number.terms[0].second;
            }
            return std::nullopt;
        }

        void add_term(ir::Number& number, const std::string& unit, double value) {
            for (auto& term : number.terms) {
                if (term.first == unit) {
                    term.second += value;
                    return;
                }
            }
            number.terms.emplace_back(unit, value);
        }

        // 단위가 있는 항과 섞인 단위 없는 항은 px로 합치고, 0이 된 항은 지운다
        void normalize(ir::Number& number) {
            const bool has_unit = std::any_of(number.terms.begin(), number.terms.end(), [](const auto& term) { return !term.first.empty(); });
            if (has_unit) {
                const auto unitless = std::find_if(number.terms.begin(), number.terms.end(), [](const auto& term) { return term.first.empty(); });
                if (unitless != number.terms.end()) {
                    const double value = unitless->second;
                    number.terms.erase(unitless);
                    add_term(number, "px", value);
                }
            }
            std::erase_if(number.terms, [](const auto& term) { return term.second == 0; });
        }

        bool numbers_equal(ir::Number a, ir::Number b) {
            normalize(a);
            normalize(b);
            if (a.terms.size() != b.terms.size()) {
                return false;
            }
            for (const auto& [unit, value] : a.terms) {
                const auto other = std::find_if(b.terms.begin(), b.terms.end(), [&](const auto& term) { return term.first == unit; });
                if (other == b.terms.end() || other->second != value) {
                    return false;
                }
            }
            return true;
        }

        std::string plain_text(const ir::Text& text) {
            std::string result;
            for (std::size_t i = 0; i < text.paragraphs.size(); ++i) {
                if (i > 0) {
                    result += '\n';
                }
                for (const auto& run : text.paragraphs[i].runs) {
                    result += run.text;
                }
            }
            return result;
        }

        // over에 지정된 속성이 base를 덮어쓴다
        ir::TextStyle merge(const ir::TextStyle& base, const ir::TextStyle& over) {
            ir::TextStyle result = base;
            if (over.color) {
                result.color = over.color;
            }
            if (over.font_weight) {
                result.font_weight = over.font_weight;
            }
            if (over.line_height) {
                result.line_height = over.line_height;
            }
            if (over.font_family) {
                result.font_family = over.font_family;
            }
            if (over.font_size) {
                result.font_size = over.font_size;
            }
            if (over.text_align) {
                result.text_align = over.text_align;
            }
            const auto take = [](auto& target, const auto& value) {
                if (value) {
                    target = value;
                }
            };
            take(result.font_style, over.font_style);
            take(result.text_decoration, over.text_decoration);
            take(result.vertical_align, over.vertical_align);
            take(result.letter_spacing, over.letter_spacing);
            take(result.highlight, over.highlight);
            take(result.text_transform, over.text_transform);
            take(result.space_before, over.space_before);
            take(result.space_after, over.space_after);
            take(result.margin_left, over.margin_left);
            take(result.text_indent, over.text_indent);
            take(result.list_marker, over.list_marker);
            take(result.list_marker_color, over.list_marker_color);
            take(result.list_style, over.list_style);
            take(result.list_start, over.list_start);
            take(result.link, over.link);
            take(result.action, over.action);
            take(result.hover_action, over.hover_action);
            return result;
        }

        std::string format_link(const ir::Link& link) {
            if (!link.url.empty()) {
                return link.url;
            }
            return link.slide > 0 ? "slide(" + std::to_string(link.slide) + ")" : link.jump;
        }

        std::string format_action(const ir::Action& action) {
            if (action.kind == "link") {
                return format_link(action.link);
            }
            std::string result = action.kind + "(\"" + action.target + "\"";
            for (const auto& argument : action.arguments) {
                if (const auto* flag = std::get_if<bool>(&argument)) {
                    result += *flag ? ", true" : ", false";
                } else if (const auto* number = std::get_if<double>(&argument)) {
                    result += ", " + ir::format_scalar(*number);
                } else {
                    result += ", \"" + std::get<std::string>(argument) + "\"";
                }
            }
            return result + ")";
        }

        std::string format_value(const ir::Value& value) {
            if (const auto* boolean = std::get_if<bool>(&value)) {
                return *boolean ? "true" : "false";
            }
            if (const auto* number = std::get_if<ir::Number>(&value)) {
                return ir::format_number(*number);
            }
            if (const auto* string = std::get_if<std::string>(&value)) {
                return *string;
            }
            if (const auto* color = std::get_if<ir::Color>(&value)) {
                return ir::format_color(*color);
            }
            if (const auto* enum_value = std::get_if<ir::EnumValue>(&value)) {
                return enum_value->member;
            }
            if (const auto* gradient = std::get_if<ir::Gradient>(&value)) {
                return ir::format_gradient(*gradient);
            }
            if (const auto* image = std::get_if<ir::Image>(&value)) {
                return "image(\"" + image->path + "\")";
            }
            if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
                return "pattern(" + pattern->kind + ", " + ir::format_color(pattern->foreground) + ", " + ir::format_color(pattern->background) + ")";
            }
            if (const auto* link = std::get_if<ir::Link>(&value)) {
                return format_link(*link);
            }
            if (const auto* action = std::get_if<ir::Action>(&value)) {
                return format_action(*action);
            }
            return plain_text(std::get<ir::Text>(value));
        }

        // text끼리는 글자 내용만 비교한다
        bool values_equal(const ir::Value& a, const ir::Value& b) {
            const auto textual = [](const ir::Value& value) -> std::optional<std::string> {
                if (const auto* string = std::get_if<std::string>(&value)) {
                    return *string;
                }
                if (const auto* text = std::get_if<ir::Text>(&value)) {
                    return plain_text(*text);
                }
                return std::nullopt;
            };
            const auto a_text = textual(a);
            const auto b_text = textual(b);
            if (a_text || b_text) {
                return a_text == b_text;
            }
            if (a.index() != b.index()) {
                return false;
            }
            if (const auto* number = std::get_if<ir::Number>(&a)) {
                return numbers_equal(*number, std::get<ir::Number>(b));
            }
            if (const auto* boolean = std::get_if<bool>(&a)) {
                return *boolean == std::get<bool>(b);
            }
            const auto colors_equal = [](const ir::Color& x, const ir::Color& y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a && x.scheme == y.scheme; };
            if (const auto* color = std::get_if<ir::Color>(&a)) {
                return colors_equal(*color, std::get<ir::Color>(b));
            }
            if (const auto* gradient = std::get_if<ir::Gradient>(&a)) {
                return ir::format_gradient(*gradient) == ir::format_gradient(std::get<ir::Gradient>(b));
            }
            if (const auto* image = std::get_if<ir::Image>(&a)) {
                return image->path == std::get<ir::Image>(b).path;
            }
            if (const auto* pattern = std::get_if<ir::Pattern>(&a)) {
                const auto& other = std::get<ir::Pattern>(b);
                return pattern->kind == other.kind && colors_equal(pattern->foreground, other.foreground) && colors_equal(pattern->background, other.background);
            }
            if (const auto* link = std::get_if<ir::Link>(&a)) {
                const auto& other = std::get<ir::Link>(b);
                return link->url == other.url && link->slide == other.slide && link->jump == other.jump;
            }
            if (const auto* action = std::get_if<ir::Action>(&a)) {
                return format_action(*action) == format_action(std::get<ir::Action>(b));
            }
            const auto& left = std::get<ir::EnumValue>(a);
            const auto& right = std::get<ir::EnumValue>(b);
            return left.type == right.type && left.member == right.member;
        }

        ir::Color hex_color(std::string digits) {
            if (digits.size() == 3 || digits.size() == 4) {
                std::string expanded;
                for (const char c : digits) {
                    expanded += c;
                    expanded += c;
                }
                digits = expanded;
            }
            const auto channel = [&](std::size_t index) { return std::stoi(digits.substr(index * 2, 2), nullptr, 16); };
            ir::Color color{channel(0), channel(1), channel(2), 1, ""};
            if (digits.size() == 8) {
                color.a = channel(3) / 255.0;
            }
            return color;
        }

        class Analyzer {
        public:
            Analyzer(std::filesystem::path packages_dir, const Overlays& overlays) : packages_dir_(std::move(packages_dir)) {
                // load()가 찾는 키와 같게 맞춘다
                for (const auto& [path, text] : overlays) {
                    overlays_.emplace(file_key(path), text);
                }
                for (const char* name : {"int", "float", "string", "text", "color", "bool", "ref", "true", "false", "theme"}) {
                    builtin_names_.insert(name);
                }
                add_builtin_enum("font_weight", {"normal", "bold"});
                add_builtin_enum("target_type", {"pptx", "html", "web"});
                add_builtin_enum("angle_convention", {"powerpoint", "css"});
                add_builtin_enum("text_align", {"left", "center", "right", "justify"});
                add_builtin_enum("font_style", {"normal", "italic"});
                add_builtin_enum("text_decoration", {"none", "underline", "double_underline", "wavy_underline", "line_through", "double_line_through"});
                add_builtin_enum("vertical_align", {"baseline", "super", "sub"});
                add_builtin_enum("text_transform", {"none", "uppercase", "small_caps"});
                add_builtin_enum("list_style", {"decimal", "lower_alpha", "upper_alpha", "lower_roman", "upper_roman", "circled"});
                add_builtin_enum("text_autofit", {"none", "shrink", "resize"});
                add_builtin_enum("text_direction", {"horizontal", "vertical", "vertical270", "stacked", "east_asian"});
                add_builtin_enum("flip_direction", {"none", "horizontal", "vertical", "both"});
                add_builtin_enum("dash_style", {"solid", "dot", "dash", "lgDash", "dashDot", "lgDashDot", "lgDashDotDot", "sysDash", "sysDot", "sysDashDot", "sysDashDotDot"});
                add_builtin_enum("line_cap", {"flat", "round", "square"});
                add_builtin_enum("line_join", {"round", "bevel", "miter"});
                add_builtin_enum("line_compound", {"single", "double", "thick_thin", "thin_thick", "triple"});
                add_builtin_enum("bevel_kind", {"circle", "relaxed_inset", "cross", "cool_slant", "angle", "soft_round", "convex", "slope", "divot", "riblet", "hard_edge", "art_deco"});
                add_builtin_enum("pattern_kind", {
                    "percent_5", "percent_10", "percent_20", "percent_25", "percent_30", "percent_40", "percent_50", "percent_60", "percent_70", "percent_75", "percent_80", "percent_90",
                    "horizontal", "vertical", "light_horizontal", "light_vertical", "dark_horizontal", "dark_vertical", "narrow_horizontal", "narrow_vertical",
                    "dashed_horizontal", "dashed_vertical", "cross", "downward_diagonal", "upward_diagonal", "light_downward_diagonal", "light_upward_diagonal",
                    "dark_downward_diagonal", "dark_upward_diagonal", "wide_downward_diagonal", "wide_upward_diagonal", "dashed_downward_diagonal", "dashed_upward_diagonal",
                    "diagonal_cross", "small_checker", "large_checker", "small_grid", "large_grid", "dotted_grid", "small_confetti", "large_confetti",
                    "horizontal_brick", "diagonal_brick", "solid_diamond", "outlined_diamond", "dotted_diamond", "plaid", "sphere", "weave", "divot",
                    "shingle", "wave", "trellis", "zigzag",
                });
                add_builtin_enum("slide_jump", {"next_slide", "previous_slide", "first_slide", "last_slide", "last_viewed_slide", "end_show"});
                add_builtin_enum("placeholder_role", {"title", "subtitle", "body"});
                properties_ = {
                    {"color", Type{Kind::Color}, [](ir::TextStyle& style, const ir::Value& value) { style.color = std::get<ir::Color>(value); }},
                    {"font-weight", enum_type("font_weight"), [](ir::TextStyle& style, const ir::Value& value) { style.font_weight = std::get<ir::EnumValue>(value); }},
                    {"line-height", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.line_height = std::get<ir::Number>(value); }},
                    {"font-family", Type{Kind::String}, [](ir::TextStyle& style, const ir::Value& value) { style.font_family = std::get<std::string>(value); }},
                    {"font-size", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.font_size = std::get<ir::Number>(value); }},
                    {"text-align", enum_type("text_align"), [](ir::TextStyle& style, const ir::Value& value) { style.text_align = std::get<ir::EnumValue>(value); }},
                    {"font-style", enum_type("font_style"), [](ir::TextStyle& style, const ir::Value& value) { style.font_style = std::get<ir::EnumValue>(value); }},
                    {"text-decoration", enum_type("text_decoration"), [](ir::TextStyle& style, const ir::Value& value) { style.text_decoration = std::get<ir::EnumValue>(value); }},
                    {"vertical-align", enum_type("vertical_align"), [](ir::TextStyle& style, const ir::Value& value) { style.vertical_align = std::get<ir::EnumValue>(value); }},
                    {"letter-spacing", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.letter_spacing = std::get<ir::Number>(value); }},
                    {"highlight", Type{Kind::Color}, [](ir::TextStyle& style, const ir::Value& value) { style.highlight = std::get<ir::Color>(value); }},
                    {"text-transform", enum_type("text_transform"), [](ir::TextStyle& style, const ir::Value& value) { style.text_transform = std::get<ir::EnumValue>(value); }},
                    {"space-before", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.space_before = std::get<ir::Number>(value); }},
                    {"space-after", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.space_after = std::get<ir::Number>(value); }},
                    {"margin-left", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.margin_left = std::get<ir::Number>(value); }},
                    {"text-indent", Type{Kind::Float}, [](ir::TextStyle& style, const ir::Value& value) { style.text_indent = std::get<ir::Number>(value); }},
                    {"list-marker", Type{Kind::String}, [](ir::TextStyle& style, const ir::Value& value) { style.list_marker = std::get<std::string>(value); }},
                    {"list-marker-color", Type{Kind::Color}, [](ir::TextStyle& style, const ir::Value& value) { style.list_marker_color = std::get<ir::Color>(value); }},
                    {"list-style", enum_type("list_style"), [](ir::TextStyle& style, const ir::Value& value) { style.list_style = std::get<ir::EnumValue>(value); }},
                    {"list-start", Type{Kind::Int}, [](ir::TextStyle& style, const ir::Value& value) { style.list_start = std::get<ir::Number>(value); }},
                };
                common_properties_ = {
                    {"rotation", Type{Kind::Float}, nullptr, true},
                    {"fill", Type{Kind::Fill}, nullptr, true},
                    {"line_color", Type{Kind::Color}, nullptr, true},
                    {"line_width", Type{Kind::Float}, nullptr, true},
                    {"shadow", Type{Kind::Color}, nullptr, true},
                    {"shadow_blur", Type{Kind::Float}, nullptr, true},
                    {"shadow_distance", Type{Kind::Float}, nullptr, true},
                    {"shadow_angle", Type{Kind::Float}, nullptr, true},
                    {"inner_shadow", Type{Kind::Color}, nullptr, true},
                    {"inner_shadow_blur", Type{Kind::Float}, nullptr, true},
                    {"inner_shadow_distance", Type{Kind::Float}, nullptr, true},
                    {"inner_shadow_angle", Type{Kind::Float}, nullptr, true},
                    {"radius", Type{Kind::Float}, nullptr, true},
                    {"flip", enum_type("flip_direction"), nullptr, true},
                    {"opacity", Type{Kind::Float}, nullptr, true},
                    {"line_dash", enum_type("dash_style"), nullptr, true},
                    {"line_cap", enum_type("line_cap"), nullptr, true},
                    {"line_join", enum_type("line_join"), nullptr, true},
                    {"line_compound", enum_type("line_compound"), nullptr, true},
                    {"glow", Type{Kind::Color}, nullptr, true},
                    {"glow_size", Type{Kind::Float}, nullptr, true},
                    {"soft_edge", Type{Kind::Float}, nullptr, true},
                    {"reflection", Type{Kind::Float}, nullptr, true},
                    {"reflection_size", Type{Kind::Float}, nullptr, true},
                    {"reflection_distance", Type{Kind::Float}, nullptr, true},
                    {"reflection_blur", Type{Kind::Float}, nullptr, true},
                    {"bevel", enum_type("bevel_kind"), nullptr, true},
                    {"bevel_width", Type{Kind::Float}, nullptr, true},
                    {"bevel_height", Type{Kind::Float}, nullptr, true},
                    {"depth", Type{Kind::Float}, nullptr, true},
                    {"depth_color", Type{Kind::Color}, nullptr, true},
                    {"rotation_x", Type{Kind::Float}, nullptr, true},
                    {"rotation_y", Type{Kind::Float}, nullptr, true},
                    {"perspective", Type{Kind::Float}, nullptr, true},
                    {"link", Type{Kind::Link}, nullptr, true},
                    // PowerPoint의 실행 설정. 누를 때와 마우스를 올릴 때 하는 일, 함께 낼 소리(wav), 강조
                    {"action", Type{Kind::Action}, nullptr, true},
                    {"hover_action", Type{Kind::Action}, nullptr, true},
                    {"action_sound", Type{Kind::String}, nullptr, true},
                    {"hover_sound", Type{Kind::String}, nullptr, true},
                    {"action_highlight", Type{Kind::Bool}, nullptr, true},
                    {"hover_highlight", Type{Kind::Bool}, nullptr, true},
                };
                for (int i = 1; i <= 8; ++i) {
                    common_properties_.push_back({"adj" + std::to_string(i), Type{Kind::Float}, nullptr, true});
                }
                // text가 있는 object(text_box, shape 등)에만 붙는 글상자 속성
                text_properties_ = {
                    {"padding", Type{Kind::Float}, nullptr, true},
                    {"padding_left", Type{Kind::Float}, nullptr, true},
                    {"padding_top", Type{Kind::Float}, nullptr, true},
                    {"padding_right", Type{Kind::Float}, nullptr, true},
                    {"padding_bottom", Type{Kind::Float}, nullptr, true},
                    {"autofit", enum_type("text_autofit"), nullptr, true},
                    {"wrap", Type{Kind::Bool}, nullptr, true},
                    {"text_direction", enum_type("text_direction"), nullptr, true},
                    {"columns", Type{Kind::Int}, nullptr, true},
                    {"column_gap", Type{Kind::Float}, nullptr, true},
                };
            }

            Result run(const std::filesystem::path& path) {
                Result result = analyze_file(path);
                for (const auto& source : sources_) {
                    result.sources.emplace_back(source.path, source.text);
                }
                result.symbols = std::move(symbols_);
                return result;
            }

            // 자동 완성에 쓸 이름. collect()로 선언을 모은 뒤에 부른다
            void gather_symbols() {
                const auto var_of = [](const VarInfo& var) {
                    return SymbolVar{var.name, type_name(var.type), var.default_value ? std::string(var.default_value->span) : "", !var.default_value && !var.optional};
                };
                const auto vars_of = [&](const std::vector<VarInfo>& vars) {
                    std::vector<SymbolVar> result;
                    for (const auto& var : vars) {
                        result.push_back(var_of(var));
                    }
                    return result;
                };
                symbols_.valid = true;
                for (const auto& [name, node] : objects_) {
                    symbols_.objects[name] = vars_of(signatures_.at(node));
                }
                for (const auto& [name, node] : templates_) {
                    symbols_.templates[name] = vars_of(signatures_.at(node));
                }
                for (const auto& [name, node] : styles_) {
                    std::vector<std::string> parameters;
                    for (const auto& var : signatures_.at(node)) {
                        parameters.push_back(type_name(var.type));
                    }
                    symbols_.styles[name] = parameters;
                }
                for (const auto& [name, info] : enums_) {
                    symbols_.enums[name] = info.members;
                }
                for (const auto& [name, master] : masters_) {
                    SymbolMaster info;
                    for (const auto& var : signatures_.at(master)) {
                        info.parameters.push_back(type_name(var.type));
                    }
                    for (const auto* statement : master->body) {
                        info.cases.push_back(static_cast<const ast::ASTMasterCase*>(statement)->name);
                    }
                    symbols_.masters[name] = info;
                }
                for (const auto& [name, theme] : themes_) {
                    symbols_.themes.push_back(name);
                }
                symbols_.common_properties = vars_of(common_properties_);
                symbols_.text_properties = vars_of(text_properties_);
                for (const auto& property : properties_) {
                    symbols_.style_properties.push_back({property.name, type_name(property.type), "", false});
                }
                for (const auto& [name, type] : slide_properties()) {
                    symbols_.slide_properties.push_back({name, type_name(type), "", false});
                }
                for (const auto& [kind, options] : transition_options()) {
                    std::vector<std::string> named;
                    for (const auto& option : options) {
                        if (!option.empty()) {
                            named.push_back(option);
                        }
                    }
                    symbols_.transitions[kind] = named;
                }
                for (const auto& [key, info] : animation_options()) {
                    std::vector<std::string> named;
                    for (const auto& option : info.options) {
                        if (!option.empty()) {
                            named.push_back(option);
                        }
                    }
                    symbols_.animations[key] = named;
                    if (info.fixed) {
                        symbols_.endless_animations.push_back(key);
                    }
                }
                for (const auto& [name, scheme] : theme_colors()) {
                    symbols_.theme_colors.push_back(name);
                }
            }

            Result analyze_file(const std::filesystem::path& path) {
                main_path_ = path.string();
                load(path, nullptr);
                if (diagnostics_.empty()) {
                    collect();
                    check();
                    gather_symbols(); // check()가 매개변수 정보(signatures_)를 채운 뒤
                }
                if (!diagnostics_.empty()) {
                    return fail();
                }
                ir::Document document = build_document();
                if (!diagnostics_.empty()) {
                    return fail();
                }
                return {std::move(document), {}};
            }

        private:
            struct Source {
                std::string path;
                std::string text;
                bool package = false; // packages_dir 안의 파일. 편집기에서 고칠 수 없다
            };

            std::filesystem::path packages_dir_;
            std::map<std::filesystem::path, std::string> overlays_; // file_key -> 내용
            std::string main_path_;
            // Token의 string_view가 가리키므로 원소의 주소가 바뀌면 안 된다
            std::deque<Source> sources_;
            std::set<std::filesystem::path> loaded_;
            std::vector<const ast::ASTNode*> statements_;

            std::vector<Diagnostic> diagnostics_;
            Symbols symbols_;
            std::set<std::tuple<std::string, std::size_t, std::size_t, std::string>> reported_;

            std::set<std::string> builtin_names_;
            std::set<std::string> declared_names_;
            std::map<std::string, EnumInfo> enums_;
            std::vector<PropertyInfo> properties_;
            // 모든 object에 자동으로 붙는 속성
            std::vector<VarInfo> common_properties_;
            std::vector<VarInfo> text_properties_;
            std::map<std::string, const ast::ASTStyle*> styles_;
            std::map<std::string, const ast::ASTTemplate*> templates_;
            std::map<std::string, const ast::ASTObject*> objects_;
            std::map<std::string, const ast::ASTMaster*> masters_;
            std::vector<const ast::ASTMaster*> master_order_;
            std::vector<const ast::ASTSlide*> slides_;
            std::vector<const ast::ASTTarget*> targets_;
            std::map<std::string, const ast::ASTTheme*> themes_;
            // slide마다 그 앞의 가장 가까운 section 문장
            std::map<const ast::ASTSlide*, const ast::ASTSection*> slide_sections_;
            const ast::ASTSection* current_section_ = nullptr;

            // style의 매개변수, template과 object의 var
            std::map<const ast::ASTNode*, std::vector<VarInfo>> signatures_;
            std::map<const ast::ASTSlide*, LayoutInfo> slide_layouts_;
            std::map<const ast::ASTFor*, Type> for_types_;
            // (master, 이름과 매개변수 값) -> Document::masters의 index
            std::map<std::pair<const ast::ASTMaster*, std::string>, std::size_t> master_instances_;
            bool expansion_failed_ = false;

            // 지금 만드는 slide나 layout에서 이름을 붙인 개체와 연결선
            struct NamedElement {
                std::string object;
                std::string kind; // 연결점 표의 도형 종류
                std::string id;   // element의 id
            };
            // 블록 안의 animate로 만든 애니메이션과 그 문장의 위치. slide를 다 펼친 뒤 재생 차례로 늘어놓는다
            struct PendingAnimation {
                ir::Animation animation;
                std::pair<std::size_t, std::size_t> position;
            };
            std::vector<PendingAnimation> slide_animations_;
            std::map<std::string, std::pair<std::size_t, std::size_t>> media_positions_; // video, audio element id -> put 문장의 위치
            struct PendingConnector {
                std::string from;
                std::string to;
                Token from_token;
                Token to_token;
            };
            struct ElementScope {
                bool layout = false;
                std::map<std::string, NamedElement> names;
                std::vector<PendingConnector> connectors;
            };
            ElementScope scope_;

            // ---- 파일 읽기와 에러

            // 같은 파일을 가리키는 경로를 하나로 맞춘 것
            static std::filesystem::path file_key(const std::filesystem::path& path) {
                std::error_code error_code;
                std::filesystem::path key = std::filesystem::weakly_canonical(path, error_code);
                return error_code ? path : key;
            }

            void load(const std::filesystem::path& path, const ast::ASTInclude* include) {
                const std::filesystem::path key = file_key(path);
                if (!loaded_.insert(key).second) {
                    return;
                }

                std::string text;
                if (const auto overlay = overlays_.find(key); overlay != overlays_.end()) {
                    text = overlay->second;
                } else {
                    std::ifstream file(path, std::ios::binary);
                    if (!file) {
                        if (include != nullptr) {
                            error(include->token, std::string(include->relative ? "Cannot find file: " : "Cannot find package: ") + path.string());
                        } else {
                            diagnostics_.push_back({path.string(), 0, 0, "Cannot open file"});
                        }
                        return;
                    }
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    text = buffer.str();
                }
                const std::filesystem::path relative = key.lexically_relative(file_key(packages_dir_));
                sources_.push_back({path.string(), std::move(text), !relative.empty() && *relative.begin() != ".."});
                const Source& source = sources_.back();

                lexor::Lexor lexer(source.text);
                parser::Parser file_parser(source.path, lexer);
                const ast::ASTFile* file_node = file_parser.parse();
                if (file_node == nullptr) {
                    for (const auto& parse_error : file_parser.errors) {
                        diagnostics_.push_back({source.path, parse_error.line, parse_error.column, parse_error.message});
                    }
                    return;
                }
                for (const auto* statement : file_node->body) {
                    if (statement->type == ast::INCLUDE) {
                        const auto* include = static_cast<const ast::ASTInclude*>(statement);
                        const std::string file = include->name + ".tlide";
                        const std::filesystem::path base = include->relative ? path.parent_path() : packages_dir_;
                        load((base / std::filesystem::path(std::u8string(file.begin(), file.end()))).lexically_normal(), include);
                    } else {
                        statements_.push_back(statement);
                    }
                }
            }

            // ---- 편집기를 위한 출처

            const Source* source_of(const char* position) const {
                const std::less<const char*> less;
                for (const auto& source : sources_) {
                    const char* begin = source.text.data();
                    if (!less(position, begin) && !less(begin + source.text.size(), position)) {
                        return &source;
                    }
                }
                return nullptr;
            }

            ir::SourceRange range_of(const ast::ASTNode* node) const {
                const Source* source = source_of(node->span.data());
                if (source == nullptr) {
                    return {};
                }
                const auto begin = static_cast<std::size_t>(node->span.data() - source->text.data());
                return {source->path, begin, begin + node->span.size()};
            }

            bool in_package(const ast::ASTNode* node) const {
                const Source* source = source_of(node->span.data());
                return source != nullptr && source->package;
            }

            static ir::Origin locked(std::string reason) {
                ir::Origin origin;
                origin.reason = std::move(reason);
                return origin;
            }

            // 바꿀 수 있는 put, slide, group 문장. 바꿀 수 없으면 env.lock을 이유로 잠근다
            ir::Origin block_of(const ast::ASTNode* statement, const Env& env) const {
                if (in_package(statement)) {
                    return locked("package");
                }
                if (!env.lock.empty()) {
                    return locked(env.lock);
                }
                ir::Origin origin;
                origin.kind = ir::Origin::Kind::BLOCK;
                origin.range = range_of(statement);
                return origin;
            }

            // 화면에서 바로 바꿀 수 있는 리터럴. 테마 색(theme.accent1)도 다른 색으로 바꿀 수 있다
            static bool is_literal(const ast::ASTNode* expr) {
                switch (expr->type) {
                    case ast::INT:
                    case ast::FLOAT:
                    case ast::DIMENSION:
                    case ast::STRING:
                    case ast::COLOR_HEX:
                    case ast::COLOR_RGB:
                    case ast::COLOR_RGBA:
                    case ast::NAME:
                        return true;
                    case ast::MEMBER: {
                        const auto* object = static_cast<const ast::ASTMember*>(expr)->object;
                        return object->type == ast::NAME && static_cast<const ast::ASTName*>(object)->name == "theme";
                    }
                    case ast::LIST: {
                        // 문자열만 든 bullets [...], numbers [...] 같은 목록. 편집기는 목록 종류를 바꿀 때 식 전체를 바꾼다
                        const auto& items = static_cast<const ast::ASTList*>(expr)->items;
                        return std::all_of(items.begin(), items.end(), [](const ast::ASTNode* item) { return item->type == ast::STRING || item->type == ast::LIST && is_literal(item); });
                    }
                    case ast::CALL: {
                        // 리터럴만 넘긴 linear(...), pattern(...), image(...), slide(...), run(...) 같은 값. 편집기는 식 전체를 바꾼다
                        static const std::set<std::string> values = {"linear", "radial", "pattern", "image", "slide", "run", "program", "macro", "file"};
                        const auto* call = static_cast<const ast::ASTCall*>(expr);
                        return values.contains(call->name->name) && std::all_of(call->arguments.begin(), call->arguments.end(), [](const ast::ASTNode* argument) {
                            return is_literal(argument);
                        });
                    }
                    default:
                        return false;
                }
            }

            // 'name = 값;' 대입의 값 출처. 바꿀 수 있으면 문장 전체를 함께 기억한다
            ir::Origin assigned_origin(const ast::ASTAssign* assign, const Env& env, const Type& type) const {
                ir::Origin origin = origin_of(assign->expression, env, type);
                if (origin.kind == ir::Origin::Kind::LITERAL) {
                    origin.statement = range_of(assign);
                }
                return origin;
            }

            // env에서 계산한 expr의 값이 원문의 어디에서 왔는지. var는 그 var에 값을 넣은 곳을 따라간다
            ir::Origin origin_of(const ast::ASTNode* expr, const Env& env, const Type& type) const {
                if (expr->type == ast::NAME) {
                    const std::string& name = static_cast<const ast::ASTName*>(expr)->name;
                    if (const auto it = env.origins.find(name); it != env.origins.end()) {
                        return it->second;
                    }
                    if (env.values.contains(name)) {
                        return locked("computed");
                    }
                }
                if (!is_literal(expr)) {
                    return locked("computed");
                }
                if (in_package(expr)) {
                    return locked("package");
                }
                if (!env.lock.empty()) {
                    return locked(env.lock);
                }
                ir::Origin origin;
                origin.kind = ir::Origin::Kind::LITERAL;
                origin.range = range_of(expr);
                if (expr->type == ast::DIMENSION) {
                    origin.unit = static_cast<const ast::ASTDimension*>(expr)->unit;
                }
                origin.integer = type.kind == Kind::Int;
                origin.text = type.kind == Kind::Text && expr->type == ast::STRING;
                return origin;
            }

            // put에 적지 않은 var. 그 put 블록에 넣으면 된다
            static ir::Origin missing(const ir::Origin& put, const VarInfo& var) {
                ir::Origin origin = put;
                if (origin.kind == ir::Origin::Kind::BLOCK) {
                    origin.name = var.name;
                    origin.integer = var.type.kind == Kind::Int;
                }
                return origin;
            }

            // 경고에 쓰는 위치 (파일:줄:칸)
            std::string where_of(const Token& token) const {
                return path_of(token) + ":" + std::to_string(token.line) + ":" + std::to_string(token.column);
            }

            std::string path_of(const Token& token) const {
                const std::less<const char*> less;
                for (const auto& source : sources_) {
                    const char* begin = source.text.data();
                    const char* end = begin + source.text.size();
                    if (!less(token.value.data(), begin) && !less(end, token.value.data())) {
                        return source.path;
                    }
                }
                return main_path_;
            }

            void error(const Token& token, const std::string& message) {
                Diagnostic diagnostic{path_of(token), token.line, token.column, message};
                if (reported_.insert({diagnostic.path, diagnostic.line, diagnostic.column, diagnostic.message}).second) {
                    diagnostics_.push_back(std::move(diagnostic));
                }
            }

            // 파일을 읽은 순서, 그 안에서는 위치 순서로 정렬해서 돌려준다
            Result fail() {
                std::map<std::string, std::size_t> order;
                for (std::size_t i = 0; i < sources_.size(); ++i) {
                    order.emplace(sources_[i].path, i);
                }
                const auto rank = [&](const Diagnostic& diagnostic) {
                    const auto it = order.find(diagnostic.path);
                    return std::tuple{it == order.end() ? 0 : it->second, diagnostic.line, diagnostic.column};
                };
                std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [&](const Diagnostic& a, const Diagnostic& b) { return rank(a) < rank(b); });
                return {std::nullopt, std::move(diagnostics_)};
            }

            // ---- 선언 수집

            void add_builtin_enum(const std::string& name, std::vector<std::string> members) {
                builtin_names_.insert(name);
                enums_.emplace(name, EnumInfo{name, std::move(members)});
            }

            Type enum_type(const std::string& name) const {
                return Type{Kind::Enum, &enums_.at(name)};
            }

            bool declare(const std::string& name, const Token& token) {
                if (builtin_names_.contains(name)) {
                    error(token, "'" + name + "' is a built-in name");
                    return false;
                }
                if (!declared_names_.insert(name).second) {
                    error(token, "'" + name + "' is already defined");
                    return false;
                }
                return true;
            }

            void collect() {
                for (const auto* statement : statements_) {
                    switch (statement->type) {
                        case ast::ENUM:
                            collect_enum(static_cast<const ast::ASTEnum*>(statement));
                            break;
                        case ast::STYLE: {
                            const auto* style = static_cast<const ast::ASTStyle*>(statement);
                            if (declare(style->name, style->token)) {
                                styles_[style->name] = style;
                            }
                            break;
                        }
                        case ast::TEMPLATE: {
                            const auto* node = static_cast<const ast::ASTTemplate*>(statement);
                            if (declare(node->name, node->token)) {
                                templates_[node->name] = node;
                            }
                            break;
                        }
                        case ast::OBJECT: {
                            const auto* node = static_cast<const ast::ASTObject*>(statement);
                            if (declare(node->name, node->token)) {
                                objects_[node->name] = node;
                            }
                            break;
                        }
                        case ast::MASTER: {
                            const auto* master = static_cast<const ast::ASTMaster*>(statement);
                            if (declare(master->name, master->token)) {
                                masters_[master->name] = master;
                                master_order_.push_back(master);
                            }
                            break;
                        }
                        case ast::SLIDE: {
                            const auto* slide = static_cast<const ast::ASTSlide*>(statement);
                            slides_.push_back(slide);
                            if (current_section_ != nullptr) {
                                slide_sections_[slide] = current_section_;
                            }
                            break;
                        }
                        case ast::THEME: {
                            const auto* theme = static_cast<const ast::ASTTheme*>(statement);
                            if (declare(theme->name, theme->token)) {
                                themes_[theme->name] = theme;
                            }
                            break;
                        }
                        case ast::SECTION:
                            current_section_ = static_cast<const ast::ASTSection*>(statement);
                            break;
                        case ast::TARGET: {
                            const auto* target = static_cast<const ast::ASTTarget*>(statement);
                            if (declare(target->name, target->token)) {
                                targets_.push_back(target);
                            }
                            break;
                        }
                        case ast::IF:
                            error(statement->token, "'if' is not supported at the top level yet");
                            break;
                        default:
                            break;
                    }
                }
            }

            void collect_enum(const ast::ASTEnum* node) {
                if (!declare(node->name, node->token)) {
                    return;
                }
                EnumInfo info{node->name, {}};
                for (const auto* member : node->members) {
                    if (info.has(member->name)) {
                        error(member->token, "Duplicate member '" + member->name + "' in enum '" + node->name + "'");
                    } else {
                        info.members.push_back(member->name);
                    }
                }
                enums_.emplace(node->name, std::move(info));
            }

            // ---- 이름과 타입 검사

            Type resolve_type(const ast::ASTName* name) {
                const std::string& type = name->name;
                if (type == "int") { return Type{Kind::Int}; }
                if (type == "float") { return Type{Kind::Float}; }
                if (type == "string") { return Type{Kind::String}; }
                if (type == "text") { return Type{Kind::Text}; }
                if (type == "color") { return Type{Kind::Color}; }
                if (type == "bool") { return Type{Kind::Bool}; }
                if (type == "ref") { return Type{Kind::Ref}; }
                if (const auto it = enums_.find(type); it != enums_.end()) {
                    return Type{Kind::Enum, &it->second};
                }
                error(name->token, "Unknown type: " + type);
                return Type{Kind::Error};
            }

            std::vector<VarInfo> parameters_of(const std::vector<ast::ASTParameter*>& declared) {
                std::vector<VarInfo> parameters;
                for (const auto* parameter : declared) {
                    if (find_var(parameters, parameter->name->name) != nullptr) {
                        error(parameter->name->token, "Duplicate parameter: " + parameter->name->name);
                    } else {
                        parameters.push_back({parameter->name->name, resolve_type(parameter->type_name), nullptr, false});
                    }
                }
                return parameters;
            }

            std::vector<VarInfo> vars_of(const std::vector<ast::ASTNode*>& body) {
                std::vector<VarInfo> vars;
                for (const auto* statement : body) {
                    if (statement->type != ast::VAR) {
                        continue;
                    }
                    const auto* var = static_cast<const ast::ASTVar*>(statement);
                    if (find_var(vars, var->name->name) != nullptr) {
                        error(var->name->token, "Duplicate variable: " + var->name->name);
                    } else {
                        vars.push_back({var->name->name, resolve_type(var->type_name), var->default_value, false});
                    }
                }
                return vars;
            }

            const PropertyInfo* find_property(const std::string& name) const {
                for (const auto& property : properties_) {
                    if (property.name == name) {
                        return &property;
                    }
                }
                return nullptr;
            }

            // put할 수 있는 template 또는 object의 var 목록
            const std::vector<VarInfo>* put_signature(const std::string& name) const {
                if (const auto it = templates_.find(name); it != templates_.end()) {
                    return &signatures_.at(it->second);
                }
                if (const auto it = objects_.find(name); it != objects_.end()) {
                    return &signatures_.at(it->second);
                }
                return nullptr;
            }

            void check() {
                for (const auto* statement : statements_) {
                    if (statement->type == ast::STYLE) {
                        const auto* style = static_cast<const ast::ASTStyle*>(statement);
                        signatures_[style] = parameters_of(style->parameters);
                    } else if (statement->type == ast::MASTER) {
                        signatures_[statement] = parameters_of(static_cast<const ast::ASTMaster*>(statement)->parameters);
                    } else if (statement->type == ast::TEMPLATE) {
                        signatures_[statement] = vars_of(static_cast<const ast::ASTTemplate*>(statement)->body);
                    } else if (statement->type == ast::OBJECT) {
                        signatures_[statement] = vars_of(static_cast<const ast::ASTObject*>(statement)->body);
                        add_common_properties(statement);
                    }
                }
                for (const auto* statement : statements_) {
                    switch (statement->type) {
                        case ast::STYLE:
                            check_style(static_cast<const ast::ASTStyle*>(statement));
                            break;
                        case ast::TEMPLATE:
                            check_defaults(statement);
                            check_statements(static_cast<const ast::ASTTemplate*>(statement)->body, Scope{&signatures_.at(statement), false}, true);
                            break;
                        case ast::OBJECT:
                            check_defaults(statement);
                            break;
                        case ast::MASTER:
                            check_master(static_cast<const ast::ASTMaster*>(statement));
                            break;
                        case ast::SLIDE:
                            check_slide(static_cast<const ast::ASTSlide*>(statement));
                            break;
                        case ast::THEME: {
                            std::set<std::string> assigned;
                            for (const auto* inner : static_cast<const ast::ASTTheme*>(statement)->body) {
                                check_assignment(static_cast<const ast::ASTAssign*>(inner), Scope{}, assigned, theme_properties(), "theme");
                            }
                            break;
                        }
                        case ast::SECTION:
                            check_as(static_cast<const ast::ASTSection*>(statement)->name, Scope{}, Type{Kind::String});
                            break;
                        default:
                            break;
                    }
                }
                // slide의 layout을 모두 안 뒤에 검사한다
                check_slide_masters();
                for (const auto* target : targets_) {
                    check_target(target);
                }
            }

            void add_common_properties(const ast::ASTNode* object) {
                auto& vars = signatures_[object];
                const bool has_text = find_var(vars, "text") != nullptr;
                for (const auto& property : common_properties_) {
                    if (find_var(vars, property.name) != nullptr) {
                        error(object->token, "'" + property.name + "' is a built-in property of every object");
                        continue;
                    }
                    vars.push_back(property);
                }
                if (!has_text) {
                    return;
                }
                for (const auto& property : text_properties_) {
                    if (find_var(vars, property.name) != nullptr) {
                        error(object->token, "'" + property.name + "' is a built-in property of every object with text");
                        continue;
                    }
                    vars.push_back(property);
                }
            }

            void check_style(const ast::ASTStyle* style) {
                const Scope scope{&signatures_.at(style), false};
                std::set<std::string> assigned;
                for (const auto* statement : style->body) {
                    check_style_assign(static_cast<const ast::ASTStyleAssign*>(statement), scope, assigned);
                }
            }

            // style 선언과 인라인 style의 속성 하나. assigned는 앞에서 정한 속성 이름들
            void check_style_assign(const ast::ASTStyleAssign* assign, const Scope& scope, std::set<std::string>& assigned) {
                const std::string& name = assign->identifier->name;
                const PropertyInfo* property = find_property(name);
                if (property == nullptr) {
                    error(assign->identifier->token, "Unknown style property: " + name);
                    check(assign->expression, scope, Type{});
                    return;
                }
                if (!assigned.insert(name).second) {
                    error(assign->identifier->token, "Duplicate style property: " + name);
                }
                check_as(assign->expression, scope, property->type);
            }

            // 기본값에서는 다른 var를 쓸 수 없다
            void check_defaults(const ast::ASTNode* node) {
                for (const auto& var : signatures_.at(node)) {
                    if (var.default_value != nullptr) {
                        check_as(var.default_value, Scope{}, var.type);
                    }
                }
            }

            void check_statements(const std::vector<ast::ASTNode*>& body, const Scope& scope, bool allow_var) {
                for (const auto* statement : body) {
                    switch (statement->type) {
                        case ast::VAR:
                            if (!allow_var) {
                                error(statement->token, "Variables must be declared at the top level of a template");
                            }
                            break;
                        case ast::PUT:
                            check_put(static_cast<const ast::ASTPut*>(statement), scope);
                            break;
                        case ast::IF:
                            check_branch(static_cast<const ast::ASTIf*>(statement), scope);
                            break;
                        case ast::FOR:
                            check_for(static_cast<const ast::ASTFor*>(statement), scope);
                            break;
                        case ast::GROUP:
                            check_group(static_cast<const ast::ASTGroup*>(statement), scope);
                            break;
                        case ast::ASSIGN:
                            error(static_cast<const ast::ASTAssign*>(statement)->name->token, "Properties such as 'background' can only be set directly inside a case");
                            break;
                        default:
                            break;
                    }
                }
            }

            // 범위는 int만 돌 수 있고, 목록의 값은 변수 타입에 맞아야 한다. 변수는 본문 안에서만 보인다
            void check_for(const ast::ASTFor* node, const Scope& scope) {
                const Type type = resolve_type(node->type_name);
                for_types_[node] = type;
                const std::string& name = node->name->name;
                if (scope.find(name) != nullptr) {
                    error(node->name->token, "'" + name + "' is already defined");
                }
                if (node->range_start != nullptr) {
                    if (type.kind != Kind::Int && type.kind != Kind::Error) {
                        error(node->type_name->token, "A range can only be used with int, but got " + type_name(type));
                    }
                    check_as(node->range_start, scope, Type{Kind::Int});
                    check_as(node->range_end, scope, Type{Kind::Int});
                } else {
                    for (const auto* item : node->items) {
                        check_as(item, scope, type);
                    }
                }
                std::vector<VarInfo> vars = scope.vars != nullptr ? *scope.vars : std::vector<VarInfo>{};
                vars.push_back({name, type, nullptr, false});
                check_statements(node->body, Scope{&vars, scope.in_master, scope.in_slide}, false);
            }

            template <typename T>
            void check_branch(const T* node, const Scope& scope) {
                const Type condition = check(node->condition, scope, Type{});
                if (condition.kind != Kind::Bool && condition.kind != Kind::Error) {
                    error(location(node->condition), "Condition must be a bool such as 'a == b', but got " + type_name(condition));
                }
                check_statements(node->body, scope, false);
                if (node->branch == nullptr) {
                    return;
                }
                if (node->branch->type == ast::ELSE_IF) {
                    check_branch(static_cast<const ast::ASTElseIf*>(node->branch), scope);
                } else {
                    check_statements(static_cast<const ast::ASTElse*>(node->branch)->body, scope, false);
                }
            }

            void check_group(const ast::ASTGroup* group, const Scope& scope) {
                check_statements(group->body, scope, false);
                check_inner_animations(group->animations, scope, "group");
            }

            // put, group 블록 안의 animate. slide에 바로 적은 블록에서만 쓸 수 있다
            void check_inner_animations(const std::vector<ast::ASTAnimate*>& animations, const Scope& scope, const std::string& object) {
                for (const auto* animation : animations) {
                    if (!scope.in_slide) {
                        error(animation->token, "'animate' can only be used in a put or group written in a slide");
                        continue;
                    }
                    check_animate(animation, scope);
                    if (animation->category->name == "media" && object != "video" && object != "audio") {
                        error(animation->category->token, "Media animations can only be used on a video or an audio");
                    }
                }
            }

            void check_put(const ast::ASTPut* put, const Scope& scope) {
                check_inner_animations(put->animations, scope, put->name);
                const std::vector<VarInfo>* vars = put_signature(put->name);
                if (vars == nullptr) {
                    error(put->token, "Unknown template or object: " + put->name);
                    for (const auto* statement : put->body) {
                        check(static_cast<const ast::ASTAssign*>(statement)->expression, scope, Type{});
                    }
                    return;
                }
                std::set<std::string> assigned;
                for (const auto* statement : put->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    const VarInfo* var = find_var(*vars, name);
                    if (var == nullptr) {
                        error(assign->name->token, "'" + put->name + "' has no property '" + name + "'");
                        check(assign->expression, scope, Type{});
                        continue;
                    }
                    if (!assigned.insert(name).second) {
                        error(assign->name->token, "Duplicate property: " + name);
                    }
                    check_as(assign->expression, scope, var->type);
                }
                for (const auto& var : *vars) {
                    if (var.default_value == nullptr && !var.optional && !assigned.contains(var.name)) {
                        error(put->token, "Missing property '" + var.name + "' for '" + put->name + "'");
                    }
                }
                if (objects_.contains(put->name)) {
                    check_object_properties(put);
                }
            }

            // 세부값만 있고 기본값이 없는 것, object가 쓸 수 없는 공통 속성
            void check_object_properties(const ast::ASTPut* put) {
                std::map<std::string, const ast::ASTAssign*> assigned;
                for (const auto* statement : put->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    assigned.emplace(assign->name->name, assign);
                }
                const auto needs = [&](const std::vector<std::string>& details, const std::string& base) {
                    for (const auto& detail : details) {
                        if (assigned.contains(detail) && !assigned.contains(base)) {
                            error(assigned.at(detail)->name->token, "'" + detail + "' needs '" + base + "'");
                        }
                    }
                };
                needs({"shadow_blur", "shadow_distance", "shadow_angle"}, "shadow");
                needs({"inner_shadow_blur", "inner_shadow_distance", "inner_shadow_angle"}, "inner_shadow");
                needs({"glow_size"}, "glow");
                needs({"reflection_size", "reflection_distance", "reflection_blur"}, "reflection");
                needs({"bevel_width", "bevel_height"}, "bevel");
                needs({"depth_color"}, "depth");
                const auto forbid = [&](const std::string& property, const std::string& message) {
                    if (assigned.contains(property)) {
                        error(assigned.at(property)->name->token, message);
                    }
                };
                // PowerPoint은 누를 때 하는 일을 하나만 가진다
                if (assigned.contains("action")) {
                    forbid("link", "Use either 'link' or 'action'; an object has one click action (write the link as action = ...)");
                }
                if (put->name == "line" || put->name == "connector") {
                    forbid("fill", "A line has no fill");
                    forbid("flip", "A line cannot be flipped; swap its end points instead");
                }
                if (put->name == "connector") {
                    forbid("rotation", "A connector cannot be rotated; it follows the objects it connects");
                }
                if (put->name == "backdrop") {
                    forbid("fill", "A backdrop has no fill; it is filled with the blurred background");
                }
            }

            // master의 매개변수는 case 안에서 변수처럼 쓴다. master 바로 안의 대입은 theme = 이름; 뿐이다
            void check_master(const ast::ASTMaster* master) {
                const Scope scope{&signatures_.at(master), true};
                std::set<std::string> properties;
                for (const auto* statement : master->properties) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    if (assign->name->name != "theme") {
                        error(assign->name->token, "Unknown master property: " + assign->name->name);
                        continue;
                    }
                    if (!properties.insert("theme").second) {
                        error(assign->name->token, "Duplicate property: theme");
                    }
                    if (assign->expression->type != ast::NAME || !themes_.contains(static_cast<const ast::ASTName*>(assign->expression)->name)) {
                        error(location(assign->expression), "Expected the name of a theme");
                    }
                }
                std::set<std::string> names;
                for (const auto* statement : master->body) {
                    const auto* layout = static_cast<const ast::ASTMasterCase*>(statement);
                    if (!names.insert(layout->name).second) {
                        error(layout->token, "Duplicate layout '" + layout->name + "' in master '" + master->name + "'");
                    }
                    // case 바로 안의 대입은 layout의 속성이다
                    std::vector<ast::ASTNode*> statements;
                    std::set<std::string> assigned;
                    for (auto* inner : layout->body) {
                        if (inner->type == ast::ASSIGN) {
                            check_assignment(static_cast<const ast::ASTAssign*>(inner), scope, assigned, {{"background", Type{Kind::Background}}}, "layout");
                        } else {
                            statements.push_back(inner);
                        }
                    }
                    check_statements(statements, scope, false);
                }
            }

            using PropertyTypes = std::vector<std::pair<std::string, Type>>;

            // slide, layout, theme, review 같은 블록의 NAME = VALUE. properties에 없는 이름이면 알 수 없는 속성이다
            void check_assignment(const ast::ASTAssign* assign, const Scope& scope, std::set<std::string>& assigned, const PropertyTypes& properties, const std::string& owner) {
                const std::string& name = assign->name->name;
                const auto it = std::find_if(properties.begin(), properties.end(), [&](const auto& property) { return property.first == name; });
                if (it == properties.end()) {
                    error(assign->name->token, "Unknown " + owner + " property: " + name);
                    check(assign->expression, scope, Type{});
                    return;
                }
                if (!assigned.insert(name).second) {
                    error(assign->name->token, "Duplicate property: " + name);
                }
                check_as(assign->expression, scope, it->second);
            }

            static const PropertyTypes& slide_properties() {
                static const PropertyTypes properties = {
                    {"background", Type{Kind::Background}}, {"title", Type{Kind::Text}}, {"subtitle", Type{Kind::Text}}, {"body", Type{Kind::Text}},
                    {"hidden", Type{Kind::Bool}}, {"advance_after", Type{Kind::Float}}, {"transition_sound", Type{Kind::String}},
                };
                return properties;
            }

            // 테마 색은 PowerPoint 테마의 dk1, lt1, dk2, lt2, accent1~6, hlink, folHlink에 대응한다
            static const std::vector<std::pair<std::string, std::string>>& theme_colors() {
                static const std::vector<std::pair<std::string, std::string>> colors = {
                    {"dark1", "dk1"}, {"light1", "lt1"}, {"dark2", "dk2"}, {"light2", "lt2"},
                    {"accent1", "accent1"}, {"accent2", "accent2"}, {"accent3", "accent3"}, {"accent4", "accent4"}, {"accent5", "accent5"}, {"accent6", "accent6"},
                    {"hyperlink", "hlink"}, {"followed_hyperlink", "folHlink"},
                };
                return colors;
            }

            static const PropertyTypes& theme_properties() {
                static const PropertyTypes properties = [] {
                    PropertyTypes result;
                    for (const auto& [name, scheme] : theme_colors()) {
                        result.emplace_back(name, Type{Kind::Color});
                    }
                    result.emplace_back("heading_font", Type{Kind::String});
                    result.emplace_back("body_font", Type{Kind::String});
                    return result;
                }();
                return properties;
            }

            void check_slide(const ast::ASTSlide* slide) {
                bool has_layout = false;
                bool has_transition = false;
                std::set<std::string> assigned;
                Scope slide_scope;
                slide_scope.in_slide = true;
                for (const auto* statement : slide->body) {
                    if (statement->type == ast::PUT) {
                        check_put(static_cast<const ast::ASTPut*>(statement), slide_scope);
                        continue;
                    }
                    if (statement->type == ast::GROUP) {
                        check_group(static_cast<const ast::ASTGroup*>(statement), slide_scope);
                        continue;
                    }
                    if (statement->type == ast::ANIMATE) {
                        check_animate(static_cast<const ast::ASTAnimate*>(statement), Scope{});
                        continue;
                    }
                    if (statement->type == ast::REVIEW) {
                        check_review(static_cast<const ast::ASTReview*>(statement));
                        continue;
                    }
                    if (statement->type == ast::COMMENT) {
                        check_as(static_cast<const ast::ASTComment*>(statement)->expression, Scope{}, Type{Kind::Text});
                        continue;
                    }
                    if (statement->type == ast::TRANSITION) {
                        if (has_transition) {
                            error(statement->token, "Duplicate transition");
                        }
                        has_transition = true;
                        check_transition(static_cast<const ast::ASTTransition*>(statement));
                        continue;
                    }
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    if (assign->name->name != "layout") {
                        check_assignment(assign, Scope{}, assigned, slide_properties(), "slide");
                        continue;
                    }
                    if (has_layout) {
                        error(assign->name->token, "Duplicate property: layout");
                        continue;
                    }
                    has_layout = true;
                    if (const auto layout = resolve_layout(assign->expression)) {
                        slide_layouts_[slide] = *layout;
                    }
                }
            }

            // 대상 이름은 slide를 펼친 뒤에 찾는다
            void check_animate(const ast::ASTAnimate* node, const Scope& scope) {
                const std::string& category = node->category->name;
                const std::string& effect = node->effect->name;
                if (category != "enter" && category != "emphasis" && category != "exit" && category != "move" && category != "media") {
                    error(node->category->token, "An animation must be enter, emphasis, exit, move or media, but got '" + category + "'");
                } else if (category == "media") {
                    if (effect != "play" && effect != "pause" && effect != "stop") {
                        error(node->effect->token, "A media animation must be play, pause or stop, but got '" + effect + "'");
                    } else if (node->option != nullptr) {
                        error(node->option->token, "Media animations have no options");
                    }
                    if (node->duration != nullptr) {
                        error(location(node->duration), "Media animations cannot have a duration");
                    }
                } else if (node->path != nullptr) {
                    if (category != "move") {
                        error(node->effect->token, "'path' can only be used with move");
                    }
                    check_as(node->path, scope, Type{Kind::String});
                } else if (const auto it = animation_options().find(category + "." + effect); it == animation_options().end()) {
                    error(node->effect->token, "Unknown " + category + " animation: " + effect);
                } else {
                    const auto& options = it->second.options;
                    if (node->option != nullptr && options.front().empty()) {
                        error(node->option->token, "Animation '" + effect + "' has no options");
                    } else if (node->option != nullptr && std::find(options.begin(), options.end(), node->option->name) == options.end()) {
                        error(node->option->token, "Animation '" + effect + "' has no option '" + node->option->name + "' (" + join(options) + ")");
                    }
                    if (it->second.fixed && node->duration != nullptr) {
                        error(location(node->duration), "Animation '" + effect + "' lasts until the slide ends, so it cannot have a duration");
                    }
                }
                if (node->duration != nullptr) {
                    check_as(node->duration, scope, Type{Kind::Float});
                }
                if (node->delay != nullptr) {
                    check_as(node->delay, scope, Type{Kind::Float});
                }
                if (node->order != nullptr) {
                    check_as(node->order, scope, Type{Kind::Int});
                }
            }

            void check_review(const ast::ASTReview* node) {
                static const PropertyTypes properties = {{"text", Type{Kind::Text}}, {"author", Type{Kind::String}}, {"x", Type{Kind::Float}}, {"y", Type{Kind::Float}}};
                std::set<std::string> assigned;
                for (const auto* statement : node->body) {
                    check_assignment(static_cast<const ast::ASTAssign*>(statement), Scope{}, assigned, properties, "review");
                }
                if (!assigned.contains("text")) {
                    error(node->token, "Missing property 'text' for review");
                }
            }

            void check_transition(const ast::ASTTransition* node) {
                const std::string& kind = node->kind->name;
                const auto it = transition_options().find(kind);
                if (it == transition_options().end()) {
                    error(node->kind->token, "Unknown transition: " + kind);
                } else if (node->option != nullptr) {
                    const auto& options = it->second;
                    if (options.front().empty()) {
                        error(node->option->token, "Transition '" + kind + "' has no options");
                    } else if (std::find(options.begin(), options.end(), node->option->name) == options.end()) {
                        error(node->option->token, "Transition '" + kind + "' has no option '" + node->option->name + "' (" + join(options) + ")");
                    }
                }
                if (node->duration != nullptr) {
                    check_as(node->duration, Scope{}, Type{Kind::Float});
                }
            }

            // <master>.<layout> 또는 매개변수가 있는 master면 <master>(<값>, ...).<layout>
            std::optional<LayoutInfo> resolve_layout(const ast::ASTNode* expr) {
                const auto* member = expr->type == ast::MEMBER ? static_cast<const ast::ASTMember*>(expr) : nullptr;
                const ast::ASTNode* object = member != nullptr ? member->object : nullptr;
                if (object == nullptr || (object->type != ast::NAME && object->type != ast::CALL)) {
                    error(location(expr), "Layout must be written as <master>.<layout> or <master>(<arguments>).<layout>");
                    return std::nullopt;
                }
                const bool called = object->type == ast::CALL;
                const std::string& master_name = called ? static_cast<const ast::ASTCall*>(object)->name->name : static_cast<const ast::ASTName*>(object)->name;
                const std::vector<ast::ASTNode*> arguments = called ? static_cast<const ast::ASTCall*>(object)->arguments : std::vector<ast::ASTNode*>{};
                const auto master = masters_.find(master_name);
                if (master == masters_.end()) {
                    error(object->token, "Unknown master: " + master_name);
                    return std::nullopt;
                }
                const auto& parameters = signatures_.at(master->second);
                if (arguments.size() != parameters.size() || (!called && !parameters.empty())) {
                    error(object->token, "Master '" + master_name + "' needs " + std::to_string(parameters.size()) + " argument(s), but got " + std::to_string(arguments.size()));
                    return std::nullopt;
                }
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    check_as(arguments[i], Scope{}, parameters[i].type);
                }
                for (const auto* statement : master->second->body) {
                    const auto* layout = static_cast<const ast::ASTMasterCase*>(statement);
                    if (layout->name == member->member->name) {
                        return LayoutInfo{master->second, layout, expr, arguments};
                    }
                }
                error(member->member->token, "Master '" + master_name + "' has no layout '" + member->member->name + "'");
                return std::nullopt;
            }

            void check_target(const ast::ASTTarget* target) {
                std::set<std::string> assigned;
                for (const auto* statement : target->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    if (name == "master") {
                        error(assign->name->token, "A target has no master; the master comes from each slide's layout");
                        continue;
                    }
                    if (name != "path" && name != "type" && name != "width" && name != "height" && name != "title" && name != "author" && name != "loop" && name != "angles" && name != "script") {
                        error(assign->name->token, "Unknown target property: " + name);
                        continue;
                    }
                    if (!assigned.insert(name).second) {
                        error(assign->name->token, "Duplicate property: " + name);
                        continue;
                    }
                    if (name == "path" || name == "title" || name == "author" || name == "script") {
                        check_as(assign->expression, Scope{}, Type{Kind::String});
                    } else if (name == "loop") {
                        check_as(assign->expression, Scope{}, Type{Kind::Bool});
                    } else if (name == "type") {
                        check_as(assign->expression, Scope{}, enum_type("target_type"));
                    } else if (name == "angles") {
                        check_as(assign->expression, Scope{}, enum_type("angle_convention"));
                    } else {
                        check_as(assign->expression, Scope{}, Type{Kind::Float});
                    }
                }
                for (const std::string required : {"path", "type"}) {
                    if (!assigned.contains(required)) {
                        error(target->token, "Missing property '" + required + "' for target '" + target->name + "'");
                    }
                }
                if (assigned.contains("width") != assigned.contains("height")) {
                    error(target->token, "Target '" + target->name + "' must set both width and height");
                }
            }

            // 모든 slide가 같은 master를 써야 한다. 매개변수 값은 달라도 된다
            void check_slide_masters() {
                const ast::ASTMaster* master = nullptr;
                for (const auto* slide : slides_) {
                    const auto it = slide_layouts_.find(slide);
                    if (it == slide_layouts_.end()) {
                        continue;
                    }
                    const LayoutInfo& layout = it->second;
                    if (master == nullptr) {
                        master = layout.master;
                    } else if (layout.master != master) {
                        error(location(layout.expression), "Every slide must use the same master, but this slide uses '" + layout.master->name + "' and an earlier slide uses '" + master->name + "'");
                    }
                }
            }

            void check_as(const ast::ASTNode* expr, const Scope& scope, const Type& expected) {
                const Type actual = check(expr, scope, expected);
                if (!assignable(actual, expected)) {
                    error(location(expr), "Expected " + type_name(expected) + ", but got " + type_name(actual));
                }
            }

            // expr의 타입. expected는 enum 값의 이름을 찾거나 text로 해석할 때 쓴다
            Type check(const ast::ASTNode* expr, const Scope& scope, const Type& expected) {
                if (expected.kind == Kind::Text) {
                    return check_text(expr, scope, false);
                }
                switch (expr->type) {
                    case ast::INT:
                        return Type{Kind::Int};
                    case ast::FLOAT:
                        return Type{Kind::Float};
                    case ast::DIMENSION: {
                        const auto* dimension = static_cast<const ast::ASTDimension*>(expr);
                        if (!px_per_unit(dimension->unit)) {
                            error(expr->token, "Unknown unit '" + dimension->unit + "' (px, pt, in, cm, mm, %, deg, s, ms)");
                        }
                        // %는 소수여도 int 자리에 쓸 수 있다. backend가 슬라이드 크기로 풀 때 int 값은 px 정수로 자른다
                        return Type{dimension->value->type == ast::FLOAT && dimension->unit != "%" ? Kind::Float : Kind::Int};
                    }
                    case ast::STRING:
                        return Type{Kind::String};
                    case ast::FSTRING:
                        for (const auto* part : static_cast<const ast::ASTFString*>(expr)->parts) {
                            if (!field_of(part).empty()) {
                                error(location(part), "This value is updated by PowerPoint, so it cannot be used in an f-string; put it directly in text");
                            }
                            check(part, scope, Type{});
                        }
                        return Type{Kind::String};
                    case ast::NAME:
                        return check_name(static_cast<const ast::ASTName*>(expr), scope, expected);
                    case ast::MEMBER:
                        return check_member(static_cast<const ast::ASTMember*>(expr), scope);
                    case ast::CONTEXT:
                        if (static_cast<const ast::ASTContext*>(expr)->name->name == "date") {
                            return Type{Kind::Text};
                        }
                        error(expr->token, "'@" + static_cast<const ast::ASTContext*>(expr)->name->name + "' must be followed by a member such as '@slide.page'");
                        return Type{Kind::Error};
                    case ast::MULTIPLY: {
                        const auto* node = static_cast<const ast::ASTMultiply*>(expr);
                        return check_arithmetic(node->left, node->right, scope);
                    }
                    case ast::ADD: {
                        const auto* node = static_cast<const ast::ASTAdd*>(expr);
                        return check_arithmetic(node->left, node->right, scope);
                    }
                    case ast::MINUS: {
                        const auto* node = static_cast<const ast::ASTMinus*>(expr);
                        return check_arithmetic(node->left, node->right, scope);
                    }
                    case ast::EQUAL:
                        return check_equal(static_cast<const ast::ASTEqual*>(expr), scope);
                    case ast::COLOR_RGB: {
                        const auto* color = static_cast<const ast::ASTColorRGB*>(expr);
                        for (const auto* component : {color->r, color->g, color->b}) {
                            check_as(component, scope, Type{Kind::Int});
                        }
                        return Type{Kind::Color};
                    }
                    case ast::COLOR_RGBA: {
                        const auto* color = static_cast<const ast::ASTColorRGBA*>(expr);
                        for (const auto* component : {color->r, color->g, color->b}) {
                            check_as(component, scope, Type{Kind::Int});
                        }
                        check_as(color->a, scope, Type{Kind::Float});
                        return Type{Kind::Color};
                    }
                    case ast::COLOR_HEX: {
                        const auto& digits = static_cast<const ast::ASTString*>(static_cast<const ast::ASTColorHex*>(expr)->hex)->value;
                        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
                            error(expr->token, "Hex color must have 3, 4, 6 or 8 digits");
                        }
                        return Type{Kind::Color};
                    }
                    case ast::CALL: {
                        const auto* call = static_cast<const ast::ASTCall*>(expr);
                        if (call->name->name == "linear") {
                            return check_gradient(call, scope, 1);
                        }
                        if (call->name->name == "image") {
                            return check_image(call, scope);
                        }
                        if (call->name->name == "radial") {
                            return check_gradient(call, scope, 0);
                        }
                        if (call->name->name == "pattern") {
                            return check_pattern(call, scope);
                        }
                        if (is_action_call(call->name->name)) {
                            return check_action_call(call, scope, expected);
                        }
                        if (call->name->name == "slide") {
                            if (call->arguments.size() != 1) {
                                error(call->token, "slide needs one slide number, such as slide(3)");
                            }
                            for (const auto* argument : call->arguments) {
                                check_as(argument, scope, Type{Kind::Int});
                            }
                            return Type{call->arguments.size() != 1 ? Kind::Error : Kind::SlideRef};
                        }
                        if (styles_.contains(call->name->name)) {
                            error(expr->token, "Style '" + call->name->name + "' can only be used inside text");
                        } else {
                            error(expr->token, "Unknown function: " + call->name->name);
                        }
                        for (const auto* argument : call->arguments) {
                            check(argument, scope, Type{});
                        }
                        return Type{Kind::Error};
                    }
                    case ast::TEXT:
                    case ast::LIST:
                        return check_text(expr, scope, false);
                    case ast::INLINE_STYLE:
                        error(expr->token, "An inline style can only be used inside text");
                        return Type{Kind::Error};
                    default:
                        error(expr->token, "Unexpected expression");
                        return Type{Kind::Error};
                }
            }

            // linear(각도, 색, 색, ...)과 radial(색, 색, ...). 색 뒤에 위치를 붙일 수 있다: hex(F00) 30%
            // leading은 색 앞의 인자 수 (linear는 각도 하나)
            Type check_gradient(const ast::ASTCall* call, const Scope& scope, std::size_t leading) {
                const auto& arguments = call->arguments;
                const std::string& name = call->name->name;
                if (arguments.size() < leading + 2) {
                    error(call->token, name + " needs " + (leading > 0 ? "an angle and " : "") + "at least 2 colors, such as " +
                                       (leading > 0 ? "linear(90, hex(FFF), hex(000))" : "radial(hex(FFF), hex(000))"));
                }
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (i < leading) {
                        check_as(arguments[i], scope, Type{Kind::Float});
                    } else if (const auto* stop = gradient_stop(arguments[i])) {
                        check_as(stop->parts[0], scope, Type{Kind::Color});
                        check_as(stop->parts[1], scope, Type{Kind::Float});
                    } else {
                        check_as(arguments[i], scope, Type{Kind::Color});
                    }
                }
                return Type{arguments.size() < leading + 2 ? Kind::Error : Kind::Gradient};
            }

            // '색 위치'처럼 두 항으로 된 색 정지점이면 그 식
            static const ast::ASTText* gradient_stop(const ast::ASTNode* argument) {
                if (argument->type != ast::TEXT) {
                    return nullptr;
                }
                const auto* stop = static_cast<const ast::ASTText*>(argument);
                return stop->parts.size() == 2 ? stop : nullptr;
            }

            // pattern(종류, 앞색, 뒷색)
            Type check_pattern(const ast::ASTCall* call, const Scope& scope) {
                const auto& arguments = call->arguments;
                if (arguments.size() != 3) {
                    error(call->token, "pattern needs a kind and 2 colors, such as pattern(diagonal_cross, hex(000), hex(FFF))");
                }
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    check_as(arguments[i], scope, i == 0 ? enum_type("pattern_kind") : Type{Kind::Color});
                }
                return Type{arguments.size() != 3 ? Kind::Error : Kind::Pattern};
            }

            // image("경로")
            Type check_image(const ast::ASTCall* call, const Scope& scope) {
                if (call->arguments.size() != 1) {
                    error(call->token, "image needs one path, such as image(\"bg.png\")");
                }
                for (const auto* argument : call->arguments) {
                    check_as(argument, scope, Type{Kind::String});
                }
                return Type{call->arguments.size() != 1 ? Kind::Error : Kind::Image};
            }

            Type check_name(const ast::ASTName* node, const Scope& scope, const Type& expected) {
                if (const VarInfo* var = scope.find(node->name)) {
                    return var->type;
                }
                if (node->name == "true" || node->name == "false") {
                    return Type{Kind::Bool};
                }
                // 개체 이름은 slide를 펼친 뒤에 찾는다
                if (expected.kind == Kind::Ref) {
                    return Type{Kind::Ref};
                }
                if ((expected.kind == Kind::Link || expected.kind == Kind::Action) && enums_.at("slide_jump").has(node->name)) {
                    return enum_type("slide_jump");
                }
                if (expected.kind == Kind::Enum) {
                    if (expected.enumeration->has(node->name)) {
                        return expected;
                    }
                    error(node->token, "'" + node->name + "' is not a value of '" + expected.enumeration->name + "'");
                    return Type{Kind::Error};
                }
                if (styles_.contains(node->name)) {
                    error(node->token, "Style '" + node->name + "' can only be used inside text");
                    return Type{Kind::Error};
                }
                error(node->token, "Unknown name: " + node->name);
                return Type{Kind::Error};
            }

            static bool is_action_call(const std::string& name) {
                return name == "run" || name == "program" || name == "macro" || name == "file";
            }

            // run("함수", 인자...), program("경로"), macro("이름"), file("경로")
            Type check_action_call(const ast::ASTCall* call, const Scope& scope, const Type& expected) {
                const std::string& name = call->name->name;
                bool valid = true;
                if (expected.kind != Kind::Action) {
                    error(call->token, "'" + name + "(...)' can only be used as an action, such as action = " + name + "(...)");
                    valid = false;
                }
                if (call->arguments.empty() || (name != "run" && call->arguments.size() != 1)) {
                    static const std::map<std::string, std::string> examples = {
                        {"run", "run(\"onClick\", @slide.number)"}, {"program", "program(\"tools/app.exe\")"},
                        {"macro", "macro(\"Module1.Hello\")"}, {"file", "file(\"docs/report.pdf\")"},
                    };
                    error(call->token, name + " needs " + (name == "run" ? "a function name and optional arguments" : "one string") + ", such as " + examples.at(name));
                    valid = false;
                }
                for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                    if (i == 0) {
                        check_as(call->arguments[i], scope, Type{Kind::String});
                        continue;
                    }
                    const Type type = check(call->arguments[i], scope, Type{});
                    if (type.kind != Kind::Error && type.kind != Kind::Bool && !is_numeric(type) && type.kind != Kind::String && type.kind != Kind::Text) {
                        error(location(call->arguments[i]), "An argument of run must be a number, string, text or bool, but got " + type_name(type));
                    }
                }
                return Type{valid ? Kind::Action : Kind::Error};
            }

            Type check_member(const ast::ASTMember* node, const Scope& scope) {
                const std::string& member = node->member->name;
                if (node->object->type == ast::NAME && static_cast<const ast::ASTName*>(node->object)->name == "theme") {
                    for (const auto& [name, scheme] : theme_colors()) {
                        if (name == member) {
                            return Type{Kind::Color};
                        }
                    }
                    if (member == "heading_font" || member == "body_font") {
                        return Type{Kind::String};
                    }
                    error(location(node), "Unknown theme value: theme." + member);
                    return Type{Kind::Error};
                }
                if (node->object->type != ast::CONTEXT) {
                    error(location(node), "Member access is only supported for '@slide.page', '@slide.number', '@slide.count', '@self.name' and 'theme.<name>'");
                    return Type{Kind::Error};
                }
                const std::string& context = static_cast<const ast::ASTContext*>(node->object)->name->name;
                // put ... as NAME의 이름. 그 put의 속성 값 안에서만 쓸 수 있고, 계산할 때 확인한다
                if (context == "self" && member == "name") {
                    return Type{Kind::String};
                }
                // 모든 슬라이드의 수는 master에서도 정해져 있다
                if (context == "slide" && member == "count") {
                    return Type{Kind::Int};
                }
                if (context != "slide" || (member != "page" && member != "number")) {
                    error(location(node), "Unknown value: @" + context + "." + member);
                    return Type{Kind::Error};
                }
                // 슬라이드 번호 필드는 PowerPoint이 채우므로 master에서도 쓸 수 있다
                if (member == "number") {
                    return Type{Kind::Text};
                }
                if (scope.in_master) {
                    error(location(node), "'@slide' cannot be used in a master");
                    return Type{Kind::Error};
                }
                return Type{Kind::Int};
            }

            Type check_arithmetic(const ast::ASTNode* left, const ast::ASTNode* right, const Scope& scope) {
                const Type left_type = check(left, scope, Type{});
                const Type right_type = check(right, scope, Type{});
                bool valid = true;
                for (const auto& [type, node] : {std::pair{left_type, left}, std::pair{right_type, right}}) {
                    if (type.kind == Kind::Error) {
                        valid = false;
                    } else if (!is_numeric(type)) {
                        error(location(node), "Expected a number, but got " + type_name(type));
                        valid = false;
                    }
                }
                if (!valid) {
                    return Type{Kind::Error};
                }
                return Type{left_type.kind == Kind::Float || right_type.kind == Kind::Float ? Kind::Float : Kind::Int};
            }

            Type check_equal(const ast::ASTEqual* node, const Scope& scope) {
                Type left;
                Type right;
                // 왼쪽이 var가 아닌 이름이면 enum 값일 수 있으므로 오른쪽 타입을 먼저 본다
                if (node->left->type == ast::NAME && scope.find(static_cast<const ast::ASTName*>(node->left)->name) == nullptr) {
                    right = check(node->right, scope, Type{});
                    left = check(node->left, scope, right);
                } else {
                    left = check(node->left, scope, Type{});
                    right = check(node->right, scope, left);
                }
                if (!comparable(left, right)) {
                    error(location(node), "Cannot compare " + type_name(left) + " with " + type_name(right));
                }
                return Type{Kind::Bool};
            }

            // text 안의 action(...), hover_action(...). 같은 이름의 style이 있으면 style이다
            bool is_text_action(const std::string& name) const {
                return (name == "action" || name == "hover_action") && !styles_.contains(name);
            }

            // text는 style, string, text, fstring, 목록을 나열한 것
            Type check_text(const ast::ASTNode* expr, const Scope& scope, bool in_item) {
                for (const auto* part : parts_of(expr)) {
                    switch (part->type) {
                        case ast::NAME: {
                            const std::string& name = static_cast<const ast::ASTName*>(part)->name;
                            if (const VarInfo* var = scope.find(name)) {
                                if (!assignable(var->type, Type{Kind::Text})) {
                                    error(part->token, "'" + name + "' is " + type_name(var->type) + " and cannot be used in text");
                                }
                            } else if (const auto style = styles_.find(name); style != styles_.end()) {
                                const auto& parameters = signatures_.at(style->second);
                                if (!parameters.empty()) {
                                    error(part->token, "Style '" + name + "' needs " + std::to_string(parameters.size()) + " argument(s)");
                                }
                            } else {
                                error(part->token, "Unknown style or variable: " + name);
                            }
                            break;
                        }
                        case ast::CALL: {
                            const auto* call = static_cast<const ast::ASTCall*>(part);
                            if (call->name->name == "link" && !styles_.contains("link")) {
                                if (call->arguments.size() != 1) {
                                    error(call->token, "link needs one target, such as link(\"https://example.com\") or link(slide(3))");
                                }
                                for (const auto* argument : call->arguments) {
                                    check_as(argument, scope, Type{Kind::Link});
                                }
                            } else if (is_text_action(call->name->name)) {
                                if (call->arguments.size() != 1) {
                                    error(call->token, call->name->name + " needs one action, such as " + call->name->name + "(run(\"onClick\")) or " + call->name->name + "(slide(3))");
                                }
                                for (const auto* argument : call->arguments) {
                                    check_as(argument, scope, Type{Kind::Action});
                                }
                            } else {
                                check_style_call(call, scope);
                            }
                            break;
                        }
                        case ast::INLINE_STYLE: {
                            // 속성 값에는 이 text에서 보이는 var를 쓸 수 있다
                            std::set<std::string> assigned;
                            for (const auto* assign : static_cast<const ast::ASTInlineStyle*>(part)->properties) {
                                check_style_assign(assign, scope, assigned);
                            }
                            break;
                        }
                        case ast::TEXT:
                            check_text(part, scope, in_item);
                            break;
                        case ast::LIST:
                            if (in_item) {
                                error(part->token, "A nested list must be a separate item of the outer list");
                            }
                            check_list(static_cast<const ast::ASTList*>(part), scope);
                            break;
                        default: {
                            const Type type = check(part, scope, Type{});
                            if (!assignable(type, Type{Kind::Text})) {
                                error(location(part), type_name(type) + " cannot be used in text");
                            }
                            break;
                        }
                    }
                }
                return Type{Kind::Text};
            }

            void check_list(const ast::ASTList* list, const Scope& scope) {
                for (const auto* item : list->items) {
                    if (item->type == ast::LIST) {
                        check_list(static_cast<const ast::ASTList*>(item), scope);
                    } else {
                        check_text(item, scope, true);
                    }
                }
            }

            void check_style_call(const ast::ASTCall* call, const Scope& scope) {
                const auto style = styles_.find(call->name->name);
                if (style == styles_.end()) {
                    error(call->token, "Unknown style: " + call->name->name);
                    for (const auto* argument : call->arguments) {
                        check(argument, scope, Type{});
                    }
                    return;
                }
                const auto& parameters = signatures_.at(style->second);
                if (call->arguments.size() != parameters.size()) {
                    error(call->token, "Style '" + call->name->name + "' needs " + std::to_string(parameters.size()) + " argument(s), but got " + std::to_string(call->arguments.size()));
                }
                for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                    if (i < parameters.size()) {
                        check_as(call->arguments[i], scope, parameters[i].type);
                    } else {
                        check(call->arguments[i], scope, Type{});
                    }
                }
            }

            // ---- 전개와 계산. 검사를 통과한 뒤에만 실행한다

            ir::Document build_document() {
                ir::Document document;
                // 매개변수가 없는 master는 미리 만들고, 있는 master는 slide가 넘긴 값마다 만든다
                for (const auto* master : master_order_) {
                    if (signatures_.at(master).empty()) {
                        master_instance(document, master, {});
                    }
                }

                for (std::size_t i = 0; i < slides_.size(); ++i) {
                    const auto* slide = slides_[i];
                    ir::Slide result;
                    result.page = static_cast<int>(i) + 1;
                    Env env;
                    env.page = result.page;
                    env.id = std::to_string(result.page);
                    result.source = block_of(slide, env);
                    if (const auto it = slide_layouts_.find(slide); it != slide_layouts_.end()) {
                        const LayoutInfo& info = it->second;
                        const auto& parameters = signatures_.at(info.master);
                        std::vector<ir::Value> arguments;
                        for (std::size_t j = 0; j < parameters.size(); ++j) {
                            arguments.push_back(evaluate(info.arguments[j], env, parameters[j].type));
                        }
                        const auto& cases = info.master->body;
                        const auto layout = static_cast<std::size_t>(std::find(cases.begin(), cases.end(), info.layout) - cases.begin());
                        result.layout = ir::LayoutRef{master_instance(document, info.master, arguments), layout};
                    }
                    scope_ = ElementScope{};
                    slide_animations_.clear();
                    media_positions_.clear();
                    execute(slide->body, env, result.elements, 0);
                    check_connectors();
                    if (const auto section = slide_sections_.find(slide); section != slide_sections_.end()) {
                        result.section = std::get<std::string>(evaluate(section->second->name, env, Type{Kind::String}));
                    }
                    for (const auto* statement : slide->body) {
                        if (statement->type == ast::COMMENT) {
                            // 여러 comment는 문단으로 이어 붙인다
                            const ir::Text notes = build_text(static_cast<const ast::ASTComment*>(statement)->expression, env);
                            result.notes.paragraphs.insert(result.notes.paragraphs.end(), notes.paragraphs.begin(), notes.paragraphs.end());
                            result.note_sources.push_back(block_of(statement, env));
                        } else if (statement->type == ast::TRANSITION) {
                            result.transition = build_transition(static_cast<const ast::ASTTransition*>(statement), env);
                            result.transition->source = block_of(statement, env);
                        } else if (statement->type == ast::ANIMATE) {
                            slide_animations_.push_back({build_animation(static_cast<const ast::ASTAnimate*>(statement), env), position_of(statement)});
                        } else if (statement->type == ast::REVIEW) {
                            result.reviews.push_back(build_review(static_cast<const ast::ASTReview*>(statement), env));
                            result.reviews.back().source = block_of(statement, env);
                        } else if (statement->type == ast::ASSIGN) {
                            const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                            if (assign->name->name == "layout") {
                                // 편집기는 layout 식(master(값).case)의 case 이름만 바꾼다
                                ir::Origin origin = block_of(assign, env);
                                if (origin.kind == ir::Origin::Kind::BLOCK) {
                                    origin.kind = ir::Origin::Kind::LITERAL;
                                    origin.statement = origin.range;
                                    origin.range = range_of(assign->expression);
                                }
                                result.origins["layout"] = origin;
                            }
                            apply_slide_property(document, result, assign, env);
                        }
                    }
                    add_media_animations(result.elements);
                    result.animations = ordered_animations();
                    document.slides.push_back(std::move(result));
                }

                for (const auto* target : targets_) {
                    document.targets.push_back(build_target(document, target));
                }
                return document;
            }

            // master를 매개변수 값으로 펼친다. 같은 master와 값이면 이미 만든 것을 쓴다
            std::size_t master_instance(ir::Document& document, const ast::ASTMaster* master, const std::vector<ir::Value>& arguments) {
                std::string name = master->name;
                if (!arguments.empty()) {
                    name += "(";
                    for (std::size_t i = 0; i < arguments.size(); ++i) {
                        const bool quoted = std::holds_alternative<std::string>(arguments[i]) || std::holds_alternative<ir::Text>(arguments[i]);
                        name += (i > 0 ? ", " : "") + (quoted ? "\"" + format_value(arguments[i]) + "\"" : format_value(arguments[i]));
                    }
                    name += ")";
                }
                const auto key = std::pair{master, name};
                if (const auto it = master_instances_.find(key); it != master_instances_.end()) {
                    return it->second;
                }
                Env env;
                env.lock = "layout";
                const auto& parameters = signatures_.at(master);
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    env.values[parameters[i].name] = arguments[i];
                }
                ir::Master result{name, {}, std::nullopt};
                for (const auto* statement : master->properties) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    result.theme = build_theme(themes_.at(static_cast<const ast::ASTName*>(assign->expression)->name));
                }
                for (const auto* statement : master->body) {
                    const auto* layout = static_cast<const ast::ASTMasterCase*>(statement);
                    ir::Layout layout_result{layout->name, {}, std::nullopt};
                    const ElementScope outer = std::move(scope_);
                    scope_ = ElementScope{true, {}, {}};
                    execute(layout->body, env, layout_result.elements, 0);
                    check_connectors();
                    scope_ = outer;
                    for (const auto* inner : layout->body) {
                        if (auto background = build_background(inner, env)) {
                            check_paint(*background, "background", location(static_cast<const ast::ASTAssign*>(inner)->expression));
                            layout_result.background = std::move(background);
                        }
                    }
                    result.layouts.push_back(std::move(layout_result));
                }
                const std::size_t index = document.masters.size();
                document.masters.push_back(std::move(result));
                master_instances_.emplace(key, index);
                return index;
            }

            // background = ...; 대입이면 그 값
            std::optional<ir::Value> build_background(const ast::ASTNode* statement, const Env& env) {
                if (statement->type != ast::ASSIGN) {
                    return std::nullopt;
                }
                const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                if (assign->name->name != "background") {
                    return std::nullopt;
                }
                return evaluate(assign->expression, env, Type{Kind::Background});
            }

            // layout이 아닌 slide 속성
            void apply_slide_property(const ir::Document& document, ir::Slide& slide, const ast::ASTAssign* assign, const Env& env) {
                const std::string& name = assign->name->name;
                const auto& properties = slide_properties();
                const auto it = std::find_if(properties.begin(), properties.end(), [&](const auto& property) { return property.first == name; });
                if (it == properties.end()) {
                    return;
                }
                ir::Value value = evaluate(assign->expression, env, it->second);
                slide.origins[name] = assigned_origin(assign, env, it->second);
                if (name == "background") {
                    check_paint(value, name, location(assign->expression));
                    slide.background = std::move(value);
                } else if (name == "title" || name == "subtitle" || name == "body") {
                    if (!slide.layout) {
                        error(assign->name->token, "'" + name + "' needs a layout with a " + name + " placeholder");
                    } else if (!has_placeholder(document.masters.at(slide.layout->master).layouts.at(slide.layout->layout).elements, name)) {
                        error(assign->name->token, "Layout '" + document.masters.at(slide.layout->master).layouts.at(slide.layout->layout).name + "' has no " + name + " placeholder");
                    }
                    slide.placeholders[name] = std::get<ir::Text>(value);
                } else if (name == "hidden") {
                    slide.hidden = std::get<bool>(value);
                } else if (name == "advance_after") {
                    check_number(as_number(value), Rule{Measure::Duration, 0}, "'" + name + "'", location(assign->expression));
                    slide.advance_after = as_number(value);
                } else if (name == "transition_sound") {
                    slide.transition_sound = std::get<std::string>(value);
                }
            }

            static bool has_placeholder(const std::vector<ir::Element>& elements, const std::string& role) {
                return std::any_of(elements.begin(), elements.end(), [&](const ir::Element& element) {
                    if (element.object == "placeholder") {
                        for (const auto& property : element.properties) {
                            if (const auto* value = std::get_if<ir::EnumValue>(&property.value); value != nullptr && property.name == "role" && value->member == role) {
                                return true;
                            }
                        }
                    }
                    return has_placeholder(element.children, role);
                });
            }

            ir::Theme build_theme(const ast::ASTTheme* theme) {
                ir::Theme result{theme->name, {}, "", ""};
                for (const auto* statement : theme->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    if (name == "heading_font" || name == "body_font") {
                        (name == "heading_font" ? result.heading_font : result.body_font) = std::get<std::string>(evaluate(assign->expression, Env{}, Type{Kind::String}));
                        continue;
                    }
                    for (const auto& [key, scheme] : theme_colors()) {
                        if (key == name) {
                            result.colors[scheme] = std::get<ir::Color>(evaluate(assign->expression, Env{}, Type{Kind::Color}));
                        }
                    }
                }
                return result;
            }

            // 대상 이름은 이 slide에서 put ... as로 붙인 이름이어야 한다
            // put, group 블록 안의 animate는 target이 그 블록이 만든 element다
            ir::Animation build_animation(const ast::ASTAnimate* node, const Env& env, const ir::Element* target = nullptr) {
                ir::Animation animation;
                animation.target = target != nullptr ? target->name : node->target->name;
                animation.element_id = target != nullptr ? target->id : "";
                animation.inside = target != nullptr;
                animation.source = block_of(node, env);
                animation.category = node->category->name;
                animation.effect = node->effect->name;
                if (node->path != nullptr) {
                    animation.path = std::get<std::string>(evaluate(node->path, env, Type{Kind::String}));
                    std::string message;
                    if (!geometry::parse_svg_path(animation.path, message)) {
                        error(location(node->path), "Invalid path: " + message);
                    }
                } else if (node->option != nullptr) {
                    animation.option = node->option->name;
                } else if (animation.category != "media") {
                    animation.option = animation_options().at(animation.category + "." + animation.effect).options.front();
                }
                if (node->duration != nullptr) {
                    animation.duration = as_number(evaluate(node->duration, env, Type{Kind::Float}));
                    check_number(*animation.duration, Rule{Measure::Duration, 0}, "'duration'", location(node->duration));
                }
                if (node->start != nullptr) {
                    animation.start = node->start->name;
                }
                if (node->delay != nullptr) {
                    animation.delay = as_number(evaluate(node->delay, env, Type{Kind::Float}));
                    check_number(*animation.delay, Rule{Measure::Duration, 0}, "'delay'", location(node->delay));
                }
                if (node->order != nullptr) {
                    const auto order = scalar(as_number(evaluate(node->order, env, Type{Kind::Int})));
                    if (!order || *order < 1) {
                        error(location(node->order), "'order' must be a number of 1 or more without a unit");
                    } else {
                        animation.order = static_cast<int>(*order);
                    }
                }
                if (target != nullptr) {
                    return animation;
                }
                const auto named = scope_.names.find(animation.target);
                if (named == scope_.names.end()) {
                    error(node->target->token, "No object named '" + animation.target + "' on this slide; give one a name with 'put ... as " + animation.target + "'");
                } else {
                    animation.element_id = named->second.id;
                    if (animation.category == "media" && named->second.object != "video" && named->second.object != "audio") {
                        error(node->target->token, "Media animations can only be used on a video or an audio, but '" + animation.target + "' is a " + named->second.object);
                    }
                }
                return animation;
            }

            // 재생 차례를 정하는 데 쓰는 animate 문장의 위치 (읽은 파일 순서, 파일 안의 위치)
            std::pair<std::size_t, std::size_t> position_of(const ast::ASTNode* node) const {
                const Source* source = source_of(node->span.data());
                if (source == nullptr) {
                    return {0, 0};
                }
                std::size_t index = 0;
                while (&sources_[index] != source) {
                    ++index;
                }
                return {index, static_cast<std::size_t>(node->span.data() - source->text.data())};
            }

            // put, group 블록 안의 animate를 그 블록이 만든 element에 건다. 이름이 없으면 붙인다
            void add_inner_animations(const std::vector<ast::ASTAnimate*>& animations, const Env& env, ir::Element& element) {
                if (animations.empty() || scope_.layout) {
                    return;
                }
                if (element.name.empty()) {
                    // 공백이 들어 있어 원문의 이름과 겹치지 않는다
                    element.name = element.object + " " + element.id;
                    element.generated_name = true;
                    scope_.names.emplace(element.name, NamedElement{element.object, "", element.id});
                }
                for (const auto* node : animations) {
                    slide_animations_.push_back({build_animation(node, env, &element), position_of(node)});
                }
            }

            // video, audio의 start가 click_sequence나 auto인데 재생하는 animate가 없으면 재생을 넣는다
            void add_media_animations(std::vector<ir::Element>& elements) {
                for (auto& element : elements) {
                    add_media_animations(element.children);
                    if (element.object != "video" && element.object != "audio") {
                        continue;
                    }
                    const auto* start = std::get_if<ir::EnumValue>(find_value(element, "start"));
                    if (start == nullptr || start->member == "when_clicked") {
                        continue;
                    }
                    const bool played = std::any_of(slide_animations_.begin(), slide_animations_.end(), [&](const PendingAnimation& pending) {
                        return pending.animation.element_id == element.id && pending.animation.category == "media" && pending.animation.effect == "play";
                    });
                    if (played) {
                        continue;
                    }
                    if (element.name.empty()) {
                        element.name = element.object + " " + element.id;
                        element.generated_name = true;
                    }
                    ir::Animation animation;
                    animation.target = element.name;
                    animation.element_id = element.id;
                    animation.category = "media";
                    animation.effect = "play";
                    animation.start = start->member == "auto" ? "after_previous" : "on_click";
                    animation.source = element.source;
                    animation.implicit = true;
                    slide_animations_.push_back({animation, media_positions_[element.id]});
                }
            }

            static const ir::Value* find_value(const ir::Element& element, const std::string& name) {
                for (const auto& property : element.properties) {
                    if (property.name == name) {
                        return &property.value;
                    }
                }
                return nullptr;
            }

            // order가 있는 것을 번호 순으로, 없는 것을 그 뒤에 적은 순서로. 같은 번호는 적은 순서를 따른다
            std::vector<ir::Animation> ordered_animations() {
                std::stable_sort(slide_animations_.begin(), slide_animations_.end(), [](const PendingAnimation& a, const PendingAnimation& b) {
                    const int max = std::numeric_limits<int>::max();
                    return std::tuple{a.animation.order.value_or(max), a.position} < std::tuple{b.animation.order.value_or(max), b.position};
                });
                std::vector<ir::Animation> result;
                for (auto& pending : slide_animations_) {
                    result.push_back(std::move(pending.animation));
                }
                slide_animations_.clear();
                return result;
            }

            ir::Review build_review(const ast::ASTReview* node, const Env& env) {
                ir::Review review{"", "templide", make_number("", 0, false), make_number("", 0, false)};
                for (const auto* statement : node->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    if (name == "text") {
                        review.text = plain_text(build_text(assign->expression, env));
                    } else if (name == "author") {
                        review.author = std::get<std::string>(evaluate(assign->expression, env, Type{Kind::String}));
                    } else if (name == "x" || name == "y") {
                        ir::Number& target = name == "x" ? review.x : review.y;
                        target = as_number(evaluate(assign->expression, env, Type{Kind::Float}));
                        check_number(target, Rule{Measure::Position}, "'" + name + "'", location(assign->expression));
                    }
                }
                return review;
            }

            ir::Transition build_transition(const ast::ASTTransition* node, const Env& env) {
                const std::string& kind = node->kind->name;
                ir::Transition transition{kind, node->option != nullptr ? node->option->name : transition_options().at(kind).front(), std::nullopt};
                if (node->duration != nullptr) {
                    transition.duration = as_number(evaluate(node->duration, env, Type{Kind::Float}));
                    check_number(*transition.duration, Rule{Measure::Duration, 0}, "'duration'", location(node->duration));
                }
                return transition;
            }

            ir::Target build_target(const ir::Document& document, const ast::ASTTarget* target) {
                ir::Target result{target->name, "", "", {}, std::nullopt, std::nullopt, "", "", false, "powerpoint"};
                result.source = block_of(target, Env{});
                result.width_origin = block_of(target, Env{});
                result.height_origin = result.width_origin;
                if (result.width_origin.kind == ir::Origin::Kind::BLOCK) {
                    result.width_origin.name = "width";
                    result.height_origin.name = "height";
                }
                for (const auto* statement : target->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    if (name == "title" || name == "author" || name == "loop") {
                        result.origins[name] = assigned_origin(assign, Env{}, name == "loop" ? Type{Kind::Bool} : Type{Kind::String});
                    }
                    if (name == "path") {
                        result.path = std::get<std::string>(evaluate(assign->expression, Env{}, Type{Kind::String}));
                    } else if (name == "type") {
                        result.type = std::get<ir::EnumValue>(evaluate(assign->expression, Env{}, enum_type("target_type"))).member;
                    } else if (name == "width" || name == "height") {
                        const ir::Number size = as_number(evaluate(assign->expression, Env{}, Type{Kind::Float}));
                        check_number(size, Rule{Measure::Length, 0}, "'" + name + "'", location(assign->expression));
                        (name == "width" ? result.width : result.height) = size;
                        (name == "width" ? result.width_origin : result.height_origin) = origin_of(assign->expression, Env{}, Type{Kind::Float});
                    } else if (name == "title" || name == "author") {
                        (name == "title" ? result.title : result.author) = std::get<std::string>(evaluate(assign->expression, Env{}, Type{Kind::String}));
                    } else if (name == "loop") {
                        result.loop = std::get<bool>(evaluate(assign->expression, Env{}, Type{Kind::Bool}));
                    } else if (name == "angles") {
                        result.angles = std::get<ir::EnumValue>(evaluate(assign->expression, Env{}, enum_type("angle_convention"))).member;
                    } else if (name == "script") {
                        result.script = std::get<std::string>(evaluate(assign->expression, Env{}, Type{Kind::String}));
                        result.script_where = where_of(location(assign->expression));
                    }
                }
                // slide들이 쓰는 master를 처음 쓰인 순서대로 모은다. 매개변수 값이 다르면 master도 여러 개가 된다
                for (const auto& slide : document.slides) {
                    if (slide.layout && std::find(result.masters.begin(), result.masters.end(), slide.layout->master) == result.masters.end()) {
                        result.masters.push_back(slide.layout->master);
                    }
                }
                return result;
            }

            void execute(const std::vector<ast::ASTNode*>& body, const Env& env, std::vector<ir::Element>& out, int depth) {
                for (const auto* statement : body) {
                    if (expansion_failed_) {
                        return;
                    }
                    if (statement->type == ast::PUT) {
                        instantiate(static_cast<const ast::ASTPut*>(statement), env, out, depth);
                    } else if (statement->type == ast::IF) {
                        execute_branch(static_cast<const ast::ASTIf*>(statement), env, out, depth);
                    } else if (statement->type == ast::FOR) {
                        execute_for(static_cast<const ast::ASTFor*>(statement), env, out, depth);
                    } else if (statement->type == ast::GROUP) {
                        const auto* group = static_cast<const ast::ASTGroup*>(statement);
                        ir::Element element{"group", {}, "", {}};
                        element.id = child_id(env, group);
                        element.source = env.instance ? *env.instance : block_of(group, env);
                        element.from_template = env.instance.has_value();
                        if (group->alias != nullptr) {
                            element.name = register_name(group->alias, "group", "", element.id);
                        }
                        execute(group->body, env, element.children, depth);
                        add_inner_animations(group->animations, env, element);
                        out.push_back(std::move(element));
                    }
                }
            }

            // env 안의 put이나 group 문장이 만드는 element의 id. layout에서는 비어 있다
            std::string child_id(const Env& env, const ast::ASTNode* statement) const {
                return env.id.empty() ? "" : env.id + "/" + std::to_string(range_of(statement).begin);
            }

            // put ... as NAME, group as NAME의 이름. slide나 layout 하나 안에서 겹치면 안 된다
            std::string register_name(const ast::ASTName* alias, const std::string& object, const std::string& kind, const std::string& id) {
                if (!scope_.names.emplace(alias->name, NamedElement{object, kind, id}).second) {
                    error(alias->token, "Duplicate object name '" + alias->name + "' on this " + (scope_.layout ? "layout" : "slide"));
                }
                return alias->name;
            }

            // number가 rule에 맞는지. 맞지 않으면 at에 에러를 남긴다. subject는 에러에서 값을 가리키는 말이다
            bool check_number(const ir::Number& number, const Rule& rule, const std::string& subject, const Token& at) {
                for (const auto& [unit, value] : number.terms) {
                    if (!unit_allowed(rule.measure, unit)) {
                        error(at, subject + " must be " + measure_name(rule.measure));
                        return false;
                    }
                }
                // %가 섞인 위치는 슬라이드 크기를 알아야 하므로 backend가 검사한다
                if (rule.measure == Measure::Position && std::any_of(number.terms.begin(), number.terms.end(), [](const auto& term) { return term.first == "%"; })) {
                    return true;
                }
                double total = 0;
                for (const auto& [unit, value] : number.terms) {
                    total += unit == "s" ? value * 1000 : value;
                }
                if (total < rule.min || total > rule.max || (rule.integer && total != std::floor(total))) {
                    const std::string unit = rule.measure == Measure::Percent ? "%" : "";
                    std::string range = rule.range;
                    if (range.empty() && std::isinf(rule.max)) {
                        range = format_bound(rule.min) + unit + " or more";
                    } else if (range.empty()) {
                        range = format_bound(rule.min) + unit + " and " + format_bound(rule.max) + unit;
                    }
                    const bool open = std::isinf(rule.max) && rule.range.empty();
                    error(at, subject + " must be " + (rule.integer ? "a whole number " : "") + (open ? range : "between " + range));
                    return false;
                }
                return true;
            }

            // fill, background의 그라데이션 각도와 색 위치
            void check_paint(const ir::Value& value, const std::string& name, const Token& at) {
                const auto* gradient = std::get_if<ir::Gradient>(&value);
                if (gradient == nullptr) {
                    return;
                }
                if (!gradient->radial) {
                    check_number(gradient->angle, Rule{Measure::Angle}, "The angle of '" + name + "'", at);
                }
                for (const auto& position : gradient->positions) {
                    if (position) {
                        check_number(*position, Rule{Measure::Percent, 0, 100}, "A color stop of '" + name + "'", at);
                    }
                }
            }

            // object가 받은 값의 단위, 범위, 도형 종류에 따른 규칙
            void check_element(const ir::Element& element, const std::map<std::string, const ast::ASTNode*>& sources, const Token& fallback) {
                const auto at = [&](const std::string& name) -> const Token& {
                    const auto it = sources.find(name);
                    return it != sources.end() ? location(it->second) : fallback;
                };
                const auto value_of = [&](const std::string& name) -> const ir::Value* {
                    for (const auto& property : element.properties) {
                        if (property.name == name) {
                            return &property.value;
                        }
                    }
                    return nullptr;
                };
                for (const auto& property : element.properties) {
                    if (const auto* number = std::get_if<ir::Number>(&property.value)) {
                        if (const auto rule = property_rules().find(property.name); rule != property_rules().end()) {
                            check_number(*number, rule->second, "'" + property.name + "'", at(property.name));
                        }
                    }
                    check_paint(property.value, property.name, at(property.name));
                }
                double crop_width = 0;
                double crop_height = 0;
                for (const auto& [name, horizontal] : {std::pair{"crop_left", true}, std::pair{"crop_right", true}, std::pair{"crop_top", false}, std::pair{"crop_bottom", false}}) {
                    if (const auto* number = std::get_if<ir::Number>(value_of(name))) {
                        (horizontal ? crop_width : crop_height) += number->terms.empty() ? 0 : number->terms[0].second;
                    }
                }
                if (crop_width >= 100 || crop_height >= 100) {
                    error(at(crop_width >= 100 ? "crop_right" : "crop_bottom"), "Crop must leave part of the image");
                }
                const std::string& object = element.object;
                const auto* kind_value = std::get_if<ir::EnumValue>(value_of("kind"));
                const std::string kind = kind_value != nullptr ? kind_value->member : "";
                const bool has_kind = object == "shape" || object == "backdrop" || object == "image";
                static const std::set<std::string> rounded = {"roundRect", "round1Rect", "round2SameRect", "round2DiagRect", "snipRoundRect"};
                if (value_of("radius") != nullptr && (!has_kind || !rounded.contains(kind))) {
                    error(at("radius"), "'radius' only applies to shapes, backdrops and images of kind roundRect, round1Rect, round2SameRect, round2DiagRect or snipRoundRect");
                }
                const auto adjustments = geometry::shape_adjustments().find(has_kind ? kind : "");
                const std::size_t count = adjustments == geometry::shape_adjustments().end() ? 0 : adjustments->second.size();
                for (std::size_t i = count; i < 8; ++i) {
                    const std::string name = "adj" + std::to_string(i + 1);
                    if (value_of(name) != nullptr) {
                        error(at(name), (has_kind ? "Kind '" + kind + "'" : "'" + object + "'") + " has no " + name + (count > 0 ? " (it has adj1 ~ adj" + std::to_string(count) + ")" : ""));
                    }
                }
                if (object == "freeform") {
                    if (const auto* path = std::get_if<std::string>(value_of("path"))) {
                        std::string message;
                        if (!geometry::parse_svg_path(*path, message)) {
                            error(at("path"), "Invalid path: " + message);
                        }
                    }
                }
                if (object == "placeholder" && !scope_.layout) {
                    error(fallback, "A placeholder can only be put in a master case; a slide fills it with title, subtitle or body");
                }
            }

            // 이름으로 가리킨 연결선의 두 끝. slide나 layout을 다 펼친 뒤에 찾는다
            void check_connectors() {
                for (const auto& connector : scope_.connectors) {
                    for (const auto& [name, token] : {std::pair{connector.from, connector.from_token}, std::pair{connector.to, connector.to_token}}) {
                        const auto it = scope_.names.find(name);
                        if (it == scope_.names.end()) {
                            error(token, "No object named '" + name + "' on this " + (scope_.layout ? "layout" : "slide"));
                        } else if (it->second.object == "group" || it->second.object == "line" || it->second.object == "connector") {
                            error(token, "Cannot connect to the " + it->second.object + " '" + name + "'");
                        } else if (!geometry::connection_sites().contains(it->second.kind)) {
                            error(token, "Shapes of kind '" + it->second.kind + "' have no connection points");
                        }
                    }
                }
            }

            void execute_for(const ast::ASTFor* node, const Env& env, std::vector<ir::Element>& out, int depth) {
                const Type& type = for_types_.at(node);
                std::vector<ir::Value> values;
                if (node->range_start != nullptr) {
                    const auto start = scalar(as_number(evaluate(node->range_start, env, Type{Kind::Int})));
                    const auto end = scalar(as_number(evaluate(node->range_end, env, Type{Kind::Int})));
                    if (!start || !end) {
                        error(location(start ? node->range_end : node->range_start), "A range must be made of numbers without units");
                        return;
                    }
                    if (*end - *start + 1 > max_loop_iterations) {
                        error(node->token, "Loop runs more than " + std::to_string(max_loop_iterations) + " times");
                        expansion_failed_ = true;
                        return;
                    }
                    for (double i = *start; i <= *end; ++i) {
                        values.push_back(make_number("", i, false));
                    }
                } else {
                    for (const auto* item : node->items) {
                        values.push_back(evaluate(item, env, type));
                    }
                }
                // 반복마다 같은 문장이 여러 element를 만드므로 화면에서 하나만 고칠 수 없다
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (expansion_failed_) {
                        return;
                    }
                    Env inner = env;
                    inner.values[node->name->name] = values[i];
                    inner.origins[node->name->name] = locked("for");
                    if (inner.lock.empty()) {
                        inner.lock = "for";
                    }
                    if (!inner.id.empty()) {
                        inner.id += "#" + std::to_string(i);
                    }
                    execute(node->body, inner, out, depth);
                }
            }

            template <typename T>
            void execute_branch(const T* node, const Env& env, std::vector<ir::Element>& out, int depth) {
                const ir::Value condition = evaluate(node->condition, env, Type{});
                const auto* result = std::get_if<bool>(&condition);
                if (result != nullptr && *result) {
                    execute(node->body, env, out, depth);
                    return;
                }
                if (node->branch == nullptr) {
                    return;
                }
                if (node->branch->type == ast::ELSE_IF) {
                    execute_branch(static_cast<const ast::ASTElseIf*>(node->branch), env, out, depth);
                } else {
                    execute(static_cast<const ast::ASTElse*>(node->branch)->body, env, out, depth);
                }
            }

            // template은 펼치고, object는 element로 만든다
            void instantiate(const ast::ASTPut* put, const Env& env, std::vector<ir::Element>& out, int depth) {
                if (depth >= max_expansion_depth) {
                    error(put->token, "Template expansion is too deep; check for a template that puts itself");
                    expansion_failed_ = true;
                    return;
                }
                const std::vector<VarInfo>& vars = *put_signature(put->name);
                const ir::Origin put_origin = block_of(put, env);
                Env self = env;
                self.self = put->alias != nullptr ? put->alias->name : "";
                Env inner;
                inner.page = env.page;
                inner.lock = "template";
                inner.id = child_id(env, put);
                for (const auto& var : vars) {
                    const ast::ASTAssign* assign = nullptr;
                    for (const auto* statement : put->body) {
                        const auto* candidate = static_cast<const ast::ASTAssign*>(statement);
                        if (candidate->name->name == var.name) {
                            assign = candidate;
                        }
                    }
                    if (assign != nullptr) {
                        inner.values[var.name] = evaluate(assign->expression, self, var.type);
                        inner.origins[var.name] = assigned_origin(assign, env, var.type);
                    } else {
                        if (var.default_value != nullptr) {
                            Env defaults;
                            defaults.page = env.page;
                            inner.values[var.name] = evaluate(var.default_value, defaults, var.type);
                        }
                        inner.origins[var.name] = missing(put_origin, var);
                    }
                    // optional인 속성은 값을 넣지 않았으면 비워 둔다
                }
                // template 안에서 만든 element는 slide에 적은 이 put을 함께 기억한다
                inner.instance = env.instance ? *env.instance : put_origin;
                inner.instance_xy = env.instance_xy;
                if (!env.instance) {
                    for (const char* name : {"x", "y", "width", "height"}) {
                        if (const auto it = inner.values.find(name); it != inner.values.end()) {
                            inner.instance_xy.push_back({name, it->second, inner.origins.at(name)});
                        }
                    }
                }

                if (objects_.contains(put->name)) {
                    ir::Element element{put->name, {}, "", {}};
                    element.id = inner.id;
                    element.source = env.instance ? *env.instance : put_origin;
                    element.from_template = env.instance.has_value();
                    if (element.from_template) {
                        element.instance = env.instance_xy;
                    }
                    std::map<std::string, const ast::ASTNode*> sources;
                    for (const auto& var : vars) {
                        if (const auto it = inner.values.find(var.name); it != inner.values.end()) {
                            element.properties.push_back({var.name, it->second, inner.origins.at(var.name)});
                        }
                        if (var.default_value != nullptr) {
                            sources[var.name] = var.default_value;
                        }
                    }
                    for (const auto* statement : put->body) {
                        const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                        sources[assign->name->name] = assign->expression;
                    }
                    check_element(element, sources, put->token);
                    // 연결점 표의 도형 종류. shape, backdrop, image가 아닌 개체는 사각형처럼 연결한다
                    std::string kind = "rect";
                    const bool has_kind = put->name == "shape" || put->name == "backdrop" || put->name == "image";
                    for (const auto& property : element.properties) {
                        if (const auto* value = std::get_if<ir::EnumValue>(&property.value); value != nullptr && has_kind && property.name == "kind") {
                            kind = value->member;
                        }
                    }
                    if (put->alias != nullptr) {
                        element.name = register_name(put->alias, put->name, kind, element.id);
                    }
                    if (put->name == "video" || put->name == "audio") {
                        media_positions_[element.id] = position_of(put);
                    }
                    add_inner_animations(put->animations, env, element);
                    if (put->name == "connector") {
                        const auto* from = std::get_if<std::string>(&inner.values["from"]);
                        const auto* to = std::get_if<std::string>(&inner.values["to"]);
                        if (from != nullptr && to != nullptr) {
                            scope_.connectors.push_back({*from, *to, location(sources.at("from")), location(sources.at("to"))});
                        }
                    }
                    out.push_back(std::move(element));
                    return;
                }
                const std::string name = put->alias != nullptr ? register_name(put->alias, "group", "", inner.id) : "";
                // 이름이 붙었거나 animate가 있는 template은 만든 개체들을 그룹 하나로 묶는다
                if (name.empty() && (put->animations.empty() || scope_.layout)) {
                    execute(templates_.at(put->name)->body, inner, out, depth + 1);
                    return;
                }
                ir::Element group{"group", {}, name, {}};
                group.id = inner.id;
                group.source = env.instance ? *env.instance : put_origin;
                group.from_template = env.instance.has_value();
                execute(templates_.at(put->name)->body, inner, group.children, depth + 1);
                add_inner_animations(put->animations, env, group);
                out.push_back(std::move(group));
            }

            ir::Value evaluate(const ast::ASTNode* expr, const Env& env, const Type& expected) {
                if (expected.kind == Kind::Text) {
                    return build_text(expr, env);
                }
                ir::Value value = evaluate_value(expr, env, expected);
                if (expected.kind == Kind::Float || expected.kind == Kind::Int) {
                    if (auto* number = std::get_if<ir::Number>(&value)) {
                        number->is_float = expected.kind == Kind::Float;
                    }
                }
                if (expected.kind == Kind::Link) {
                    return to_link(value);
                }
                if (expected.kind == Kind::Action && !std::holds_alternative<ir::Action>(value)) {
                    return ir::Action{"link", to_link(value), "", {}, ""};
                }
                return value;
            }

            // url 문자열, slide(n), slide_jump 값을 링크로
            static ir::Link to_link(const ir::Value& value) {
                if (const auto* link = std::get_if<ir::Link>(&value)) {
                    return *link;
                }
                if (const auto* url = std::get_if<std::string>(&value)) {
                    return ir::Link{*url, 0, ""};
                }
                if (const auto* jump = std::get_if<ir::EnumValue>(&value)) {
                    return ir::Link{"", 0, jump->member};
                }
                return ir::Link{};
            }

            ir::Value evaluate_value(const ast::ASTNode* expr, const Env& env, const Type& expected) {
                switch (expr->type) {
                    case ast::INT:
                        return make_number("", static_cast<double>(static_cast<const ast::ASTInt*>(expr)->value), false);
                    case ast::FLOAT:
                        return make_number("", static_cast<double>(static_cast<const ast::ASTFloat*>(expr)->value), true);
                    case ast::DIMENSION: {
                        // pt, in, cm, mm는 px로 바꾸고 %, deg, s, ms는 그대로 둔다
                        const auto* dimension = static_cast<const ast::ASTDimension*>(expr);
                        ir::Number number = as_number(evaluate_value(dimension->value, env, Type{}));
                        const double factor = px_per_unit(dimension->unit).value_or(0);
                        number.terms[0].first = factor > 0 ? "px" : dimension->unit;
                        number.terms[0].second *= factor > 0 ? factor : 1;
                        return number;
                    }
                    case ast::STRING:
                        return static_cast<const ast::ASTString*>(expr)->value;
                    case ast::FSTRING: {
                        std::string result;
                        for (const auto* part : static_cast<const ast::ASTFString*>(expr)->parts) {
                            result += part->type == ast::STRING ? static_cast<const ast::ASTString*>(part)->value : format_value(evaluate(part, env, Type{}));
                        }
                        return result;
                    }
                    case ast::NAME: {
                        const std::string& name = static_cast<const ast::ASTName*>(expr)->name;
                        if (const auto it = env.values.find(name); it != env.values.end()) {
                            return it->second;
                        }
                        if (name == "true" || name == "false") {
                            return name == "true";
                        }
                        if (expected.kind == Kind::Enum) {
                            return ir::EnumValue{expected.enumeration->name, name};
                        }
                        if (expected.kind == Kind::Ref) {
                            return name;
                        }
                        if (expected.kind == Kind::Link || expected.kind == Kind::Action) {
                            return ir::EnumValue{"slide_jump", name};
                        }
                        return ir::Value{};
                    }
                    case ast::MEMBER:
                        return evaluate_member(static_cast<const ast::ASTMember*>(expr), env);
                    case ast::CONTEXT:
                        // 검사를 통과했으면 @date뿐이다
                        return field_text("datetime1", today());
                    case ast::ADD: {
                        const auto* node = static_cast<const ast::ASTAdd*>(expr);
                        return arithmetic(node->left, node->right, env, 1);
                    }
                    case ast::MINUS: {
                        const auto* node = static_cast<const ast::ASTMinus*>(expr);
                        return arithmetic(node->left, node->right, env, -1);
                    }
                    case ast::MULTIPLY: {
                        const auto* node = static_cast<const ast::ASTMultiply*>(expr);
                        return multiply(node->left, node->right, env);
                    }
                    case ast::EQUAL:
                        return evaluate_equal(static_cast<const ast::ASTEqual*>(expr), env);
                    case ast::COLOR_RGB: {
                        const auto* color = static_cast<const ast::ASTColorRGB*>(expr);
                        return ir::Color{color_component(color->r, env), color_component(color->g, env), color_component(color->b, env), 1, ""};
                    }
                    case ast::COLOR_RGBA: {
                        const auto* color = static_cast<const ast::ASTColorRGBA*>(expr);
                        return ir::Color{color_component(color->r, env), color_component(color->g, env), color_component(color->b, env), color_alpha(color->a, env), ""};
                    }
                    case ast::COLOR_HEX:
                        return hex_color(static_cast<const ast::ASTString*>(static_cast<const ast::ASTColorHex*>(expr)->hex)->value);
                    case ast::CALL:
                        // 검사를 통과했으면 linear나 image뿐이다
                        return evaluate_call(static_cast<const ast::ASTCall*>(expr), env);
                    case ast::TEXT:
                    case ast::LIST:
                        return build_text(expr, env);
                    default:
                        return ir::Value{};
                }
            }

            // theme.<이름>, @slide.number, @slide.page, @slide.count, @self.name
            ir::Value evaluate_member(const ast::ASTMember* node, const Env& env) {
                const std::string& member = node->member->name;
                if (node->object->type == ast::CONTEXT && static_cast<const ast::ASTContext*>(node->object)->name->name == "self") {
                    if (!env.self) {
                        error(location(node), "'@self' can only be used in the properties of a put");
                        return std::string();
                    }
                    if (env.self->empty()) {
                        error(location(node), "'@self.name' needs a named object; write 'put ... as NAME'");
                    }
                    return *env.self;
                }
                if (node->object->type == ast::CONTEXT && member == "count") {
                    return make_number("", static_cast<double>(slides_.size()), false);
                }
                if (node->object->type == ast::NAME) {
                    if (member == "heading_font" || member == "body_font") {
                        return std::string(member == "heading_font" ? "+mj" : "+mn");
                    }
                    for (const auto& [name, scheme] : theme_colors()) {
                        if (name == member) {
                            return ir::default_theme_color(scheme);
                        }
                    }
                    return ir::Value{};
                }
                if (member == "number") {
                    // layout에서는 PowerPoint처럼 ‹#›로 보인다
                    return field_text("slidenum", env.page ? std::to_string(*env.page) : "\xE2\x80\xB9#\xE2\x80\xBA");
                }
                if (!env.page) {
                    error(location(node), "'@slide' cannot be used while expanding a master");
                    return make_number("", 0, false);
                }
                return make_number("", *env.page, false);
            }

            // 필드 하나로 된 text
            static ir::Text field_text(const std::string& field, const std::string& text) {
                ir::Text result;
                result.paragraphs.push_back({ir::ListKind::NONE, 0, {ir::Run{text, {}, field}}});
                return result;
            }

            static std::string today() {
                const std::time_t now = std::time(nullptr);
                char buffer[16];
                std::strftime(buffer, sizeof buffer, "%Y-%m-%d", std::localtime(&now));
                return buffer;
            }

            ir::Value evaluate_call(const ast::ASTCall* call, const Env& env) {
                const auto& arguments = call->arguments;
                const std::string& name = call->name->name;
                if (name == "image") {
                    const ir::Value path = evaluate(arguments[0], env, Type{Kind::String});
                    const auto* string = std::get_if<std::string>(&path);
                    return ir::Image{string != nullptr ? *string : ""};
                }
                if (is_action_call(name)) {
                    return evaluate_action_call(call, env);
                }
                if (name == "slide") {
                    const auto page = scalar(as_number(evaluate(arguments[0], env, Type{Kind::Int})));
                    if (!page || *page < 1 || *page != std::floor(*page)) {
                        error(location(arguments[0]), "A slide number must be a whole number from 1 without a unit");
                        return ir::Link{};
                    }
                    if (*page > static_cast<double>(slides_.size())) {
                        error(location(arguments[0]), "There is no slide " + format_bound(*page) + " to link to");
                    }
                    return ir::Link{"", static_cast<int>(*page), ""};
                }
                if (name == "pattern") {
                    const ir::Value kind = evaluate(arguments[0], env, enum_type("pattern_kind"));
                    const ir::Value foreground = evaluate(arguments[1], env, Type{Kind::Color});
                    const ir::Value background = evaluate(arguments[2], env, Type{Kind::Color});
                    return ir::Pattern{std::get<ir::EnumValue>(kind).member, std::get<ir::Color>(foreground), std::get<ir::Color>(background)};
                }
                // linear(각도, ...) 또는 radial(...)
                const bool radial = name == "radial";
                ir::Gradient gradient{radial ? ir::Number{} : as_number(evaluate(arguments[0], env, Type{Kind::Float})), {}, {}, radial};
                for (std::size_t i = radial ? 0 : 1; i < arguments.size(); ++i) {
                    const ast::ASTNode* color = arguments[i];
                    std::optional<ir::Number> position;
                    if (const auto* stop = gradient_stop(arguments[i])) {
                        color = stop->parts[0];
                        position = as_number(evaluate(stop->parts[1], env, Type{Kind::Float}));
                    }
                    const ir::Value value = evaluate(color, env, Type{Kind::Color});
                    if (const auto* rgb = std::get_if<ir::Color>(&value)) {
                        gradient.colors.push_back(*rgb);
                        gradient.positions.push_back(position);
                    }
                }
                return gradient;
            }

            ir::Action evaluate_action_call(const ast::ASTCall* call, const Env& env) {
                using Argument = std::variant<bool, double, std::string>;
                const auto& arguments = call->arguments;
                ir::Action action{call->name->name, {}, "", {}, where_of(call->token)};
                const ir::Value target = evaluate(arguments[0], env, Type{Kind::String});
                if (const auto* string = std::get_if<std::string>(&target)) {
                    action.target = *string;
                }
                if (action.target.empty()) {
                    error(location(arguments[0]), "The " + std::string(action.kind == "run" ? "function name" : action.kind == "macro" ? "macro name" : "path") + " must not be empty");
                }
                // 단위가 없는 수는 수로, 단위가 있는 수는 "10px" 같은 문자열로 넘긴다. @slide.number는 그 슬라이드의 번호다
                for (std::size_t i = 1; i < arguments.size(); ++i) {
                    const ir::Value value = evaluate_value(arguments[i], env, Type{});
                    if (const auto* flag = std::get_if<bool>(&value)) {
                        action.arguments.emplace_back(*flag);
                    } else if (const auto* number = std::get_if<ir::Number>(&value)) {
                        const auto plain = scalar(*number);
                        action.arguments.push_back(plain ? Argument(*plain) : Argument(ir::format_number(*number)));
                    } else if (const auto* string = std::get_if<std::string>(&value)) {
                        action.arguments.emplace_back(*string);
                    } else if (const auto* text = std::get_if<ir::Text>(&value)) {
                        const bool slide_number = text->paragraphs.size() == 1 && text->paragraphs[0].runs.size() == 1 && text->paragraphs[0].runs[0].field == "slidenum";
                        if (slide_number && !env.page) {
                            error(location(arguments[i]), "'@slide.number' cannot be passed to run in a master; there is no slide yet");
                        }
                        action.arguments.push_back(slide_number && env.page ? Argument(static_cast<double>(*env.page)) : Argument(plain_text(*text)));
                    }
                }
                return action;
            }

            ir::Value arithmetic(const ast::ASTNode* left, const ast::ASTNode* right, const Env& env, double sign) {
                ir::Number result = as_number(evaluate_value(left, env, Type{}));
                const ir::Number other = as_number(evaluate_value(right, env, Type{}));
                for (const auto& [unit, value] : other.terms) {
                    add_term(result, unit, sign * value);
                }
                result.is_float = result.is_float || other.is_float;
                normalize(result);
                return result;
            }

            // 단위는 한쪽에만 붙을 수 있다. 3 * 20px -> 60px
            ir::Value multiply(const ast::ASTNode* left, const ast::ASTNode* right, const Env& env) {
                const ir::Number a = as_number(evaluate_value(left, env, Type{}));
                const ir::Number b = as_number(evaluate_value(right, env, Type{}));
                const auto a_scalar = scalar(a);
                const auto b_scalar = scalar(b);
                if (!a_scalar && !b_scalar) {
                    error(location(left), "Cannot multiply two numbers that both have units");
                    return ir::Number{};
                }
                ir::Number result = a_scalar ? b : a;
                const double factor = a_scalar ? *a_scalar : *b_scalar;
                if (result.terms.empty()) {
                    result.terms.emplace_back("", 0);
                }
                for (auto& term : result.terms) {
                    term.second *= factor;
                }
                result.is_float = a.is_float || b.is_float;
                normalize(result);
                return result;
            }

            ir::Value evaluate_equal(const ast::ASTEqual* node, const Env& env) {
                const auto expected_for = [&](const ir::Value& value) {
                    const auto* enum_value = std::get_if<ir::EnumValue>(&value);
                    return enum_value == nullptr ? Type{} : enum_type(enum_value->type);
                };
                ir::Value left;
                ir::Value right;
                if (node->left->type == ast::NAME && !env.values.contains(static_cast<const ast::ASTName*>(node->left)->name)) {
                    right = evaluate(node->right, env, Type{});
                    left = evaluate(node->left, env, expected_for(right));
                } else {
                    left = evaluate(node->left, env, Type{});
                    right = evaluate(node->right, env, expected_for(left));
                }
                return values_equal(left, right);
            }

            int color_component(const ast::ASTNode* expr, const Env& env) {
                const auto value = scalar(as_number(evaluate(expr, env, Type{Kind::Int})));
                if (!value || *value < 0 || *value > 255) {
                    error(location(expr), "Color component must be a number from 0 to 255 without a unit");
                    return 0;
                }
                return static_cast<int>(*value);
            }

            double color_alpha(const ast::ASTNode* expr, const Env& env) {
                const auto value = scalar(as_number(evaluate(expr, env, Type{Kind::Float})));
                if (!value || *value < 0 || *value > 1) {
                    error(location(expr), "Alpha must be a number from 0 to 1 without a unit");
                    return 1;
                }
                return *value;
            }

            // ---- text 만들기

            ir::Text build_text(const ast::ASTNode* expr, const Env& env) {
                TextBuilder builder;
                append_sequence(expr, env, ir::TextStyle{}, ParagraphContext{ir::ListKind::NONE, 0}, builder);
                return std::move(builder.text);
            }

            // style은 값으로 받아서 이 text 안에서만 바뀐다
            void append_parts(const std::vector<const ast::ASTNode*>& parts, const Env& env, ir::TextStyle style, const ParagraphContext& context, TextBuilder& builder) {
                for (const auto* part : parts) {
                    switch (part->type) {
                        case ast::NAME: {
                            const std::string& name = static_cast<const ast::ASTName*>(part)->name;
                            if (const auto it = env.values.find(name); it != env.values.end()) {
                                append_value(builder, it->second, style, context, origin_of(part, env, Type{}));
                            } else {
                                apply_style(style, styles_.at(name), {}, env);
                            }
                            break;
                        }
                        case ast::CALL: {
                            const auto* call = static_cast<const ast::ASTCall*>(part);
                            if (call->name->name == "link" && !styles_.contains("link")) {
                                style.link = to_link(evaluate(call->arguments[0], env, Type{Kind::Link}));
                                if (style.action) {
                                    error(call->token, "This text already has an action; use either link(...) or action(...)");
                                }
                            } else if (is_text_action(call->name->name)) {
                                const ir::Value value = evaluate(call->arguments[0], env, Type{Kind::Action});
                                (call->name->name == "action" ? style.action : style.hover_action) = std::get<ir::Action>(value);
                                if (call->name->name == "action" && style.link) {
                                    error(call->token, "This text already has a link; use either link(...) or action(...)");
                                }
                            } else {
                                apply_style(style, styles_.at(call->name->name), call->arguments, env);
                            }
                            break;
                        }
                        case ast::INLINE_STYLE:
                            for (const auto* assign : static_cast<const ast::ASTInlineStyle*>(part)->properties) {
                                apply_style_assign(style, assign, env);
                            }
                            break;
                        case ast::TEXT:
                            append_sequence(part, env, style, context, builder);
                            break;
                        case ast::LIST:
                            append_list(builder, static_cast<const ast::ASTList*>(part), env, style, context);
                            break;
                        default:
                            append_value(builder, evaluate_value(part, env, Type{}), style, context, origin_of(part, env, Type{Kind::Text}));
                            break;
                    }
                }
            }

            // node(text 식, 괄호 묶음, 목록 항목)의 항들을 이어 붙인다.
            // 항이 (style(...) "글자")뿐이면 편집기가 서식을 합치거나 지울 수 있게 그 style과 묶음의 범위를 기억한다
            void append_sequence(const ast::ASTNode* node, const Env& env, const ir::TextStyle& style, const ParagraphContext& context, TextBuilder& builder) {
                const auto parts = parts_of(node);
                if (parts.size() != 2 || parts[0]->type != ast::INLINE_STYLE || parts[1]->type != ast::STRING) {
                    append_parts(parts, env, style, context, builder);
                    return;
                }
                ir::TextStyle wrapped = style;
                for (const auto* assign : static_cast<const ast::ASTInlineStyle*>(parts[0])->properties) {
                    apply_style_assign(wrapped, assign, env);
                }
                ir::Origin origin = origin_of(parts[1], env, Type{Kind::Text});
                if (origin.kind == ir::Origin::Kind::LITERAL) {
                    origin.style = range_of(parts[0]);
                    origin.group = range_of(node);
                }
                append_value(builder, evaluate_value(parts[1], env, Type{}), wrapped, context, origin);
            }

            void apply_style(ir::TextStyle& style, const ast::ASTStyle* declaration, const std::vector<ast::ASTNode*>& arguments, const Env& env) {
                const auto& parameters = signatures_.at(declaration);
                Env style_env;
                style_env.page = env.page;
                for (std::size_t i = 0; i < parameters.size() && i < arguments.size(); ++i) {
                    style_env.values[parameters[i].name] = evaluate(arguments[i], env, parameters[i].type);
                }
                for (const auto* statement : declaration->body) {
                    apply_style_assign(style, static_cast<const ast::ASTStyleAssign*>(statement), style_env);
                }
            }

            void apply_style_assign(ir::TextStyle& style, const ast::ASTStyleAssign* assign, const Env& env) {
                const PropertyInfo* property = find_property(assign->identifier->name);
                const ir::Value value = evaluate(assign->expression, env, property->type);
                if (const auto* number = std::get_if<ir::Number>(&value)) {
                    if (const auto rule = style_rules().find(property->name); rule != style_rules().end()) {
                        check_number(*number, rule->second, "'" + property->name + "'", location(assign->expression));
                    }
                }
                property->apply(style, value);
            }

            // origin은 문자열 값의 출처. text 값의 run은 자기 출처를 이미 가지고 있다
            void append_value(TextBuilder& builder, const ir::Value& value, const ir::TextStyle& style, const ParagraphContext& context, const ir::Origin& origin) {
                if (const auto* string = std::get_if<std::string>(&value)) {
                    append_run(builder, *string, style, context, origin);
                } else if (const auto* text = std::get_if<ir::Text>(&value)) {
                    append_text(builder, *text, style, context);
                }
            }

            void append_run(TextBuilder& builder, const std::string& text, const ir::TextStyle& style, const ParagraphContext& context, const ir::Origin& origin) {
                if (text.empty()) {
                    return;
                }
                if (!builder.open) {
                    builder.text.paragraphs.push_back({context.list, context.level, {}});
                    builder.open = true;
                }
                builder.text.paragraphs.back().runs.push_back({text, style, "", origin});
            }

            // 이미 만들어진 text를 끼워 넣는다. 바깥 style 위에 run 자신의 style을 덮어쓴다
            void append_text(TextBuilder& builder, const ir::Text& text, const ir::TextStyle& style, const ParagraphContext& context) {
                for (std::size_t i = 0; i < text.paragraphs.size(); ++i) {
                    const ir::Paragraph& paragraph = text.paragraphs[i];
                    std::vector<ir::Run> runs;
                    for (const auto& run : paragraph.runs) {
                        runs.push_back({run.text, merge(style, run.style), run.field, run.origin});
                    }
                    if (paragraph.list != ir::ListKind::NONE) {
                        const int offset = context.list == ir::ListKind::NONE ? 0 : context.level + 1;
                        builder.text.paragraphs.push_back({paragraph.list, paragraph.level + offset, std::move(runs)});
                        builder.open = false;
                        continue;
                    }
                    if (i > 0 || !builder.open) {
                        builder.text.paragraphs.push_back({context.list, context.level, {}});
                        builder.open = true;
                    }
                    auto& target = builder.text.paragraphs.back().runs;
                    target.insert(target.end(), runs.begin(), runs.end());
                }
            }

            // 목록 안의 목록은 한 단계 깊어진다. paragraphs는 기호 없는 문단이라 깊이를 바꾸지 않는다
            void append_list(TextBuilder& builder, const ast::ASTList* list, const Env& env, const ir::TextStyle& style, const ParagraphContext& context) {
                const ir::ListKind kind = to_ir(list->kind);
                const int level = list->kind == ast::ListKind::PARAGRAPHS ? context.level : context.list == ir::ListKind::NONE ? 0 : context.level + 1;
                builder.open = false;
                for (const auto* item : list->items) {
                    const ParagraphContext item_context{kind, level};
                    if (item->type == ast::LIST) {
                        append_list(builder, static_cast<const ast::ASTList*>(item), env, style, item_context);
                        continue;
                    }
                    builder.text.paragraphs.push_back({kind, level, {}});
                    builder.open = true;
                    append_sequence(item, env, style, item_context, builder);
                    builder.open = false;
                }
            }
        };
    }

    Result analyze(const std::filesystem::path& path, const std::filesystem::path& packages_dir, const Overlays& overlays) {
        return Analyzer(packages_dir, overlays).run(path);
    }
}

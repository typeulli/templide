#include "analyzer.h"
#include "geometry.h"
#include "source_text.h"
#include "tasset.h"
#include "color.h"
#include "../format/options.h"
#include "latex.h"

#include "../frontend/lexor.h"
#include "../frontend/parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
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
            // Action은 Link가 받는 값과 run(...), program(...), macro(...), action_file(...)을 받는다.
            // Image, Video, Audio는 그림, 비디오, 오디오 파일이고 File(file(...), asset(...))이나 문자열 경로를 받는다
            // List는 element 타입의 값을 [a, b, c]로 나열한 list<T>다
            enum class Kind { Any, Error, Bool, Int, Float, String, Text, Color, Enum, Gradient, Image, Fill, Background, Pattern, Link, SlideRef, Ref, Action, Video, Audio, File, Font, List };
            Kind kind = Kind::Any;
            const EnumInfo* enumeration = nullptr;
            std::shared_ptr<const Type> element; // kind가 List일 때 항목의 타입
        };
        using Kind = Type::Kind;

        Type list_of(const Type& element) {
            return Type{Kind::List, nullptr, std::make_shared<const Type>(element)};
        }

        bool same_type(const Type& a, const Type& b) {
            if (a.kind != b.kind || a.enumeration != b.enumeration) {
                return false;
            }
            return a.kind != Kind::List || same_type(*a.element, *b.element);
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
            // 문자열 경로는 이름 없는 file("경로")로 본다
            const bool media = to.kind == Kind::Image || to.kind == Kind::Video || to.kind == Kind::Audio;
            return (from.kind == Kind::String && to.kind == Kind::Text) || (from.kind == Kind::Int && to.kind == Kind::Float)
                || (paint && (to.kind == Kind::Fill || to.kind == Kind::Background)) || (link && (to.kind == Kind::Link || to.kind == Kind::Action))
                || ((from.kind == Kind::String || from.kind == Kind::File) && media) || (from.kind == Kind::File && (to.kind == Kind::Fill || to.kind == Kind::Background));
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
                case Kind::Video: return "video";
                case Kind::Audio: return "audio";
                case Kind::File: return "file";
                case Kind::Font: return "font";
                case Kind::List: return "list<" + type_name(*type.element) + ">";
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
            std::vector<ast::ASTNode*> arguments; // master의 매개변수마다 넘기는 값. 넘기지 않아 기본값을 쓰는 매개변수는 nullptr
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
                    {"header_rows", {Measure::Ratio, 0, 1000, true}}, {"header_cols", {Measure::Ratio, 0, 1000, true}},
                    {"col_span", {Measure::Ratio, 1, 1000, true}}, {"row_span", {Measure::Ratio, 1, 1000, true}},
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
                case ast::ListKind::PLAIN: return ir::ListKind::NONE;
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
            std::string result = (action.kind == "file" ? "action_file" : action.kind) + "(\"" + action.target + "\"";
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
                add_builtin_enum("target_type", {"pptx", "html", "web", "pdf", "png"});
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
                    {"font-family", Type{Kind::Font}, [](ir::TextStyle& style, const ir::Value& value) { style.font_family = std::get<std::string>(value); }},
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
                    {"action_sound", Type{Kind::Audio}, nullptr, true},
                    {"hover_sound", Type{Kind::Audio}, nullptr, true},
                    {"action_highlight", Type{Kind::Bool}, nullptr, true},
                    {"hover_highlight", Type{Kind::Bool}, nullptr, true},
                    // 대체 텍스트. 화면 낭독기가 읽는다
                    {"alt", Type{Kind::String}, nullptr, true},
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
                sort_diagnostics(warnings_);
                result.warnings = std::move(warnings_);
                for (const auto& source : sources_) {
                    result.sources.emplace_back(source.path, source.text);
                }
                result.includes = std::move(includes_);
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
                        info.parameters.push_back(var_of(var));
                    }
                    for (const auto* statement : master->body) {
                        info.cases.push_back(static_cast<const ast::ASTMasterCase*>(statement)->name);
                    }
                    symbols_.masters[name] = info;
                }
                for (const auto& [name, theme] : themes_) {
                    symbols_.themes.push_back(name);
                }
                for (const auto& [name, node] : templates_) {
                    const ir::SourceRange range = range_of(node);
                    symbols_.definitions.push_back({"template", name, {}, range.path, range.begin, range.end, in_package(node)});
                }
                for (const auto& [name, node] : masters_) {
                    const ir::SourceRange range = range_of(node);
                    SymbolDefinition definition{"master", name, {}, range.path, range.begin, range.end, in_package(node)};
                    for (const auto* statement : node->body) {
                        definition.cases.push_back(static_cast<const ast::ASTMasterCase*>(statement)->name);
                    }
                    symbols_.definitions.push_back(std::move(definition));
                }
                for (const auto& [name, constant] : constants_) {
                    symbols_.constants[name] = constant->type_name->name;
                }
                for (const auto* node : assets_) {
                    SymbolAsset info;
                    info.bundle = utf8(bundle_of(node));
                    info.written = node->path;
                    info.is_default = node->is_default;
                    info.has_by = node->has_by;
                    std::string message;
                    const auto entries = tasset::list(bundle_of(node), message);
                    info.entries = entries.value_or(std::vector<std::string>{});
                    for (const auto& item : node->items) {
                        if (item.all) {
                            info.namespaces.push_back(item.alias->name);
                            continue;
                        }
                        for (const auto& entry : info.entries) {
                            if (entry == item.name || without_extension(entry) == item.name) {
                                info.aliases[item.alias->name] = entry;
                            }
                        }
                    }
                    symbols_.assets.push_back(std::move(info));
                }
                symbols_.common_properties = vars_of(common_properties_);
                symbols_.text_properties = vars_of(text_properties_);
                for (const auto& property : properties_) {
                    symbols_.style_properties.push_back({property.name, type_name(property.type), "", false});
                }
                for (const auto& [name, type] : slide_properties()) {
                    symbols_.slide_properties.push_back({name, type_name(type), "", false});
                }
                for (const auto& [name, type] : cell_properties()) {
                    symbols_.cell_properties.push_back({name, type_name(type), "", false});
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
                main_folder_ = path.parent_path();
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
                Result result{std::move(document), {}};
                if (definition_request_) {
                    build_definition(result);
                }
                return result;
            }

            // 편집기가 정의 하나를 슬라이드로 보려 할 때
            void request_definition(const DefinitionRequest& request) {
                definition_request_ = request;
            }

        private:
            struct Source {
                std::string path;
                std::string text;
                bool package = false; // packages_dir 안의 파일. 편집기에서 고칠 수 없다
                std::filesystem::path file;
            };

            std::optional<DefinitionRequest> definition_request_;
            std::filesystem::path packages_dir_;
            std::map<std::filesystem::path, std::string> overlays_; // file_key -> 내용
            std::string main_path_;
            std::filesystem::path main_folder_; // 그림, 미디어 경로의 기준
            // Token의 string_view가 가리키므로 원소의 주소가 바뀌면 안 된다
            std::deque<Source> sources_;
            std::vector<Include> includes_;
            std::set<std::filesystem::path> loaded_;
            std::vector<const ast::ASTNode*> statements_;

            std::vector<Diagnostic> diagnostics_;
            std::vector<Diagnostic> warnings_; // 컴파일은 되지만 알릴 것 (sRGB 밖의 색 등)
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
            // image, video, audio로 이름 붙인 값. evaluating_은 지금 계산하는 것들이다 (자기 자신을 가리키는지 찾는다)
            std::map<std::string, const ast::ASTConstant*> constants_;
            std::set<const ast::ASTConstant*> evaluating_;
            // asset("이름")이 가리키는 파일들. 경로는 'x.tasset/파일'이고 main 폴더 기준이다. 둘 이상이면 이름이 겹친 것이다
            std::map<std::string, std::vector<std::string>> asset_names_;
            std::vector<const ast::ASTAsset*> assets_;
            const ast::ASTAsset* default_asset_ = nullptr;
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

            // 파일 읽기와 에러

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
                            if (include->relative) {
                                include_error("E2001", include, "Cannot find file: " + path.string());
                            } else {
                                include_error("E2153", include, "Cannot find package: " + path.string());
                            }
                        } else {
                            diagnostics_.push_back({path.string(), 0, 0, "Cannot open file", 0, Level::Error, "E2162"});
                        }
                        return;
                    }
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string decode_error;
                    auto decoded = decode_source(buffer.str(), decode_error);
                    if (!decoded) {
                        if (include != nullptr) {
                            include_error("E2002", include, path.string() + " " + decode_error);
                        } else {
                            diagnostics_.push_back({path.string(), 0, 0, path.string() + " " + decode_error, 0, Level::Error, "E2002"});
                        }
                        return;
                    }
                    text = std::move(*decoded);
                }
                const std::filesystem::path relative = key.lexically_relative(file_key(packages_dir_));
                sources_.push_back({path.string(), std::move(text), !relative.empty() && *relative.begin() != "..", path});
                const Source& source = sources_.back();

                lexor::Lexor lexer(source.text);
                parser::Parser file_parser(source.path, lexer);
                const ast::ASTFile* file_node = file_parser.parse();
                if (file_node == nullptr) {
                    for (const auto& parse_error : file_parser.errors) {
                        diagnostics_.push_back({source.path, parse_error.line, parse_error.column, parse_error.message, 0, Level::Error, parse_error.code, parse_error.help});
                    }
                    return;
                }
                check_fmt_directives(source.path, file_parser.fmt_directives);
                for (const auto* statement : file_node->body) {
                    if (statement->type == ast::INCLUDE) {
                        const auto* include = static_cast<const ast::ASTInclude*>(statement);
                        const std::string file = include->name + ".tlide";
                        const std::filesystem::path base = include->relative ? path.parent_path() : packages_dir_;
                        const std::filesystem::path target = (base / std::filesystem::path(std::u8string(file.begin(), file.end()))).lexically_normal();
                        includes_.push_back({source.path, target.string(), include->path_token.line, include->path_token.column, include->path_length});
                        load(target, include);
                    } else {
                        statements_.push_back(statement);
                    }
                }
            }

            // 편집기를 위한 출처

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
                    case ast::COLOR_SPACE:
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
                        // 리터럴만 넘긴 linear(...), pattern(...), image(...), file(...), slide(...), run(...), font(...) 같은 값. 편집기는 식 전체를 바꾼다
                        static const std::set<std::string> values = {"linear", "radial", "pattern", "image", "file", "asset", "slide", "run", "program", "macro", "action_file", "font"};
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
                // [a, b, c] 리터럴은 항목이 무엇이든 식 전체를 바꿀 수 있다
                const bool plain_list = expr->type == ast::LIST && static_cast<const ast::ASTList*>(expr)->kind == ast::ListKind::PLAIN;
                if (!plain_list && !is_literal(expr)) {
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

            template <typename Map>
            static std::vector<std::string> names_of(const Map& map) {
                std::vector<std::string> names;
                for (const auto& entry : map) {
                    names.push_back(entry.first);
                }
                return names;
            }

            static std::vector<std::string> names_of(const std::vector<VarInfo>& vars) {
                std::vector<std::string> names;
                for (const auto& var : vars) {
                    names.push_back(var.name);
                }
                return names;
            }

            static std::vector<std::string> join_names(std::vector<std::string> first, const std::vector<std::string>& second) {
                first.insert(first.end(), second.begin(), second.end());
                return first;
            }

            void error(const char* code, const Token& token, const std::string& message, const std::string& help = "") {
                Diagnostic diagnostic{path_of(token), token.line, token.column, message, 0, Level::Error, code, help};
                if (reported_.insert({diagnostic.path, diagnostic.line, diagnostic.column, diagnostic.message}).second) {
                    diagnostics_.push_back(std::move(diagnostic));
                }
            }

            // include한 파일을 읽지 못한 오류. #이 아니라 <...>나 "..." 경로에 칠한다
            void include_error(const char* code, const ast::ASTInclude* include, const std::string& message) {
                Diagnostic diagnostic{path_of(include->token), include->path_token.line, include->path_token.column, message, include->path_length, Level::Error, code};
                if (reported_.insert({diagnostic.path, diagnostic.line, diagnostic.column, diagnostic.message}).second) {
                    diagnostics_.push_back(std::move(diagnostic));
                }
            }

            // template을 여러 번 펼쳐도 같은 자리의 같은 경고는 한 번만 남긴다
            void warning(const char* code, const Token& token, const std::string& message, const std::string& help = "") {
                Diagnostic diagnostic{path_of(token), token.line, token.column, message, 0, Level::Warning, code, help};
                if (reported_.insert({diagnostic.path, diagnostic.line, diagnostic.column, "warning: " + diagnostic.message}).second) {
                    warnings_.push_back(std::move(diagnostic));
                }
            }

            // 파일을 읽은 순서, 그 안에서는 위치 순서로
            void sort_diagnostics(std::vector<Diagnostic>& list) const {
                std::map<std::string, std::size_t> order;
                for (std::size_t i = 0; i < sources_.size(); ++i) {
                    order.emplace(sources_[i].path, i);
                }
                const auto rank = [&](const Diagnostic& diagnostic) {
                    const auto it = order.find(diagnostic.path);
                    return std::tuple{it == order.end() ? 0 : it->second, diagnostic.line, diagnostic.column};
                };
                std::stable_sort(list.begin(), list.end(), [&](const Diagnostic& a, const Diagnostic& b) { return rank(a) < rank(b); });
            }

            // 파일을 읽은 순서, 그 안에서는 위치 순서로 정렬해서 돌려준다
            Result fail() {
                sort_diagnostics(diagnostics_);
                return {std::nullopt, std::move(diagnostics_)};
            }

            // #fmt의 옵션 이름과 값을 검사한다. 문서의 뜻에는 영향이 없으므로 경고뿐이다
            void check_fmt_directives(const std::string& path, const std::vector<parser::FmtDirective>& directives) {
                if (directives.empty()) {
                    return;
                }
                std::vector<format::Directive> list;
                for (const auto& each : directives) {
                    format::Directive directive;
                    directive.key = std::string(each.key.value);
                    directive.value = std::string(each.value.value);
                    directive.line = each.key.line;
                    directive.column = each.key.column;
                    directive.key_length = each.key.value.size();
                    directive.value_line = each.value.line;
                    directive.value_column = each.value.column;
                    directive.value_length = each.value.value.size();
                    list.push_back(std::move(directive));
                }
                std::vector<format::OptionProblem> problems;
                format::read_settings(list, &problems);
                for (const auto& problem : problems) {
                    warnings_.push_back(format::option_diagnostic(path, list[problem.directive], problem));
                }
            }

            // 선언 수집

            void add_builtin_enum(const std::string& name, std::vector<std::string> members) {
                builtin_names_.insert(name);
                enums_.emplace(name, EnumInfo{name, std::move(members)});
            }

            Type enum_type(const std::string& name) const {
                return Type{Kind::Enum, &enums_.at(name)};
            }

            bool declare(const std::string& name, const Token& token) {
                if (builtin_names_.contains(name)) {
                    error("E2003", token, "'" + name + "' is a built-in name");
                    return false;
                }
                if (!declared_names_.insert(name).second) {
                    error("E2004", token, "'" + name + "' is already defined");
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
                        case ast::CONSTANT: {
                            const auto* constant = static_cast<const ast::ASTConstant*>(statement);
                            if (declare(constant->name->name, constant->name->token)) {
                                constants_[constant->name->name] = constant;
                            }
                            break;
                        }
                        case ast::ASSET:
                            collect_asset(static_cast<const ast::ASTAsset*>(statement));
                            break;
                        case ast::IF:
                            error("E2005", statement->token, "'if' is not supported at the top level yet");
                            break;
                        default:
                            break;
                    }
                }
            }

            static std::string utf8(const std::filesystem::path& path) {
                const std::u8string text = path.generic_u8string();
                return std::string(text.begin(), text.end());
            }

            static std::filesystem::path from_utf8(const std::string& text) {
                return std::filesystem::path(std::u8string(text.begin(), text.end()));
            }

            // 묶음 안 파일 이름에서 확장자를 뺀 것. images/logo.png -> images/logo
            static std::string without_extension(const std::string& name) {
                const std::size_t slash = name.find_last_of('/');
                const std::size_t dot = name.find_last_of('.');
                return dot != std::string::npos && (slash == std::string::npos || dot > slash + 1) ? name.substr(0, dot) : name;
            }

            // asset 문이 불러오는 묶음의 경로. 그 문장을 적은 파일의 폴더 기준이다
            std::filesystem::path bundle_of(const ast::ASTAsset* node) const {
                const Source* source = source_of(node->span.data());
                const std::filesystem::path folder = source != nullptr ? source->file.parent_path() : main_folder_;
                return (folder / from_utf8(node->path)).lexically_normal();
            }

            // asset "경로" [by { ... }] [default]; 묶음의 파일 이름을 읽어 asset("이름")으로 찾을 수 있게 한다
            void collect_asset(const ast::ASTAsset* node) {
                assets_.push_back(node);
                const std::filesystem::path bundle = bundle_of(node);
                if (node->is_default) {
                    if (default_asset_ != nullptr) {
                        error("E2006", node->token, "Only one asset can be the default; '" + default_asset_->path + "' is already the default");
                    } else {
                        default_asset_ = node;
                    }
                }
                std::string message;
                const auto entries = tasset::list(bundle, message);
                if (!entries) {
                    error("E2007", node->path_token, "Cannot open the asset bundle '" + node->path + "' (" + message + ")");
                    return;
                }
                // 파일을 가리키는 경로는 main 폴더 기준으로 적는다. 다른 드라이브면 절대 경로다
                const std::filesystem::path relative = bundle.lexically_relative(main_folder_.lexically_normal());
                const std::string prefix = utf8(relative.empty() ? bundle : relative) + "/";
                const auto add_name = [&](const std::string& name, const std::string& entry) {
                    auto& paths = asset_names_[name];
                    if (std::find(paths.begin(), paths.end(), prefix + entry) == paths.end()) {
                        paths.push_back(prefix + entry);
                    }
                };
                const auto add_entry = [&](const std::string& head, const std::string& entry) {
                    add_name(head + entry, entry);
                    add_name(head + without_extension(entry), entry);
                };
                if (!node->has_by) {
                    for (const auto& entry : *entries) {
                        add_entry("", entry);
                    }
                }
                std::set<std::string> aliases;
                for (const auto& item : node->items) {
                    if (!aliases.insert(item.alias->name).second) {
                        error("E2008", item.alias->token, "Duplicate name '" + item.alias->name + "' in this asset");
                        continue;
                    }
                    if (item.all) {
                        for (const auto& entry : *entries) {
                            add_entry(item.alias->name + ".", entry);
                        }
                        continue;
                    }
                    std::vector<std::string> matches;
                    for (const auto& entry : *entries) {
                        if (entry == item.name || without_extension(entry) == item.name) {
                            matches.push_back(entry);
                        }
                    }
                    if (matches.empty()) {
                        error("E2009", item.token, "'" + node->path + "' has no file named '" + item.name + "'");
                    } else if (matches.size() > 1) {
                        error("E2010", item.token, "'" + item.name + "' matches more than one file in '" + node->path + "' (" + join(matches) + "); write the full file name");
                    } else {
                        add_name(item.alias->name, matches.front());
                    }
                }
            }

            // asset("이름")이 가리키는 파일의 경로. 없거나 둘 이상이면 at에 에러를 남긴다
            std::optional<std::string> resolve_asset(const std::string& name, const Token& at) {
                const auto it = asset_names_.find(name);
                if (it == asset_names_.end()) {
                    if (assets_.empty()) {
                        error("E2011", at, "No asset bundle is loaded; add 'asset \"file.tasset\";' first");
                    } else {
                        error("E2154", at, "No asset named '" + name + "'");
                    }
                    return std::nullopt;
                }
                if (it->second.size() > 1) {
                    error("E2012", at, "'" + name + "' is ambiguous (" + join(it->second) + "); write the full file name or name it with 'asset ... by { ... }'");
                    return std::nullopt;
                }
                return it->second.front();
            }

            static Type constant_type(const ast::ASTConstant* node) {
                const std::string& type = node->type_name->name;
                return Type{type == "video" ? Kind::Video : type == "audio" ? Kind::Audio : Kind::Image};
            }

            void collect_enum(const ast::ASTEnum* node) {
                if (!declare(node->name, node->token)) {
                    return;
                }
                EnumInfo info{node->name, {}};
                for (const auto* member : node->members) {
                    if (info.has(member->name)) {
                        error("E2013", member->token, "Duplicate member '" + member->name + "' in enum '" + node->name + "'");
                    } else {
                        info.members.push_back(member->name);
                    }
                }
                enums_.emplace(node->name, std::move(info));
            }

            // 이름과 타입 검사

            Type resolve_type(const ast::ASTName* name) {
                std::string type;
                for (const char c : name->name) {
                    if (!std::isspace(static_cast<unsigned char>(c))) {
                        type += c;
                    }
                }
                return resolve_type_name(type, name->token);
            }

            // 타입 이름 하나. list<T>는 안쪽 T를 다시 풀어 list_of로 묶는다
            Type resolve_type_name(const std::string& type, const Token& at) {
                if (type.starts_with("list<") && type.ends_with(">")) {
                    const Type element = resolve_type_name(type.substr(5, type.size() - 6), at);
                    return element.kind == Kind::Error ? element : list_of(element);
                }
                if (type == "int") { return Type{Kind::Int}; }
                if (type == "float") { return Type{Kind::Float}; }
                if (type == "string") { return Type{Kind::String}; }
                if (type == "text") { return Type{Kind::Text}; }
                if (type == "color") { return Type{Kind::Color}; }
                if (type == "bool") { return Type{Kind::Bool}; }
                if (type == "ref") { return Type{Kind::Ref}; }
                if (type == "image") { return Type{Kind::Image}; }
                if (type == "video") { return Type{Kind::Video}; }
                if (type == "audio") { return Type{Kind::Audio}; }
                if (type == "font") { return Type{Kind::Font}; }
                if (type == "fill") { return Type{Kind::Fill}; }
                if (const auto it = enums_.find(type); it != enums_.end()) {
                    return Type{Kind::Enum, &it->second};
                }
                error("E2014", at, "Unknown type: " + type,
                      did_you_mean(type, join_names({"int", "float", "string", "text", "color", "bool", "ref", "image", "video", "audio", "font", "fill"}, names_of(enums_))));
                return Type{Kind::Error};
            }

            std::vector<VarInfo> parameters_of(const std::vector<ast::ASTParameter*>& declared) {
                std::vector<VarInfo> parameters;
                for (const auto* parameter : declared) {
                    if (find_var(parameters, parameter->name->name) != nullptr) {
                        error("E2015", parameter->name->token, "Duplicate parameter: " + parameter->name->name);
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
                        error("E2016", var->name->token, "Duplicate variable: " + var->name->name);
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
                        signatures_[statement] = vars_of(static_cast<const ast::ASTMaster*>(statement)->vars);
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
                            check_defaults(statement);
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
                        case ast::CONSTANT: {
                            const auto* constant = static_cast<const ast::ASTConstant*>(statement);
                            check_as(constant->expression, Scope{}, constant_type(constant));
                            break;
                        }
                        default:
                            break;
                    }
                }
                for (const auto* target : targets_) {
                    check_target(target);
                }
            }

            void add_common_properties(const ast::ASTNode* object) {
                auto& vars = signatures_[object];
                const bool has_text = find_var(vars, "text") != nullptr;
                for (const auto& property : common_properties_) {
                    if (find_var(vars, property.name) != nullptr) {
                        error("E2017", object->token, "'" + property.name + "' is a built-in property of every object");
                        continue;
                    }
                    vars.push_back(property);
                }
                // 표는 칸 안쪽 여백을 padding 하나로, 또는 방향마다 따로 정할 수 있다
                if (static_cast<const ast::ASTObject*>(object)->name == "table") {
                    for (const char* side : {"left", "top", "right", "bottom"}) {
                        const std::string name = std::string("padding_") + side;
                        if (find_var(vars, name) == nullptr) {
                            vars.push_back({name, Type{Kind::Float}, nullptr, true});
                        }
                    }
                }
                if (!has_text) {
                    return;
                }
                for (const auto& property : text_properties_) {
                    if (find_var(vars, property.name) != nullptr) {
                        error("E2018", object->token, "'" + property.name + "' is a built-in property of every object with text");
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
                    {
                        std::vector<std::string> candidates;
                        for (const auto& known : properties_) {
                            candidates.push_back(known.name);
                        }
                        error("E2019", assign->identifier->token, "Unknown style property: " + name, did_you_mean(name, candidates));
                    }
                    check(assign->expression, scope, Type{});
                    return;
                }
                if (!assigned.insert(name).second) {
                    error("E2020", assign->identifier->token, "Duplicate style property: " + name);
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
                                error("E2021", statement->token, "Variables must be declared at the top level of a template");
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
                            error("E2022", static_cast<const ast::ASTAssign*>(statement)->name->token, "Properties such as 'background' can only be set directly inside a case");
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
                    error("E2004", node->name->token, "'" + name + "' is already defined");
                }
                if (node->range_start != nullptr) {
                    if (type.kind != Kind::Int && type.kind != Kind::Error) {
                        error("E2023", node->type_name->token, "A range can only be used with int, but got " + type_name(type));
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
                    error("E2024", location(node->condition), "Condition must be a bool such as 'a == b', but got " + type_name(condition));
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
                        error("E2025", animation->token, "'animate' can only be used in a put or group written in a slide");
                        continue;
                    }
                    check_animate(animation, scope);
                    if (animation->category->name == "media" && object != "video" && object != "audio") {
                        error("E2026", animation->category->token, "Media animations can only be used on a video or an audio");
                    }
                }
            }

            void check_put(const ast::ASTPut* put, const Scope& scope) {
                check_inner_animations(put->animations, scope, put->name);
                const std::vector<VarInfo>* vars = put_signature(put->name);
                if (vars == nullptr) {
                    error("E2027", put->token, "Unknown template or object: " + put->name, did_you_mean(put->name, join_names(names_of(templates_), names_of(objects_))));
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
                    if (var == nullptr && name == "path" && objects_.contains(put->name) && find_var(*vars, "data") != nullptr) {
                        error("E2028", assign->name->token, "'" + put->name + "' has no property 'path'; give the file with data, such as data = file(\"picture.png\")");
                        check(assign->expression, scope, Type{});
                        assigned.insert("data"); // data가 없다는 에러를 또 내지 않는다
                        continue;
                    }
                    if (var == nullptr) {
                        error("E2029", assign->name->token, "'" + put->name + "' has no property '" + name + "'", did_you_mean(name, names_of(*vars)));
                        check(assign->expression, scope, Type{});
                        continue;
                    }
                    if (!assigned.insert(name).second) {
                        error("E2030", assign->name->token, "Duplicate property: " + name);
                    }
                    check_as(assign->expression, scope, var->type);
                }
                for (const auto& var : *vars) {
                    if (var.default_value == nullptr && !var.optional && !assigned.contains(var.name)) {
                        error("E2031", put->token, "Missing property '" + var.name + "' for '" + put->name + "'");
                    }
                }
                if (objects_.contains(put->name)) {
                    check_object_properties(put);
                }
                check_cells(put, scope);
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
                            error("E2032", assigned.at(detail)->name->token, "'" + detail + "' needs '" + base + "'");
                        }
                    }
                };
                needs({"shadow_blur", "shadow_distance", "shadow_angle"}, "shadow");
                needs({"inner_shadow_blur", "inner_shadow_distance", "inner_shadow_angle"}, "inner_shadow");
                needs({"glow_size"}, "glow");
                needs({"reflection_size", "reflection_distance", "reflection_blur"}, "reflection");
                needs({"bevel_width", "bevel_height"}, "bevel");
                needs({"depth_color"}, "depth");
                const auto forbid = [&](const char* code, const std::string& property, const std::string& message) {
                    if (assigned.contains(property)) {
                        error(code, assigned.at(property)->name->token, message);
                    }
                };
                // PowerPoint은 누를 때 하는 일을 하나만 가진다
                if (assigned.contains("action")) {
                    forbid("E2033", "link", "Use either 'link' or 'action'; an object has one click action (write the link as action = ...)");
                }
                if (put->name == "line" || put->name == "connector") {
                    forbid("E2157", "fill", "A line has no fill");
                    forbid("E2158", "flip", "A line cannot be flipped; swap its end points instead");
                }
                if (put->name == "connector") {
                    forbid("E2159", "rotation", "A connector cannot be rotated; it follows the objects it connects");
                }
                if (put->name == "backdrop") {
                    forbid("E2160", "fill", "A backdrop has no fill; it is filled with the blurred background");
                }
                if (put->name == "table") {
                    for (const char* property : {"rotation", "flip", "radius", "opacity", "shadow", "inner_shadow", "glow", "soft_edge", "reflection", "bevel", "depth",
                                                 "rotation_x", "rotation_y", "perspective", "line_cap", "line_join", "line_compound"}) {
                        forbid("E2161", property, std::string("A table has no '") + property + "'");
                    }
                }
            }

            // put table 안 cell(행, 열) 블록의 속성. 정한 것만 그 칸에 적용되고, 안 정한 것은 표의 값을 따른다
            std::vector<std::pair<std::string, Type>> cell_properties() const {
                std::vector<std::pair<std::string, Type>> result = {
                    {"col_span", Type{Kind::Int}}, {"row_span", Type{Kind::Int}}, {"fill", Type{Kind::Fill}},
                    {"anchor", enums_.contains("text_anchor") ? enum_type("text_anchor") : Type{Kind::Any}}, {"align", enum_type("text_align")},
                    {"padding", Type{Kind::Float}}, {"padding_left", Type{Kind::Float}}, {"padding_top", Type{Kind::Float}},
                    {"padding_right", Type{Kind::Float}}, {"padding_bottom", Type{Kind::Float}},
                    {"line_color", Type{Kind::Color}}, {"line_width", Type{Kind::Float}}, {"line_dash", enum_type("dash_style")},
                };
                for (const char* side : {"left", "top", "right", "bottom"}) {
                    result.emplace_back(std::string("line_color_") + side, Type{Kind::Color});
                    result.emplace_back(std::string("line_width_") + side, Type{Kind::Float});
                }
                return result;
            }

            // put 블록의 cell 문장. 표(table)에만 쓴다
            void check_cells(const ast::ASTPut* put, const Scope& scope) {
                for (const auto* cell : put->cells) {
                    if (put->name != "table") {
                        error("E2034", cell->token, "'cell' can only be used in a put table");
                    }
                    check_as(cell->row, scope, Type{Kind::Int});
                    check_as(cell->column, scope, Type{Kind::Int});
                    std::set<std::string> assigned;
                    for (const auto* statement : cell->body) {
                        check_assignment(static_cast<const ast::ASTAssign*>(statement), scope, assigned, cell_properties(), "cell");
                    }
                }
            }

            // master의 매개변수는 case 안에서 변수처럼 쓴다. master 바로 안의 대입은 theme = 이름; 뿐이다
            void check_master(const ast::ASTMaster* master) {
                const Scope scope{&signatures_.at(master), true};
                std::set<std::string> properties;
                for (const auto* statement : master->properties) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    if (assign->name->name != "theme") {
                        error("E2035", assign->name->token, "Unknown master property: " + assign->name->name, did_you_mean(assign->name->name, {"theme"}));
                        continue;
                    }
                    if (!properties.insert("theme").second) {
                        error("E2036", assign->name->token, "Duplicate property: theme");
                    }
                    if (assign->expression->type != ast::NAME || !themes_.contains(static_cast<const ast::ASTName*>(assign->expression)->name)) {
                        error("E2037", location(assign->expression), "Expected the name of a theme");
                    }
                }
                std::set<std::string> names;
                for (const auto* statement : master->body) {
                    const auto* layout = static_cast<const ast::ASTMasterCase*>(statement);
                    if (!names.insert(layout->name).second) {
                        error("E2038", layout->token, "Duplicate layout '" + layout->name + "' in master '" + master->name + "'");
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
                    {
                        std::vector<std::string> candidates;
                        for (const auto& known : properties) {
                            candidates.push_back(known.first);
                        }
                        error("E2039", assign->name->token, "Unknown " + owner + " property: " + name, did_you_mean(name, candidates));
                    }
                    check(assign->expression, scope, Type{});
                    return;
                }
                if (!assigned.insert(name).second) {
                    error("E2030", assign->name->token, "Duplicate property: " + name);
                }
                check_as(assign->expression, scope, it->second);
            }

            static const PropertyTypes& slide_properties() {
                static const PropertyTypes properties = {
                    {"background", Type{Kind::Background}}, {"title", Type{Kind::Text}}, {"subtitle", Type{Kind::Text}}, {"body", Type{Kind::Text}},
                    {"hidden", Type{Kind::Bool}}, {"advance_after", Type{Kind::Float}}, {"transition_sound", Type{Kind::Audio}},
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
                    result.emplace_back("heading_font", Type{Kind::Font});
                    result.emplace_back("body_font", Type{Kind::Font});
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
                            error("E2040", statement->token, "Duplicate transition");
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
                        error("E2041", assign->name->token, "Duplicate property: layout");
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
                    error("E2042", node->category->token, "An animation must be enter, emphasis, exit, move or media, but got '" + category + "'");
                } else if (category == "media") {
                    if (effect != "play" && effect != "pause" && effect != "stop") {
                        error("E2043", node->effect->token, "A media animation must be play, pause or stop, but got '" + effect + "'");
                    } else if (node->option != nullptr) {
                        error("E2044", node->option->token, "Media animations have no options");
                    }
                    if (node->duration != nullptr) {
                        error("E2045", location(node->duration), "Media animations cannot have a duration");
                    }
                } else if (node->path != nullptr) {
                    if (category != "move") {
                        error("E2046", node->effect->token, "'path' can only be used with move");
                    }
                    check_as(node->path, scope, Type{Kind::String});
                } else if (const auto it = animation_options().find(category + "." + effect); it == animation_options().end()) {
                    {
                    std::vector<std::string> candidates;
                    for (const auto& [key, info] : animation_options()) {
                        if (key.starts_with(category + ".")) {
                            candidates.push_back(key.substr(category.size() + 1));
                        }
                    }
                    error("E2047", node->effect->token, "Unknown " + category + " animation: " + effect, did_you_mean(effect, candidates));
                }
                } else {
                    const auto& options = it->second.options;
                    if (node->option != nullptr && options.front().empty()) {
                        error("E2048", node->option->token, "Animation '" + effect + "' has no options");
                    } else if (node->option != nullptr && std::find(options.begin(), options.end(), node->option->name) == options.end()) {
                        error("E2049", node->option->token, "Animation '" + effect + "' has no option '" + node->option->name + "' (" + join(options) + ")", did_you_mean(node->option->name, options));
                    }
                    if (it->second.fixed && node->duration != nullptr) {
                        error("E2050", location(node->duration), "Animation '" + effect + "' lasts until the slide ends, so it cannot have a duration");
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
                    error("E2051", node->token, "Missing property 'text' for review");
                }
            }

            void check_transition(const ast::ASTTransition* node) {
                const std::string& kind = node->kind->name;
                const auto it = transition_options().find(kind);
                if (it == transition_options().end()) {
                    error("E2052", node->kind->token, "Unknown transition: " + kind, did_you_mean(kind, names_of(transition_options())));
                } else if (node->option != nullptr) {
                    const auto& options = it->second;
                    if (options.front().empty()) {
                        error("E2053", node->option->token, "Transition '" + kind + "' has no options");
                    } else if (std::find(options.begin(), options.end(), node->option->name) == options.end()) {
                        error("E2054", node->option->token, "Transition '" + kind + "' has no option '" + node->option->name + "' (" + join(options) + ")", did_you_mean(node->option->name, options));
                    }
                }
                if (node->duration != nullptr) {
                    check_as(node->duration, Scope{}, Type{Kind::Float});
                }
            }

            // 이름으로 넘기는 인자(이름 = 값)는 layout의 master 호출에서만 쓴다
            void reject_named(const ast::ASTCall* call) {
                if (!call->named.empty()) {
                    error("E2055", call->named.front().first->token, "Named arguments can only be used when choosing a layout, such as layout = brand(ink = hex(F00)).title");
                }
            }

            // master의 매개변수 값. given이 nullptr인 매개변수(넘기지 않은 것)는 기본값을 쓴다
            std::vector<ir::Value> master_arguments(const ast::ASTMaster* master, const std::vector<ast::ASTNode*>& given, const Env& env) {
                const auto& parameters = signatures_.at(master);
                std::vector<ir::Value> values;
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (i < given.size() && given[i] != nullptr) {
                        values.push_back(evaluate(given[i], env, parameters[i].type));
                    } else {
                        Env defaults;
                        defaults.page = env.page;
                        values.push_back(evaluate(parameters[i].default_value, defaults, parameters[i].type));
                    }
                }
                return values;
            }

            // <master>.<layout>, 또는 <master>(<값>, 이름 = <값>, ...).<layout>. 기본값이 있는 매개변수는 생략할 수 있다
            std::optional<LayoutInfo> resolve_layout(const ast::ASTNode* expr) {
                const auto* member = expr->type == ast::MEMBER ? static_cast<const ast::ASTMember*>(expr) : nullptr;
                const ast::ASTNode* object = member != nullptr ? member->object : nullptr;
                if (object == nullptr || (object->type != ast::NAME && object->type != ast::CALL)) {
                    error("E2056", location(expr), "Layout must be written as <master>.<layout> or <master>(<arguments>).<layout>");
                    return std::nullopt;
                }
                const bool called = object->type == ast::CALL;
                const std::string& master_name = called ? static_cast<const ast::ASTCall*>(object)->name->name : static_cast<const ast::ASTName*>(object)->name;
                const std::vector<ast::ASTNode*> arguments = called ? static_cast<const ast::ASTCall*>(object)->arguments : std::vector<ast::ASTNode*>{};
                const auto master = masters_.find(master_name);
                if (master == masters_.end()) {
                    error("E2057", object->token, "Unknown master: " + master_name, did_you_mean(master_name, names_of(masters_)));
                    return std::nullopt;
                }
                const auto& parameters = signatures_.at(master->second);
                // 위치로 넘긴 인자를 앞에서부터, 이름으로 넘긴 인자를 그 이름의 매개변수에 맞추고, 남은 매개변수는 기본값을 쓴다
                std::vector<ast::ASTNode*> bound(parameters.size(), nullptr);
                bool valid = true;
                if (arguments.size() > parameters.size()) {
                    error("E2058", object->token, "Master '" + master_name + "' takes at most " + std::to_string(parameters.size()) + " argument(s), but got " + std::to_string(arguments.size()));
                    valid = false;
                }
                for (std::size_t i = 0; i < arguments.size() && i < parameters.size(); ++i) {
                    bound[i] = arguments[i];
                }
                if (called) {
                    for (const auto& [name, value] : static_cast<const ast::ASTCall*>(object)->named) {
                        const auto found = std::find_if(parameters.begin(), parameters.end(), [&](const VarInfo& var) { return var.name == name->name; });
                        if (found == parameters.end()) {
                            error("E2059", name->token, "Master '" + master_name + "' has no parameter '" + name->name + "'", did_you_mean(name->name, names_of(parameters)));
                            valid = false;
                        } else if (bound[static_cast<std::size_t>(found - parameters.begin())] != nullptr) {
                            error("E2060", name->token, "Argument '" + name->name + "' is given more than once");
                            valid = false;
                        } else {
                            bound[static_cast<std::size_t>(found - parameters.begin())] = value;
                        }
                    }
                }
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (bound[i] == nullptr && parameters[i].default_value == nullptr) {
                        error("E2061", object->token, "Missing argument '" + parameters[i].name + "' for master '" + master_name + "'");
                        valid = false;
                    }
                }
                if (!valid) {
                    return std::nullopt;
                }
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (bound[i] != nullptr) {
                        check_as(bound[i], Scope{}, parameters[i].type);
                    }
                }
                for (const auto* statement : master->second->body) {
                    const auto* layout = static_cast<const ast::ASTMasterCase*>(statement);
                    if (layout->name == member->member->name) {
                        return LayoutInfo{master->second, layout, bound};
                    }
                }
                {
                    std::vector<std::string> layouts;
                    for (const auto* statement : master->second->body) {
                        layouts.push_back(static_cast<const ast::ASTMasterCase*>(statement)->name);
                    }
                    error("E2062", member->member->token, "Master '" + master_name + "' has no layout '" + member->member->name + "'", did_you_mean(member->member->name, layouts));
                }
                return std::nullopt;
            }

            void check_target(const ast::ASTTarget* target) {
                std::set<std::string> assigned;
                for (const auto* statement : target->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                    const std::string& name = assign->name->name;
                    if (name == "master") {
                        error("E2063", assign->name->token, "A target has no master; the master comes from each slide's layout");
                        continue;
                    }
                    if (name != "path" && name != "type" && name != "width" && name != "height" && name != "title" && name != "author" && name != "loop" && name != "angles" && name != "script"
                        && name != "scale" && name != "transparent") {
                        error("E2064", assign->name->token, "Unknown target property: " + name,
                              did_you_mean(name, {"path", "type", "width", "height", "title", "author", "loop", "angles", "script", "scale", "transparent"}));
                        continue;
                    }
                    if (!assigned.insert(name).second) {
                        error("E2030", assign->name->token, "Duplicate property: " + name);
                        continue;
                    }
                    if (name == "path" || name == "title" || name == "author" || name == "script") {
                        check_as(assign->expression, Scope{}, Type{Kind::String});
                    } else if (name == "loop" || name == "transparent") {
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
                        error("E2065", target->token, "Missing property '" + required + "' for target '" + target->name + "'");
                    }
                }
                if (assigned.contains("width") != assigned.contains("height")) {
                    error("E2066", target->token, "Target '" + target->name + "' must set both width and height");
                }
            }

            void check_as(const ast::ASTNode* expr, const Scope& scope, const Type& expected) {
                const Type actual = check(expr, scope, expected);
                if (expected.kind == Kind::Font && actual.kind == Kind::String) {
                    error("E2067", location(expr), "Write a font as font(\"...\"), such as font(\"Arial\")");
                } else if (!assignable(actual, expected)) {
                    error("E2068", location(expr), "Expected " + type_name(expected) + ", but got " + type_name(actual));
                }
            }

            // expr의 타입. expected는 enum 값의 이름을 찾거나 text로 해석할 때 쓴다
            Type check(const ast::ASTNode* expr, const Scope& scope, const Type& expected) {
                if (expected.kind == Kind::List && expr->type == ast::LIST && static_cast<const ast::ASTList*>(expr)->kind == ast::ListKind::PLAIN) {
                    for (const auto* item : static_cast<const ast::ASTList*>(expr)->items) {
                        check_as(item, scope, *expected.element);
                    }
                    return expected;
                }
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
                            error("E2069", expr->token, "Unknown unit '" + dimension->unit + "' (px, pt, in, cm, mm, %, deg, s, ms)", did_you_mean(dimension->unit, {"px", "pt", "in", "cm", "mm", "deg", "ms"}));
                        }
                        // %는 소수여도 int 자리에 쓸 수 있다. backend가 슬라이드 크기로 풀 때 int 값은 px 정수로 자른다
                        return Type{dimension->value->type == ast::FLOAT && dimension->unit != "%" ? Kind::Float : Kind::Int};
                    }
                    case ast::STRING:
                        return Type{Kind::String};
                    case ast::FSTRING:
                        for (const auto* part : static_cast<const ast::ASTFString*>(expr)->parts) {
                            if (!field_of(part).empty()) {
                                error("E2070", location(part), "This value is updated by PowerPoint, so it cannot be used in an f-string; put it directly in text");
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
                        error("E2071", expr->token, "'@" + static_cast<const ast::ASTContext*>(expr)->name->name + "' must be followed by a member such as '@slide.page'");
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
                    case ast::COLOR_SPACE: {
                        const auto* color = static_cast<const ast::ASTColorSpace*>(expr);
                        if (color->arguments.size() != 3 && color->arguments.size() != 4) {
                            error("E2072", expr->token, color->space + " needs three values and an optional alpha, such as " + color::example(color->space));
                        }
                        for (const auto* component : color->arguments) {
                            check_as(component, scope, Type{Kind::Float});
                        }
                        return Type{Kind::Color};
                    }
                    case ast::COLOR_HEX: {
                        const auto& digits = static_cast<const ast::ASTString*>(static_cast<const ast::ASTColorHex*>(expr)->hex)->value;
                        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
                            error("E2073", expr->token, "Hex color must have 3, 4, 6 or 8 digits");
                        }
                        return Type{Kind::Color};
                    }
                    case ast::CALL: {
                        const auto* call = static_cast<const ast::ASTCall*>(expr);
                        reject_named(call);
                        if (call->name->name == "linear") {
                            return check_gradient(call, scope, 1);
                        }
                        if (call->name->name == "image") {
                            return check_image(call, scope);
                        }
                        if (call->name->name == "file" || call->name->name == "asset") {
                            return check_source(call, scope, expected);
                        }
                        if (call->name->name == "font") {
                            if (call->arguments.size() != 1) {
                                error("E2074", call->token, "font needs one font name, such as font(\"Arial\")");
                                for (const auto* argument : call->arguments) {
                                    check(argument, scope, Type{});
                                }
                                return Type{Kind::Error};
                            }
                            check_as(call->arguments[0], scope, Type{Kind::String});
                            return Type{Kind::Font};
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
                                error("E2075", call->token, "slide needs one slide number, such as slide(3)");
                            }
                            for (const auto* argument : call->arguments) {
                                check_as(argument, scope, Type{Kind::Int});
                            }
                            return Type{call->arguments.size() != 1 ? Kind::Error : Kind::SlideRef};
                        }
                        if (styles_.contains(call->name->name)) {
                            error("E2076", expr->token, "Style '" + call->name->name + "' can only be used inside text");
                        } else {
                            error("E2077", expr->token, "Unknown function: " + call->name->name,
                                  did_you_mean(call->name->name, join_names({"linear", "radial", "pattern", "image", "file", "asset", "font", "slide", "run", "program", "macro", "action_file"}, names_of(styles_))));
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
                        error("E2078", expr->token, "An inline style can only be used inside text");
                        return Type{Kind::Error};
                    default:
                        error("E2079", expr->token, "Unexpected expression");
                        return Type{Kind::Error};
                }
            }

            // linear(각도, 색, 색, ...)과 radial(색, 색, ...). 색 뒤에 위치를 붙일 수 있다: hex(F00) 30%
            // leading은 색 앞의 인자 수 (linear는 각도 하나)
            Type check_gradient(const ast::ASTCall* call, const Scope& scope, std::size_t leading) {
                const auto& arguments = call->arguments;
                const std::string& name = call->name->name;
                if (arguments.size() < leading + 2) {
                    error("E2080", call->token, name + " needs " + (leading > 0 ? "an angle and " : "") + "at least 2 colors, such as " +
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
                    error("E2081", call->token, "pattern needs a kind and 2 colors, such as pattern(diagonal_cross, hex(000), hex(FFF))");
                }
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    check_as(arguments[i], scope, i == 0 ? enum_type("pattern_kind") : Type{Kind::Color});
                }
                return Type{arguments.size() != 3 ? Kind::Error : Kind::Pattern};
            }

            // image("경로")
            Type check_image(const ast::ASTCall* call, const Scope& scope) {
                if (call->arguments.size() != 1) {
                    error("E2082", call->token, "image needs one path, such as image(\"bg.png\")");
                }
                for (const auto* argument : call->arguments) {
                    check_as(argument, scope, Type{Kind::String});
                }
                return Type{call->arguments.size() != 1 ? Kind::Error : Kind::Image};
            }

            // file("경로")는 디스크의 파일(경로가 'x.tasset/이름'이면 그 묶음 안의 파일), asset("이름")은 asset 문으로 불러온 묶음의 파일
            Type check_source(const ast::ASTCall* call, const Scope& scope, const Type& expected) {
                const std::string& name = call->name->name;
                if (name == "file" && (expected.kind == Kind::Action || expected.kind == Kind::Link)) {
                    error("E2083", call->token, "To open a file when clicked, write action_file(\"...\"); file(...) gives a picture, video or audio file");
                    return Type{Kind::Error};
                }
                if (call->arguments.size() != 1) {
                    if (name == "file") {
                        error("E2084", call->token, "file needs one path, such as file(\"logo.png\") or file(\"slides.tasset/logo.png\")");
                    } else {
                        error("E2155", call->token, "asset needs one file name, such as asset(\"logo\")");
                    }
                    for (const auto* argument : call->arguments) {
                        check(argument, scope, Type{});
                    }
                    return Type{Kind::Error};
                }
                check_as(call->arguments[0], scope, Type{Kind::String});
                if (name == "asset" && call->arguments[0]->type == ast::STRING) {
                    resolve_asset(static_cast<const ast::ASTString*>(call->arguments[0])->value, call->arguments[0]->token);
                }
                return Type{Kind::File};
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
                    error("E2085", node->token, "'" + node->name + "' is not a value of '" + expected.enumeration->name + "'", did_you_mean(node->name, expected.enumeration->members));
                    return Type{Kind::Error};
                }
                if (const auto constant = constants_.find(node->name); constant != constants_.end()) {
                    return constant_type(constant->second);
                }
                if (styles_.contains(node->name)) {
                    error("E2076", node->token, "Style '" + node->name + "' can only be used inside text");
                    return Type{Kind::Error};
                }
                error("E2086", node->token, "Unknown name: " + node->name,
                      did_you_mean(node->name, join_names(scope.vars != nullptr ? names_of(*scope.vars) : std::vector<std::string>{}, names_of(constants_))));
                return Type{Kind::Error};
            }

            static bool is_action_call(const std::string& name) {
                return name == "run" || name == "program" || name == "macro" || name == "action_file";
            }

            // run("함수", 인자...), program("경로"), macro("이름"), action_file("경로")
            Type check_action_call(const ast::ASTCall* call, const Scope& scope, const Type& expected) {
                const std::string& name = call->name->name;
                bool valid = true;
                if (expected.kind != Kind::Action) {
                    error("E2087", call->token, "'" + name + "(...)' can only be used as an action, such as action = " + name + "(...)");
                    valid = false;
                }
                if (call->arguments.empty() || (name != "run" && call->arguments.size() != 1)) {
                    static const std::map<std::string, std::string> examples = {
                        {"run", "run(\"onClick\", @slide.number)"}, {"program", "program(\"tools/app.exe\")"},
                        {"macro", "macro(\"Module1.Hello\")"}, {"action_file", "action_file(\"docs/report.pdf\")"},
                    };
                    error("E2088", call->token, name + " needs " + (name == "run" ? "a function name and optional arguments" : "one string") + ", such as " + examples.at(name));
                    valid = false;
                }
                for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                    if (i == 0) {
                        check_as(call->arguments[i], scope, Type{Kind::String});
                        continue;
                    }
                    const Type type = check(call->arguments[i], scope, Type{});
                    if (type.kind != Kind::Error && type.kind != Kind::Bool && !is_numeric(type) && type.kind != Kind::String && type.kind != Kind::Text) {
                        error("E2089", location(call->arguments[i]), "An argument of run must be a number, string, text or bool, but got " + type_name(type));
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
                        return Type{Kind::Font};
                    }
                    {
                        std::vector<std::string> candidates = {"heading_font", "body_font"};
                        for (const auto& [name, scheme] : theme_colors()) {
                            candidates.push_back(name);
                        }
                        error("E2090", location(node), "Unknown theme value: theme." + member, did_you_mean(member, candidates));
                    }
                    return Type{Kind::Error};
                }
                if (node->object->type != ast::CONTEXT) {
                    error("E2091", location(node), "Member access is only supported for '@slide.page', '@slide.number', '@slide.count', '@self.name' and 'theme.<name>'");
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
                    error("E2092", location(node), "Unknown value: @" + context + "." + member);
                    return Type{Kind::Error};
                }
                // 슬라이드 번호 필드는 PowerPoint이 채우므로 master에서도 쓸 수 있다
                if (member == "number") {
                    return Type{Kind::Text};
                }
                if (scope.in_master) {
                    error("E2093", location(node), "'@slide' cannot be used in a master");
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
                        error("E2094", location(node), "Expected a number, but got " + type_name(type));
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
                    error("E2095", location(node), "Cannot compare " + type_name(left) + " with " + type_name(right));
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
                                    error("E2096", part->token, "'" + name + "' is " + type_name(var->type) + " and cannot be used in text");
                                }
                            } else if (const auto style = styles_.find(name); style != styles_.end()) {
                                const auto& parameters = signatures_.at(style->second);
                                if (!parameters.empty()) {
                                    error("E2097", part->token, "Style '" + name + "' needs " + std::to_string(parameters.size()) + " argument(s)");
                                }
                            } else {
                                error("E2098", part->token, "Unknown style or variable: " + name,
                                      did_you_mean(name, join_names(scope.vars != nullptr ? names_of(*scope.vars) : std::vector<std::string>{}, names_of(styles_))));
                            }
                            break;
                        }
                        case ast::CALL: {
                            const auto* call = static_cast<const ast::ASTCall*>(part);
                            reject_named(call);
                            if (call->name->name == "link" && !styles_.contains("link")) {
                                if (call->arguments.size() != 1) {
                                    error("E2099", call->token, "link needs one target, such as link(\"https://example.com\") or link(slide(3))");
                                }
                                for (const auto* argument : call->arguments) {
                                    check_as(argument, scope, Type{Kind::Link});
                                }
                            } else if (is_text_action(call->name->name)) {
                                if (call->arguments.size() != 1) {
                                    error("E2100", call->token, call->name->name + " needs one action, such as " + call->name->name + "(run(\"onClick\")) or " + call->name->name + "(slide(3))");
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
                            if (static_cast<const ast::ASTList*>(part)->kind == ast::ListKind::PLAIN) {
                                error("E2101", part->token, "A list must follow 'bullets', 'numbers' or 'dashes'");
                                break;
                            }
                            if (in_item) {
                                error("E2102", part->token, "A nested list must be a separate item of the outer list");
                            }
                            check_list(static_cast<const ast::ASTList*>(part), scope);
                            break;
                        default: {
                            const Type type = check(part, scope, Type{});
                            if (!assignable(type, Type{Kind::Text})) {
                                error("E2103", location(part), type_name(type) + " cannot be used in text");
                            }
                            break;
                        }
                    }
                }
                return Type{Kind::Text};
            }

            void check_list(const ast::ASTList* list, const Scope& scope) {
                for (const auto* item : list->items) {
                    if (item->type == ast::LIST && static_cast<const ast::ASTList*>(item)->kind == ast::ListKind::PLAIN) {
                        error("E2101", item->token, "A list must follow 'bullets', 'numbers' or 'dashes'");
                    } else if (item->type == ast::LIST) {
                        check_list(static_cast<const ast::ASTList*>(item), scope);
                    } else {
                        check_text(item, scope, true);
                    }
                }
            }

            void check_style_call(const ast::ASTCall* call, const Scope& scope) {
                const auto style = styles_.find(call->name->name);
                if (style == styles_.end()) {
                    error("E2104", call->token, "Unknown style: " + call->name->name, did_you_mean(call->name->name, names_of(styles_)));
                    for (const auto* argument : call->arguments) {
                        check(argument, scope, Type{});
                    }
                    return;
                }
                const auto& parameters = signatures_.at(style->second);
                if (call->arguments.size() != parameters.size()) {
                    error("E2105", call->token, "Style '" + call->name->name + "' needs " + std::to_string(parameters.size()) + " argument(s), but got " + std::to_string(call->arguments.size()));
                }
                for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                    if (i < parameters.size()) {
                        check_as(call->arguments[i], scope, parameters[i].type);
                    } else {
                        check(call->arguments[i], scope, Type{});
                    }
                }
            }

            // 전개와 계산. 검사를 통과한 뒤에만 실행한다

            ir::Document build_document() {
                ir::Document document;
                for (const auto& [name, constant] : constants_) {
                    evaluate_constant(constant);
                }
                // 매개변수가 없거나 모두 기본값이 있는 master는 미리 만들고, 그 밖의 master는 slide가 넘긴 값마다 만든다
                for (const auto* master : master_order_) {
                    const auto& parameters = signatures_.at(master);
                    if (std::all_of(parameters.begin(), parameters.end(), [](const VarInfo& var) { return var.default_value != nullptr; })) {
                        Env defaults;
                        master_instance(document, master, master_arguments(master, {}, defaults));
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
                        const std::vector<ir::Value> arguments = master_arguments(info.master, info.arguments, env);
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

            // 값이 없는 var를 미리 볼 때 쓰는 타입의 기본값. int는 0, 목록은 빈 목록, 글은 빈 글 ...
            ir::Value default_value_of(const Type& type) const {
                switch (type.kind) {
                    case Kind::Int:
                        return ir::Number{{{"", 0}}, false};
                    case Kind::Float:
                        return ir::Number{{{"", 0}}, true};
                    case Kind::String:
                    case Kind::Font:
                    case Kind::Ref:
                    case Kind::File:
                    case Kind::Video:
                    case Kind::Audio:
                        return std::string();
                    case Kind::Text:
                        return ir::Text{};
                    case Kind::Color:
                    case Kind::Fill:
                    case Kind::Background:
                    case Kind::Gradient:
                    case Kind::Pattern:
                        return ir::Color{0, 0, 0, 1, ""};
                    case Kind::Enum:
                        return ir::EnumValue{type.enumeration->name, type.enumeration->members.empty() ? std::string() : type.enumeration->members.front()};
                    case Kind::Image:
                        return ir::Image{};
                    case Kind::Link:
                    case Kind::SlideRef:
                        return ir::Link{};
                    case Kind::Action:
                        return ir::Action{"link", {}, "", {}, ""};
                    case Kind::List:
                        return ir::List{};
                    default:
                        return false;
                }
            }

            // template이나 master case 하나를 slide 한 장으로 펼친다. 편집기는 이 slide를 보통 slide처럼 보여 주고 고친다.
            // 안의 요소는 잠기지 않아 slide에 직접 적은 것처럼 끌고 바꿀 수 있고, 매개변수에서 온 값만 잠긴다
            void build_definition(Result& result) {
                const DefinitionRequest& request = *definition_request_;
                DefinitionInfo& info = result.definition;
                info.kind = request.kind;
                info.name = request.name;
                const std::size_t errors_before = diagnostics_.size();
                ir::Document document = *result.document;
                document.slides.clear();
                ir::Slide slide;
                slide.page = 1;
                Env env;
                env.page = 1;
                env.id = "1";
                expansion_failed_ = false;
                scope_ = ElementScope{};
                slide_animations_.clear();
                media_positions_.clear();

                // 편집기가 보여 줄 var 목록 (선언 위치, 기본값, 미리 보는 값)
                const auto collect_vars = [&](const std::vector<ast::ASTNode*>& body) {
                    for (const auto* statement : body) {
                        if (statement->type != ast::VAR) {
                            continue;
                        }
                        const auto* declared = static_cast<const ast::ASTVar*>(statement);
                        const auto found = env.values.find(declared->name->name);
                        if (found == env.values.end()) {
                            continue;
                        }
                        DefinitionVar entry;
                        entry.name = declared->name->name;
                        entry.type = type_name(resolve_type(declared->type_name));
                        entry.has_default = declared->default_value != nullptr;
                        entry.value = found->second;
                        entry.declaration = range_of(declared);
                        if (entry.has_default) {
                            entry.default_text = std::string(declared->default_value->span);
                            entry.default_range = range_of(declared->default_value);
                            entry.default_origin = origin_of(declared->default_value, Env{}, resolve_type(declared->type_name));
                        }
                        info.vars.push_back(std::move(entry));
                    }
                };

                const auto bind = [&](const std::vector<VarInfo>& vars, bool use_defaults) {
                    for (const auto& var : vars) {
                        ir::Value value = default_value_of(var.type);
                        if (use_defaults && var.default_value != nullptr) {
                            Env defaults;
                            defaults.page = 1;
                            value = evaluate(var.default_value, defaults, var.type);
                        }
                        env.values[var.name] = std::move(value);
                        env.origins[var.name] = locked("parameter");
                    }
                };

                if (request.kind == "template") {
                    const auto it = templates_.find(request.name);
                    if (it == templates_.end()) {
                        info.error = "No template named '" + request.name + "'";
                        return;
                    }
                    const ast::ASTTemplate* node = it->second;
                    info.package = in_package(node);
                    info.statement = range_of(node);
                    bind(signatures_.at(node), true);
                    collect_vars(node->body);
                    slide.source = block_of(node, env);
                    execute(node->body, env, slide.elements, 0);
                    check_connectors();
                } else if (request.kind == "master") {
                    const auto it = masters_.find(request.name);
                    if (it == masters_.end()) {
                        info.error = "No master named '" + request.name + "'";
                        return;
                    }
                    const ast::ASTMaster* master = it->second;
                    info.package = in_package(master);
                    info.statement = range_of(master);
                    const auto& parameters = signatures_.at(master);
                    const ast::ASTMasterCase* layout = nullptr;
                    std::size_t layout_index = 0;
                    for (std::size_t i = 0; i < master->body.size(); ++i) {
                        const auto* each = static_cast<const ast::ASTMasterCase*>(master->body[i]);
                        info.cases.push_back(each->name);
                        if (layout == nullptr && (request.layout.empty() || each->name == request.layout)) {
                            layout = each;
                            layout_index = i;
                        }
                    }
                    if (layout == nullptr) {
                        info.error = master->body.empty() ? "Master '" + request.name + "' has no layout" : "Master '" + request.name + "' has no layout '" + request.layout + "'";
                        return;
                    }
                    info.layout = layout->name;
                    bind(parameters, true);
                    collect_vars(master->vars);
                    std::vector<ir::Value> arguments;
                    for (const auto& parameter : parameters) {
                        arguments.push_back(env.values.at(parameter.name));
                    }
                    // theme 색은 master의 것을 쓰고, 편집하는 case는 비워 두어 슬라이드의 요소와 겹쳐 그려지지 않게 한다
                    const std::size_t index = master_instance(document, master, arguments);
                    document.masters[index].layouts[layout_index].elements.clear();
                    document.masters[index].layouts[layout_index].background.reset();
                    slide.layout = ir::LayoutRef{index, layout_index};
                    for (auto& target : document.targets) {
                        target.masters = {index};
                    }
                    env.page.reset();
                    slide.source = block_of(layout, env);
                    scope_ = ElementScope{true, {}, {}};
                    execute(layout->body, env, slide.elements, 0);
                    check_connectors();
                    for (const auto* inner : layout->body) {
                        if (inner->type != ast::ASSIGN) {
                            continue;
                        }
                        const auto* assign = static_cast<const ast::ASTAssign*>(inner);
                        if (assign->name->name == "background") {
                            ir::Value value = evaluate(assign->expression, env, Type{Kind::Background});
                            check_paint(value, "background", location(assign->expression));
                            slide.origins["background"] = assigned_origin(assign, env, Type{Kind::Background});
                            slide.background = std::move(value);
                        }
                    }
                } else {
                    info.error = "Unknown definition kind: " + request.kind;
                    return;
                }
                add_media_animations(slide.elements);
                slide.animations = ordered_animations();
                document.slides.push_back(std::move(slide));
                result.definition_document = std::move(document);
                // 펼치다 난 오류는 정의를 보는 데 방해가 되지 않게 따로 둔다
                info.diagnostics.assign(diagnostics_.begin() + static_cast<std::ptrdiff_t>(errors_before), diagnostics_.end());
                diagnostics_.resize(errors_before);
                expansion_failed_ = false;
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
                        error("E2106", assign->name->token, "'" + name + "' needs a layout with a " + name + " placeholder");
                    } else if (!has_placeholder(document.masters.at(slide.layout->master).layouts.at(slide.layout->layout).elements, name)) {
                        error("E2107", assign->name->token, "Layout '" + document.masters.at(slide.layout->master).layouts.at(slide.layout->layout).name + "' has no " + name + " placeholder");
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
                        (name == "heading_font" ? result.heading_font : result.body_font) = std::get<std::string>(evaluate(assign->expression, Env{}, Type{Kind::Font}));
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
                        error("E2108", location(node->path), "Invalid path: " + message);
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
                        error("E2109", location(node->order), "'order' must be a number of 1 or more without a unit");
                    } else {
                        animation.order = static_cast<int>(*order);
                    }
                }
                if (target != nullptr) {
                    return animation;
                }
                const auto named = scope_.names.find(animation.target);
                if (named == scope_.names.end()) {
                    error("E2110", node->target->token, "No object named '" + animation.target + "' on this slide; give one a name with 'put ... as " + animation.target + "'");
                } else {
                    animation.element_id = named->second.id;
                    if (animation.category == "media" && named->second.object != "video" && named->second.object != "audio") {
                        error("E2111", node->target->token, "Media animations can only be used on a video or an audio, but '" + animation.target + "' is a " + named->second.object);
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
                transition.where = where_of(node->kind->token);
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
                    } else if (name == "scale") {
                        const ir::Number scale = as_number(evaluate(assign->expression, Env{}, Type{Kind::Float}));
                        if (check_number(scale, Rule{Measure::Ratio, 0.1, 10}, "'scale'", location(assign->expression))) {
                            result.scale = scalar(scale).value_or(1);
                        }
                        result.scale_where = where_of(location(assign->expression));
                    } else if (name == "transparent") {
                        result.transparent = std::get<bool>(evaluate(assign->expression, Env{}, Type{Kind::Bool}));
                        result.transparent_where = where_of(location(assign->expression));
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
                    error("E2112", alias->token, "Duplicate object name '" + alias->name + "' on this " + (scope_.layout ? "layout" : "slide"));
                }
                return alias->name;
            }

            // number가 rule에 맞는지. 맞지 않으면 at에 에러를 남긴다. subject는 에러에서 값을 가리키는 말이다
            bool check_number(const ir::Number& number, const Rule& rule, const std::string& subject, const Token& at) {
                for (const auto& [unit, value] : number.terms) {
                    if (!unit_allowed(rule.measure, unit)) {
                        error("E2113", at, subject + " must be " + measure_name(rule.measure));
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
                    error("E2114", at, subject + " must be " + (rule.integer ? "a whole number " : "") + (open ? range : "between " + range));
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
                    error("E2115", at(crop_width >= 100 ? "crop_right" : "crop_bottom"), "Crop must leave part of the image");
                }
                const std::string& object = element.object;
                const auto* kind_value = std::get_if<ir::EnumValue>(value_of("kind"));
                const std::string kind = kind_value != nullptr ? kind_value->member : "";
                const bool has_kind = object == "shape" || object == "backdrop" || object == "image";
                static const std::set<std::string> rounded = {"roundRect", "round1Rect", "round2SameRect", "round2DiagRect", "snipRoundRect"};
                if (value_of("radius") != nullptr && (!has_kind || !rounded.contains(kind))) {
                    error("E2116", at("radius"), "'radius' only applies to shapes, backdrops and images of kind roundRect, round1Rect, round2SameRect, round2DiagRect or snipRoundRect");
                }
                const auto adjustments = geometry::shape_adjustments().find(has_kind ? kind : "");
                const std::size_t count = adjustments == geometry::shape_adjustments().end() ? 0 : adjustments->second.size();
                for (std::size_t i = count; i < 8; ++i) {
                    const std::string name = "adj" + std::to_string(i + 1);
                    if (value_of(name) != nullptr) {
                        error("E2117", at(name), (has_kind ? "Kind '" + kind + "'" : "'" + object + "'") + " has no " + name + (count > 0 ? " (it has adj1 ~ adj" + std::to_string(count) + ")" : ""));
                    }
                }
                if (object == "freeform") {
                    if (const auto* path = std::get_if<std::string>(value_of("path"))) {
                        std::string message;
                        if (!geometry::parse_svg_path(*path, message)) {
                            error("E2108", at("path"), "Invalid path: " + message);
                        }
                    }
                }
                if (object == "placeholder" && !scope_.layout) {
                    error("E2118", fallback, "A placeholder can only be put in a master case; a slide fills it with title, subtitle or body");
                }
            }

            // 이름으로 가리킨 연결선의 두 끝. slide나 layout을 다 펼친 뒤에 찾는다
            void check_connectors() {
                for (const auto& connector : scope_.connectors) {
                    for (const auto& [name, token] : {std::pair{connector.from, connector.from_token}, std::pair{connector.to, connector.to_token}}) {
                        const auto it = scope_.names.find(name);
                        if (it == scope_.names.end()) {
                            error("E2119", token, "No object named '" + name + "' on this " + (scope_.layout ? "layout" : "slide"));
                        } else if (it->second.object == "group" || it->second.object == "line" || it->second.object == "connector") {
                            error("E2120", token, "Cannot connect to the " + it->second.object + " '" + name + "'");
                        } else if (!geometry::connection_sites().contains(it->second.kind)) {
                            error("E2121", token, "Shapes of kind '" + it->second.kind + "' have no connection points");
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
                        error("E2122", location(start ? node->range_end : node->range_start), "A range must be made of numbers without units");
                        return;
                    }
                    if (*end - *start + 1 > max_loop_iterations) {
                        error("E2123", node->token, "Loop runs more than " + std::to_string(max_loop_iterations) + " times");
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

            // 칸의 글이 비었는지
            static bool is_empty_text(const ir::Text& text) {
                for (const auto& paragraph : text.paragraphs) {
                    for (const auto& run : paragraph.runs) {
                        if (!run.text.empty() || !run.field.empty()) {
                            return false;
                        }
                    }
                }
                return true;
            }

            // put table. cells, col_widths, row_heights가 서로 맞는지 보고, cell(행, 열) 문장을 계산해 element.cell_styles에 넣는다.
            // 병합한 칸은 겹치거나 표 밖으로 나가면 안 되고, 병합에 가려지는 칸의 글은 비어 있어야 한다
            // 문서의 target 가운데 pptx가 있는가. type = pptx로 쓴 것만 센다
            bool pptx_targeted() const {
                for (const auto* target : targets_) {
                    for (const auto* statement : target->body) {
                        const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                        if (assign->name->name == "type" && assign->expression->type == ast::NAME && static_cast<const ast::ASTName*>(assign->expression)->name == "pptx") {
                            return true;
                        }
                    }
                }
                return false;
            }

            // 수식(math)의 LaTeX가 맞는지. 문법 오류는 늘 오류이고, PowerPoint 수식으로 바꿀 수 없는 명령은 pptx를 만드는 문서에서만 오류다
            void check_math(const ast::ASTPut* put, const ir::Element& element, const std::map<std::string, const ast::ASTNode*>& sources) {
                const auto* value = [&]() -> const std::string* {
                    for (const auto& each : element.properties) {
                        if (each.name == "latex") {
                            return std::get_if<std::string>(&each.value);
                        }
                    }
                    return nullptr;
                }();
                if (value == nullptr) {
                    return;
                }
                const Token* at = &put->token;
                for (const auto* statement : put->body) {
                    if (static_cast<const ast::ASTAssign*>(statement)->name->name == "latex") {
                        at = &location(sources.at("latex"));
                    }
                }
                if (value->find_first_not_of(" \t\r\n") == std::string::npos) {
                    error("E2174", *at, "The LaTeX is empty");
                    return;
                }
                const latex::Result parsed = latex::parse(*value);
                if (!parsed.error.empty()) {
                    error("E2172", *at, "The LaTeX has a syntax error: " + parsed.error);
                } else if (!parsed.unsupported.empty() && pptx_targeted()) {
                    error("E2173", *at, "The LaTeX '" + parsed.unsupported + "' cannot be written as a PowerPoint equation; remove it, or remove pptx from the targets");
                }
            }

            // 차트의 데이터가 맞는지. values는 계열마다 한 줄이고 categories(산점도는 xs)와 수가 맞아야 한다
            void check_chart(const ast::ASTPut* put, const ir::Element& element, const std::map<std::string, const ast::ASTNode*>& sources) {
                // 오류는 put 블록에 적은 값을 가리킨다. 적지 않은(기본값을 쓴) 속성이면 put 자리를 가리킨다
                const auto at = [&](const std::string& name) -> const Token& {
                    for (const auto* statement : put->body) {
                        if (static_cast<const ast::ASTAssign*>(statement)->name->name == name) {
                            return location(sources.at(name));
                        }
                    }
                    return put->token;
                };
                const auto property = [&](const std::string& name) -> const ir::Value* {
                    for (const auto& each : element.properties) {
                        if (each.name == name) {
                            return &each.value;
                        }
                    }
                    return nullptr;
                };
                const auto lists = [&](const std::string& name) -> std::vector<const ir::List*> {
                    std::vector<const ir::List*> result;
                    if (const auto* outer = std::get_if<ir::List>(property(name))) {
                        for (const auto& item : outer->items) {
                            if (const auto* row = std::get_if<ir::List>(&item)) {
                                result.push_back(row);
                            }
                        }
                    }
                    return result;
                };
                const auto count_of = [&](const std::string& name) -> std::size_t {
                    const auto* list = std::get_if<ir::List>(property(name));
                    return list == nullptr ? 0 : list->items.size();
                };
                const auto* kind = std::get_if<ir::EnumValue>(property("kind"));
                const bool scatter = kind != nullptr && kind->member == "scatter";
                const bool pie = kind != nullptr && kind->member == "pie";
                const auto values = lists("values");
                if (values.empty()) {
                    error("E2163", at("values"), "A chart needs at least one series in 'values'");
                    return;
                }
                bool unitless = true;
                bool negative = false;
                for (const auto* name : {"values", "xs"}) {
                    for (const auto* row : lists(name)) {
                        for (const auto& item : row->items) {
                            const auto* number = std::get_if<ir::Number>(&item);
                            const auto value = number == nullptr ? std::nullopt : scalar(*number);
                            if (!value) {
                                unitless = false;
                            } else if (*value < 0 && std::string(name) == "values") {
                                negative = true;
                            }
                        }
                    }
                }
                if (!unitless) {
                    error("E2170", at("values"), "Chart values must be numbers without a unit");
                    return;
                }
                const std::size_t categories = count_of("categories");
                if (!scatter && categories > 0) {
                    for (std::size_t i = 0; i < values.size(); ++i) {
                        if (values[i]->items.size() != categories) {
                            error("E2164", at("values"), "Series " + std::to_string(i + 1) + " of 'values' has " + std::to_string(values[i]->items.size()) + " values, but 'categories' has "
                                                             + std::to_string(categories));
                        }
                    }
                } else if (!scatter) {
                    for (std::size_t i = 1; i < values.size(); ++i) {
                        if (values[i]->items.size() != values[0]->items.size()) {
                            error("E2164", at("values"), "Series " + std::to_string(i + 1) + " of 'values' has " + std::to_string(values[i]->items.size()) + " values, but 'categories' has "
                                                             + std::to_string(values[0]->items.size()));
                        }
                    }
                }
                if (scatter) {
                    const auto xs = lists("xs");
                    if (xs.empty()) {
                        error("E2171", at("xs"), "A scatter chart needs 'xs' with one list of x values per series");
                    } else if (xs.size() != values.size()) {
                        error("E2165", at("xs"), "'xs' has " + std::to_string(xs.size()) + " series, but 'values' has " + std::to_string(values.size()));
                    } else {
                        for (std::size_t i = 0; i < xs.size(); ++i) {
                            if (xs[i]->items.size() != values[i]->items.size()) {
                                error("E2166", at("xs"), "Series " + std::to_string(i + 1) + " of 'xs' has " + std::to_string(xs[i]->items.size())
                                                             + " values, but the same series of 'values' has " + std::to_string(values[i]->items.size()));
                            }
                        }
                    }
                } else if (count_of("xs") > 0) {
                    warning("W2006", at("xs"), "'xs' is only used by scatter charts");
                }
                if (const std::size_t names = count_of("names"); names > 0 && names != values.size()) {
                    error("E2167", at("names"), "'names' has " + std::to_string(names) + " names, but 'values' has " + std::to_string(values.size()) + " series");
                }
                if (const auto* range = std::get_if<ir::List>(property("range")); range != nullptr && !range->items.empty()) {
                    bool valid = range->items.size() == 2;
                    std::vector<double> bounds;
                    for (const auto& item : range->items) {
                        const auto* number = std::get_if<ir::Number>(&item);
                        const auto value = number == nullptr ? std::nullopt : scalar(*number);
                        if (!value) {
                            valid = false;
                        } else {
                            bounds.push_back(*value);
                        }
                    }
                    if (!valid || bounds[0] >= bounds[1]) {
                        error("E2168", at("range"), "'range' must be empty or two numbers, [minimum, maximum], with the minimum below the maximum");
                    }
                }
                if (pie) {
                    if (negative) {
                        error("E2169", at("values"), "A pie chart cannot show negative values");
                    }
                    if (values.size() > 1) {
                        warning("W2005", at("values"), "A pie chart shows only the first of " + std::to_string(values.size()) + " series");
                    }
                }
            }

            void build_table(const ast::ASTPut* put, const Env& env, const Env& self, ir::Element& element, const std::map<std::string, const ast::ASTNode*>& sources) {
                const auto at = [&](const std::string& name) -> const Token& {
                    const auto it = sources.find(name);
                    return it != sources.end() ? location(it->second) : put->token;
                };
                const auto property = [&](const std::string& name) -> const ir::Value* {
                    for (const auto& each : element.properties) {
                        if (each.name == name) {
                            return &each.value;
                        }
                    }
                    return nullptr;
                };
                const auto* grid = std::get_if<ir::List>(property("cells"));
                if (grid == nullptr) {
                    return;
                }
                const std::size_t rows = grid->items.size();
                std::size_t columns = 0;
                bool ragged = false;
                for (std::size_t r = 0; r < rows; ++r) {
                    const auto* row = std::get_if<ir::List>(&grid->items[r]);
                    if (row == nullptr) {
                        ragged = true;
                    } else if (r == 0) {
                        columns = row->items.size();
                    } else if (row->items.size() != columns) {
                        error("E2124", at("cells"), "Row " + std::to_string(r + 1) + " of 'cells' has " + std::to_string(row->items.size()) + " cells, but row 1 has " + std::to_string(columns));
                        ragged = true;
                    }
                }
                if (rows == 0 || columns == 0) {
                    error("E2125", at("cells"), "A table needs at least one row and one column");
                    return;
                }
                if (ragged) {
                    return;
                }
                const auto check_sizes = [&](const std::string& name, std::size_t count, const std::string& what) {
                    const auto* list = std::get_if<ir::List>(property(name));
                    if (list == nullptr || list->items.empty()) {
                        return;
                    }
                    if (list->items.size() != count) {
                        error("E2126", at(name), "'" + name + "' has " + std::to_string(list->items.size()) + " values, but the table has " + std::to_string(count) + " " + what);
                    }
                    for (const auto& item : list->items) {
                        const auto* number = std::get_if<ir::Number>(&item);
                        if (number != nullptr && check_number(*number, Rule{Measure::Position, 0}, "A value of '" + name + "'", at(name))) {
                            double total = 0;
                            for (const auto& term : number->terms) {
                                total += term.second;
                            }
                            if (total <= 0) {
                                error("E2127", at(name), "A value of '" + name + "' must be more than 0");
                            }
                        }
                    }
                };
                check_sizes("col_widths", columns, "columns");
                check_sizes("row_heights", rows, "rows");
                const auto check_header = [&](const std::string& name, std::size_t limit, const std::string& what) {
                    if (const auto* number = std::get_if<ir::Number>(property(name)); number != nullptr) {
                        if (const auto value = scalar(*number); value && *value > static_cast<double>(limit)) {
                            error("E2128", at(name), "'" + name + "' is " + ir::format_scalar(*value) + ", but the table has only " + std::to_string(limit) + " " + what);
                        }
                    }
                };
                check_header("header_rows", rows, "rows");
                check_header("header_cols", columns, "columns");

                struct Region {
                    std::size_t row;
                    std::size_t column;
                    std::size_t row_span;
                    std::size_t column_span;
                    const ast::ASTCell* node;
                };
                std::vector<Region> regions;
                std::set<std::pair<std::size_t, std::size_t>> seen;
                const auto types = cell_properties();
                for (const auto* node : put->cells) {
                    ir::CellStyle style;
                    style.source = block_of(node, env);
                    style.row_origin = origin_of(node->row, env, Type{Kind::Int});
                    style.column_origin = origin_of(node->column, env, Type{Kind::Int});
                    const auto row = scalar(as_number(evaluate(node->row, self, Type{Kind::Int})));
                    const auto column = scalar(as_number(evaluate(node->column, self, Type{Kind::Int})));
                    bool valid = true;
                    for (const auto& [number, expression, what] : {std::tuple{row, node->row, "row"}, std::tuple{column, node->column, "column"}}) {
                        if (!number || *number != std::floor(*number)) {
                            error("E2129", location(expression), std::string("A cell ") + what + " must be a whole number without a unit");
                            valid = false;
                        }
                    }
                    if (valid && (*row < 1 || *row > static_cast<double>(rows) || *column < 1 || *column > static_cast<double>(columns))) {
                        error("E2130", node->token, "cell(" + ir::format_scalar(*row) + ", " + ir::format_scalar(*column) + ") is outside the table, which has "
                                               + std::to_string(rows) + " rows and " + std::to_string(columns) + " columns");
                        valid = false;
                    }
                    std::size_t row_span = 1;
                    std::size_t column_span = 1;
                    for (const auto* statement : node->body) {
                        const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                        const std::string& name = assign->name->name;
                        const auto type = std::find_if(types.begin(), types.end(), [&](const auto& each) { return each.first == name; });
                        if (type == types.end()) {
                            continue;
                        }
                        ir::Value value = evaluate(assign->expression, self, type->second);
                        if (const auto* number = std::get_if<ir::Number>(&value)) {
                            if (const auto rule = property_rules().find(name); rule != property_rules().end()) {
                                check_number(*number, rule->second, "'" + name + "'", location(assign->expression));
                            }
                            if (const auto span = scalar(*number); span && *span >= 1 && (name == "col_span" || name == "row_span")) {
                                (name == "col_span" ? column_span : row_span) = static_cast<std::size_t>(*span);
                            }
                        }
                        check_paint(value, name, location(assign->expression));
                        style.properties.push_back({name, std::move(value), assigned_origin(assign, env, type->second)});
                    }
                    if (valid) {
                        style.row = static_cast<int>(*row);
                        style.column = static_cast<int>(*column);
                        const auto r = static_cast<std::size_t>(*row);
                        const auto c = static_cast<std::size_t>(*column);
                        if (!seen.insert({r, c}).second) {
                            error("E2131", node->token, "Duplicate cell(" + std::to_string(r) + ", " + std::to_string(c) + ")");
                        } else if (r + row_span - 1 > rows || c + column_span - 1 > columns) {
                            error("E2132", node->token, "The merged cell at cell(" + std::to_string(r) + ", " + std::to_string(c) + ") reaches outside the table, which has "
                                                   + std::to_string(rows) + " rows and " + std::to_string(columns) + " columns");
                        } else {
                            regions.push_back({r, c, row_span, column_span, node});
                        }
                    }
                    element.cell_styles.push_back(std::move(style));
                }

                // 병합한 칸이 덮는 자리. 다른 병합과 겹치거나, 병합에 가려지는 칸에 cell 블록이 있거나, 글이 있으면 에러다
                std::vector<int> owner(rows * columns, -1);
                const auto name_of = [](const Region& region) { return "cell(" + std::to_string(region.row) + ", " + std::to_string(region.column) + ")"; };
                for (std::size_t i = 0; i < regions.size(); ++i) {
                    const Region& region = regions[i];
                    if (region.row_span * region.column_span == 1) {
                        continue;
                    }
                    for (std::size_t r = region.row; r < region.row + region.row_span; ++r) {
                        for (std::size_t c = region.column; c < region.column + region.column_span; ++c) {
                            int& slot = owner[(r - 1) * columns + (c - 1)];
                            if (slot != -1) {
                                error("E2133", region.node->token, "The merged cell at " + name_of(region) + " overlaps the merged cell at " + name_of(regions[static_cast<std::size_t>(slot)]));
                            } else {
                                slot = static_cast<int>(i);
                            }
                        }
                    }
                }
                for (std::size_t i = 0; i < regions.size(); ++i) {
                    const Region& region = regions[i];
                    const int slot = owner[(region.row - 1) * columns + (region.column - 1)];
                    // 병합한 칸이 다른 병합과 겹치는 것은 위에서 알렸으므로, 병합하지 않은 칸의 cell 블록만 본다
                    if (slot != -1 && static_cast<std::size_t>(slot) != i && region.row_span * region.column_span == 1) {
                        error("E2134", region.node->token, name_of(region) + " is covered by the merged cell at " + name_of(regions[static_cast<std::size_t>(slot)]) + ", so it cannot have its own cell block");
                    }
                    if (region.row_span * region.column_span == 1) {
                        continue;
                    }
                    for (std::size_t r = region.row; r < region.row + region.row_span; ++r) {
                        for (std::size_t c = region.column; c < region.column + region.column_span; ++c) {
                            if (r == region.row && c == region.column) {
                                continue;
                            }
                            const auto* row = std::get_if<ir::List>(&grid->items[r - 1]);
                            const auto* text = std::get_if<ir::Text>(&row->items[c - 1]);
                            if (text != nullptr && !is_empty_text(*text)) {
                                error("E2135", at("cells"), "The cell in row " + std::to_string(r) + ", column " + std::to_string(c) + " is covered by the merged cell at " + name_of(region)
                                                       + ", so its text must be empty (\"\")");
                            }
                        }
                    }
                }
            }

            // template은 펼치고, object는 element로 만든다
            void instantiate(const ast::ASTPut* put, const Env& env, std::vector<ir::Element>& out, int depth) {
                if (depth >= max_expansion_depth) {
                    error("E2136", put->token, "Template expansion is too deep; check for a template that puts itself");
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
                    if (put->name == "table") {
                        build_table(put, env, self, element, sources);
                    }
                    if (put->name == "chart") {
                        check_chart(put, element, sources);
                    }
                    if (put->name == "math") {
                        check_math(put, element, sources);
                    }
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

            // [a, b, c]. 항목은 expected.element 타입으로 계산한다
            ir::List evaluate_list(const ast::ASTList* list, const Env& env, const Type& expected) {
                ir::List result;
                result.origin = origin_of(list, env, expected);
                for (const auto* item : list->items) {
                    result.items.push_back(evaluate(item, env, *expected.element));
                    result.origins.push_back(origin_of(item, env, *expected.element));
                    result.ranges.push_back(range_of(item));
                }
                return result;
            }

            ir::Value evaluate(const ast::ASTNode* expr, const Env& env, const Type& expected) {
                if (expected.kind == Kind::List && expr->type == ast::LIST && static_cast<const ast::ASTList*>(expr)->kind == ast::ListKind::PLAIN) {
                    return evaluate_list(static_cast<const ast::ASTList*>(expr), env, expected);
                }
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
                // 그림은 image(경로)로, 비디오와 오디오는 경로 문자열로 넘긴다. 문자열 경로는 file(경로)와 같다
                if (const auto* path = std::get_if<std::string>(&value); path != nullptr && expected.kind == Kind::Image) {
                    return ir::Image{*path};
                }
                if (const auto* image = std::get_if<ir::Image>(&value); image != nullptr && (expected.kind == Kind::Video || expected.kind == Kind::Audio)) {
                    return image->path;
                }
                return value;
            }

            // image NAME = 값;의 값. 그림은 ir::Image, 비디오와 오디오는 경로 문자열이다
            ir::Value evaluate_constant(const ast::ASTConstant* node) {
                if (!evaluating_.insert(node).second) {
                    error("E2137", node->name->token, "'" + node->name->name + "' refers to itself");
                    return ir::Image{};
                }
                ir::Value value = evaluate(node->expression, Env{}, constant_type(node));
                evaluating_.erase(node);
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
                        if (const auto constant = constants_.find(name); constant != constants_.end() && expected.kind != Kind::Ref) {
                            return evaluate_constant(constant->second);
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
                    case ast::COLOR_SPACE:
                        return evaluate_color_space(static_cast<const ast::ASTColorSpace*>(expr), env);
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
                        error("E2138", location(node), "'@self' can only be used in the properties of a put");
                        return std::string();
                    }
                    if (env.self->empty()) {
                        error("E2139", location(node), "'@self.name' needs a named object; write 'put ... as NAME'");
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
                    error("E2140", location(node), "'@slide' cannot be used while expanding a master");
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
                // image("경로")는 file("경로")와 같다. 경로는 main 폴더 기준이다
                if (name == "image" || name == "file" || name == "asset") {
                    const ir::Value argument = evaluate(arguments[0], env, Type{Kind::String});
                    const auto* string = std::get_if<std::string>(&argument);
                    if (string == nullptr || string->empty()) {
                        if (name == "asset") {
                            error("E2141", location(arguments[0]), "The file name must not be empty");
                        } else {
                            error("E2156", location(arguments[0]), "The path must not be empty");
                        }
                        return ir::Image{};
                    }
                    if (name == "asset") {
                        return ir::Image{resolve_asset(*string, location(arguments[0])).value_or("")};
                    }
                    return ir::Image{*string};
                }
                if (name == "font") {
                    const ir::Value argument = evaluate(arguments[0], env, Type{Kind::String});
                    const auto* family = std::get_if<std::string>(&argument);
                    if (family == nullptr || family->empty()) {
                        error("E2142", location(arguments[0]), "The font name must not be empty");
                        return std::string();
                    }
                    return *family;
                }
                if (is_action_call(name)) {
                    return evaluate_action_call(call, env);
                }
                if (name == "slide") {
                    const auto page = scalar(as_number(evaluate(arguments[0], env, Type{Kind::Int})));
                    if (!page || *page < 1 || *page != std::floor(*page)) {
                        error("E2143", location(arguments[0]), "A slide number must be a whole number from 1 without a unit");
                        return ir::Link{};
                    }
                    if (*page > static_cast<double>(slides_.size())) {
                        error("E2144", location(arguments[0]), "There is no slide " + format_bound(*page) + " to link to");
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
                ir::Action action{call->name->name == "action_file" ? "file" : call->name->name, {}, "", {}, where_of(call->token)};
                const ir::Value target = evaluate(arguments[0], env, Type{Kind::String});
                if (const auto* string = std::get_if<std::string>(&target)) {
                    action.target = *string;
                }
                if (action.target.empty()) {
                    error("E2145", location(arguments[0]), "The " + std::string(action.kind == "run" ? "function name" : action.kind == "macro" ? "macro name" : "path") + " must not be empty");
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
                            error("E2146", location(arguments[i]), "'@slide.number' cannot be passed to run in a master; there is no slide yet");
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
                    error("E2147", location(left), "Cannot multiply two numbers that both have units");
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
                    error("E2148", location(expr), "Color component must be a number from 0 to 255 without a unit");
                    return 0;
                }
                return static_cast<int>(*value);
            }

            // hsl(...), oklch(...) 등을 sRGB 색으로. 값의 단위와 범위는 CSS Color 4를 따른다 (color::parse).
            // sRGB 밖의 색(lab, lch, oklab, oklch)은 채도를 줄여 가장 가까운 색으로 맞추고 경고한다
            ir::Color evaluate_color_space(const ast::ASTColorSpace* node, const Env& env) {
                std::vector<color::Component> components;
                for (const auto* argument : node->arguments) {
                    const ir::Number number = as_number(evaluate(argument, env, Type{Kind::Float}));
                    if (number.terms.empty()) {
                        components.push_back({0, ""});
                    } else if (number.terms.size() == 1) {
                        components.push_back({number.terms[0].second, number.terms[0].first});
                    } else {
                        components.push_back({0, "?"});
                    }
                }
                std::size_t index = 0;
                std::string message;
                const auto parsed = color::parse(node->space, components, index, message);
                if (!parsed) {
                    error("E2149", index < node->arguments.size() ? location(node->arguments[index]) : node->token, message);
                    return ir::Color{};
                }
                color::Rgb rgb = color::to_srgb(node->space, parsed->values);
                const bool outside = !color::in_gamut(rgb);
                rgb = color::gamut_map(rgb);
                const auto byte = [](double c) { return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255)); };
                ir::Color result{byte(rgb.r), byte(rgb.g), byte(rgb.b), parsed->alpha, ""};
                if (outside) {
                    warning("W2001", node->token, "This " + node->space + " color is outside the sRGB range that pptx and html can show; using the nearest color "
                        + color::format("hex", result.r, result.g, result.b, 1));
                }
                return result;
            }

            double color_alpha(const ast::ASTNode* expr, const Env& env) {
                const auto value = scalar(as_number(evaluate(expr, env, Type{Kind::Float})));
                if (!value || *value < 0 || *value > 1) {
                    error("E2150", location(expr), "Alpha must be a number from 0 to 1 without a unit");
                    return 1;
                }
                return *value;
            }

            // text 만들기

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
                                    error("E2151", call->token, "This text already has an action; use either link(...) or action(...)");
                                }
                            } else if (is_text_action(call->name->name)) {
                                const ir::Value value = evaluate(call->arguments[0], env, Type{Kind::Action});
                                (call->name->name == "action" ? style.action : style.hover_action) = std::get<ir::Action>(value);
                                if (call->name->name == "action" && style.link) {
                                    error("E2152", call->token, "This text already has a link; use either link(...) or action(...)");
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

    Result analyze(const std::filesystem::path& path, const std::filesystem::path& packages_dir, const Overlays& overlays, const std::optional<DefinitionRequest>& definition) {
        Analyzer analyzer(packages_dir, overlays);
        if (definition) {
            analyzer.request_definition(*definition);
        }
        return analyzer.run(path);
    }
}

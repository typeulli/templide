#include "html.h"
#include "backend.h"
#include "raster.h"
#include "../middleend/geometry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace templide::backend::html {
    namespace {
        using namespace templide::geometry;

        // 길이는 모두 px이다
        constexpr double pt = 4.0 / 3.0;
        constexpr double default_slide_width = 1280; // 16:9
        constexpr double default_slide_height = 720;
        constexpr double list_indent = 36;           // 목록 문단의 들여쓰기 (PowerPoint의 342900 EMU)
        constexpr double connector_margin = 24;      // 꺾인 연결선이 되돌아갈 때 도형에서 떨어지는 거리 (0.25in)
        constexpr double shape_line_width = pt;      // 도형의 기본 선 두께 (p:style의 lnRef 2)
        constexpr double line_line_width = pt / 2;   // 선의 기본 두께 (p:style의 lnRef 1)
        constexpr double plain_line_width = 0.75 * pt; // p:style이 없는 개체에 두께 없이 선을 넣었을 때
        constexpr long long default_transition_ms = 500; // 시간을 정하지 않은 전환 (PowerPoint의 spd 기본값 fast)
        const std::array<double, 4> default_insets = {9.6, 4.8, 9.6, 4.8}; // 글상자 안쪽 여백 (왼쪽, 위, 오른쪽, 아래)

        // 입자나 휘어지는 면으로 그리는 전환과 randomBars(PowerPoint 안의 기울어진 노이즈 텍스처로 그린다).
        // html은 비슷하게 흉내만 낼 수 있으므로 에러로 알린다. "종류" 또는 "종류.옵션"이다
        const std::set<std::string> unsupported_transitions = {
            "vortex", "ripple", "glitter", "honeycomb", "shred", "warp", "drape", "curtains", "wind", "prestige",
            "fracture", "crush", "peelOff", "pageCurlSingle", "pageCurlDouble", "airplane", "origami", "morph", "fallOver",
            "randomBars",
        };

        // ---- JSON

        std::string format_number(double value) {
            if (!std::isfinite(value)) {
                return "0";
            }
            double rounded = std::round(value * 1000) / 1000;
            if (rounded == 0) {
                rounded = 0; // -0을 0으로
            }
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.15g", rounded);
            return buffer;
        }

        // 슬라이드에 대한 비율처럼 작은 수
        std::string format_fraction(double value) {
            double rounded = std::round(value * 1000000) / 1000000;
            if (rounded == 0) {
                rounded = 0;
            }
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.15g", rounded);
            return buffer;
        }

        // '<'도 바꿔서 JSON을 <script> 안에 그대로 넣을 수 있게 한다
        std::string quote(const std::string& text) {
            std::string result = "\"";
            for (const char c : text) {
                const auto byte = static_cast<unsigned char>(c);
                if (c == '"') {
                    result += "\\\"";
                } else if (c == '\\') {
                    result += "\\\\";
                } else if (c == '\n') {
                    result += "\\n";
                } else if (c == '<') {
                    result += "\\u003c";
                } else if (byte < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", byte);
                    result += buffer;
                } else {
                    result += c;
                }
            }
            return result + "\"";
        }

        // 적어 둔 JSON 값 하나
        class Json {
        public:
            Json() : text_("null") {}
            Json(bool value) : text_(value ? "true" : "false") {}
            Json(int value) : text_(std::to_string(value)) {}
            Json(long long value) : text_(std::to_string(value)) {}
            Json(std::size_t value) : text_(std::to_string(value)) {}
            Json(double value) : text_(format_number(value)) {}
            Json(const std::string& value) : text_(quote(value)) {}
            Json(const char* value) : text_(quote(value)) {}

            static Json raw(std::string text) {
                Json json;
                json.text_ = std::move(text);
                return json;
            }

            const std::string& text() const {
                return text_;
            }

            bool is_null() const {
                return text_ == "null";
            }

        private:
            std::string text_;
        };

        class Object {
        public:
            // null은 적지 않는다
            Object& set(const std::string& key, const Json& value) {
                if (!value.is_null()) {
                    text_ += (text_.empty() ? "" : ",") + quote(key) + ":" + value.text();
                }
                return *this;
            }

            bool empty() const {
                return text_.empty();
            }

            operator Json() const {
                return Json::raw("{" + text_ + "}");
            }

        private:
            std::string text_;
        };

        class Array {
        public:
            Array& push(const Json& value) {
                text_ += (text_.empty() ? "" : ",") + value.text();
                return *this;
            }

            bool empty() const {
                return text_.empty();
            }

            operator Json() const {
                return Json::raw("[" + text_ + "]");
            }

        private:
            std::string text_;
        };

        // 참이면 1, 아니면 적지 않는다
        Json flag(bool value) {
            return value ? Json(1) : Json();
        }

        std::string base64(const std::string& bytes) {
            static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string result;
            result.reserve((bytes.size() + 2) / 3 * 4);
            for (std::size_t i = 0; i < bytes.size(); i += 3) {
                const unsigned value = static_cast<unsigned char>(bytes[i]) << 16
                    | (i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0)
                    | (i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0);
                result += digits[value >> 18 & 63];
                result += digits[value >> 12 & 63];
                result += i + 1 < bytes.size() ? digits[value >> 6 & 63] : '=';
                result += i + 2 < bytes.size() ? digits[value & 63] : '=';
            }
            return result;
        }

        std::string escape_html(const std::string& text) {
            std::string result;
            for (const char c : text) {
                switch (c) {
                    case '&': result += "&amp;"; break;
                    case '<': result += "&lt;"; break;
                    case '>': result += "&gt;"; break;
                    case '"': result += "&quot;"; break;
                    default: result += c; break;
                }
            }
            return result;
        }

        // <script> 안에 넣을 JS. 문자열 안의 </script가 script를 닫지 않게 한다
        std::string inline_script(std::string code) {
            for (std::size_t at = code.find("</"); at != std::string::npos; at = code.find("</", at + 3)) {
                if (code.compare(at + 2, 6, "script") == 0 || code.compare(at + 2, 6, "SCRIPT") == 0) {
                    code.replace(at, 2, "<\\/");
                }
            }
            return code;
        }

        double milliseconds(const ir::Number& number) {
            double total = 0;
            for (const auto& [unit, value] : number.terms) {
                total += unit == "s" ? value * 1000 : value;
            }
            return total;
        }

        double scalar(const ir::Number& number) {
            double total = 0;
            for (const auto& [unit, value] : number.terms) {
                total += value;
            }
            return total;
        }

        // 두 번째 수준 이상의 문단과 개체 틀이 아닌 글자가 따르는 기본 모양
        struct TextDefaults {
            double size;
            ir::Color color;
            std::string font; // +mj, +mn 또는 글꼴 이름
        };

        struct Rect {
            double x;
            double y;
            double width;
            double height;
        };

        class Writer {
        public:
            Writer(const ir::Document& document, const ir::Target& target, std::filesystem::path base_dir, std::filesystem::path libs_dir)
                : document_(document), target_(target), base_dir_(std::move(base_dir)), libs_dir_(std::move(libs_dir)) {}

            std::vector<std::string> run() {
                const std::string deck = build();
                if (errors_.empty()) {
                    save(deck);
                }
                return errors_;
            }

            // 편집기 미리보기용. element에 IR의 id("eid")를 넣고, 그릴 수 없는 것이 있어도 덱을 만든다
            std::string run_editor(std::vector<std::string>& errors) {
                editor_ = true;
                const std::string deck = build();
                errors = errors_;
                return deck;
            }

        private:
            std::string build() {
                web_ = target_.type == "web";
                read_slide_size();
                std::map<std::pair<std::size_t, std::size_t>, std::size_t> layout_numbers;
                Array layouts;
                for (const std::size_t index : target_.masters) {
                    const ir::Master& master = document_.masters.at(index);
                    theme_ = master.theme ? &*master.theme : nullptr;
                    for (std::size_t j = 0; j < master.layouts.size(); ++j) {
                        const ir::Layout& layout = master.layouts[j];
                        layout_numbers[{index, j}] = layout_numbers.size();
                        begin_tree(true, layout.elements);
                        layouts.push(Object().set("els", elements_json(layout.elements, "layout " + master.name + "." + layout.name)));
                    }
                }
                Array slides;
                std::map<const ir::Theme*, std::size_t> theme_numbers;
                Array themes;
                // layout이 없는 slide는 PowerPoint처럼 첫 master의 빈 layout을 쓰므로 그 테마를 따른다
                const ir::Theme* first_theme = nullptr;
                if (!target_.masters.empty() && document_.masters.at(target_.masters.front()).theme) {
                    first_theme = &*document_.masters.at(target_.masters.front()).theme;
                }
                for (const ir::Slide& slide : document_.slides) {
                    theme_ = first_theme;
                    const ir::Layout* layout = nullptr;
                    std::optional<std::size_t> layout_number;
                    if (slide.layout) {
                        const auto it = layout_numbers.find({slide.layout->master, slide.layout->layout});
                        if (it == layout_numbers.end()) {
                            error("slide " + std::to_string(slide.page) + ": its layout does not belong to the masters of this target");
                            continue;
                        }
                        const ir::Master& master = document_.masters.at(slide.layout->master);
                        theme_ = master.theme ? &*master.theme : nullptr;
                        layout = &master.layouts.at(slide.layout->layout);
                        layout_number = it->second;
                    }
                    // 애니메이션이 바꾸는 테마 색
                    auto theme = theme_numbers.find(theme_);
                    if (theme == theme_numbers.end()) {
                        theme = theme_numbers.emplace(theme_, theme_numbers.size()).first;
                        themes.push(theme_json());
                    }
                    slides.push(slide_json(slide, layout, layout_number, theme->second));
                }
                theme_ = nullptr;
                const Json deck = Object()
                    .set("v", 1)
                    .set("title", target_.title.empty() ? target_.name : target_.title)
                    .set("author", target_.author.empty() ? "templide" : target_.author)
                    .set("w", width_)
                    .set("h", height_)
                    .set("loop", flag(target_.loop))
                    .set("themes", themes)
                    .set("layouts", layouts)
                    .set("slides", slides);
                return deck.text();
            }

            const ir::Document& document_;
            const ir::Target& target_;
            std::filesystem::path base_dir_;
            std::filesystem::path libs_dir_;
            bool web_ = false;
            bool editor_ = false;
            double width_ = default_slide_width;
            double height_ = default_slide_height;
            std::vector<std::string> errors_;
            std::string where_; // 지금 만드는 element의 위치. 실행 설정의 소리 파일 에러에 쓴다

            std::map<std::filesystem::path, std::string> media_;           // 그림과 소리 파일 -> 쓸 주소
            std::vector<std::pair<std::string, std::string>> media_files_; // web의 media 폴더에 쓸 (이름, 내용)

            // 지금 만드는 slide나 layout
            bool in_layout_ = false;
            const ir::Theme* theme_ = nullptr;
            std::map<const ir::Element*, int> ids_;
            std::map<std::string, const ir::Element*> named_;
            // 지금 만드는 element
            double opacity_ = 1;

            void error(const std::string& message) {
                errors_.push_back(message);
            }

            std::filesystem::path resolve(const std::string& path) const {
                const std::filesystem::path result = utf8_path(path);
                return result.is_absolute() ? result : base_dir_ / result;
            }

            // ---- 길이

            // %는 reference에 대한 비율이고 나머지는 px이다. int 값의 %는 px 정수로 자른다
            static double to_px(const ir::Number& number, double reference = 0) {
                double total = 0;
                for (const auto& [unit, value] : number.terms) {
                    if (unit == "%") {
                        const double px = value / 100 * reference;
                        total += number.is_float ? px : std::trunc(px);
                    } else {
                        total += value;
                    }
                }
                return total;
            }

            void read_slide_size() {
                if (!target_.width || !target_.height) {
                    return;
                }
                const double width = to_px(*target_.width);
                const double height = to_px(*target_.height);
                if (width <= 0 || height <= 0) {
                    error("slide width and height must be more than 0");
                    return;
                }
                width_ = width;
                height_ = height;
            }

            double length(const ir::Element& element, const std::string& name, double reference) const {
                const auto* number = std::get_if<ir::Number>(find_property(element, name));
                return number != nullptr ? to_px(*number, reference) : 0;
            }

            std::optional<double> optional_number(const ir::Element& element, const std::string& name) const {
                if (const auto* number = std::get_if<ir::Number>(find_property(element, name))) {
                    return to_px(*number);
                }
                return std::nullopt;
            }

            // %를 슬라이드 크기로 푼 뒤에야 알 수 있는 음수 크기는 여기서 알린다
            std::optional<Rect> element_rect(const ir::Element& element, const std::string& where) {
                const Rect rect{length(element, "x", width_), length(element, "y", height_), length(element, "width", width_), length(element, "height", height_)};
                if (rect.width < 0 || rect.height < 0) {
                    error(where + ": width and height must not be negative");
                    return std::nullopt;
                }
                return rect;
            }

            Rect line_rect(const ir::Element& element) const {
                const double x1 = length(element, "x1", width_);
                const double y1 = length(element, "y1", height_);
                const double x2 = length(element, "x2", width_);
                const double y2 = length(element, "y2", height_);
                return Rect{std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1), std::abs(y2 - y1)};
            }

            // freeform의 상자와 path. path의 좌표는 (x, y)에서 잰 px이다
            std::optional<std::pair<Rect, ParsedPath>> freeform_shape(const ir::Element& element) const {
                const auto* text = std::get_if<std::string>(find_property(element, "path"));
                std::string message;
                auto path = text != nullptr ? parse_svg_path(*text, message) : std::nullopt;
                if (!path) {
                    return std::nullopt;
                }
                const Rect rect{length(element, "x", width_) + path->min.x, length(element, "y", height_) + path->min.y,
                                std::max(1.0, path->max.x - path->min.x), std::max(1.0, path->max.y - path->min.y)};
                return std::pair{rect, std::move(*path)};
            }

            // ---- 색과 채우기

            ir::Color theme_color(const std::string& scheme) const {
                ir::Color color = ir::default_theme_color(scheme);
                if (theme_ != nullptr) {
                    if (const auto it = theme_->colors.find(scheme); it != theme_->colors.end()) {
                        color.r = it->second.r;
                        color.g = it->second.g;
                        color.b = it->second.b;
                    }
                }
                return color;
            }

            // 테마 색은 지금 테마의 값을 쓴다. 지금 element의 opacity만큼 더 투명해진다
            std::string color_css(const ir::Color& color) const {
                ir::Color resolved = color;
                if (!color.scheme.empty()) {
                    const ir::Color scheme = theme_color(color.scheme);
                    resolved.r = scheme.r;
                    resolved.g = scheme.g;
                    resolved.b = scheme.b;
                }
                const double alpha = std::clamp(color.a * opacity_, 0.0, 1.0);
                char buffer[48];
                if (alpha >= 1) {
                    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", resolved.r, resolved.g, resolved.b);
                } else {
                    std::snprintf(buffer, sizeof buffer, "rgba(%d,%d,%d,%s)", resolved.r, resolved.g, resolved.b, format_number(alpha).c_str());
                }
                return buffer;
            }

            // DrawingML의 shade. 선형 RGB에 amount를 곱한다
            static ir::Color shade(ir::Color color, double amount) {
                for (int* channel : {&color.r, &color.g, &color.b}) {
                    *channel = static_cast<int>(std::lround(linear_to_srgb(srgb_to_linear(*channel / 255.0) * amount) * 255));
                }
                color.scheme.clear();
                return color;
            }

            // 지금 테마의 색. PowerPoint의 색 대응(tx1 = dk1, bg1 = lt1 등)도 함께 적는다
            Json theme_json() const {
                Object colors;
                for (const char* scheme : {"dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"}) {
                    colors.set(scheme, color_css(theme_color(scheme)));
                }
                for (const auto& [alias, scheme] : std::vector<std::pair<const char*, const char*>>{{"tx1", "dk1"}, {"bg1", "lt1"}, {"tx2", "dk2"}, {"bg2", "lt2"}}) {
                    colors.set(alias, color_css(theme_color(scheme)));
                }
                return colors;
            }

            Json solid(const ir::Color& color) const {
                return Object().set("k", "s").set("c", color_css(color));
            }

            // 색, linear(...), radial(...), pattern(...), image(...). 그라데이션 각도는 CSS의 linear-gradient 각도로 바꾼다
            Json paint_json(const ir::Value& value, const std::string& where) {
                if (const auto* color = std::get_if<ir::Color>(&value)) {
                    return solid(*color);
                }
                if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
                    return Object().set("k", "pat").set("p", pattern->kind).set("fg", color_css(pattern->foreground)).set("bg", color_css(pattern->background));
                }
                if (const auto* image = std::get_if<ir::Image>(&value)) {
                    const auto source = add_media(image->path, where, "image");
                    return source ? Json(Object().set("k", "img").set("src", *source).set("o", opacity_ < 1 ? Json(opacity_) : Json())) : Json();
                }
                const auto* gradient = std::get_if<ir::Gradient>(&value);
                if (gradient == nullptr || gradient->colors.size() < 2) {
                    return Json();
                }
                const auto positions = gradient_stops(*gradient);
                Array stops;
                for (std::size_t i = 0; i < gradient->colors.size(); ++i) {
                    stops.push(Array().push(positions[i]).push(color_css(gradient->colors[i])));
                }
                // sm: PowerPoint처럼 선형광에서 곡선으로 섞는 두 색 그라데이션
                const Json smooth = flag(smooth_gradient(*gradient));
                if (gradient->radial) {
                    return Object().set("k", "rad").set("st", stops).set("sm", smooth);
                }
                const double angle = scalar(gradient->angle) + (target_.angles == "css" ? 0 : 90);
                return Object().set("k", "lin").set("a", std::fmod(std::fmod(angle, 360) + 360, 360)).set("st", stops).set("sm", smooth);
            }

            // 선. style은 p:style이 주는 기본 선(색, 두께)이고, 없으면 선 속성을 하나라도 정했을 때만 선이 있다
            Json line_json(const ir::Element& element, const std::optional<std::pair<ir::Color, double>>& style, bool arrows) const {
                const auto* color = std::get_if<ir::Color>(find_property(element, "line_color"));
                const auto width = optional_number(element, "line_width");
                const std::string dash = enum_member(element, "line_dash");
                const std::string cap = enum_member(element, "line_cap");
                const std::string join = enum_member(element, "line_join");
                const std::string compound = enum_member(element, "line_compound");
                const std::string head = arrows ? enum_member(element, "start_arrow") : "";
                const std::string tail = arrows ? enum_member(element, "end_arrow") : "";
                const bool plain = color == nullptr && !width && dash.empty() && cap.empty() && join.empty() && compound.empty()
                    && (head.empty() || head == "none") && (tail.empty() || tail == "none");
                if (plain && !style) {
                    return Json();
                }
                const ir::Color line_color = color != nullptr ? *color : style ? style->first : theme_color("dk1");
                Object line;
                line.set("c", color_css(line_color)).set("w", width ? *width : style ? style->second : plain_line_width);
                if (!dash.empty() && dash != "solid") {
                    line.set("d", dash);
                }
                if (!cap.empty()) {
                    line.set("cap", cap);
                }
                if (!join.empty()) {
                    line.set("j", join);
                }
                if (!compound.empty() && compound != "single") {
                    line.set("cmpd", compound);
                }
                if (!head.empty() && head != "none") {
                    line.set("he", head);
                }
                if (!tail.empty() && tail != "none") {
                    line.set("te", tail);
                }
                return line;
            }

            // 도형 모양. radius는 짧은 변에 대한 비율로 첫 조정값이 되고, 반을 넘으면 반으로 줄인다
            Json geometry_json(const std::string& kind, const ir::Element& element, const Rect& rect) const {
                Object adjust;
                const auto defaults = shape_adjustments().find(kind);
                if (defaults != shape_adjustments().end()) {
                    for (std::size_t i = 0; i < defaults->second.size(); ++i) {
                        if (const auto value = optional_number(element, "adj" + std::to_string(i + 1))) {
                            adjust.set(defaults->second[i].first, static_cast<double>(std::llround(*value * 100000)));
                        } else if (i == 0) {
                            const auto radius = optional_number(element, "radius");
                            const double side = std::min(rect.width, rect.height);
                            if (radius && side > 0) {
                                adjust.set(defaults->second[i].first, static_cast<double>(std::min<long long>(50000, std::llround(100000 * *radius / side))));
                            }
                        }
                    }
                }
                return Object().set("p", kind).set("a", adjust.empty() ? Json() : Json(adjust));
            }

            // 그림자. 거리와 방향을 x, y 거리로 바꾼다
            Json shadow_json(const ir::Element& element, const std::string& prefix) const {
                const auto* color = std::get_if<ir::Color>(find_property(element, prefix));
                if (color == nullptr) {
                    return Json();
                }
                const double blur = optional_number(element, prefix + "_blur").value_or(4 * pt);
                const double distance = optional_number(element, prefix + "_distance").value_or(3 * pt);
                double angle = 45;
                if (const auto* number = std::get_if<ir::Number>(find_property(element, prefix + "_angle"))) {
                    angle = scalar(*number) - (target_.angles == "css" ? 90 : 0);
                }
                const double radians = angle * std::numbers::pi / 180;
                return Object().set("c", color_css(*color)).set("b", blur).set("dx", distance * std::cos(radians)).set("dy", distance * std::sin(radians));
            }

            // 네온, 그림자, 반사, 부드러운 가장자리, 3차원 회전
            Json effects_json(const ir::Element& element) const {
                Object effects;
                effects.set("sh", shadow_json(element, "shadow"));
                effects.set("ish", shadow_json(element, "inner_shadow"));
                if (const auto* glow = std::get_if<ir::Color>(find_property(element, "glow"))) {
                    effects.set("gl", Object().set("c", color_css(*glow)).set("r", optional_number(element, "glow_size").value_or(8 * pt)));
                }
                if (const auto soft = optional_number(element, "soft_edge")) {
                    effects.set("se", *soft);
                }
                if (const auto reflection = optional_number(element, "reflection")) {
                    effects.set("rf", Object()
                                          .set("a", *reflection * opacity_)
                                          .set("s", optional_number(element, "reflection_size").value_or(0.35))
                                          .set("d", optional_number(element, "reflection_distance").value_or(0))
                                          .set("b", optional_number(element, "reflection_blur").value_or(pt / 2)));
                }
                // PowerPoint의 X 회전은 CSS의 rotateY, Y 회전은 rotateX처럼 보인다
                const auto x = optional_number(element, "rotation_x");
                const auto y = optional_number(element, "rotation_y");
                if (target_.angles == "css") {
                    effects.set("rx", x ? Json(*x) : Json()).set("ry", y ? Json(*y) : Json());
                } else {
                    effects.set("rx", y ? Json(*y * css_rotate_x_sign) : Json()).set("ry", x ? Json(*x * css_rotate_y_sign) : Json());
                }
                if (const auto perspective = optional_number(element, "perspective"); perspective && *perspective > 0) {
                    effects.set("ps", *perspective);
                }
                return effects.empty() ? Json() : Json(effects);
            }

            Json link_json(const ir::Link& link) const {
                if (!link.url.empty()) {
                    return Object().set("url", link.url);
                }
                if (link.slide > 0) {
                    return Object().set("slide", link.slide);
                }
                return link.jump.empty() ? Json() : Json(Object().set("jump", link.jump));
            }

            // 실행 설정 하나. 링크는 link_json과 같고 run(...)은 {"run": 함수 이름, "a": 인자}, file(...)은 html에서 본 상대 주소다.
            // 소리는 "snd", 강조는 "hl"이다. program(...)과 macro(...)는 PowerPoint에만 있으므로 뺀다 (경고는 target_warnings가 낸다)
            Json action_json(const std::optional<ir::Action>& action, const std::string& sound, bool highlight, const std::string& where) {
                Object result = action_object(action);
                if (!sound.empty()) {
                    if (const auto source = add_media(sound, where, "sound")) {
                        result.set("snd", *source);
                    }
                }
                if (highlight) {
                    result.set("hl", 1);
                }
                return result.empty() ? Json() : Json(result);
            }

            Object action_object(const std::optional<ir::Action>& action) const {
                Object result;
                if (action && action->kind == "link") {
                    const ir::Link& link = action->link;
                    if (!link.url.empty()) {
                        result.set("url", link.url);
                    } else if (link.slide > 0) {
                        result.set("slide", link.slide);
                    } else if (!link.jump.empty()) {
                        result.set("jump", link.jump);
                    }
                } else if (action && action->kind == "run") {
                    Array arguments;
                    for (const auto& argument : action->arguments) {
                        if (const auto* value = std::get_if<bool>(&argument)) {
                            arguments.push(Json(*value));
                        } else if (const auto* number = std::get_if<double>(&argument)) {
                            arguments.push(Json(*number));
                        } else {
                            arguments.push(Json(std::get<std::string>(argument)));
                        }
                    }
                    result.set("run", action->target).set("a", arguments);
                } else if (action && action->kind == "file") {
                    result.set("url", file_address(action->target)).set("file", 1);
                }
                return result;
            }

            // file(...)의 경로(.tlide 기준)를 html 파일(web은 index.html)에서 본 주소로
            std::string file_address(const std::string& path) const {
                const std::filesystem::path file = resolve(path).lexically_normal();
                const std::filesystem::path output = resolve(target_.path).lexically_normal();
                // 절대 경로로 적었으면 file:/// 주소로 둔다
                const std::filesystem::path relative = utf8_path(path).is_absolute() ? std::filesystem::path() : file.lexically_relative(web_ ? output : output.parent_path());
                const std::string text = relative.empty() ? "file:///" + display(file.generic_u8string()) : display(relative.generic_u8string());
                std::string result;
                for (const char c : text) {
                    if (c == ' ') {
                        result += "%20";
                    } else if (c == '%') {
                        result += "%25";
                    } else if (c == '#') {
                        result += "%23";
                    } else if (c == '?') {
                        result += "%3F";
                    } else {
                        result += c;
                    }
                }
                return result;
            }

            // ---- 글자

            // CSS font-family. +mj, +mn은 테마의 제목, 본문 글꼴이고 기본 테마는 Calibri와 맑은 고딕이다
            std::string font_css(const std::string& family) const {
                std::string latin = family;
                std::string east_asian = family;
                if (family == "+mj" || family == "+mn") {
                    const std::string themed = theme_ == nullptr ? "" : family == "+mj" ? theme_->heading_font : theme_->body_font;
                    latin = themed.empty() ? (family == "+mj" ? "Calibri Light" : "Calibri") : themed;
                    east_asian = themed.empty() ? "Malgun Gothic" : themed;
                }
                // 없는 글꼴의 라틴 글자는 PowerPoint처럼 테마의 본문 글꼴로 그린다. 한글까지 가져가지 않게 templide.js가
                // "templide latin 글꼴"을 라틴 문자 범위만 가진 별칭으로 만든다.
                // 한글이 없는 글꼴이면 PowerPoint처럼 세리프 글꼴에 바탕을, 나머지에 맑은 고딕을 쓴다
                const bool serif = serif_font(latin);
                const std::string body = theme_ != nullptr && !theme_->body_font.empty() ? theme_->body_font : "Calibri";
                std::vector<std::string> families = {latin, east_asian, "templide latin " + body};
                if (serif) {
                    families.insert(families.end(), {"Batang", "\xEB\xB0\x94\xED\x83\x95", "AppleMyungjo", "Noto Serif KR", "Noto Serif CJK KR"});
                } else {
                    families.insert(families.end(), {"Malgun Gothic", "\xEB\xA7\x91\xEC\x9D\x80 \xEA\xB3\xA0\xEB\x94\x95", "Apple SD Gothic Neo", "Noto Sans KR", "Noto Sans CJK KR"});
                }
                std::string result;
                for (const auto& name : families) {
                    if (result.find(quote(name)) == std::string::npos) {
                        result += (result.empty() ? "" : ",") + quote(name);
                    }
                }
                return result + (serif ? ",serif" : ",sans-serif");
            }

            // 세리프 글꼴인지. 글꼴 파일을 읽을 수 없어 잘 알려진 이름과 이름 속 낱말로 가린다
            static bool serif_font(const std::string& family) {
                static const std::set<std::string> known = {
                    "georgia", "times new roman", "times", "cambria", "constantia", "book antiqua", "bookman old style", "garamond",
                    "palatino linotype", "palatino", "century", "century schoolbook", "baskerville old face", "baskerville", "bodoni mt",
                    "californian fb", "calisto mt", "centaur", "goudy old style", "high tower text", "perpetua", "rockwell", "bell mt",
                    "bernard mt condensed", "elephant", "footlight mt light", "lucida bright", "modern no. 20", "poor richard", "sylfaen",
                    "sitka text", "sitka small", "sitka heading", "sitka display", "sitka subheading", "sitka banner", "didot",
                    "hoefler text", "charter", "iowan old style", "merriweather", "playfair display", "lora", "pt serif", "libre baskerville",
                    "eb garamond", "crimson text", "cormorant garamond", "noto serif", "source serif pro", "source serif 4", "dm serif display",
                    "gentium", "minion pro", "adobe garamond pro", "caslon", "big caslon",
                };
                std::string name;
                for (const char c : family) {
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (known.contains(name)) {
                    return true;
                }
                if (name.find("sans") != std::string::npos) {
                    return false;
                }
                for (const char* word : {"serif", "times", "garamond", "roman", "bodoni", "baskerville", "caslon", "palatino", "antiqua", "myungjo", "batang"}) {
                    if (name.find(word) != std::string::npos) {
                        return true;
                    }
                }
                return false;
            }

            // 문단 단위 값은 그 값을 가진 첫 run의 것을 쓴다
            template <typename T>
            static const T* paragraph_value(const ir::Paragraph& paragraph, std::optional<T> ir::TextStyle::* member, const ir::TextStyle* inherited) {
                for (const auto& run : paragraph.runs) {
                    if (run.style.*member) {
                        return &*(run.style.*member);
                    }
                }
                return inherited != nullptr && inherited->*member ? &*(inherited->*member) : nullptr;
            }

            template <typename T>
            static const T* run_value(const ir::Run& run, std::optional<T> ir::TextStyle::* member, const ir::TextStyle* inherited) {
                if (run.style.*member) {
                    return &*(run.style.*member);
                }
                return inherited != nullptr && inherited->*member ? &*(inherited->*member) : nullptr;
            }

            // inherited는 개체 틀의 첫 수준 문단이 물려받는 모양이다
            Json run_json(const ir::Run& run, const TextDefaults& defaults, const ir::TextStyle* inherited) const {
                Object result;
                result.set("t", run.text);
                const auto* family = run_value(run, &ir::TextStyle::font_family, inherited);
                result.set("f", font_css(family != nullptr ? *family : defaults.font));
                const auto* size = run_value(run, &ir::TextStyle::font_size, inherited);
                result.set("sz", size != nullptr ? to_px(*size) : defaults.size);
                if (const auto* weight = run_value(run, &ir::TextStyle::font_weight, inherited); weight != nullptr && weight->member == "bold") {
                    result.set("b", 1);
                }
                if (const auto* style = run_value(run, &ir::TextStyle::font_style, inherited); style != nullptr && style->member == "italic") {
                    result.set("i", 1);
                }
                // 누를 때 하는 일이 있는 글자는 링크처럼 보인다
                const auto* link = run_value(run, &ir::TextStyle::link, inherited);
                const auto* action = run_value(run, &ir::TextStyle::action, inherited);
                const bool linked = link != nullptr || action != nullptr;
                if (const auto* decoration = run_value(run, &ir::TextStyle::text_decoration, inherited); decoration != nullptr) {
                    static const std::map<std::string, std::pair<const char*, const char*>> decorations = {
                        {"underline", {"u", "sng"}}, {"double_underline", {"u", "dbl"}}, {"wavy_underline", {"u", "wavy"}},
                        {"line_through", {"st", "sng"}}, {"double_line_through", {"st", "dbl"}},
                    };
                    if (const auto it = decorations.find(decoration->member); it != decorations.end()) {
                        result.set(it->second.first, it->second.second);
                    }
                } else if (linked) {
                    result.set("u", "sng");
                }
                if (const auto* transform = run_value(run, &ir::TextStyle::text_transform, inherited); transform != nullptr && transform->member != "none") {
                    result.set("cap", transform->member == "uppercase" ? "all" : "small");
                }
                if (const auto* spacing = run_value(run, &ir::TextStyle::letter_spacing, inherited)) {
                    result.set("sp", to_px(*spacing));
                }
                if (const auto* align = run_value(run, &ir::TextStyle::vertical_align, inherited); align != nullptr && align->member != "baseline") {
                    result.set("bl", align->member == "super" ? 30 : -25);
                }
                // 링크는 테마의 하이퍼링크 색이다
                const auto* color = run_value(run, &ir::TextStyle::color, inherited);
                result.set("c", color_css(linked ? theme_color("hlink") : color != nullptr ? *color : defaults.color));
                if (const auto* highlight = run_value(run, &ir::TextStyle::highlight, inherited)) {
                    result.set("hl", color_css(*highlight));
                }
                if (link != nullptr) {
                    result.set("k", link_json(*link));
                } else if (action != nullptr) {
                    const Object click = action_object(*action);
                    result.set("k", click.empty() ? Json() : Json(click));
                }
                if (const auto* hover = run_value(run, &ir::TextStyle::hover_action, inherited)) {
                    const Object object = action_object(*hover);
                    result.set("kh", object.empty() ? Json() : Json(object));
                }
                if (!run.field.empty()) {
                    result.set("fld", run.field);
                }
                return result;
            }

            Json paragraph_json(const ir::Paragraph& paragraph, const TextDefaults& defaults, const ir::TextStyle* inherited, const std::string& default_align) const {
                const ir::TextStyle* level = paragraph.level == 0 ? inherited : nullptr;
                Object result;
                static const std::map<std::string, std::string> aligns = {{"left", "l"}, {"center", "ctr"}, {"right", "r"}, {"justify", "just"}};
                const auto* align = paragraph_value(paragraph, &ir::TextStyle::text_align, level);
                const std::string align_code = align != nullptr ? aligns.at(align->member) : default_align;
                if (!align_code.empty() && align_code != "l") {
                    result.set("al", align_code);
                }
                if (const auto* height = paragraph_value(paragraph, &ir::TextStyle::line_height, level)) {
                    result.set("lh", scalar(*height));
                }
                if (const auto* before = paragraph_value(paragraph, &ir::TextStyle::space_before, level)) {
                    result.set("sb", to_px(*before));
                }
                if (const auto* after = paragraph_value(paragraph, &ir::TextStyle::space_after, level)) {
                    result.set("sa", to_px(*after));
                }
                std::optional<double> margin;
                std::optional<double> indent;
                if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::margin_left, level)) {
                    margin = to_px(*value);
                }
                if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::text_indent, level)) {
                    indent = to_px(*value);
                }
                if (paragraph.list != ir::ListKind::NONE) {
                    const int depth = std::clamp(paragraph.level, 0, 8);
                    margin = margin.value_or(list_indent * (depth + 1));
                    indent = indent.value_or(-list_indent);
                    result.set("lvl", depth);
                    Object bullet;
                    if (paragraph.list == ir::ListKind::NUMBERS) {
                        static const std::map<std::string, std::string> styles = {
                            {"decimal", "arabicPeriod"}, {"lower_alpha", "alphaLcPeriod"}, {"upper_alpha", "alphaUcPeriod"},
                            {"lower_roman", "romanLcPeriod"}, {"upper_roman", "romanUcPeriod"}, {"circled", "circleNumDbPlain"},
                        };
                        const auto* style = paragraph_value(paragraph, &ir::TextStyle::list_style, level);
                        bullet.set("n", style != nullptr ? styles.at(style->member) : "arabicPeriod");
                        if (const auto* start = paragraph_value(paragraph, &ir::TextStyle::list_start, level)) {
                            bullet.set("s", static_cast<long long>(std::llround(scalar(*start))));
                        }
                    } else {
                        const auto* marker = paragraph_value(paragraph, &ir::TextStyle::list_marker, level);
                        bullet.set("ch", marker != nullptr ? *marker : paragraph.list == ir::ListKind::BULLETS ? "\xE2\x80\xA2" : "\xE2\x80\x93");
                    }
                    if (const auto* color = paragraph_value(paragraph, &ir::TextStyle::list_marker_color, level)) {
                        bullet.set("c", color_css(*color));
                    }
                    result.set("bu", bullet);
                }
                if (margin) {
                    result.set("ml", *margin);
                }
                if (indent) {
                    result.set("ind", *indent);
                }
                Array runs;
                for (const auto& run : paragraph.runs) {
                    runs.push(run_json(run, defaults, level));
                }
                result.set("rs", runs);
                // 빈 문단의 높이
                if (paragraph.runs.empty()) {
                    const auto* size = level != nullptr && level->font_size ? &*level->font_size : nullptr;
                    result.set("sz", size != nullptr ? to_px(*size) : defaults.size);
                }
                return result;
            }

            // 글상자의 여백, 줄 바꿈, 세로 맞춤, 방향, 단, 자동 맞춤
            Object body_json(const ir::Element& element, const std::string& default_anchor) const {
                Object body;
                const auto all = optional_number(element, "padding");
                Array insets;
                const std::array<const char*, 4> names = {"padding_left", "padding_top", "padding_right", "padding_bottom"};
                for (std::size_t i = 0; i < names.size(); ++i) {
                    insets.push(optional_number(element, names[i]).value_or(all.value_or(default_insets[i])));
                }
                body.set("ins", insets);
                if (const auto* wrap = std::get_if<bool>(find_property(element, "wrap")); wrap != nullptr && !*wrap) {
                    body.set("nowrap", 1);
                }
                static const std::map<std::string, std::string> anchors = {{"top", "t"}, {"middle", "ctr"}, {"bottom", "b"}};
                const auto anchor = anchors.find(enum_member(element, "anchor"));
                body.set("an", anchor != anchors.end() ? anchor->second : default_anchor);
                static const std::map<std::string, std::string> directions = {
                    {"vertical", "vert"}, {"vertical270", "vert270"}, {"stacked", "wordArtVert"}, {"east_asian", "eaVert"},
                };
                if (const auto it = directions.find(enum_member(element, "text_direction")); it != directions.end()) {
                    body.set("vert", it->second);
                }
                if (const auto columns = optional_number(element, "columns"); columns && *columns > 1) {
                    body.set("cols", static_cast<long long>(std::llround(*columns)));
                    body.set("gap", optional_number(element, "column_gap").value_or(0));
                }
                const std::string autofit = enum_member(element, "autofit");
                if (autofit == "shrink" || autofit == "resize") {
                    body.set("fit", autofit);
                }
                return body;
            }

            Json text_json(const ir::Text& text, Object body, const TextDefaults& defaults, const std::string& default_align, const ir::TextStyle* inherited = nullptr) const {
                Array paragraphs;
                for (const auto& paragraph : text.paragraphs) {
                    paragraphs.push(paragraph_json(paragraph, defaults, inherited, default_align));
                }
                body.set("ps", paragraphs);
                return body;
            }

            // ---- 미디어

            // html은 data URI를, web은 media 폴더 안의 주소를 돌려준다. 같은 파일은 한 번만 넣는다
            std::optional<std::string> add_media(const std::string& path, const std::string& where, const std::string& kind) {
                static const std::map<std::string, std::string> images = {{".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".bmp", "image/bmp"}};
                static const std::map<std::string, std::string> sounds = {{".wav", "audio/wav"}};
                static const std::map<std::string, std::string> videos = {{".mp4", "video/mp4"}, {".webm", "video/webm"}};
                static const std::map<std::string, std::string> audios = {{".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".m4a", "audio/mp4"}};
                static const std::map<std::string, std::pair<const std::map<std::string, std::string>*, std::string>> kinds = {
                    {"image", {&images, "png, jpg, gif, bmp"}}, {"sound", {&sounds, "wav"}}, {"video", {&videos, "mp4, webm"}}, {"audio", {&audios, "mp3, wav, m4a"}},
                };
                const std::filesystem::path file = resolve(path);
                std::error_code error_code;
                std::filesystem::path key = std::filesystem::weakly_canonical(file, error_code);
                if (error_code) {
                    key = file;
                }
                if (const auto it = media_.find(key); it != media_.end()) {
                    return it->second;
                }
                std::string extension = display(file.extension());
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto& [types, supported] = kinds.at(kind);
                const auto type = types->find(extension);
                if (type == types->end()) {
                    error(where + ": unsupported " + kind + " format '" + extension + "' (" + supported + ")");
                    return std::nullopt;
                }
                // 편집기는 비디오와 오디오를 덱에 넣지 않고 파일 경로를 받아 직접 읽는다
                if (editor_ && (kind == "video" || kind == "audio")) {
                    if (!std::filesystem::exists(file)) {
                        error(where + ": cannot open " + kind + " file " + display(file));
                        return std::nullopt;
                    }
                    const std::string address = "file:" + display(std::filesystem::absolute(file));
                    media_.emplace(key, address);
                    return address;
                }
                auto bytes = read_file(file);
                if (!bytes) {
                    error(where + ": cannot open " + kind + " file " + display(file));
                    return std::nullopt;
                }
                std::string address;
                if (web_) {
                    const auto number = std::count_if(media_files_.begin(), media_files_.end(), [&](const auto& file) { return file.first.starts_with("media/" + kind); }) + 1;
                    address = "media/" + kind + std::to_string(number) + extension;
                    media_files_.emplace_back(address, std::move(*bytes));
                } else {
                    address = "data:" + type->second + ";base64," + base64(*bytes);
                }
                media_.emplace(key, address);
                return address;
            }

            std::optional<std::pair<int, int>> image_size(const std::string& path) const {
                const auto bytes = read_file(resolve(path));
                return bytes ? raster::image_size(*bytes) : std::nullopt;
            }

            // ---- 개체

            // slide나 layout 하나를 만들기 시작한다. 이름을 붙인 element를 찾을 수 있게 한다
            void begin_tree(bool layout, const std::vector<ir::Element>& elements) {
                in_layout_ = layout;
                ids_.clear();
                named_.clear();
                assign_ids(elements);
            }

            void assign_ids(const std::vector<ir::Element>& elements) {
                for (const auto& element : elements) {
                    ids_[&element] = static_cast<int>(ids_.size()) + 1;
                    if (!element.name.empty()) {
                        named_[element.name] = &element;
                    }
                    assign_ids(element.children);
                }
            }

            Array elements_json(const std::vector<ir::Element>& elements, const std::string& where) {
                Array result;
                for (const auto& element : elements) {
                    if (const Json json = element_json(element, where); !json.is_null()) {
                        result.push(json);
                    }
                }
                return result;
            }

            // PowerPoint에서만 보이는 3차원 입체
            void check_supported(const ir::Element& element, const std::string& where) {
                for (const char* name : {"bevel", "depth"}) {
                    if (find_property(element, name) != nullptr) {
                        error(where + ": the html backend cannot draw '" + name + "'; 3D bevels and extrusions only render in PowerPoint");
                    }
                }
            }

            // 모든 개체의 위치, 회전, 뒤집기, 링크와 실행 설정
            void common_json(Object& result, const ir::Element& element, const Rect& rect) {
                result.set("x", rect.x).set("y", rect.y).set("w", rect.width).set("h", rect.height);
                if (const auto rotation = optional_number(element, "rotation")) {
                    const double degrees = std::fmod(std::fmod(*rotation, 360) + 360, 360);
                    result.set("r", degrees == 0 ? Json() : Json(degrees));
                }
                const std::string flip = enum_member(element, "flip");
                result.set("fh", flag(flip == "horizontal" || flip == "both")).set("fv", flag(flip == "vertical" || flip == "both"));
                const auto* link = std::get_if<ir::Link>(find_property(element, "link"));
                const auto* sound = std::get_if<std::string>(find_property(element, "action_sound"));
                const auto* highlight = std::get_if<bool>(find_property(element, "action_highlight"));
                std::optional<ir::Action> click;
                if (link != nullptr) {
                    click = ir::Action{"link", *link, "", {}, ""};
                } else if (const auto* action = std::get_if<ir::Action>(find_property(element, "action"))) {
                    click = *action;
                }
                result.set("k", action_json(click, sound != nullptr ? *sound : "", highlight != nullptr && *highlight, where_ + ", action_sound"));
                std::optional<ir::Action> hover;
                if (const auto* action = std::get_if<ir::Action>(find_property(element, "hover_action"))) {
                    hover = *action;
                }
                sound = std::get_if<std::string>(find_property(element, "hover_sound"));
                highlight = std::get_if<bool>(find_property(element, "hover_highlight"));
                result.set("kh", action_json(hover, sound != nullptr ? *sound : "", highlight != nullptr && *highlight, where_ + ", hover_sound"));
                result.set("fx", effects_json(element));
            }

            Json fill_json(const ir::Element& element, const std::string& where) {
                const auto* fill = find_property(element, "fill");
                return fill != nullptr ? paint_json(*fill, where + ", fill") : Json();
            }

            // 도형의 기본 선. PowerPoint에서 새로 넣은 도형처럼 accent1을 50% 어둡게 한 1pt 선이다
            std::pair<ir::Color, double> shape_line() const {
                return {shade(theme_color("accent1"), 0.5), shape_line_width};
            }

            Json element_json(const ir::Element& element, const std::string& where) {
                const std::string element_where = where + ", " + element.object + (element.name.empty() || element.generated_name ? "" : " " + element.name);
                where_ = element_where;
                Object result;
                if (!in_layout_) {
                    result.set("id", ids_.at(&element));
                    if (!element.name.empty() && !element.generated_name) {
                        result.set("n", element.name);
                    }
                    if (editor_ && !element.id.empty()) {
                        result.set("eid", element.id);
                    }
                }
                if (element.object == "group") {
                    return result.set("t", "grp").set("c", elements_json(element.children, element_where));
                }
                if (element.object == "placeholder") {
                    return Json(); // layout의 개체 틀은 slide가 채울 때만 보인다
                }
                check_supported(element, element_where);
                opacity_ = optional_number(element, "opacity").value_or(1);
                const TextDefaults text_defaults{18 * pt, theme_color(element.object == "shape" ? "lt1" : "dk1"), "+mn"};
                Json json;
                if (element.object == "text_box" || element.object == "shape") {
                    const bool text_box = element.object == "text_box";
                    const auto rect = element_rect(element, element_where);
                    const auto* text = std::get_if<ir::Text>(find_property(element, "text"));
                    if (rect && text != nullptr) {
                        common_json(result, element, *rect);
                        const std::string kind = text_box ? "rect" : enum_member(element, "kind");
                        const Json fill = fill_json(element, element_where);
                        result.set("t", text_box ? "tb" : "sp")
                            .set("g", geometry_json(kind, element, *rect))
                            .set("f", !fill.is_null() || text_box ? fill : solid(theme_color("accent1")))
                            .set("l", line_json(element, text_box ? std::nullopt : std::optional(shape_line()), false))
                            .set("tx", text_json(*text, body_json(element, text_box ? "t" : "ctr"), text_defaults, text_box ? "l" : "ctr"));
                        json = result;
                    }
                } else if (element.object == "image") {
                    json = image_json(result, element, element_where);
                } else if (element.object == "line") {
                    const Rect rect = line_rect(element);
                    common_json(result, element, rect);
                    result.set("t", "ln")
                        .set("fh", flag(length(element, "x2", width_) < length(element, "x1", width_)))
                        .set("fv", flag(length(element, "y2", height_) < length(element, "y1", height_)))
                        .set("g", Object().set("p", "line"))
                        .set("l", line_json(element, std::pair{theme_color("accent1"), line_line_width}, true));
                    json = result;
                } else if (element.object == "connector") {
                    json = connector_json(result, element);
                } else if (element.object == "freeform") {
                    if (const auto shape = freeform_shape(element)) {
                        const Rect& rect = shape->first;
                        common_json(result, element, rect);
                        std::string path;
                        const auto point = [&](const Point& p) { return format_number(p.x - shape->second.min.x) + " " + format_number(p.y - shape->second.min.y); };
                        for (const auto& segment : shape->second.segments) {
                            path += (path.empty() ? "" : " ") + std::string(1, segment.command);
                            for (const auto& p : segment.points) {
                                path += " " + point(p);
                            }
                        }
                        const Json fill = fill_json(element, element_where);
                        result.set("t", "ff")
                            .set("g", Object().set("d", path))
                            .set("f", !fill.is_null() ? fill : solid(theme_color("accent1")))
                            .set("l", line_json(element, shape_line(), false));
                        json = result;
                    }
                } else if (element.object == "video" || element.object == "audio") {
                    json = media_json(result, element, element_where);
                } else if (element.object == "backdrop") {
                    const auto rect = element_rect(element, element_where);
                    if (rect) {
                        common_json(result, element, *rect);
                        const std::string kind = enum_member(element, "kind");
                        result.set("t", "bd")
                            .set("g", geometry_json(kind.empty() ? "rect" : kind, element, *rect))
                            .set("blur", optional_number(element, "blur").value_or(0))
                            .set("o", opacity_ < 1 ? Json(opacity_) : Json())
                            .set("l", line_json(element, std::nullopt, false));
                        json = result;
                    }
                } else {
                    error(element_where + ": the html backend does not support this object");
                }
                opacity_ = 1;
                return json;
            }

            // 그림. fit과 crop_*은 pptx와 같이 틀과 자르기 비율로 바꾼다
            Json image_json(Object& result, const ir::Element& element, const std::string& where) {
                const auto rect = element_rect(element, where);
                const auto* path = std::get_if<std::string>(find_property(element, "path"));
                if (path == nullptr) {
                    return Json();
                }
                std::array<double, 4> crop = {0, 0, 0, 0};
                const std::array<const char*, 4> crop_names = {"crop_left", "crop_top", "crop_right", "crop_bottom"};
                for (std::size_t i = 0; i < crop.size(); ++i) {
                    if (const auto* number = std::get_if<ir::Number>(find_property(element, crop_names[i]))) {
                        crop[i] = scalar(*number) / 100;
                    }
                }
                const auto source = add_media(*path, where, "image");
                if (!rect || !source) {
                    return Json();
                }
                const std::string fit = enum_member(element, "fit");
                const ImageFit fitted = fit_image({rect->x, rect->y, rect->width, rect->height, crop}, fit, fit == "cover" || fit == "contain" ? image_size(*path) : std::nullopt);
                common_json(result, element, Rect{fitted.x, fitted.y, fitted.width, fitted.height});
                const std::string kind = enum_member(element, "kind");
                Array crops;
                for (const double value : fitted.crop) {
                    crops.push(value);
                }
                result.set("t", "pic")
                    .set("src", *source)
                    .set("crop", std::any_of(fitted.crop.begin(), fitted.crop.end(), [](double value) { return value != 0; }) ? Json(crops) : Json())
                    .set("o", opacity_ < 1 ? Json(opacity_) : Json())
                    .set("g", geometry_json(kind.empty() ? "rect" : kind, element, Rect{fitted.x, fitted.y, fitted.width, fitted.height}))
                    .set("f", fill_json(element, where))
                    .set("l", line_json(element, std::nullopt, false));
                return result;
            }

            // 비디오와 오디오. len은 잘라 낸 뒤의 재생 길이(ms, 알 수 없으면 0)로 뒤에 이어지는 애니메이션의 시작에 쓴다
            Json media_json(Object& result, const ir::Element& element, const std::string& where) {
                const bool video = element.object == "video";
                const auto rect = element_rect(element, where);
                const auto* path = std::get_if<std::string>(find_property(element, "path"));
                if (!rect || path == nullptr) {
                    return Json();
                }
                const auto source = add_media(*path, where, video ? "video" : "audio");
                if (!source) {
                    return Json();
                }
                Json poster;
                if (const auto* file = std::get_if<std::string>(find_property(element, "poster")); file != nullptr && !file->empty()) {
                    if (const auto image = add_media(*file, where + ", poster", "image")) {
                        poster = Json(*image);
                    }
                }
                const auto duration_of = [&](const char* name) {
                    const auto* number = std::get_if<ir::Number>(find_property(element, name));
                    return number != nullptr ? std::llround(milliseconds(*number)) : 0LL;
                };
                const auto flag_of = [&](const char* name) {
                    const auto* value = std::get_if<bool>(find_property(element, name));
                    return flag(value != nullptr && *value);
                };
                const long long trim_start = duration_of("trim_start");
                const long long trim_end = duration_of("trim_end");
                long long length = 0;
                if (const auto bytes = read_file(resolve(*path))) {
                    length = std::max(0LL, media_duration(*bytes, display(resolve(*path).extension())) - trim_start - trim_end);
                }
                common_json(result, element, *rect);
                const auto* volume_number = std::get_if<ir::Number>(find_property(element, "volume"));
                const std::optional<double> volume = volume_number != nullptr ? std::optional(scalar(*volume_number)) : std::nullopt;
                result.set("t", video ? "vid" : "aud")
                    .set("src", *source)
                    .set("poster", poster)
                    .set("start", enum_member(element, "start"))
                    .set("len", length)
                    .set("vol", volume ? Json(*volume / 100) : Json())
                    .set("ts", trim_start > 0 ? Json(trim_start) : Json())
                    .set("te", trim_end > 0 ? Json(trim_end) : Json())
                    .set("fi", duration_of("fade_in") > 0 ? Json(duration_of("fade_in")) : Json())
                    .set("fo", duration_of("fade_out") > 0 ? Json(duration_of("fade_out")) : Json())
                    .set("loop", flag_of("loop"))
                    .set("rew", flag_of("rewind"))
                    .set("full", video ? flag_of("fullscreen") : Json())
                    .set("hide", flag_of(video ? "hide_when_stopped" : "hide_icon"))
                    .set("across", video ? Json() : flag_of("across_slides"))
                    .set("o", opacity_ < 1 ? Json(opacity_) : Json())
                    .set("l", line_json(element, std::nullopt, false));
                return result;
            }

            // 연결선이 붙는 개체의 모양과 자리. 대상은 미들 엔드가 검사했다
            std::optional<Anchor> connector_anchor(const ir::Element& connector, const std::string& property) {
                const auto* name = std::get_if<std::string>(find_property(connector, property));
                const auto it = name != nullptr ? named_.find(*name) : named_.end();
                if (it == named_.end()) {
                    return std::nullopt;
                }
                const ir::Element& target = *it->second;
                const std::size_t error_count = errors_.size();
                std::optional<Rect> rect;
                if (target.object == "freeform") {
                    if (const auto shape = freeform_shape(target)) {
                        rect = shape->first;
                    }
                } else {
                    rect = element_rect(target, "");
                }
                errors_.resize(error_count);
                if (!rect) {
                    return std::nullopt;
                }
                const bool has_kind = target.object == "shape" || target.object == "backdrop" || target.object == "image";
                const std::string kind = has_kind ? enum_member(target, "kind") : "rect";
                const std::string flip = enum_member(target, "flip");
                return Anchor{kind.empty() ? "rect" : kind, rect->x, rect->y, rect->width, rect->height,
                              optional_number(target, "rotation").value_or(0) * std::numbers::pi / 180,
                              flip == "horizontal" || flip == "both", flip == "vertical" || flip == "both"};
            }

            Json connector_json(Object& result, const ir::Element& element) {
                const auto from = connector_anchor(element, "from");
                const auto to = connector_anchor(element, "to");
                if (!from || !to) {
                    return Json();
                }
                const auto plan = plan_connector(*from, *to, enum_member(element, "from_side"), enum_member(element, "to_side"), enum_member(element, "kind"), connector_margin);
                if (!plan) {
                    return Json();
                }
                const ConnectorGeometry& geometry = plan->geometry;
                common_json(result, element, Rect{geometry.x, geometry.y, geometry.width, geometry.height});
                Object adjust;
                for (std::size_t i = 0; i < geometry.adjust.size(); ++i) {
                    adjust.set("adj" + std::to_string(i + 1), static_cast<double>(geometry.adjust[i]));
                }
                return result.set("t", "cxn")
                    .set("r", geometry.rotation != 0 ? Json(geometry.rotation) : Json())
                    .set("fh", flag(geometry.flip_h))
                    .set("fv", flag(geometry.flip_v))
                    .set("g", Object().set("p", geometry.preset).set("a", adjust.empty() ? Json() : Json(adjust)))
                    .set("l", line_json(element, std::pair{theme_color("accent1"), line_line_width}, true));
            }

            // ---- slide

            static const ir::Element* find_placeholder(const std::vector<ir::Element>& elements, const std::string& role) {
                for (const auto& element : elements) {
                    if (element.object == "placeholder" && enum_member(element, "role") == role) {
                        return &element;
                    }
                    if (const auto* found = find_placeholder(element.children, role)) {
                        return found;
                    }
                }
                return nullptr;
            }

            // slide의 title, subtitle, body. layout 개체 틀의 자리와 모양에 내용만 채운다.
            // 첫 수준 문단은 개체 틀 안내 글의 첫 run 모양을, 나머지는 master의 글자 모양(제목 44pt, 본문 28pt)을 따른다
            Json slide_placeholder_json(const std::string& role, const ir::Text& text, const ir::Layout& layout, int id, const std::string& where) {
                const ir::Element* placeholder = find_placeholder(layout.elements, role);
                if (placeholder == nullptr) {
                    return Json();
                }
                opacity_ = optional_number(*placeholder, "opacity").value_or(1);
                Object result;
                result.set("id", id);
                const auto rect = element_rect(*placeholder, where + ", " + role);
                Json json;
                if (rect) {
                    const ir::TextStyle* inherited = nullptr;
                    if (const auto* prompt = std::get_if<ir::Text>(find_property(*placeholder, "text")); prompt != nullptr && !prompt->paragraphs.empty() && !prompt->paragraphs.front().runs.empty()) {
                        inherited = &prompt->paragraphs.front().runs.front().style;
                    }
                    const TextDefaults defaults{role == "title" ? 44 * pt : 28 * pt, theme_color("dk1"), role == "title" ? "+mj" : "+mn"};
                    where_ = where + ", " + role;
                    common_json(result, *placeholder, *rect);
                    result.set("t", "tb")
                        .set("g", Object().set("p", "rect"))
                        .set("f", fill_json(*placeholder, where + ", " + role))
                        .set("l", line_json(*placeholder, std::nullopt, false))
                        .set("tx", text_json(text, body_json(*placeholder, "t"), defaults, "l", inherited));
                    json = result;
                }
                opacity_ = 1;
                return json;
            }

            // 이동 경로를 슬라이드 크기에 대한 비율로. PowerPoint처럼 M, L, C, Z와 끝의 E를 쓴다
            std::string motion_path(const std::string& text) const {
                std::string message;
                const auto path = parse_svg_path(text, message);
                if (!path) {
                    return "";
                }
                std::string result;
                for (const auto& segment : path->segments) {
                    result += (result.empty() ? "" : " ") + std::string(1, segment.command);
                    for (const auto& point : segment.points) {
                        result += " " + format_fraction(point.x / width_) + " " + format_fraction(point.y / height_);
                    }
                }
                return result + " E";
            }

            Json slide_json(const ir::Slide& slide, const ir::Layout* layout, std::optional<std::size_t> layout_number, std::size_t theme) {
                const std::string where = "slide " + std::to_string(slide.page);
                begin_tree(false, slide.elements);
                Object result;
                result.set("num", slide.page).set("th", theme);
                if (layout_number) {
                    result.set("lay", *layout_number);
                }
                // 배경은 slide, layout, 테마의 lt1 순서로 찾는다
                const ir::Value* background = slide.background ? &*slide.background : layout != nullptr && layout->background ? &*layout->background : nullptr;
                result.set("bg", background != nullptr ? paint_json(*background, where + ", background") : solid(theme_color("lt1")));
                Array elements = elements_json(slide.elements, where);
                int next_id = static_cast<int>(ids_.size()) + 1;
                if (layout != nullptr) {
                    for (const auto& [role, text] : slide.placeholders) {
                        if (const Json json = slide_placeholder_json(role, text, *layout, next_id++, where); !json.is_null()) {
                            elements.push(json);
                        }
                    }
                }
                result.set("els", elements);
                result.set("hid", flag(slide.hidden));
                if (slide.advance_after) {
                    result.set("adv", milliseconds(*slide.advance_after));
                }
                if (slide.transition) {
                    const ir::Transition& transition = *slide.transition;
                    const std::string name = transition.kind + (transition.option.empty() ? "" : "." + transition.option);
                    if (unsupported_transitions.contains(transition.kind) || unsupported_transitions.contains(name)) {
                        error(where + ": the html backend does not support transition " + name + "; it only renders in PowerPoint");
                    }
                    result.set("tr", Object()
                                         .set("k", transition.kind)
                                         .set("o", transition.option.empty() ? Json() : Json(transition.option))
                                         .set("d", transition.duration ? milliseconds(*transition.duration) : static_cast<double>(default_transition_ms)));
                }
                if (slide.transition_sound) {
                    if (const auto sound = add_media(*slide.transition_sound, where + ", transition_sound", "sound")) {
                        result.set("snd", *sound);
                    }
                }
                if (slide.section) {
                    result.set("sec", *slide.section);
                }
                if (!slide.notes.paragraphs.empty()) {
                    const TextDefaults defaults{12 * pt, theme_color("dk1"), "+mn"};
                    Array notes;
                    for (const auto& paragraph : slide.notes.paragraphs) {
                        notes.push(paragraph_json(paragraph, defaults, nullptr, "l"));
                    }
                    result.set("notes", notes);
                }
                Array animations;
                for (const auto& animation : slide.animations) {
                    const auto target = named_.find(animation.target);
                    if (target == named_.end()) {
                        continue;
                    }
                    Object item;
                    item.set("el", ids_.at(target->second))
                        .set("key", !animation.path.empty() ? std::string("move.right")
                                                           : animation.category + "." + animation.effect + (animation.option.empty() ? "" : "." + animation.option))
                        .set("st", animation.start)
                        .set("d", animation.duration ? Json(milliseconds(*animation.duration)) : Json())
                        .set("dl", animation.delay ? Json(milliseconds(*animation.delay)) : Json());
                    if (!animation.path.empty()) {
                        item.set("path", motion_path(animation.path));
                    }
                    animations.push(item);
                }
                if (!animations.empty()) {
                    result.set("an", animations);
                }
                Array reviews;
                for (const auto& review : slide.reviews) {
                    reviews.push(Object().set("tx", review.text).set("au", review.author).set("x", to_px(review.x, width_)).set("y", to_px(review.y, height_)));
                }
                if (!reviews.empty()) {
                    result.set("rv", reviews);
                }
                return result;
            }

            // ---- 파일

            std::optional<std::string> read_lib(const std::string& name) {
                const std::filesystem::path file = libs_dir_ / utf8_path(name);
                auto bytes = read_file(file);
                if (!bytes) {
                    error("cannot read " + display(file) + "; the html backend needs the libs folder next to templide.exe");
                }
                return bytes;
            }

            bool write_file(const std::filesystem::path& file, const std::string& content) {
                std::ofstream output(file, std::ios::binary);
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
                if (!output) {
                    error("cannot write " + display(file));
                    return false;
                }
                return true;
            }

            // html은 모든 것을 넣은 파일 하나를, web은 path 폴더에 index.html과 라이브러리, media를 쓴다
            void save(const std::string& deck) {
                const std::array<std::string, 4> libs = {"templide.js", "reveal/reveal.js", "reveal/reveal.css", "reveal/plugin/notes.js"};
                std::array<std::string, 4> code;
                for (std::size_t i = 0; i < libs.size(); ++i) {
                    auto bytes = read_lib(libs[i]);
                    if (!bytes) {
                        return;
                    }
                    code[i] = std::move(*bytes);
                }
                const auto script = [&](std::size_t index) {
                    return web_ ? "<script src=\"" + libs[index] + "\"></script>\n" : "<script>" + inline_script(code[index]) + "</script>\n";
                };
                // run(...) 동작이 부르는 함수가 든 사용자 script. 덱을 띄우기 전에 읽는다. web은 scripts 폴더에 복사한다
                std::string user_script;
                std::string user_script_name;
                if (!target_.script.empty()) {
                    const std::filesystem::path file = resolve(target_.script);
                    auto bytes = read_file(file);
                    if (!bytes) {
                        error("script: cannot open " + display(file));
                        return;
                    }
                    user_script = std::move(*bytes);
                    user_script_name = "scripts/" + display(file.filename());
                }
                const std::string user_script_tag = target_.script.empty() ? ""
                    : web_ ? "<script src=\"" + escape_html(user_script_name) + "\"></script>\n" : "<script>" + inline_script(user_script) + "</script>\n";
                const std::string title = target_.title.empty() ? target_.name : target_.title;
                const std::string author = target_.author.empty() ? "templide" : target_.author;
                // JSON의 '<'는 모두 <로 바뀌어 있다
                const std::string page = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<meta name=\"generator\" content=\"templide\">\n"
                    "<meta name=\"author\" content=\"" + escape_html(author) + "\">\n"
                    "<title>" + escape_html(title) + "</title>\n"
                    + (web_ ? "<link rel=\"stylesheet\" href=\"reveal/reveal.css\">\n" : "<style>" + code[2] + "</style>\n")
                    + "</head>\n<body>\n<div class=\"reveal\"><div class=\"slides\"></div></div>\n"
                    + script(1) + script(3) + script(0) + user_script_tag
                    + "<script type=\"application/json\" id=\"templide-deck\">" + deck + "</script>\n"
                    "<script>Templide.mount(document.querySelector(\".reveal\"), JSON.parse(document.getElementById(\"templide-deck\").textContent));</script>\n"
                    "</body>\n</html>\n";
                const std::filesystem::path output = resolve(target_.path);
                if (!web_) {
                    write_file(output, page);
                    return;
                }
                std::error_code error_code;
                for (const auto& folder : {output, output / "reveal" / "plugin", output / "media"}) {
                    std::filesystem::create_directories(folder, error_code);
                    if (error_code) {
                        error("cannot create folder " + display(folder));
                        return;
                    }
                }
                if (!write_file(output / "index.html", page)) {
                    return;
                }
                for (std::size_t i = 0; i < libs.size(); ++i) {
                    if (!write_file(output / utf8_path(libs[i]), code[i])) {
                        return;
                    }
                }
                if (const auto license = read_lib("reveal/LICENSE")) {
                    write_file(output / "reveal" / "LICENSE", *license);
                }
                for (const auto& [name, bytes] : media_files_) {
                    if (!write_file(output / utf8_path(name), bytes)) {
                        return;
                    }
                }
                if (!user_script_name.empty()) {
                    std::filesystem::create_directories(output / "scripts", error_code);
                    if (error_code) {
                        error("cannot create folder " + display(output / "scripts"));
                        return;
                    }
                    write_file(output / utf8_path(user_script_name), user_script);
                }
            }
        };
    }

    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir) {
        return Writer(document, target, base_dir, libs_dir).run();
    }

    std::string deck_json(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir, std::vector<std::string>& errors) {
        return Writer(document, target, base_dir, {}).run_editor(errors);
    }
}

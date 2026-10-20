#include "accessibility.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <tuple>
#include <variant>

namespace templide::middleend {
    namespace {
        constexpr double default_width = 1280;      // 슬라이드 크기를 정하지 않았을 때 (16:9, backend와 같다)
        constexpr double default_height = 720;
        constexpr double min_font_px = 16;          // 12pt
        constexpr double large_font_px = 24;        // 18pt
        constexpr double large_bold_font_px = 18.66; // 14pt, 굵은 글
        constexpr double min_contrast = 4.5;
        constexpr double min_contrast_large = 3.0;
        constexpr std::size_t max_animations = 8;
        constexpr double reading_order_gap = 40;    // 이만큼 이상 위에 있어야 순서가 뒤바뀐 것으로 본다 (px)

        const ir::Value* find_property(const ir::Element& element, const std::string& name) {
            for (const auto& property : element.properties) {
                if (property.name == name) {
                    return &property.value;
                }
            }
            return nullptr;
        }

        // elements 안에 role = title인 개체 틀(placeholder)이 있는지
        bool has_title_placeholder(const std::vector<ir::Element>& elements) {
            return std::any_of(elements.begin(), elements.end(), [](const ir::Element& element) {
                if (element.object == "placeholder") {
                    const auto* role = find_property(element, "role");
                    const auto* value = role == nullptr ? nullptr : std::get_if<ir::EnumValue>(role);
                    if (value != nullptr && value->member == "title") {
                        return true;
                    }
                }
                return has_title_placeholder(element.children);
            });
        }

        // 수를 px로. %는 basis(슬라이드 너비나 높이)에 대한 비율이고, px나 단위 없음이 아닌 단위가 섞였으면 nullopt다
        std::optional<double> pixels(const ir::Number& number, double basis) {
            double total = 0;
            for (const auto& [unit, value] : number.terms) {
                if (unit.empty() || unit == "px") {
                    total += value;
                } else if (unit == "%") {
                    total += value * basis / 100;
                } else {
                    return std::nullopt;
                }
            }
            return total;
        }

        std::optional<double> number_property(const ir::Element& element, const std::string& name, double basis) {
            const auto* value = find_property(element, name);
            const auto* number = value == nullptr ? nullptr : std::get_if<ir::Number>(value);
            return number == nullptr ? std::nullopt : pixels(*number, basis);
        }

        // ----- 색

        double channel(int value) {
            const double v = value / 255.0;
            return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        }

        double luminance(const ir::Color& color) {
            return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
        }

        // top을 bottom 위에 올렸을 때 보이는 색
        ir::Color over(const ir::Color& top, const ir::Color& bottom) {
            if (top.a >= 1) {
                return {top.r, top.g, top.b, 1, ""};
            }
            const auto mix = [&](int a, int b) { return static_cast<int>(std::lround(a * top.a + b * (1 - top.a))); };
            return {mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), 1, ""};
        }

        double contrast_ratio(const ir::Color& a, const ir::Color& b) {
            const double la = luminance(a);
            const double lb = luminance(b);
            return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
        }

        std::string hex_of(const ir::Color& color) {
            std::ostringstream out;
            out << '#' << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << color.r << std::setw(2) << color.g << std::setw(2) << color.b;
            return out.str();
        }

        std::string decimal(double value, int digits) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(digits) << value;
            return out.str();
        }

        // 10.0 -> 10, 10.5 -> 10.5
        std::string compact(double value) {
            std::string text = decimal(value, 1);
            if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) {
                text.resize(text.size() - 2);
            }
            return text;
        }

        // 테마 색 이름이 있으면 문서의 테마에서 실제 값을 찾는다
        ir::Color resolved(const ir::Color& color, const ir::Theme* theme) {
            if (!color.scheme.empty() && theme != nullptr) {
                if (const auto it = theme->colors.find(color.scheme); it != theme->colors.end()) {
                    ir::Color result = it->second;
                    result.a = color.a;
                    return result;
                }
            }
            return color;
        }

        // 바탕이 될 수 있는 색들. 그라데이션은 색 정지점마다, 무늬는 두 색 모두다. 그림은 알 수 없으므로 nullopt다
        using Backdrop = std::optional<std::vector<ir::Color>>;

        Backdrop backdrop_of(const ir::Value& value, const ir::Theme* theme, const std::vector<ir::Color>& under) {
            std::vector<ir::Color> tops;
            if (const auto* color = std::get_if<ir::Color>(&value)) {
                tops.push_back(resolved(*color, theme));
            } else if (const auto* gradient = std::get_if<ir::Gradient>(&value)) {
                for (const auto& stop : gradient->colors) {
                    tops.push_back(resolved(stop, theme));
                }
            } else if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
                tops.push_back(resolved(pattern->foreground, theme));
                tops.push_back(resolved(pattern->background, theme));
            } else {
                return std::nullopt;
            }
            std::vector<ir::Color> result;
            for (const auto& top : tops) {
                for (const auto& bottom : under) {
                    result.push_back(over(top, bottom));
                }
            }
            return result;
        }

        // ----- 글

        bool has_text(const ir::Text& text) {
            for (const auto& paragraph : text.paragraphs) {
                for (const auto& run : paragraph.runs) {
                    if (run.text.find_first_not_of(" \t\r\n") != std::string::npos) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool element_has_text(const ir::Element& element) {
            const auto* value = find_property(element, "text");
            const auto* text = value == nullptr ? nullptr : std::get_if<ir::Text>(value);
            return text != nullptr && has_text(*text);
        }

        // ----- 원문 위치

        struct Locator {
            const std::vector<std::pair<std::string, std::string>>& sources;

            // 파일 path 안의 바이트 위치를 줄과 칸(1부터)으로. 파일을 못 찾으면 줄 0
            std::pair<std::size_t, std::size_t> position(const std::string& path, std::size_t offset) const {
                for (const auto& [name, text] : sources) {
                    if (name != path) {
                        continue;
                    }
                    offset = std::min(offset, text.size());
                    std::size_t line = 1;
                    std::size_t line_start = 0;
                    for (std::size_t i = 0; i < offset; ++i) {
                        if (text[i] == '\n') {
                            ++line;
                            line_start = i + 1;
                        }
                    }
                    return {line, offset - line_start + 1};
                }
                return {0, 0};
            }
        };

        class Checker {
        public:
            Checker(const ir::Document& document, const std::vector<std::pair<std::string, std::string>>& sources) : document_(document), locator_{sources} {
                for (const auto& target : document.targets) {
                    if (target.width && target.height) {
                        const auto width = pixels(*target.width, default_width);
                        const auto height = pixels(*target.height, default_height);
                        if (width && height) {
                            width_ = *width;
                            height_ = *height;
                            break;
                        }
                    }
                }
            }

            std::vector<Diagnostic> run() {
                for (const auto& slide : document_.slides) {
                    check_slide(slide);
                }
                // 원문의 순서로 (같은 파일 안에서 줄, 칸 순서)
                std::stable_sort(found_.begin(), found_.end(), [](const Diagnostic& a, const Diagnostic& b) {
                    return std::tie(a.path, a.line, a.column) < std::tie(b.path, b.line, b.column);
                });
                return std::move(found_);
            }

        private:
            const ir::Document& document_;
            Locator locator_;
            double width_ = default_width;
            double height_ = default_height;
            std::vector<Diagnostic> found_;

            void add(const char* code, Level level, const ir::Origin& at, const std::string& message, const std::string& help = "") {
                Diagnostic diagnostic{at.range.path, 0, 0, message, 0, level, code, help};
                const auto [line, column] = locator_.position(at.range.path, at.range.begin);
                diagnostic.line = line;
                diagnostic.column = column;
                found_.push_back(std::move(diagnostic));
            }

            static std::string label(const ir::Element& element) {
                return element.name.empty() || element.generated_name ? element.object : element.name;
            }

            const ir::Theme* theme_of(const ir::Slide& slide) const {
                if (slide.layout && slide.layout->master < document_.masters.size() && document_.masters[slide.layout->master].theme) {
                    return &*document_.masters[slide.layout->master].theme;
                }
                return nullptr;
            }

            // 슬라이드의 바탕색들: 슬라이드의 background, 없으면 layout의 background, 없으면 흰색
            Backdrop slide_backdrop(const ir::Slide& slide, const ir::Theme* theme) const {
                const std::vector<ir::Color> white = {{255, 255, 255, 1, ""}};
                const std::optional<ir::Value>* background = &slide.background;
                if (!slide.background && slide.layout && slide.layout->master < document_.masters.size()) {
                    const auto& master = document_.masters[slide.layout->master];
                    if (slide.layout->layout < master.layouts.size()) {
                        background = &master.layouts[slide.layout->layout].background;
                    }
                }
                if (!*background) {
                    return white;
                }
                return backdrop_of(**background, theme, white);
            }

            // 제목을 다는 방법. 제목은 layout의 title 개체 틀에 들어가므로, 그런 layout을 쓰고 있는지에 따라 달라진다
            std::string title_help(const ir::Slide& slide) const {
                if (slide.layout) {
                    const auto& layout = document_.masters[slide.layout->master].layouts[slide.layout->layout];
                    if (has_title_placeholder(layout.elements)) {
                        return "add title = \"...\"; to the slide";
                    }
                }
                std::string names;
                for (const auto& master : document_.masters) {
                    for (const auto& layout : master.layouts) {
                        if (has_title_placeholder(layout.elements)) {
                            names += (names.empty() ? "" : ", ") + master.name + "." + layout.name;
                        }
                    }
                }
                if (names.empty()) {
                    return "no layout has a title placeholder; add 'put placeholder { role = title; ... }' to a master case, then set layout and title on the slide";
                }
                return "use a layout with a title placeholder (layout = NAME; title = \"...\";): " + names;
            }

            void check_slide(const ir::Slide& slide) {
                const ir::Theme* theme = theme_of(slide);
                const Backdrop base = slide_backdrop(slide, theme);
                if (!slide.hidden) {
                    const auto title = slide.placeholders.find("title");
                    if (title == slide.placeholders.end() || !has_text(title->second)) {
                        add("A0004", Level::Warning, slide.source, "Slide " + std::to_string(slide.page) + " has no title; screen readers use it to name the slide", title_help(slide));
                    }
                }
                check_elements(slide.elements, theme, base);
                check_reading_order(slide);
                if (slide.animations.size() > max_animations) {
                    add("A0006", Level::Info, slide.source,
                        "Slide " + std::to_string(slide.page) + " has " + std::to_string(slide.animations.size()) + " animations; many animations are hard to follow");
                }
            }

            void check_elements(const std::vector<ir::Element>& elements, const ir::Theme* theme, const Backdrop& base) {
                for (const auto& element : elements) {
                    check_alt(element);
                    if (element.object == "table") {
                        check_table(element);
                    }
                    check_text(element, theme, base);
                    check_elements(element.children, theme, base);
                }
            }

            void check_alt(const ir::Element& element) {
                const bool described = find_property(element, "alt") != nullptr; // alt = ""도 장식이라고 밝힌 것이다
                if (described) {
                    return;
                }
                if (element.object == "image" || element.object == "video" || element.object == "audio" || element.object == "chart") {
                    add("A0001", Level::Error, element.source, "The " + element.object + " has no alt text; add alt = \"...\", or alt = \"\" if it is only decoration");
                } else if ((element.object == "shape" || element.object == "backdrop" || element.object == "line" || element.object == "connector" || element.object == "freeform")
                           && !element_has_text(element)) {
                    add("A0008", Level::Info, element.source,
                        "The " + element.object + " has no alt text; describe it with alt = \"...\" or mark it as decoration with alt = \"\"");
                }
            }

            void check_table(const ir::Element& element) {
                const auto rows = number_property(element, "header_rows", 1);
                const auto columns = number_property(element, "header_cols", 1);
                if (rows.value_or(0) < 1 && columns.value_or(0) < 1) {
                    add("A0007", Level::Warning, element.source, "The table has no header row or column; set header_rows = 1 so screen readers can read the headings");
                }
            }

            // 글의 색 대비와 크기. 개체마다 가장 나쁜 하나만 알린다
            void check_text(const ir::Element& element, const ir::Theme* theme, const Backdrop& base) {
                const auto* value = find_property(element, "text");
                const auto* text = value == nullptr ? nullptr : std::get_if<ir::Text>(value);
                if (text == nullptr || !has_text(*text)) {
                    return;
                }
                // 개체 자신의 fill이 있으면 그 위에, 없으면 슬라이드 바탕 위에 글이 있다
                Backdrop behind = base;
                if (const auto* fill = find_property(element, "fill"); fill != nullptr && behind) {
                    behind = backdrop_of(*fill, theme, *behind);
                }
                const ir::Color default_text = resolved(ir::default_theme_color("dk1"), theme);
                double worst_ratio = 1000;
                double worst_needed = min_contrast;
                std::optional<ir::Color> worst_text;
                std::optional<ir::Color> worst_back;
                double smallest = 1e9;
                for (const auto& paragraph : text->paragraphs) {
                    for (const auto& run : paragraph.runs) {
                        if (run.text.find_first_not_of(" \t\r\n") == std::string::npos) {
                            continue;
                        }
                        std::optional<double> size;
                        if (run.style.font_size) {
                            size = pixels(*run.style.font_size, height_);
                        }
                        if (size) {
                            smallest = std::min(smallest, *size);
                        }
                        if (!behind) {
                            continue;
                        }
                        const bool bold = run.style.font_weight && (run.style.font_weight->member == "bold" || run.style.font_weight->member == "bolder");
                        const bool large = size ? (*size >= large_font_px || (bold && *size >= large_bold_font_px)) : true; // 크기를 모르면 큰 글로 보아 덜 엄격하게 본다
                        const double needed = large ? min_contrast_large : min_contrast;
                        const ir::Color fore = run.style.color ? resolved(*run.style.color, theme) : default_text;
                        for (const auto& back : *behind) {
                            const double ratio = contrast_ratio(over(fore, back), back);
                            if (ratio < needed && ratio < worst_ratio) {
                                worst_ratio = ratio;
                                worst_needed = needed;
                                worst_text = over(fore, back);
                                worst_back = back;
                            }
                        }
                    }
                }
                if (worst_text && worst_back) {
                    add("A0002", Level::Warning, element.source,
                        "Text color " + hex_of(*worst_text) + " on " + hex_of(*worst_back) + " has a contrast ratio of " + decimal(worst_ratio, 1) + ":1; at least "
                            + decimal(worst_needed, 1) + ":1 is needed");
                }
                if (smallest < min_font_px) {
                    add("A0003", Level::Warning, element.source, "Text is " + compact(smallest * 0.75) + "pt, smaller than the " + compact(min_font_px * 0.75) + "pt minimum");
                }
            }

            // 소스의 순서가 읽는 순서다. 앞에 있는 개체가 화면에서 뒤에 있는 개체보다 한참 아래에 있으면 순서가 뒤바뀐 것이다
            void check_reading_order(const ir::Slide& slide) {
                struct Box {
                    const ir::Element* element;
                    double top;
                    double bottom;
                };
                std::vector<Box> boxes;
                for (const auto& element : slide.elements) {
                    if (element.object == "backdrop" || element.object == "line" || element.object == "connector") {
                        continue;
                    }
                    const auto y = number_property(element, "y", height_);
                    const auto h = number_property(element, "height", height_);
                    if (y && h) {
                        boxes.push_back({&element, *y, *y + *h});
                    }
                }
                for (std::size_t later = 1; later < boxes.size(); ++later) {
                    for (std::size_t earlier = 0; earlier < later; ++earlier) {
                        if (boxes[later].bottom <= boxes[earlier].top && boxes[earlier].top - boxes[later].bottom >= 0
                            && boxes[earlier].top - boxes[later].top >= reading_order_gap) {
                            add("A0005", Level::Warning, boxes[later].element->source,
                                "Reading order differs from the visual order: '" + label(*boxes[later].element) + "' is read after '" + label(*boxes[earlier].element)
                                    + "' but is placed above it");
                            return; // 슬라이드마다 첫 번째만 알린다
                        }
                    }
                }
            }
        };
    }

    std::vector<Diagnostic> check_accessibility(const ir::Document& document, const std::vector<std::pair<std::string, std::string>>& sources) {
        return Checker(document, sources).run();
    }
}

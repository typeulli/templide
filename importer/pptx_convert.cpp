#include "pptx_import.h"
#include "pptx_drawing.h"
#include "pptx_model.h"
#include "../backend/pptx.h"
#include "../backend/pptx_animations.h"
#include "../backend/raster.h"
#include "../middleend/analyzer.h"
#include "../middleend/geometry.h"
#include "../middleend/tasset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <numbers>
#include <sstream>

// 고른 노드를 .tlide 코드로 쓴다
namespace templide::importer {
    namespace {
        // ---- 글로 쓰기

        std::string format(double value, int decimals = 2) {
            char text[64];
            std::snprintf(text, sizeof text, "%.*f", decimals, value);
            std::string result = text;
            if (result.find('.') != std::string::npos) {
                while (result.back() == '0') {
                    result.pop_back();
                }
                if (result.back() == '.') {
                    result.pop_back();
                }
            }
            return result == "-0" ? "0" : result;
        }

        // 위치와 크기는 int 속성이라 px 정수로 쓴다
        std::string px(double emu) {
            return std::to_string(std::llround(emu / emu_per_px)) + "px";
        }

        std::string px_float(double emu) {
            return format(emu / emu_per_px) + "px";
        }

        std::string pt(double points) {
            return format(points) + "pt";
        }

        std::string seconds(long long milliseconds) {
            return format(static_cast<double>(milliseconds) / 1000, 3) + "s";
        }

        std::string quote(const std::string& text) {
            std::string result = "\"";
            for (const char c : text) {
                switch (c) {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\n': result += "\\n"; break;
                    case '\t': result += "\\t"; break;
                    case '{': result += "\\{"; break;
                    case '}': result += "\\}"; break;
                    case '\r':
                    case '\v':
                        result += "\\n";
                        break;
                    default:
                        if (static_cast<unsigned char>(c) >= 0x20) {
                            result += c;
                        }
                        break;
                }
            }
            return result + "\"";
        }

        // 영문자, 숫자, _만 남긴 소문자 이름. 남는 것이 없으면 빈 문자열
        std::string identifier(const std::string& text) {
            std::string result;
            bool gap = false;
            for (const char c : text) {
                const auto u = static_cast<unsigned char>(c);
                if (std::isalnum(u) && u < 0x80) {
                    if (gap && !result.empty()) {
                        result += '_';
                    }
                    gap = false;
                    result += static_cast<char>(std::tolower(u));
                } else {
                    gap = true;
                }
            }
            return result;
        }

        const std::set<std::string>& reserved_names() {
            static const std::set<std::string> names = {
                // 키워드와 내장 이름
                "slide", "put", "template", "style", "object", "var", "master", "case", "target", "if", "else", "for", "in", "enum", "transition", "animate", "group", "as",
                "theme", "section", "review", "comment", "bullets", "numbers", "dashes", "paragraphs", "true", "false", "color", "int", "float", "string", "text", "bool",
                "ref", "asset", "by", "default", "image", "video", "audio", "file", "link", "action", "hover_action", "run", "program", "macro", "action_file", "linear",
                "radial", "pattern", "hex", "rgb", "rgba", "include",
                // stddef
                "yellow", "bold", "reset", "brgap", "text_anchor", "image_fit", "shape_kind", "line_arrow", "connector_kind", "connector_side", "media_start", "text_box",
                "shape", "backdrop", "line", "connector", "freeform", "placeholder",
                // 내장 enum
                "font_weight", "target_type", "angle_convention", "text_align", "font_style", "text_decoration", "vertical_align", "text_transform", "list_style",
                "text_autofit", "text_direction", "flip_direction", "dash_style", "line_cap", "line_join", "line_compound", "bevel_kind", "pattern_kind", "slide_jump",
                "placeholder_role",
            };
            return names;
        }

        std::string unique(std::string base, const std::string& fallback, std::set<std::string>& taken) {
            if (base.empty()) {
                base = fallback;
            }
            if (std::isdigit(static_cast<unsigned char>(base.front()))) {
                base = fallback + "_" + base;
            }
            std::string name = base;
            for (int number = 2; taken.contains(name) || reserved_names().contains(name); ++number) {
                name = base + "_" + std::to_string(number);
            }
            taken.insert(name);
            return name;
        }

        std::string color_code(const Color& color) {
            if (!color.scheme.empty()) {
                return "theme." + color.scheme;
            }
            char text[64];
            if (color.a < 0.995) {
                std::snprintf(text, sizeof text, "rgba(%d, %d, %d, %s)", color.r, color.g, color.b, format(color.a).c_str());
            } else {
                std::snprintf(text, sizeof text, "hex(%02X%02X%02X)", color.r, color.g, color.b);
            }
            return text;
        }

        bool has_hangul(const std::string& text) {
            for (std::size_t i = 0; i + 2 < text.size(); ++i) {
                const auto lead = static_cast<unsigned char>(text[i]);
                if (lead == 0xEA || lead == 0xEB || lead == 0xEC || (lead == 0xED && static_cast<unsigned char>(text[i + 1]) < 0x9E)) {
                    return true;
                }
            }
            return false;
        }

        // +mj-lt -> theme.heading_font, +mn-ea -> theme.body_font, 나머지는 font("글꼴 이름")
        std::string font_code(const std::string& typeface) {
            if (typeface.starts_with("+mj")) {
                return "theme.heading_font";
            }
            if (typeface.starts_with("+mn")) {
                return "theme.body_font";
            }
            return "font(" + quote(typeface) + ")";
        }

        bool supported_image(const std::string& part) {
            std::string name = part;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const char* extension : {".png", ".jpg", ".jpeg", ".gif", ".bmp"}) {
                if (name.ends_with(extension)) {
                    return true;
                }
            }
            return false;
        }

        // 자손 중 이름이 name인 것 모두
        void find_all(const xml::Node& node, const std::string& name, std::vector<const xml::Node*>& out) {
            for (const auto& child : node.children) {
                if (child.name == name) {
                    out.push_back(&child);
                }
                find_all(child, name, out);
            }
        }

        // ---- 자리

        struct Frame {
            double x = 0; // EMU
            double y = 0;
            double w = 0;
            double h = 0;
            double rotation = 0; // 도
            bool flip_h = false;
            bool flip_v = false;
            bool found = false;
        };

        Frame read_xfrm(const xml::Node* xfrm) {
            Frame frame;
            if (xfrm == nullptr) {
                return frame;
            }
            if (const auto* offset = xfrm->child("a:off")) {
                frame.x = static_cast<double>(offset->integer("x").value_or(0));
                frame.y = static_cast<double>(offset->integer("y").value_or(0));
                frame.found = true;
            }
            if (const auto* extent = xfrm->child("a:ext")) {
                frame.w = static_cast<double>(extent->integer("cx").value_or(0));
                frame.h = static_cast<double>(extent->integer("cy").value_or(0));
            }
            frame.rotation = static_cast<double>(xfrm->integer("rot").value_or(0)) / 60000;
            frame.flip_h = xfrm->flag("flipH");
            frame.flip_v = xfrm->flag("flipV");
            return frame;
        }

        const xml::Node* xfrm_of(const xml::Node& shape) {
            if (const auto* properties = shape.child("p:spPr")) {
                return properties->child("a:xfrm");
            }
            if (const auto* properties = shape.child("p:grpSpPr")) {
                return properties->child("a:xfrm");
            }
            return shape.child("p:xfrm");
        }

        // 그룹 안의 좌표를 그룹 바깥 좌표로
        struct GroupTransform {
            double offset_x;
            double offset_y;
            double child_x;
            double child_y;
            double scale_x;
            double scale_y;
            double center_x;
            double center_y;
            double rotation; // 도
            bool flip_h;
            bool flip_v;
            const GroupTransform* parent;
        };

        void map_once(double& x, double& y, const GroupTransform& group) {
            x = group.offset_x + (x - group.child_x) * group.scale_x;
            y = group.offset_y + (y - group.child_y) * group.scale_y;
            if (group.flip_h) {
                x = 2 * group.center_x - x;
            }
            if (group.flip_v) {
                y = 2 * group.center_y - y;
            }
            if (group.rotation != 0) {
                const double radians = group.rotation * std::numbers::pi / 180;
                const double dx = x - group.center_x;
                const double dy = y - group.center_y;
                x = group.center_x + dx * std::cos(radians) - dy * std::sin(radians);
                y = group.center_y + dx * std::sin(radians) + dy * std::cos(radians);
            }
        }

        void map_point(double& x, double& y, const GroupTransform* group) {
            for (; group != nullptr; group = group->parent) {
                map_once(x, y, *group);
            }
        }

        Frame map_frame(Frame frame, const GroupTransform* group) {
            for (; group != nullptr; group = group->parent) {
                double cx = frame.x + frame.w / 2;
                double cy = frame.y + frame.h / 2;
                map_once(cx, cy, *group);
                frame.w *= group->scale_x;
                frame.h *= group->scale_y;
                if (group->flip_h) {
                    frame.flip_h = !frame.flip_h;
                    frame.rotation = -frame.rotation;
                }
                if (group->flip_v) {
                    frame.flip_v = !frame.flip_v;
                    frame.rotation = -frame.rotation;
                }
                frame.rotation += group->rotation;
                frame.x = cx - frame.w / 2;
                frame.y = cy - frame.h / 2;
            }
            frame.rotation = std::fmod(frame.rotation, 360);
            if (frame.rotation < 0) {
                frame.rotation += 360;
            }
            return frame;
        }

        // ---- 글자 모양의 기준

        // 만든 코드가 아무 style 없이 보일 모양. 다른 것만 style(...)로 적는다
        struct Baseline {
            double size = 18;
            std::string color = "theme.dark1";
            std::string font = "theme.body_font";
            bool bold = false;
            bool italic = false;
            std::string align = "l";
        };

        struct Properties {
            std::vector<std::string> lines;

            void add(const std::string& name, const std::string& value) {
                lines.push_back(name + " = " + value + ";");
            }
            void raw(const std::string& line) {
                lines.push_back(line);
            }
        };

        // 자리를 비워 둔 줄들을 블록 안에 들여 쓴다
        std::string block(const std::string& head, const std::vector<std::string>& lines, int indent) {
            const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
            std::string result = pad + head + " {\n";
            for (const auto& line : lines) {
                // 여러 줄이면 줄마다 들여 쓴다
                std::size_t start = 0;
                while (start <= line.size()) {
                    const auto newline = line.find('\n', start);
                    const std::string piece = line.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
                    result += pad + "    " + piece + "\n";
                    if (newline == std::string::npos) {
                        break;
                    }
                    start = newline + 1;
                }
            }
            return result + pad + "}\n";
        }

        struct Scope {
            const Part* part = nullptr;
            ColorContext colors;
            const Master* master = nullptr;
            const Layout* layout = nullptr;
            const Slide* slide = nullptr;
            std::size_t slide_index = 0;
            std::string prefix; // 노드 id 앞부분 (m1, m1/l2, s3)
            bool template_body = false; // 마스터 도형의 template
            std::map<int, std::string> names; // 개체 id -> put ... as 이름
            std::map<int, std::vector<std::pair<const Animation*, int>>> animations; // 대상 id -> (애니메이션, 차례)
            std::map<std::string, std::string> roles; // 슬라이드의 title, subtitle, body -> 글
            std::string where;
            std::optional<Fill> group_fill;
        };

        struct ActionCode {
            std::string code;
            bool link = false; // 링크만이면 link, 아니면 action
            std::string sound; // 소리 파트
            bool highlight = false;
        };

        class Converter {
        public:
            Converter(const Presentation& presentation, const std::set<std::string>& selection, const std::filesystem::path& output)
                : presentation_(presentation), selection_(selection), output_(output) {
                bundle_ = output;
                bundle_.replace_extension(".tasset");
            }

            ImportResult run(const std::filesystem::path& packages_dir);

        private:
            const Presentation& presentation_;
            const std::set<std::string>& selection_;
            std::filesystem::path output_;
            std::filesystem::path bundle_;
            std::vector<std::string> warnings_;
            std::set<std::string> taken_; // 파일 전체에서 쓰는 이름

            struct Media {
                std::string entry;
                std::string kind;
                std::string name;
                std::string bytes; // 불러오며 만든 그림이면 그 내용. 아니면 pptx의 파트를 그대로 넣는다
            };
            std::map<std::string, Media> media_; // 파트 이름 -> 묶음의 파일
            std::vector<std::string> media_order_;
            std::set<std::string> entries_;
            std::map<std::size_t, int> slide_numbers_; // 원래 슬라이드 index -> 새 번호
            std::map<std::string, std::size_t> slide_parts_; // 슬라이드 파트 이름 -> 원래 index
            std::map<int, std::string> master_names_;
            std::map<int, std::string> theme_names_;
            std::map<int, std::string> template_names_;
            std::map<int, std::string> case_names_;

            bool selected(const std::string& id) const {
                return selection_.contains(id);
            }

            void warn(const std::string& where, const std::string& message) {
                const std::string line = where.empty() ? message : where + ": " + message;
                if (std::find(warnings_.begin(), warnings_.end(), line) == warnings_.end()) {
                    warnings_.push_back(line);
                }
            }

            // ---- 묶음의 파일

            // 파트를 묶음에 넣고 붙인 이름을 돌려준다
            std::string media(const std::string& part, const std::string& kind) {
                if (const auto it = media_.find(part); it != media_.end()) {
                    return it->second.name;
                }
                std::string entry = part.substr(part.find_last_of('/') + 1);
                const auto dot = entry.find_last_of('.');
                const std::string stem = dot == std::string::npos ? entry : entry.substr(0, dot);
                const std::string extension = dot == std::string::npos ? "" : entry.substr(dot);
                for (int number = 2; entries_.contains(entry); ++number) {
                    entry = stem + "-" + std::to_string(number) + extension;
                }
                entries_.insert(entry);
                const std::string name = unique(identifier(stem), kind, taken_);
                media_[part] = {entry, kind, name, ""};
                media_order_.push_back(part);
                return name;
            }

            // 그림에 색 바꾸기가 있으면 바꾼 그림을 만들어 넣는다. key는 같은 그림과 효과를 한 번만 만들려고 쓴다
            std::string image_media(const std::string& part, const ImageEffects& effects, const std::string& key, const std::string& where) {
                for (const auto& dropped : effects.dropped) {
                    warn(where, "그림 효과 '" + dropped + "'는 빠졌습니다");
                }
                if (!effects.any()) {
                    return media(part, "image");
                }
                const std::string id = part + "#" + key;
                if (const auto it = media_.find(id); it != media_.end()) {
                    return it->second.name;
                }
                const auto bytes = presentation_.package.read(part);
                auto raster = bytes ? backend::raster::decode(*bytes) : std::nullopt;
                if (!raster) {
                    warn(where, "그림의 색 바꾸기를 적용하지 못해 원래 그림을 넣었습니다");
                    return media(part, "image");
                }
                const auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
                const auto srgb = [](double c) {
                    c = std::clamp(c, 0.0, 1.0);
                    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
                };
                for (std::size_t i = 0; i + 2 < raster->pixels.size(); i += 3) {
                    double r = raster->pixels[i] / 255.0;
                    double g = raster->pixels[i + 1] / 255.0;
                    double b = raster->pixels[i + 2] / 255.0;
                    if (effects.bright != 0 || effects.contrast != 0) {
                        const auto adjust = [&](double c) { return std::clamp((c - 0.5) * (1 + effects.contrast) + 0.5 + effects.bright, 0.0, 1.0); };
                        r = adjust(r);
                        g = adjust(g);
                        b = adjust(b);
                    }
                    if (effects.grayscale || effects.duotone) {
                        const double gray = 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
                        if (effects.duotone) {
                            const Color& dark = effects.duotone->first;
                            const Color& light = effects.duotone->second;
                            const auto mix = [&](int from, int to) { return srgb(linear(from / 255.0) * (1 - gray) + linear(to / 255.0) * gray); };
                            r = mix(dark.r, light.r);
                            g = mix(dark.g, light.g);
                            b = mix(dark.b, light.b);
                        } else {
                            r = g = b = srgb(gray);
                        }
                    }
                    raster->pixels[i] = static_cast<float>(r * 255);
                    raster->pixels[i + 1] = static_cast<float>(g * 255);
                    raster->pixels[i + 2] = static_cast<float>(b * 255);
                }
                std::string stem = part.substr(part.find_last_of('/') + 1);
                stem = stem.substr(0, stem.find_last_of('.'));
                std::string entry = stem + "-recolored.png";
                for (int number = 2; entries_.contains(entry); ++number) {
                    entry = stem + "-recolored-" + std::to_string(number) + ".png";
                }
                entries_.insert(entry);
                const std::string name = unique(identifier(stem) + "_recolored", "image", taken_);
                media_[id] = {entry, "image", name, backend::raster::encode_png(*raster)};
                media_order_.push_back(id);
                return name;
            }

            // ---- 개체 틀의 상속

            // 개체와 그 개체 틀이 이어받는 레이아웃, 마스터의 개체 틀
            std::vector<const xml::Node*> inheritance(const Shape& shape, const Scope& scope) const {
                std::vector<const xml::Node*> chain{shape.node};
                if (shape.ph_type.empty() || scope.master == nullptr) {
                    return chain;
                }
                const Shape* layout_match = nullptr;
                if (scope.slide != nullptr && scope.layout != nullptr) {
                    layout_match = match_placeholder(scope.layout->shapes, shape);
                    if (layout_match != nullptr) {
                        chain.push_back(layout_match->node);
                    }
                }
                if (scope.layout != nullptr || scope.slide != nullptr) {
                    if (const Shape* master_match = match_placeholder(scope.master->shapes, layout_match != nullptr ? *layout_match : shape)) {
                        chain.push_back(master_match->node);
                    }
                }
                return chain;
            }

            static const xml::Node* first_xfrm(const std::vector<const xml::Node*>& chain) {
                for (const auto* node : chain) {
                    if (const auto* xfrm = xfrm_of(*node)) {
                        return xfrm;
                    }
                }
                return nullptr;
            }

            static const xml::Node* first_style(const std::vector<const xml::Node*>& chain) {
                for (const auto* node : chain) {
                    if (const auto* style = node->child("p:style")) {
                        return style;
                    }
                }
                return nullptr;
            }

            // 글의 상속 순서: 개체들의 lstStyle, 마스터의 글자 서식, 프레젠테이션 기본 서식. own에 개체들의 lstStyle 수를 넣는다
            StyleChain text_chain(const std::vector<const xml::Node*>& chain, const Shape& shape, const Scope& scope, std::size_t* own = nullptr) const {
                StyleChain result;
                for (const auto* node : chain) {
                    if (const auto* list = node->path({"p:txBody", "a:lstStyle"})) {
                        result.push_back(list);
                    }
                }
                if (own != nullptr) {
                    *own = result.size();
                }
                if (scope.master != nullptr) {
                    if (const auto* styles = scope.master->part->root.child("p:txStyles")) {
                        const std::string& type = shape.ph_type;
                        if (type == "title" || type == "ctrTitle") {
                            result.push_back(styles->child("p:titleStyle"));
                        } else if (!type.empty() && type != "dt" && type != "ftr" && type != "sldNum" && type != "hdr") {
                            result.push_back(styles->child("p:bodyStyle"));
                        } else {
                            result.push_back(styles->child("p:otherStyle"));
                        }
                    }
                }
                if (const auto* defaults = presentation_.presentation->root.child("p:defaultTextStyle")) {
                    result.push_back(defaults);
                }
                std::erase(result, nullptr);
                return result;
            }

            struct Body {
                std::map<std::string, std::string> attributes;
                std::string autofit;
                double font_scale = 1;     // 자동 맞춤으로 줄인 글자 비율
                double line_reduction = 0; // 자동 맞춤으로 줄인 줄 간격
            };

            static Body read_body(const std::vector<const xml::Node*>& chain) {
                Body body;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    const auto* properties = (*it)->path({"p:txBody", "a:bodyPr"});
                    if (properties == nullptr) {
                        continue;
                    }
                    for (const auto& [key, value] : properties->attributes) {
                        body.attributes[key] = value;
                    }
                    for (const auto& child : properties->children) {
                        if (child.name == "a:noAutofit" || child.name == "a:normAutofit" || child.name == "a:spAutoFit") {
                            body.autofit = child.name;
                            body.font_scale = static_cast<double>(child.integer("fontScale").value_or(100000)) / 100000;
                            body.line_reduction = static_cast<double>(child.integer("lnSpcReduction").value_or(0)) / 100000;
                        }
                    }
                }
                return body;
            }

            // ---- 색, 채우기, 선

            std::string fill_code(const Fill& fill, const Scope& scope, const std::string& where) {
                switch (fill.kind) {
                    case Fill::Kind::None:
                        return "rgba(0, 0, 0, 0)";
                    case Fill::Kind::Solid:
                        return color_code(fill.color);
                    case Fill::Kind::Gradient: {
                        std::string stops;
                        for (const auto& stop : fill.stops) {
                            stops += ", " + color_code(stop.color) + " " + format(stop.position) + "%";
                        }
                        return fill.radial ? "radial(" + stops.substr(2) + ")" : "linear(" + format(fill.angle) + stops + ")";
                    }
                    case Fill::Kind::Pattern: {
                        std::string kind = "percent_50";
                        for (const auto& [name, preset] : backend::pptx::pattern_presets()) {
                            if (preset.first == fill.pattern) {
                                kind = name;
                            }
                        }
                        return "pattern(" + kind + ", " + color_code(fill.foreground) + ", " + color_code(fill.background) + ")";
                    }
                    case Fill::Kind::Image:
                        if (!supported_image(fill.image) || !presentation_.package.exists(fill.image)) {
                            warn(where, "채우기 그림의 형식을 쓸 수 없어 뺐습니다");
                            return "";
                        }
                        return image_media(fill.image, fill.image_effects, std::to_string(reinterpret_cast<std::uintptr_t>(fill.blip)), where);
                    case Fill::Kind::Group:
                        return scope.group_fill ? fill_code(*scope.group_fill, scope, where) : "";
                }
                return "";
            }

            // 개체의 채우기: 개체 틀이 이어받는 것까지 보고, 없으면 p:style의 테마 채우기
            std::optional<Fill> effective_fill(const std::vector<const xml::Node*>& chain, const Scope& scope) const {
                for (const auto* node : chain) {
                    if (const auto fill = read_fill(node->child("p:spPr"), *scope.part, scope.colors)) {
                        return fill;
                    }
                }
                if (const auto* style = first_style(chain)) {
                    return theme_fill(style->child("a:fillRef"), *scope.part, scope.colors);
                }
                return std::nullopt;
            }

            Line effective_line(const std::vector<const xml::Node*>& chain, const Scope& scope) const {
                Line line;
                if (const auto* style = first_style(chain)) {
                    line = theme_line(style->child("a:lnRef"), scope.colors);
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    if (const auto* properties = (*it)->child("p:spPr")) {
                        line = read_line(properties->child("a:ln"), scope.colors, line);
                    }
                }
                return line;
            }

            std::optional<Color> parse_color(const std::string& xml, const ColorContext& colors) const {
                std::string error;
                const auto node = xml::parse("<root xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">" + xml + "</root>", error);
                return node ? read_color(&*node, colors) : std::nullopt;
            }

            // 채우기와 선. styled면 templide가 PowerPoint 기본 도형 모양(accent1 채우기, 진한 accent1 선)을 쓰는 개체다
            void add_paint(Properties& props, const std::vector<const xml::Node*>& chain, const Scope& scope, const std::string& object, const std::string& where) {
                const bool styled = object == "shape" || object == "freeform";
                const bool line_object = object == "line" || object == "connector";
                if (!line_object) {
                    const auto fill = effective_fill(chain, scope);
                    bool emit = fill.has_value() && (styled || fill->kind != Fill::Kind::None);
                    if (styled && fill && fill->kind == Fill::Kind::Solid && fill->color.scheme == "accent1") {
                        emit = false; // 기본 채우기와 같다
                    }
                    if (styled && !fill) {
                        emit = true; // 채우기도 p:style도 없으면 채우지 않는다
                    }
                    if (emit) {
                        const std::string code = fill ? fill_code(*fill, scope, where) : "rgba(0, 0, 0, 0)";
                        if (!code.empty()) {
                            props.add("fill", code);
                        }
                    }
                }
                const Line line = effective_line(chain, scope);
                const bool has_line = line.present && !line.none;
                if ((styled || line_object) && (!line.present || line.none)) {
                    props.add("line_color", "rgba(0, 0, 0, 0)");
                    return;
                }
                if (!has_line) {
                    return;
                }
                // templide 기본 선: 도형은 진한 accent1 1pt, 선은 accent1 0.5pt
                std::optional<Color> default_color;
                std::optional<Emu> default_width;
                if (styled) {
                    default_color = parse_color("<a:schemeClr val=\"accent1\"><a:shade val=\"50000\"/></a:schemeClr>", scope.colors);
                    default_width = 12700;
                } else if (line_object) {
                    default_color = parse_color("<a:schemeClr val=\"accent1\"/>", scope.colors);
                    default_width = 6350;
                }
                if (line.color && (!default_color || !same_color(*line.color, *default_color))) {
                    props.add("line_color", color_code(*line.color));
                }
                const Emu width = line.width.value_or(9525);
                if (!default_width || std::llabs(width - *default_width) > 100) {
                    props.add("line_width", pt(static_cast<double>(width) / emu_per_pt));
                }
                if (!line.dash.empty() && line.dash != "solid") {
                    props.add("line_dash", line.dash);
                }
                if (!line.cap.empty() && line.cap != "flat") {
                    props.add("line_cap", line.cap);
                }
                if (!line.join.empty() && line.join != "round") {
                    props.add("line_join", line.join);
                }
                if (!line.compound.empty() && line.compound != "single") {
                    props.add("line_compound", line.compound);
                }
            }

            void add_arrows(Properties& props, const std::vector<const xml::Node*>& chain, const Scope& scope) {
                const Line line = effective_line(chain, scope);
                static const std::set<std::string> arrows = {"triangle", "stealth", "diamond", "oval", "arrow"};
                if (arrows.contains(line.head)) {
                    props.add("start_arrow", line.head);
                }
                if (arrows.contains(line.tail)) {
                    props.add("end_arrow", line.tail);
                }
            }

            void add_effects(Properties& props, const std::vector<const xml::Node*>& chain, const Scope& scope, const std::string& where) {
                const xml::Node* properties = nullptr;
                for (const auto* node : chain) {
                    if (const auto* own = node->child("p:spPr"); own != nullptr && (own->child("a:effectLst") != nullptr || own->child("a:scene3d") != nullptr || own->child("a:sp3d") != nullptr)) {
                        properties = own;
                        break;
                    }
                }
                if (properties == nullptr) {
                    properties = chain.front()->child("p:spPr");
                }
                const auto* style = first_style(chain);
                const Effects effects = read_effects(properties, style != nullptr ? style->child("a:effectRef") : nullptr, scope.colors);
                const auto shadow = [&](const std::optional<Shadow>& value, const std::string& name) {
                    if (!value) {
                        return;
                    }
                    props.add(name, color_code(value->color));
                    if (std::abs(value->blur - 4) > 0.05) {
                        props.add(name + "_blur", pt(value->blur));
                    }
                    if (std::abs(value->distance - 3) > 0.05) {
                        props.add(name + "_distance", pt(value->distance));
                    }
                    if (std::abs(value->angle - 45) > 0.05) {
                        props.add(name + "_angle", format(value->angle));
                    }
                };
                shadow(effects.shadow, "shadow");
                shadow(effects.inner_shadow, "inner_shadow");
                if (effects.glow) {
                    props.add("glow", color_code(*effects.glow));
                    if (std::abs(effects.glow_size - 8) > 0.05) {
                        props.add("glow_size", pt(effects.glow_size));
                    }
                }
                if (effects.soft_edge) {
                    props.add("soft_edge", pt(*effects.soft_edge));
                }
                if (effects.reflection) {
                    props.add("reflection", format(std::clamp(*effects.reflection, 0.0, 1.0)));
                    props.add("reflection_size", format(std::clamp(effects.reflection_size, 0.0, 1.0)));
                    if (effects.reflection_distance > 0.05) {
                        props.add("reflection_distance", pt(effects.reflection_distance));
                    }
                    if (std::abs(effects.reflection_blur - 0.5) > 0.05) {
                        props.add("reflection_blur", pt(effects.reflection_blur));
                    }
                }
                if (effects.rotation_x) {
                    props.add("rotation_x", format(*effects.rotation_x));
                }
                if (effects.rotation_y) {
                    props.add("rotation_y", format(*effects.rotation_y));
                }
                if (effects.perspective) {
                    props.add("perspective", format(std::clamp(*effects.perspective, 0.0, 180.0)));
                }
                if (!effects.bevel.empty()) {
                    props.add("bevel", effects.bevel);
                    if (std::abs(effects.bevel_width - 6) > 0.05) {
                        props.add("bevel_width", pt(effects.bevel_width));
                    }
                    if (std::abs(effects.bevel_height - 6) > 0.05) {
                        props.add("bevel_height", pt(effects.bevel_height));
                    }
                }
                if (effects.depth) {
                    props.add("depth", pt(*effects.depth));
                    if (effects.depth_color) {
                        props.add("depth_color", color_code(*effects.depth_color));
                    }
                }
                for (const auto& dropped : effects.dropped) {
                    warn(where, "효과 '" + dropped + "'는 비슷하게 바꾸거나 뺐습니다");
                }
            }

            // ---- 링크와 실행 설정

            std::optional<ActionCode> action_of(const xml::Node& hyperlink, const Part& part, const std::string& where) {
                ActionCode result;
                const std::string action = hyperlink.get("action");
                const auto* relationship = part.rel(hyperlink.get("r:id"));
                result.highlight = hyperlink.flag("highlightClick");
                if (const auto* sound = hyperlink.child("a:snd")) {
                    if (const auto* sound_relationship = part.rel(sound->get("r:embed")); sound_relationship != nullptr && !sound_relationship->external) {
                        result.sound = sound_relationship->target;
                    }
                }
                const auto slide_link = [&](const std::string& target) -> std::optional<std::string> {
                    const auto found = slide_parts_.find(target);
                    if (found == slide_parts_.end() || !slide_numbers_.contains(found->second)) {
                        warn(where, "가리키는 슬라이드를 가져오지 않아 링크를 뺐습니다");
                        return std::nullopt;
                    }
                    return "slide(" + std::to_string(slide_numbers_.at(found->second)) + ")";
                };
                if (action.empty() || action == "ppaction://hlinksldjump") {
                    if (relationship == nullptr) {
                        return std::nullopt;
                    }
                    if (relationship->external) {
                        result.code = quote(relationship->target);
                    } else if (relationship->type == "slide") {
                        const auto code = slide_link(relationship->target);
                        if (!code) {
                            return std::nullopt;
                        }
                        result.code = *code;
                    } else {
                        return std::nullopt;
                    }
                    result.link = true;
                } else if (action.starts_with("ppaction://hlinkshowjump")) {
                    static const std::map<std::string, std::string> jumps = {
                        {"nextslide", "next_slide"}, {"previousslide", "previous_slide"}, {"firstslide", "first_slide"}, {"lastslide", "last_slide"},
                        {"lastslideviewed", "last_viewed_slide"}, {"endshow", "end_show"},
                    };
                    const auto equal = action.find("jump=");
                    const auto it = equal == std::string::npos ? jumps.end() : jumps.find(action.substr(equal + 5));
                    if (it == jumps.end()) {
                        return std::nullopt;
                    }
                    result.code = it->second;
                    result.link = true;
                } else if (action == "ppaction://program" || action == "ppaction://hlinkfile" || action.starts_with("ppaction://hlinkpres")) {
                    if (relationship == nullptr) {
                        return std::nullopt;
                    }
                    std::string target = relationship->target;
                    if (target.starts_with("file:///")) {
                        target = target.substr(8);
                    }
                    result.code = (action == "ppaction://program" ? "program(" : "action_file(") + quote(target) + ")";
                } else if (action.starts_with("ppaction://macro")) {
                    const auto equal = action.find("name=");
                    result.code = "macro(" + quote(equal == std::string::npos ? "" : action.substr(equal + 5)) + ")";
                } else if (action == "ppaction://noaction") {
                    if (result.sound.empty() && !result.highlight) {
                        return std::nullopt;
                    }
                } else if (action == "ppaction://media") {
                    return std::nullopt;
                } else {
                    warn(where, "templide에 없는 실행 설정(" + action + ")은 뺐습니다");
                    return std::nullopt;
                }
                return result;
            }

            void add_actions(Properties& props, const Shape& shape, const Scope& scope, const std::string& where) {
                const xml::Node* properties = nullptr;
                for (const char* nv : {"p:nvSpPr", "p:nvPicPr", "p:nvCxnSpPr", "p:nvGrpSpPr"}) {
                    if (const auto* found = shape.node->path({nv, "p:cNvPr"})) {
                        properties = found;
                    }
                }
                if (properties == nullptr) {
                    return;
                }
                const auto apply = [&](const char* tag, const std::string& action, const std::string& sound, const std::string& highlight) {
                    const auto* hyperlink = properties->child(tag);
                    if (hyperlink == nullptr) {
                        return;
                    }
                    const auto code = action_of(*hyperlink, *scope.part, where);
                    if (!code) {
                        return;
                    }
                    if (!code->code.empty()) {
                        props.add(action == "action" && code->link && code->sound.empty() && !code->highlight ? "link" : action, code->code);
                    }
                    if (!code->sound.empty()) {
                        if (code->sound.ends_with(".wav") || code->sound.ends_with(".WAV")) {
                            props.add(sound, media(code->sound, "audio"));
                        } else {
                            warn(where, "실행 설정의 소리는 wav만 쓸 수 있어 뺐습니다");
                        }
                    }
                    if (code->highlight) {
                        props.add(highlight, "true");
                    }
                };
                apply("a:hlinkClick", "action", "action_sound", "action_highlight");
                apply("a:hlinkHover", "hover_action", "hover_sound", "hover_highlight");
            }

            // ---- 글

            struct Piece {
                std::string text;
                std::string field; // slidenum, datetime
                RunStyle style;
                std::string link;  // link(...) 또는 action(...)
                std::string hover; // hover_action(...)
            };

            struct ParagraphPiece {
                int level = 0;
                ParagraphStyle style;
                std::vector<Piece> pieces;
                RunStyle end_style; // 문단 끝(endParaRPr)의 모양
                std::string kind; // none, bullets, numbers, dashes
                int depth = 0;    // 목록 안의 깊이
            };

            struct TextJob {
                const xml::Node* body = nullptr;
                StyleChain chain;
                std::size_t own = 0; // chain의 앞에서 개체 자신(과 개체 틀)의 lstStyle 수
                // PowerPoint이 자동 맞춤으로 줄여 보이는 비율. 보이는 모양대로 옮기려고 글자 크기와 줄 간격에 곱한다
                double font_scale = 1;
                double line_reduction = 0;
                const xml::Node* font_reference = nullptr; // p:style의 a:fontRef
                std::function<Baseline(int depth)> baseline;
            };

            // 글자 모양을 정하지 않은 값은 p:style의 fontRef와 기본값으로 채운다
            RunStyle complete(RunStyle style, const xml::Node* font_reference, const Scope& scope) const {
                if (!style.color && font_reference != nullptr) {
                    style.color = read_color(font_reference, scope.colors);
                }
                if (!style.color) {
                    style.color = parse_color("<a:schemeClr val=\"tx1\"/>", scope.colors);
                }
                if (!style.latin && !style.ea && font_reference != nullptr) {
                    const std::string prefix = font_reference->get("idx") == "major" ? "+mj" : "+mn";
                    style.latin = prefix + "-lt";
                    style.ea = prefix + "-ea";
                }
                if (!style.latin && !style.ea) {
                    style.latin = "+mn-lt";
                }
                if (!style.size) {
                    style.size = 18;
                }
                return style;
            }

            // 정하지 않은 값만 from에서 가져온다
            static void fill_unset(RunStyle& style, const RunStyle& from) {
                const auto take = [](auto& target, const auto& value) {
                    if (!target && value) {
                        target = value;
                    }
                };
                take(style.size, from.size);
                take(style.bold, from.bold);
                take(style.italic, from.italic);
                take(style.underline, from.underline);
                take(style.strike, from.strike);
                take(style.cap, from.cap);
                take(style.spacing, from.spacing);
                take(style.baseline, from.baseline);
                take(style.color, from.color);
                take(style.highlight, from.highlight);
                if (!style.latin && !style.ea) {
                    style.latin = from.latin;
                    style.ea = from.ea;
                }
            }

            // 글자 모양. PowerPoint처럼 rPr, 개체의 lstStyle, p:style의 fontRef, 마스터와 프레젠테이션의 기본 서식 순서로 찾는다
            RunStyle text_run(const xml::Node* run_properties, const StyleChain& chain, std::size_t own, const xml::Node* font_reference, int level, const Scope& scope) const {
                own = std::min(own, chain.size());
                const StyleChain first(chain.begin(), chain.begin() + static_cast<long>(own));
                const StyleChain rest(chain.begin() + static_cast<long>(own), chain.end());
                RunStyle style = resolve_run(run_properties, first, level, scope.colors);
                if (font_reference != nullptr) {
                    if (!style.color) {
                        style.color = read_color(font_reference, scope.colors);
                    }
                    if (!style.latin && !style.ea) {
                        const std::string prefix = font_reference->get("idx") == "major" ? "+mj" : "+mn";
                        style.latin = prefix + "-lt";
                        style.ea = prefix + "-ea";
                    }
                }
                fill_unset(style, resolve_run(nullptr, rest, level, scope.colors));
                return complete(style, nullptr, scope);
            }

            static std::string typeface_of(const RunStyle& style, const std::string& text) {
                const std::string latin = style.latin.value_or("");
                const std::string ea = style.ea.value_or("");
                if (!ea.empty() && (has_hangul(text) || latin.empty())) {
                    return ea;
                }
                return latin.empty() ? ea : latin;
            }

            // templide의 테마 글꼴은 하나라서, 한글이 있는 발표 자료면 한글 글꼴(ea)을, 아니면 영문 글꼴(latin)을 테마 글꼴로 쓴다
            std::string pick_font(const std::string& latin, const std::string& ea) const {
                return presentation_.hangul && !ea.empty() ? ea : latin.empty() ? ea : latin;
            }

            // +mn-lt 같은 테마 글꼴은 templide 테마 글꼴과 같은 글꼴이면 theme.body_font로, 아니면 그 글꼴 이름으로 쓴다
            std::string font_code_in(const std::string& typeface, const Scope& scope) const {
                if (!typeface.starts_with("+mj") && !typeface.starts_with("+mn")) {
                    return font_code(typeface);
                }
                const bool major = typeface.starts_with("+mj");
                const std::string reference = major ? "theme.heading_font" : "theme.body_font";
                if (scope.master == nullptr) {
                    return reference;
                }
                const Theme& theme = scope.master->theme;
                const std::string& latin = major ? theme.major_latin : theme.minor_latin;
                const std::string& ea = major ? theme.major_ea : theme.minor_ea;
                const std::string wanted = typeface.ends_with("-ea") ? (ea.empty() ? latin : ea) : (latin.empty() ? ea : latin);
                return wanted.empty() || wanted == pick_font(latin, ea) ? reference : "font(" + quote(wanted) + ")";
            }

            Baseline baseline_of(const RunStyle& style, const ParagraphStyle& paragraph, const std::string& sample, const Scope& scope) const {
                Baseline baseline;
                baseline.size = style.size.value_or(18);
                baseline.color = style.color ? color_code(*style.color) : "theme.dark1";
                const std::string typeface = typeface_of(style, sample);
                baseline.font = typeface.empty() ? "theme.body_font" : font_code_in(typeface, scope);
                baseline.bold = style.bold.value_or(false);
                baseline.italic = style.italic.value_or(false);
                baseline.align = paragraph.align.value_or("l");
                return baseline;
            }

            std::vector<std::string> run_properties(const RunStyle& style, const Baseline& baseline, const std::string& text, const Scope& scope) const {
                std::vector<std::string> result;
                if (style.size && std::abs(*style.size - baseline.size) > 0.01) {
                    result.push_back("font-size = " + pt(*style.size));
                }
                if (style.bold.value_or(false) != baseline.bold) {
                    result.push_back(std::string("font-weight = ") + (style.bold.value_or(false) ? "bold" : "normal"));
                }
                if (style.italic.value_or(false) != baseline.italic) {
                    result.push_back(std::string("font-style = ") + (style.italic.value_or(false) ? "italic" : "normal"));
                }
                const std::string underline = style.underline.value_or("none");
                const std::string strike = style.strike.value_or("noStrike");
                if (underline != "none") {
                    result.push_back(std::string("text-decoration = ") + (underline == "dbl" ? "double_underline" : underline.starts_with("wavy") ? "wavy_underline" : "underline"));
                } else if (strike == "sngStrike" || strike == "dblStrike") {
                    result.push_back(std::string("text-decoration = ") + (strike == "dblStrike" ? "double_line_through" : "line_through"));
                }
                if (const std::string cap = style.cap.value_or("none"); cap == "all" || cap == "small") {
                    result.push_back(std::string("text-transform = ") + (cap == "all" ? "uppercase" : "small_caps"));
                }
                if (style.spacing && std::abs(*style.spacing) > 0.01) {
                    result.push_back("letter-spacing = " + pt(*style.spacing));
                }
                if (style.baseline && std::abs(*style.baseline) > 0.5) {
                    result.push_back(std::string("vertical-align = ") + (*style.baseline > 0 ? "super" : "sub"));
                }
                if (style.color) {
                    const std::string code = color_code(*style.color);
                    if (code != baseline.color) {
                        result.push_back("color = " + code);
                    }
                }
                if (style.highlight) {
                    result.push_back("highlight = " + color_code(*style.highlight));
                }
                if (const std::string typeface = typeface_of(style, text); !typeface.empty()) {
                    const std::string code = font_code_in(typeface, scope);
                    if (code != baseline.font) {
                        result.push_back("font-family = " + code);
                    }
                }
                return result;
            }

            static std::string kind_of(const ParagraphStyle& style) {
                const std::string bullet = style.bullet.value_or("none");
                if (bullet == "auto") {
                    return "numbers";
                }
                if (bullet == "char") {
                    return style.bullet_char == "\xE2\x80\x93" || style.bullet_char == "-" ? "dashes" : "bullets";
                }
                if (bullet == "blip") {
                    return "bullets";
                }
                return "none";
            }

            std::vector<std::string> paragraph_properties(const ParagraphPiece& paragraph, const Baseline& baseline) const {
                std::vector<std::string> result;
                const ParagraphStyle& style = paragraph.style;
                static const std::map<std::string, std::string> aligns = {{"l", "left"}, {"ctr", "center"}, {"r", "right"}, {"just", "justify"}, {"dist", "justify"}};
                const std::string align = style.align.value_or("l");
                if (align != baseline.align && aligns.contains(align)) {
                    result.push_back("text-align = " + aligns.at(align));
                }
                const double size = paragraph.pieces.empty() ? baseline.size : paragraph.pieces.front().style.size.value_or(baseline.size);
                if (style.line_percent && std::abs(*style.line_percent - 1) > 0.005) {
                    result.push_back("line-height = " + format(*style.line_percent));
                } else if (style.line_points && size > 0) {
                    result.push_back("line-height = " + format(*style.line_points / (size * 1.2)));
                }
                const auto spacing = [&](const std::optional<double>& points, const std::optional<double>& percent, const char* name) {
                    const double value = points ? *points : percent ? *percent * size * 1.2 : 0;
                    if (value > 0.05) {
                        result.push_back(std::string(name) + " = " + pt(value));
                    }
                };
                spacing(style.before_points, style.before_percent, "space-before");
                spacing(style.after_points, style.after_percent, "space-after");
                const bool listed = paragraph.kind != "none";
                const double default_margin = listed ? 342900.0 * (paragraph.depth + 1) : 0;
                const double default_indent = listed ? -342900.0 : 0;
                if (style.margin && std::abs(static_cast<double>(*style.margin) - default_margin) > 6350) {
                    result.push_back("margin-left = " + pt(static_cast<double>(*style.margin) / emu_per_pt));
                }
                if (style.indent && std::abs(static_cast<double>(*style.indent) - default_indent) > 6350) {
                    result.push_back("text-indent = " + pt(static_cast<double>(*style.indent) / emu_per_pt));
                }
                if (paragraph.kind == "bullets" && style.bullet == "char" && style.bullet_char != "\xE2\x80\xA2") {
                    result.push_back("list-marker = " + quote(style.bullet_char));
                }
                if (listed && style.bullet_color) {
                    result.push_back("list-marker-color = " + color_code(*style.bullet_color));
                }
                if (paragraph.kind == "numbers") {
                    static const std::map<std::string, std::string> types = {
                        {"alphaLcPeriod", "lower_alpha"}, {"alphaLcParenR", "lower_alpha"}, {"alphaLcParenBoth", "lower_alpha"}, {"alphaUcPeriod", "upper_alpha"},
                        {"alphaUcParenR", "upper_alpha"}, {"alphaUcParenBoth", "upper_alpha"}, {"romanLcPeriod", "lower_roman"}, {"romanLcParenR", "lower_roman"},
                        {"romanLcParenBoth", "lower_roman"}, {"romanUcPeriod", "upper_roman"}, {"romanUcParenR", "upper_roman"}, {"romanUcParenBoth", "upper_roman"},
                        {"circleNumDbPlain", "circled"}, {"circleNumWdBlackPlain", "circled"}, {"circleNumWdWhitePlain", "circled"},
                    };
                    if (const auto it = types.find(style.auto_type); it != types.end()) {
                        result.push_back("list-style = " + it->second);
                    }
                    if (style.start_at && *style.start_at > 1) {
                        result.push_back("list-start = " + std::to_string(*style.start_at));
                    }
                }
                return result;
            }

            std::vector<ParagraphPiece> read_paragraphs(const TextJob& job, const Scope& scope) {
                std::vector<ParagraphPiece> paragraphs;
                if (job.body == nullptr) {
                    return paragraphs;
                }
                for (const auto* paragraph : job.body->all("a:p")) {
                    ParagraphPiece piece;
                    const auto* properties = paragraph->child("a:pPr");
                    piece.level = properties != nullptr ? static_cast<int>(std::clamp(properties->integer("lvl").value_or(0), 0LL, 8LL)) : 0;
                    piece.style = resolve_paragraph(properties, job.chain, piece.level, scope.colors);
                    piece.kind = kind_of(piece.style);
                    if (job.line_reduction > 0) {
                        const double factor = 1 - job.line_reduction;
                        piece.style.line_percent = piece.style.line_percent.value_or(1) - job.line_reduction;
                        if (piece.style.before_points) {
                            *piece.style.before_points *= factor;
                        }
                        if (piece.style.after_points) {
                            *piece.style.after_points *= factor;
                        }
                    }
                    // 빈 문단의 높이는 문단 끝 글자의 크기를 따른다
                    piece.end_style = text_run(paragraph->child("a:endParaRPr"), job.chain, job.own, job.font_reference, piece.level, scope);
                    if (piece.end_style.size) {
                        *piece.end_style.size *= job.font_scale;
                    }
                    const std::function<void(const xml::Node&)> collect = [&](const xml::Node& node) {
                        for (const auto& child : node.children) {
                            if (child.name == "mc:AlternateContent") {
                                if (const auto* fallback = child.child("mc:Fallback")) {
                                    collect(*fallback);
                                }
                                continue;
                            }
                            if (child.name != "a:r" && child.name != "a:br" && child.name != "a:fld") {
                                continue;
                            }
                            Piece run;
                            const auto* run_properties = child.child("a:rPr");
                            run.style = text_run(run_properties, job.chain, job.own, job.font_reference, piece.level, scope);
                            if (run.style.size) {
                                *run.style.size *= job.font_scale;
                            }
                            if (child.name == "a:br") {
                                run.text = "\n";
                            } else {
                                if (const auto* text = child.child("a:t")) {
                                    run.text = text->text;
                                }
                                if (child.name == "a:fld") {
                                    const std::string type = child.get("type");
                                    run.field = type == "slidenum" ? "slidenum" : type.starts_with("datetime") ? "datetime" : "";
                                }
                            }
                            if (run_properties != nullptr) {
                                if (const auto* click = run_properties->child("a:hlinkClick")) {
                                    if (const auto code = action_of(*click, *scope.part, scope.where); code && !code->code.empty()) {
                                        run.link = (code->link ? "link(" : "action(") + code->code + ")";
                                    }
                                }
                                if (const auto* hover = run_properties->child("a:hlinkMouseOver")) {
                                    if (const auto code = action_of(*hover, *scope.part, scope.where); code && !code->code.empty()) {
                                        run.hover = "hover_action(" + code->code + ")";
                                    }
                                }
                            }
                            if (run.text.empty() && run.field.empty()) {
                                continue;
                            }
                            piece.pieces.push_back(run);
                        }
                    };
                    collect(*paragraph);
                    paragraphs.push_back(std::move(piece));
                }
                // 글이 없는 문단은 글머리표가 보이지 않는다. 크기를 지키려고 공백을 넣으면 글머리표가 생기므로 목록에서 뺀다 (번호는 이어져야 하므로 그대로 둔다)
                for (auto& piece : paragraphs) {
                    if (piece.pieces.empty() && piece.kind != "numbers" && piece.kind != "none") {
                        piece.kind = "none";
                        piece.style.margin.reset();
                        piece.style.indent.reset();
                    }
                }
                return paragraphs;
            }

            // 문단들의 목록 깊이를 정한다. 목록 안에서 더 깊은 level의 문단은 앞 항목 아래의 목록이 된다
            static void assign_depths(std::vector<ParagraphPiece>& paragraphs) {
                std::vector<int> levels; // 지금 열린 목록들의 level
                for (auto& paragraph : paragraphs) {
                    if (paragraph.kind == "none") {
                        levels.clear();
                        paragraph.depth = 0;
                        continue;
                    }
                    while (!levels.empty() && levels.back() > paragraph.level) {
                        levels.pop_back();
                    }
                    if (levels.empty() || levels.back() < paragraph.level) {
                        levels.push_back(paragraph.level);
                    }
                    paragraph.depth = static_cast<int>(levels.size()) - 1;
                }
            }

            std::string paragraph_code(const ParagraphPiece& paragraph, const Baseline& baseline, const Scope& scope) const {
                // 모양이 같은 이웃 조각은 합친다
                std::vector<std::pair<std::vector<std::string>, Piece>> runs;
                for (const auto& piece : paragraph.pieces) {
                    std::vector<std::string> properties = run_properties(piece.style, baseline, piece.text, scope);
                    if (!runs.empty() && piece.field.empty() && runs.back().second.field.empty() && runs.back().first == properties && runs.back().second.link == piece.link
                        && runs.back().second.hover == piece.hover) {
                        runs.back().second.text += piece.text;
                        continue;
                    }
                    runs.emplace_back(std::move(properties), piece);
                }
                const std::vector<std::string> paragraph_level = paragraph_properties(paragraph, baseline);
                if (runs.empty()) {
                    // 글이 없는 문단은 templide에서 기본 크기가 되므로, 크기가 다르면 공백 하나에 그 크기를 준다
                    std::vector<std::string> properties = paragraph_level;
                    std::string text = "\"\"";
                    if (paragraph.end_style.size && std::abs(*paragraph.end_style.size - baseline.size) > 0.01) {
                        properties.push_back("font-size = " + pt(*paragraph.end_style.size));
                        text = "\" \"";
                    }
                    if (properties.empty()) {
                        return text;
                    }
                    std::string joined;
                    for (const auto& property : properties) {
                        joined += (joined.empty() ? "" : ", ") + property;
                    }
                    return "(style(" + joined + ") " + text + ")";
                }
                std::string code;
                for (std::size_t i = 0; i < runs.size(); ++i) {
                    std::vector<std::string> properties = runs[i].first;
                    if (i == 0) {
                        properties.insert(properties.begin(), paragraph_level.begin(), paragraph_level.end());
                    }
                    const Piece& piece = runs[i].second;
                    std::string value;
                    if (piece.field == "slidenum") {
                        value = "@slide.number";
                    } else if (piece.field == "datetime") {
                        value = "@date";
                    } else {
                        value = quote(piece.text);
                    }
                    std::string parts;
                    if (!properties.empty()) {
                        std::string joined;
                        for (const auto& property : properties) {
                            joined += (joined.empty() ? "" : ", ") + property;
                        }
                        parts += "style(" + joined + ") ";
                    }
                    if (!piece.link.empty()) {
                        parts += piece.link + " ";
                    }
                    if (!piece.hover.empty()) {
                        parts += piece.hover + " ";
                    }
                    code += (code.empty() ? "" : " ") + (parts.empty() ? value : "(" + parts + value + ")");
                }
                return code;
            }

            std::string list_word(const std::string& kind) const {
                return kind == "numbers" ? "numbers" : kind == "dashes" ? "dashes" : "bullets";
            }

            // [begin, end)의 목록 문단들. 모두 depth 이상이고 begin은 depth에 있거나 더 깊다. 종류가 바뀌면 목록을 나눈다
            std::vector<std::string> list_code(const std::vector<ParagraphPiece>& paragraphs, const std::vector<std::string>& codes, std::size_t begin, std::size_t end, int depth) const {
                std::vector<std::string> lists;
                std::vector<std::string> items;
                std::string kind;
                std::size_t i = begin;
                const auto flush = [&]() {
                    if (!items.empty()) {
                        std::string joined;
                        for (const auto& item : items) {
                            joined += (joined.empty() ? "" : ", ") + item;
                        }
                        lists.push_back(list_word(kind) + " [" + joined + "]");
                        items.clear();
                    }
                };
                while (i < end) {
                    if (paragraphs[i].depth > depth) {
                        std::size_t j = i;
                        while (j < end && paragraphs[j].depth > depth) {
                            ++j;
                        }
                        if (items.empty()) {
                            kind = paragraphs[i].kind;
                        }
                        // 안쪽 목록은 하나씩 바깥 목록의 항목이 된다
                        for (auto& inner : list_code(paragraphs, codes, i, j, depth + 1)) {
                            items.push_back(std::move(inner));
                        }
                        i = j;
                        continue;
                    }
                    if (!items.empty() && paragraphs[i].kind != kind) {
                        flush();
                    }
                    kind = paragraphs[i].kind;
                    items.push_back(codes[i]);
                    ++i;
                }
                flush();
                return lists;
            }

            std::string text_code(const TextJob& job, const Scope& scope) {
                std::vector<ParagraphPiece> paragraphs = read_paragraphs(job, scope);
                if (paragraphs.empty() || (paragraphs.size() == 1 && paragraphs.front().pieces.empty())) {
                    return "\"\"";
                }
                assign_depths(paragraphs);
                std::vector<std::string> codes;
                for (const auto& paragraph : paragraphs) {
                    codes.push_back(paragraph_code(paragraph, job.baseline(paragraph.kind == "none" ? 0 : paragraph.depth), scope));
                }
                // 목록이 아닌 문단과 목록을 차례로 잇는다
                std::string result;
                std::vector<std::string> plain;
                const auto flush_plain = [&]() {
                    if (plain.empty()) {
                        return;
                    }
                    std::string part;
                    if (plain.size() == 1 && result.empty() && paragraphs.size() == 1) {
                        part = plain.front();
                    } else {
                        std::string joined;
                        for (const auto& code : plain) {
                            joined += (joined.empty() ? "" : ", ") + code;
                        }
                        part = "paragraphs [" + joined + "]";
                    }
                    result += (result.empty() ? "" : " ") + part;
                    plain.clear();
                };
                std::size_t i = 0;
                while (i < paragraphs.size()) {
                    if (paragraphs[i].kind == "none") {
                        plain.push_back(codes[i]);
                        ++i;
                        continue;
                    }
                    flush_plain();
                    std::size_t j = i;
                    while (j < paragraphs.size() && paragraphs[j].kind != "none") {
                        ++j;
                    }
                    for (const auto& list : list_code(paragraphs, codes, i, j, 0)) {
                        result += (result.empty() ? "" : " ") + list;
                    }
                    i = j;
                }
                flush_plain();
                return result;
            }

            // 글상자 속성 (anchor, padding, autofit 등). anchor는 object의 기본값과 다를 때만 쓴다
            void add_body(Properties& props, const Body& body, const std::string& default_anchor) {
                static const std::map<std::string, std::string> anchors = {{"t", "top"}, {"ctr", "middle"}, {"b", "bottom"}, {"just", "middle"}, {"dist", "middle"}};
                if (const auto it = body.attributes.find("anchor"); it != body.attributes.end() && anchors.contains(it->second) && anchors.at(it->second) != default_anchor) {
                    props.add("anchor", anchors.at(it->second));
                }
                const std::array<std::pair<const char*, long long>, 4> insets = {{{"lIns", 91440}, {"tIns", 45720}, {"rIns", 91440}, {"bIns", 45720}}};
                const std::array<const char*, 4> names = {"padding_left", "padding_top", "padding_right", "padding_bottom"};
                for (std::size_t i = 0; i < insets.size(); ++i) {
                    if (const auto it = body.attributes.find(insets[i].first); it != body.attributes.end()) {
                        try {
                            const long long value = std::stoll(it->second);
                            if (std::llabs(value - insets[i].second) > 127) {
                                props.add(names[i], pt(static_cast<double>(value) / emu_per_pt));
                            }
                        } catch (...) {
                        }
                    }
                }
                if (body.autofit == "a:normAutofit") {
                    props.add("autofit", "shrink");
                } else if (body.autofit == "a:spAutoFit") {
                    props.add("autofit", "resize");
                }
                if (const auto it = body.attributes.find("wrap"); it != body.attributes.end() && it->second == "none") {
                    props.add("wrap", "false");
                }
                static const std::map<std::string, std::string> directions = {
                    {"vert", "vertical"}, {"vert270", "vertical270"}, {"wordArtVert", "stacked"}, {"eaVert", "east_asian"}, {"mongolianVert", "vertical"}, {"wordArtVertRtl", "stacked"},
                };
                if (const auto it = body.attributes.find("vert"); it != body.attributes.end() && directions.contains(it->second)) {
                    props.add("text_direction", directions.at(it->second));
                }
                if (const auto it = body.attributes.find("numCol"); it != body.attributes.end() && it->second != "1") {
                    props.add("columns", it->second);
                    if (const auto gap = body.attributes.find("spcCol"); gap != body.attributes.end()) {
                        try {
                            props.add("column_gap", pt(static_cast<double>(std::stoll(gap->second)) / emu_per_pt));
                        } catch (...) {
                        }
                    }
                }
            }

            // ---- 개체

            static void add_frame(Properties& props, const Frame& frame) {
                props.raw("x = " + px(frame.x) + "; y = " + px(frame.y) + "; width = " + px(std::max(0.0, frame.w)) + "; height = " + px(std::max(0.0, frame.h)) + ";");
                if (std::abs(frame.rotation) > 0.01 && std::abs(frame.rotation - 360) > 0.01) {
                    props.add("rotation", format(frame.rotation));
                }
                if (frame.flip_h || frame.flip_v) {
                    props.add("flip", frame.flip_h && frame.flip_v ? "both" : frame.flip_h ? "horizontal" : "vertical");
                }
            }

            // prstGeom의 조정값. 둥근 모서리는 radius로 쓴다
            static void add_geometry(Properties& props, const xml::Node* preset, const Frame& frame) {
                if (preset == nullptr) {
                    return;
                }
                const std::string kind = preset->get("prst");
                const auto defaults = geometry::shape_adjustments().find(kind);
                const auto* list = preset->child("a:avLst");
                if (defaults == geometry::shape_adjustments().end() || list == nullptr) {
                    return;
                }
                static const std::set<std::string> rounded = {"roundRect", "round1Rect", "round2SameRect", "round2DiagRect", "snipRoundRect"};
                for (const auto* guide : list->all("a:gd")) {
                    const std::string name = guide->get("name");
                    const std::string formula = guide->get("fmla");
                    if (!formula.starts_with("val ")) {
                        continue;
                    }
                    double value = 0;
                    try {
                        value = std::stod(formula.substr(4));
                    } catch (...) {
                        continue;
                    }
                    for (std::size_t i = 0; i < defaults->second.size(); ++i) {
                        const std::string& default_name = defaults->second[i].first;
                        if (default_name != name && !(name == "adj" && default_name == "adj1") && !(name == "adj1" && default_name == "adj")) {
                            continue;
                        }
                        if (std::abs(value - static_cast<double>(defaults->second[i].second)) < 0.5) {
                            break;
                        }
                        if (i == 0 && rounded.contains(kind)) {
                            props.add("radius", px_float(value / 100000 * std::min(frame.w, frame.h)));
                        } else {
                            props.add("adj" + std::to_string(i + 1), format(value / 100000, 5));
                        }
                        break;
                    }
                }
            }

            std::string name_of(const Shape& shape, const Scope& scope) const {
                const auto it = scope.names.find(shape.id);
                return it == scope.names.end() ? "" : " as " + it->second;
            }

            void add_animations(std::vector<std::string>& lines, int target, const Scope& scope) {
                const auto it = scope.animations.find(target);
                if (it == scope.animations.end()) {
                    return;
                }
                for (const auto& [animation, order] : it->second) {
                    std::string line = "animate " + animation->category + " " + animation->effect;
                    if (animation->effect == "path") {
                        line += " " + quote(animation->path);
                    } else if (!animation->option.empty()) {
                        line += "." + animation->option;
                    }
                    if (animation->duration) {
                        line += " " + seconds(*animation->duration);
                    }
                    if (animation->start != "on_click") {
                        line += " " + animation->start;
                    }
                    if (animation->delay) {
                        line += " delay " + seconds(*animation->delay);
                    }
                    lines.push_back(line + " order " + std::to_string(order) + ";");
                    for (const auto& note : animation->notes) {
                        warn(scope.where, note);
                    }
                }
            }

            std::string where_of(const Shape& shape, const Scope& scope) const {
                return scope.where + ", " + (shape.name.empty() ? shape.object : shape.name);
            }

            // 글이 있는 개체 (글상자, 도형, 슬라이드에서 글상자로 옮기는 개체 틀)
            std::string text_of(const Shape& shape, const std::vector<const xml::Node*>& chain, const Scope& scope, const Baseline& baseline) {
                const auto* body = text_body(*shape.node);
                if (body == nullptr) {
                    return "\"\"";
                }
                TextJob job;
                job.body = body;
                job.chain = text_chain(chain, shape, scope, &job.own);
                const Body body_properties = read_body(chain);
                job.font_scale = body_properties.font_scale;
                job.line_reduction = body_properties.line_reduction;
                if (const auto* style = first_style(chain)) {
                    job.font_reference = style->child("a:fontRef");
                }
                job.baseline = [baseline](int) { return baseline; };
                return text_code(job, scope);
            }

            static Baseline object_baseline(const std::string& object) {
                Baseline baseline;
                baseline.color = object == "shape" || object == "freeform" ? "theme.light1" : "theme.dark1";
                baseline.align = object == "shape" ? "ctr" : "l";
                return baseline;
            }

            std::string emit_text_object(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent, const std::string& object) {
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                const Frame frame = map_frame(read_xfrm(first_xfrm(chain)), group);
                Properties props;
                std::string kind;
                const xml::Node* preset = nullptr;
                for (const auto* node : chain) {
                    if (const auto* geometry = node->path({"p:spPr", "a:prstGeom"})) {
                        preset = geometry;
                        break;
                    }
                }
                if (object == "shape") {
                    kind = preset != nullptr ? preset->get("prst", "rect") : "rect";
                    props.add("kind", kind);
                }
                if (object == "text_box" || shape.has_text) {
                    props.add("text", text_of(shape, chain, scope, object_baseline(object)));
                }
                add_frame(props, frame);
                if (object == "shape") {
                    add_geometry(props, preset, frame);
                }
                add_body(props, read_body(chain), object == "shape" ? "middle" : "top");
                add_paint(props, chain, scope, object, where);
                add_effects(props, chain, scope, where);
                add_actions(props, shape, scope, where);
                add_animations(props.lines, shape.id, scope);
                return block("put " + object + name_of(shape, scope), props.lines, indent);
            }

            std::string emit_image(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent) {
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                const auto* fill = shape.node->child("p:blipFill");
                const auto* blip = fill != nullptr ? fill->child("a:blip") : nullptr;
                const auto* relationship = blip != nullptr ? scope.part->rel(blip->get("r:embed")) : nullptr;
                if (relationship == nullptr) {
                    warn(where, "그림 파일을 찾을 수 없어 뺐습니다");
                    return "";
                }
                const Frame frame = map_frame(read_xfrm(first_xfrm(chain)), group);
                Properties props;
                props.add("data", image_media(relationship->target, read_image_effects(blip, scope.colors), std::to_string(reinterpret_cast<std::uintptr_t>(blip)), where));
                add_frame(props, frame);
                if (const auto* crop = fill->child("a:srcRect")) {
                    const std::array<std::pair<const char*, const char*>, 4> sides = {{{"l", "crop_left"}, {"t", "crop_top"}, {"r", "crop_right"}, {"b", "crop_bottom"}}};
                    for (const auto& [attribute, name] : sides) {
                        const double value = static_cast<double>(crop->integer(attribute).value_or(0)) / 1000;
                        if (value > 0.001) {
                            props.add(name, format(std::min(value, 99.0)) + "%");
                        }
                    }
                }
                if (const auto* alpha = blip->child("a:alphaModFix")) {
                    props.add("opacity", format(static_cast<double>(alpha->integer("amt").value_or(100000)) / 100000));
                }
                if (const auto* preset = shape.node->path({"p:spPr", "a:prstGeom"}); preset != nullptr && preset->get("prst", "rect") != "rect") {
                    props.add("kind", preset->get("prst"));
                    add_geometry(props, preset, frame);
                }
                add_paint(props, chain, scope, "image", where);
                add_effects(props, chain, scope, where);
                add_actions(props, shape, scope, where);
                add_animations(props.lines, shape.id, scope);
                return block("put image" + name_of(shape, scope), props.lines, indent);
            }

            // 비디오와 오디오의 재생 설정은 슬라이드 timing의 p:video, p:audio에 있다
            const xml::Node* media_node(const Scope& scope, int id) const {
                if (scope.slide == nullptr) {
                    return nullptr;
                }
                const auto* timing = scope.slide->part->root.child("p:timing");
                if (timing == nullptr) {
                    return nullptr;
                }
                std::vector<const xml::Node*> nodes;
                find_all(*timing, "p:video", nodes);
                find_all(*timing, "p:audio", nodes);
                for (const auto* node : nodes) {
                    if (const auto* target = node->find("p:spTgt"); target != nullptr && target->integer("spid").value_or(-1) == id) {
                        return node;
                    }
                }
                return nullptr;
            }

            std::string emit_media(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent) {
                const bool video = shape.object == "video";
                const std::string where = where_of(shape, scope);
                const auto* nvPr = shape.node->path({"p:nvPicPr", "p:nvPr"});
                const auto* embedded = nvPr != nullptr ? nvPr->find("p14:media") : nullptr;
                const auto* relationship = embedded != nullptr ? scope.part->rel(embedded->get("r:embed")) : nullptr;
                if (relationship == nullptr) {
                    warn(where, "미디어 파일을 찾을 수 없어 뺐습니다");
                    return "";
                }
                const auto chain = inheritance(shape, scope);
                const Frame frame = map_frame(read_xfrm(first_xfrm(chain)), group);
                Properties props;
                props.add("data", media(relationship->target, video ? "video" : "audio"));
                add_frame(props, frame);
                if (video) {
                    if (const auto* blip = shape.node->path({"p:blipFill", "a:blip"})) {
                        if (const auto* poster = scope.part->rel(blip->get("r:embed")); poster != nullptr && !poster->external && supported_image(poster->target)) {
                            props.add("poster", media(poster->target, "image"));
                        }
                    }
                }
                if (scope.slide != nullptr) {
                    const auto start = scope.slide->media_start.find(shape.id);
                    props.add("start", start != scope.slide->media_start.end() ? start->second : "when_clicked");
                }
                if (const auto* node = media_node(scope, shape.id)) {
                    if (node->flag("fullScrn") && video) {
                        props.add("fullscreen", "true");
                    }
                    if (const auto* media_node = node->child("p:cMediaNode")) {
                        if (const auto volume = media_node->integer("vol"); volume && *volume != 100000) {
                            props.add("volume", format(static_cast<double>(*volume) / 1000) + "%");
                        }
                        if (!media_node->flag("showWhenStopped", true)) {
                            props.add(video ? "hide_when_stopped" : "hide_icon", "true");
                        }
                        if (!video && media_node->integer("numSld").value_or(1) > 1) {
                            props.add("across_slides", "true");
                        }
                        if (const auto* time = media_node->child("p:cTn")) {
                            if (time->get("repeatCount") == "indefinite") {
                                props.add("loop", "true");
                            }
                            if (time->get("fill") == "remove") {
                                props.add("rewind", "true");
                            }
                        }
                    }
                }
                if (const auto* trim = embedded->child("p14:trim")) {
                    if (const auto value = trim->integer("st"); value && *value > 0) {
                        props.add("trim_start", seconds(*value));
                    }
                    if (const auto value = trim->integer("end"); value && *value > 0) {
                        props.add("trim_end", seconds(*value));
                    }
                }
                if (const auto* fade = embedded->child("p14:fade")) {
                    if (const auto value = fade->integer("in"); value && *value > 0) {
                        props.add("fade_in", seconds(*value));
                    }
                    if (const auto value = fade->integer("out"); value && *value > 0) {
                        props.add("fade_out", seconds(*value));
                    }
                }
                add_effects(props, chain, scope, where);
                add_animations(props.lines, shape.id, scope);
                return block(std::string("put ") + (video ? "video" : "audio") + name_of(shape, scope), props.lines, indent);
            }

            // 두 점. 상자의 왼쪽 위에서 오른쪽 아래로 가는 선을 뒤집고 돌린다
            void line_points(const Frame& local, const GroupTransform* group, double& x1, double& y1, double& x2, double& y2) const {
                x1 = local.x;
                y1 = local.y;
                x2 = local.x + local.w;
                y2 = local.y + local.h;
                if (local.flip_h) {
                    std::swap(x1, x2);
                }
                if (local.flip_v) {
                    std::swap(y1, y2);
                }
                if (std::abs(local.rotation) > 0.01) {
                    const double cx = local.x + local.w / 2;
                    const double cy = local.y + local.h / 2;
                    const double radians = local.rotation * std::numbers::pi / 180;
                    const auto rotate = [&](double& x, double& y) {
                        const double dx = x - cx;
                        const double dy = y - cy;
                        x = cx + dx * std::cos(radians) - dy * std::sin(radians);
                        y = cy + dx * std::sin(radians) + dy * std::cos(radians);
                    };
                    rotate(x1, y1);
                    rotate(x2, y2);
                }
                map_point(x1, y1, group);
                map_point(x2, y2, group);
            }

            std::string emit_line(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent) {
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                double x1 = 0;
                double y1 = 0;
                double x2 = 0;
                double y2 = 0;
                line_points(read_xfrm(first_xfrm(chain)), group, x1, y1, x2, y2);
                Properties props;
                props.raw("x1 = " + px(x1) + "; y1 = " + px(y1) + "; x2 = " + px(x2) + "; y2 = " + px(y2) + ";");
                add_arrows(props, chain, scope);
                add_paint(props, chain, scope, "line", where);
                add_effects(props, chain, scope, where);
                add_actions(props, shape, scope, where);
                add_animations(props.lines, shape.id, scope);
                return block("put line" + name_of(shape, scope), props.lines, indent);
            }

            std::string emit_connector(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent) {
                const auto* properties = shape.node->path({"p:nvCxnSpPr", "p:cNvCxnSpPr"});
                const auto* start = properties != nullptr ? properties->child("a:stCxn") : nullptr;
                const auto* end = properties != nullptr ? properties->child("a:endCxn") : nullptr;
                const auto named = [&](const xml::Node* connection) {
                    return connection != nullptr && scope.names.contains(static_cast<int>(connection->integer("id").value_or(-1)));
                };
                if (!named(start) || !named(end)) {
                    return emit_line(shape, scope, group, indent);
                }
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                Properties props;
                const int from = static_cast<int>(start->integer("id").value_or(0));
                const int to = static_cast<int>(end->integer("id").value_or(0));
                props.add("from", scope.names.at(from));
                props.add("to", scope.names.at(to));
                const auto* preset = shape.node->path({"p:spPr", "a:prstGeom"});
                const std::string kind = preset != nullptr ? preset->get("prst") : "straightConnector1";
                if (kind.starts_with("bentConnector")) {
                    props.add("kind", "elbow");
                } else if (kind.starts_with("curvedConnector")) {
                    props.add("kind", "curved");
                }
                // 연결점 번호 -> 붙는 쪽
                const auto side = [&](int target, long long index) -> std::string {
                    const Shape* found = find_shape(scope, target);
                    std::string target_kind = "rect";
                    if (found != nullptr) {
                        if (const auto* geometry = found->node->path({"p:spPr", "a:prstGeom"})) {
                            target_kind = geometry->get("prst", "rect");
                        }
                    }
                    const auto sites = geometry::connection_sites().find(target_kind);
                    if (sites == geometry::connection_sites().end()) {
                        return "";
                    }
                    static const std::array<const char*, 4> names = {"top", "right", "bottom", "left"};
                    for (std::size_t i = 0; i < sites->second.size(); ++i) {
                        if (sites->second[i].index == index) {
                            return names[i];
                        }
                    }
                    return "";
                };
                if (const std::string from_side = side(from, start->integer("idx").value_or(-1)); !from_side.empty()) {
                    props.add("from_side", from_side);
                }
                if (const std::string to_side = side(to, end->integer("idx").value_or(-1)); !to_side.empty()) {
                    props.add("to_side", to_side);
                }
                add_arrows(props, chain, scope);
                add_paint(props, chain, scope, "connector", where);
                add_effects(props, chain, scope, where);
                add_actions(props, shape, scope, where);
                add_animations(props.lines, shape.id, scope);
                return block("put connector" + name_of(shape, scope), props.lines, indent);
            }

            static const Shape* find_in(const std::vector<Shape>& shapes, int id) {
                for (const auto& shape : shapes) {
                    if (shape.id == id) {
                        return &shape;
                    }
                    if (const Shape* found = find_in(shape.children, id)) {
                        return found;
                    }
                }
                return nullptr;
            }

            const Shape* find_shape(const Scope& scope, int id) const {
                if (scope.slide != nullptr) {
                    return find_in(scope.slide->shapes, id);
                }
                if (scope.layout != nullptr) {
                    return find_in(scope.layout->shapes, id);
                }
                return scope.master != nullptr ? find_in(scope.master->shapes, id) : nullptr;
            }

            // ---- 자유형

            // DrawingML 도형 수식(gdLst)을 계산하는 작은 계산기
            struct Guides {
                std::map<std::string, double> values;

                double get(const std::string& token) const {
                    if (const auto it = values.find(token); it != values.end()) {
                        return it->second;
                    }
                    try {
                        return std::stod(token);
                    } catch (...) {
                        return 0;
                    }
                }

                void evaluate(const std::string& name, const std::string& formula) {
                    std::stringstream stream(formula);
                    std::string op;
                    stream >> op;
                    std::vector<double> args;
                    for (std::string token; stream >> token;) {
                        args.push_back(get(token));
                    }
                    args.resize(3, 0);
                    const double x = args[0];
                    const double y = args[1];
                    const double z = args[2];
                    const double radians = std::numbers::pi / 180 / 60000;
                    double result = 0;
                    if (op == "val") {
                        result = x;
                    } else if (op == "*/") {
                        result = z != 0 ? x * y / z : 0;
                    } else if (op == "+-") {
                        result = x + y - z;
                    } else if (op == "+/") {
                        result = z != 0 ? (x + y) / z : 0;
                    } else if (op == "?:") {
                        result = x > 0 ? y : z;
                    } else if (op == "abs") {
                        result = std::abs(x);
                    } else if (op == "at2") {
                        result = std::atan2(y, x) / radians;
                    } else if (op == "cat2") {
                        result = x * std::cos(std::atan2(z, y));
                    } else if (op == "sat2") {
                        result = x * std::sin(std::atan2(z, y));
                    } else if (op == "cos") {
                        result = x * std::cos(y * radians);
                    } else if (op == "sin") {
                        result = x * std::sin(y * radians);
                    } else if (op == "tan") {
                        result = x * std::tan(y * radians);
                    } else if (op == "max") {
                        result = std::max(x, y);
                    } else if (op == "min") {
                        result = std::min(x, y);
                    } else if (op == "mod") {
                        result = std::sqrt(x * x + y * y + z * z);
                    } else if (op == "pin") {
                        result = y < x ? x : y > z ? z : y;
                    } else if (op == "sqrt") {
                        result = std::sqrt(std::max(0.0, x));
                    }
                    values[name] = result;
                }
            };

            // custGeom의 경로를 상자 안 px 좌표의 SVG path로
            std::string freeform_path(const xml::Node& geometry, const Frame& frame) const {
                Guides guides;
                const double w = frame.w;
                const double h = frame.h;
                guides.values = {{"w", w}, {"h", h}, {"l", 0}, {"t", 0}, {"r", w}, {"b", h}, {"hc", w / 2}, {"vc", h / 2}, {"wd2", w / 2}, {"hd2", h / 2},
                                 {"wd4", w / 4}, {"hd4", h / 4}, {"ss", std::min(w, h)}, {"ls", std::max(w, h)}, {"cd2", 10800000}, {"cd4", 5400000}, {"3cd4", 16200000},
                                 {"cd8", 2700000}, {"3cd8", 8100000}, {"5cd8", 13500000}, {"7cd8", 18900000}};
                for (const char* list : {"a:avLst", "a:gdLst"}) {
                    if (const auto* guides_node = geometry.child(list)) {
                        for (const auto* guide : guides_node->all("a:gd")) {
                            guides.evaluate(guide->get("name"), guide->get("fmla"));
                        }
                    }
                }
                std::string path;
                const auto number = [](double value) { return format(value / emu_per_px); };
                const auto* paths = geometry.child("a:pathLst");
                if (paths == nullptr) {
                    return "";
                }
                for (const auto* each : paths->all("a:path")) {
                    const double path_w = static_cast<double>(each->integer("w").value_or(0));
                    const double path_h = static_cast<double>(each->integer("h").value_or(0));
                    const double sx = path_w > 0 ? w / path_w : 1;
                    const double sy = path_h > 0 ? h / path_h : 1;
                    double cx = 0;
                    double cy = 0;
                    const auto point = [&](const xml::Node* pt_node, double& x, double& y) {
                        x = guides.get(pt_node->get("x")) * sx;
                        y = guides.get(pt_node->get("y")) * sy;
                    };
                    for (const auto& command : each->children) {
                        if (command.name == "a:moveTo" || command.name == "a:lnTo") {
                            if (const auto* p = command.child("a:pt")) {
                                point(p, cx, cy);
                                path += (path.empty() ? "" : " ") + std::string(command.name == "a:moveTo" ? "M " : "L ") + number(cx) + " " + number(cy);
                            }
                        } else if (command.name == "a:cubicBezTo" || command.name == "a:quadBezTo") {
                            std::string segment = command.name == "a:cubicBezTo" ? "C" : "Q";
                            for (const auto* p : command.all("a:pt")) {
                                point(p, cx, cy);
                                segment += " " + number(cx) + " " + number(cy);
                            }
                            path += " " + segment;
                        } else if (command.name == "a:arcTo") {
                            // 지금 점에서 시작하는 타원 호. 90도 이하의 베지어로 나눈다
                            const double rx = guides.get(command.get("wR")) * sx;
                            const double ry = guides.get(command.get("hR")) * sy;
                            const double start = guides.get(command.get("stAng")) / 60000 * std::numbers::pi / 180;
                            const double sweep = guides.get(command.get("swAng")) / 60000 * std::numbers::pi / 180;
                            const double center_x = cx - rx * std::cos(start);
                            const double center_y = cy - ry * std::sin(start);
                            const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2))));
                            const double step = sweep / steps;
                            for (int i = 0; i < steps; ++i) {
                                const double a1 = start + step * i;
                                const double a2 = a1 + step;
                                const double k = 4.0 / 3 * std::tan(step / 4);
                                const double x1 = center_x + rx * (std::cos(a1) - k * std::sin(a1));
                                const double y1 = center_y + ry * (std::sin(a1) + k * std::cos(a1));
                                const double x2 = center_x + rx * (std::cos(a2) + k * std::sin(a2));
                                const double y2 = center_y + ry * (std::sin(a2) - k * std::cos(a2));
                                cx = center_x + rx * std::cos(a2);
                                cy = center_y + ry * std::sin(a2);
                                path += " C " + number(x1) + " " + number(y1) + " " + number(x2) + " " + number(y2) + " " + number(cx) + " " + number(cy);
                            }
                        } else if (command.name == "a:close") {
                            path += " Z";
                        }
                    }
                }
                return path;
            }

            std::string emit_freeform(const Shape& shape, const Scope& scope, const GroupTransform* group, int indent) {
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                const auto* geometry = shape.node->path({"p:spPr", "a:custGeom"});
                const Frame local = read_xfrm(first_xfrm(chain));
                const Frame frame = map_frame(local, group);
                // 그룹 안에서 늘어난 만큼 경로도 늘린다
                Frame scaled = local;
                scaled.w = frame.w;
                scaled.h = frame.h;
                const std::string path = geometry != nullptr ? freeform_path(*geometry, scaled) : "";
                if (path.empty()) {
                    warn(where, "자유형의 경로를 읽을 수 없어 뺐습니다");
                    return "";
                }
                Properties props;
                props.raw("x = " + px(frame.x) + "; y = " + px(frame.y) + ";");
                props.add("path", quote(path));
                if (std::abs(frame.rotation) > 0.01) {
                    props.add("rotation", format(frame.rotation));
                }
                if (frame.flip_h || frame.flip_v) {
                    props.add("flip", frame.flip_h && frame.flip_v ? "both" : frame.flip_h ? "horizontal" : "vertical");
                }
                add_paint(props, chain, scope, "freeform", where);
                add_effects(props, chain, scope, where);
                add_actions(props, shape, scope, where);
                add_animations(props.lines, shape.id, scope);
                std::string result = block("put freeform" + name_of(shape, scope), props.lines, indent);
                // 자유형에는 글이 없으므로 같은 자리에 글상자를 겹친다
                if (shape.has_text) {
                    Properties text_props;
                    text_props.add("text", text_of(shape, chain, scope, object_baseline("freeform")));
                    add_frame(text_props, frame);
                    add_body(text_props, read_body(chain), "middle");
                    result += block("put text_box", text_props.lines, indent);
                }
                return result;
            }

            // ---- 개체 틀

            // 레이아웃 개체 틀의 첫 문단 모양 (슬라이드의 글이 이어받는 모양)
            Baseline placeholder_baseline(const std::vector<const xml::Node*>& chain, const Shape& shape, const Scope& scope) const {
                std::size_t own = 0;
                const StyleChain styles = text_chain(chain, shape, scope, &own);
                const xml::Node* font_reference = nullptr;
                if (const auto* style = first_style(chain)) {
                    font_reference = style->child("a:fontRef");
                }
                const RunStyle run = text_run(nullptr, styles, own, font_reference, 0, scope);
                const ParagraphStyle paragraph = resolve_paragraph(nullptr, styles, 0, scope.colors);
                return baseline_of(run, paragraph, presentation_.hangul ? "\xEA\xB0\x80" : "a", scope);
            }

            std::string emit_layout_placeholder(const Shape& shape, const Scope& scope, int indent) {
                const auto roles = layout_roles(*scope.layout);
                const std::string role = role_of(shape.ph_type);
                if (role.empty() || !roles.contains(role) || roles.at(role) != shape.id) {
                    return "";
                }
                const auto chain = inheritance(shape, scope);
                const std::string where = where_of(shape, scope);
                const Frame frame = read_xfrm(first_xfrm(chain));
                const Baseline baseline = placeholder_baseline(chain, shape, scope);
                // 안내 글의 첫 글자 모양이 슬라이드 글의 기본 모양이 된다
                std::string prompt;
                if (const auto* body = text_body(*shape.node)) {
                    if (const auto* paragraph = body->child("a:p")) {
                        xml::Node only;
                        only.name = "p:txBody";
                        only.children.push_back(*paragraph);
                        prompt = plain_text(&only, 200);
                    }
                }
                if (prompt.empty()) {
                    prompt = role == "title" ? "제목을 입력하세요" : role == "subtitle" ? "부제목을 입력하세요" : "내용을 입력하세요";
                }
                std::vector<std::string> style = {"font-size = " + pt(baseline.size), "color = " + baseline.color, "font-family = " + baseline.font};
                if (baseline.bold) {
                    style.push_back("font-weight = bold");
                }
                if (baseline.italic) {
                    style.push_back("font-style = italic");
                }
                static const std::map<std::string, std::string> aligns = {{"ctr", "center"}, {"r", "right"}, {"just", "justify"}, {"dist", "justify"}};
                if (const auto it = aligns.find(baseline.align); it != aligns.end()) {
                    style.push_back("text-align = " + it->second);
                }
                std::string joined;
                for (const auto& property : style) {
                    joined += (joined.empty() ? "" : ", ") + property;
                }
                Properties props;
                props.add("role", role);
                props.add("text", "(style(" + joined + ") " + quote(prompt) + ")");
                add_frame(props, frame);
                add_body(props, read_body(chain), "top");
                add_paint(props, chain, scope, "placeholder", where);
                add_effects(props, chain, scope, where);
                return block("put placeholder", props.lines, indent);
            }

            // 슬라이드의 title, subtitle, body로 쓰는 글
            std::string role_text(const Shape& shape, const Scope& scope) {
                const auto chain = inheritance(shape, scope);
                TextJob job;
                job.body = text_body(*shape.node);
                job.chain = text_chain(chain, shape, scope, &job.own);
                const Body body_properties = read_body(chain);
                job.font_scale = body_properties.font_scale;
                job.line_reduction = body_properties.line_reduction;
                if (const auto* style = first_style(chain)) {
                    job.font_reference = style->child("a:fontRef");
                }
                // 첫 단계는 레이아웃 개체 틀의 모양을, 더 깊은 단계는 templide 마스터의 기본 모양(제목 44pt, 본문 28pt)을 이어받는다
                const Shape* layout_match = match_placeholder(scope.layout->shapes, shape);
                Scope layout_scope = scope;
                layout_scope.slide = nullptr;
                layout_scope.part = scope.layout->part.get();
                const auto layout_chain = layout_match != nullptr ? inheritance(*layout_match, layout_scope) : chain;
                const Baseline first = placeholder_baseline(layout_chain, layout_match != nullptr ? *layout_match : shape, layout_scope);
                const bool title = role_of(shape.ph_type) == "title";
                job.baseline = [first, title](int depth) {
                    if (depth == 0) {
                        return first;
                    }
                    Baseline deeper;
                    deeper.size = title ? 44 : 28;
                    deeper.font = title ? "theme.heading_font" : "theme.body_font";
                    return deeper;
                };
                return text_code(job, scope);
            }

            // ---- 개체 하나

            std::string emit(const Shape& shape, Scope& scope, const GroupTransform* group, int indent) {
                if (!shape.reason.empty() || !selected(scope.prefix + "/e" + std::to_string(shape.id))) {
                    return "";
                }
                const std::string& object = shape.object;
                if (object == "group") {
                    return emit_group(shape, scope, group, indent);
                }
                if (object == "placeholder") {
                    if (scope.slide == nullptr) {
                        return scope.layout != nullptr && !scope.template_body ? emit_layout_placeholder(shape, scope, indent) : "";
                    }
                    if (!shape.has_text) {
                        return "";
                    }
                    if (const std::string role = slide_role(presentation_, *scope.slide, shape); !role.empty() && !scope.roles.contains(role)) {
                        scope.roles[role] = role_text(shape, scope);
                        return "";
                    }
                    return emit_text_object(shape, scope, group, indent, "text_box");
                }
                if (object == "text_box" || object == "shape") {
                    return emit_text_object(shape, scope, group, indent, object);
                }
                if (object == "image") {
                    return emit_image(shape, scope, group, indent);
                }
                if (object == "video" || object == "audio") {
                    return emit_media(shape, scope, group, indent);
                }
                if (object == "line") {
                    return emit_line(shape, scope, group, indent);
                }
                if (object == "connector") {
                    return emit_connector(shape, scope, group, indent);
                }
                if (object == "freeform") {
                    return emit_freeform(shape, scope, group, indent);
                }
                return "";
            }

            std::string emit_group(const Shape& shape, Scope& scope, const GroupTransform* parent, int indent) {
                const auto* properties = shape.node->child("p:grpSpPr");
                const auto* xfrm = properties != nullptr ? properties->child("a:xfrm") : nullptr;
                GroupTransform transform{0, 0, 0, 0, 1, 1, 0, 0, 0, false, false, parent};
                if (xfrm != nullptr) {
                    const Frame frame = read_xfrm(xfrm);
                    transform.offset_x = frame.x;
                    transform.offset_y = frame.y;
                    if (const auto* offset = xfrm->child("a:chOff")) {
                        transform.child_x = static_cast<double>(offset->integer("x").value_or(0));
                        transform.child_y = static_cast<double>(offset->integer("y").value_or(0));
                    }
                    if (const auto* extent = xfrm->child("a:chExt")) {
                        const double cw = static_cast<double>(extent->integer("cx").value_or(0));
                        const double ch = static_cast<double>(extent->integer("cy").value_or(0));
                        transform.scale_x = cw > 0 ? frame.w / cw : 1;
                        transform.scale_y = ch > 0 ? frame.h / ch : 1;
                    }
                    transform.center_x = frame.x + frame.w / 2;
                    transform.center_y = frame.y + frame.h / 2;
                    transform.rotation = frame.rotation;
                    transform.flip_h = frame.flip_h;
                    transform.flip_v = frame.flip_v;
                }
                const std::optional<Fill> outer_fill = scope.group_fill;
                if (properties != nullptr) {
                    if (auto fill = read_fill(properties, *scope.part, scope.colors); fill && fill->kind != Fill::Kind::Group) {
                        scope.group_fill = fill;
                    }
                }
                std::vector<const Shape*> children;
                for (const auto& child : shape.children) {
                    if (child.reason.empty() && selected(scope.prefix + "/e" + std::to_string(child.id))) {
                        children.push_back(&child);
                    }
                }
                std::string result;
                const bool animated = scope.animations.contains(shape.id);
                if (children.size() == 1 && !animated && !scope.names.contains(shape.id)) {
                    // 하나만 남으면 그룹을 풀어 넣는다
                    result = emit(*children.front(), scope, &transform, indent);
                } else if (!children.empty()) {
                    std::vector<std::string> lines;
                    add_animations(lines, shape.id, scope);
                    std::string body;
                    for (const Shape* child : children) {
                        body += emit(*child, scope, &transform, 0);
                    }
                    // 자식 문장들은 이미 들여 쓴 블록이므로 줄 단위로 넣는다
                    if (!body.empty() && body.back() == '\n') {
                        body.pop_back();
                    }
                    if (!body.empty()) {
                        lines.push_back(body);
                    }
                    result = block("group" + name_of(shape, scope), lines, indent);
                }
                scope.group_fill = outer_fill;
                return result;
            }

            std::string emit_all(const std::vector<Shape>& shapes, Scope& scope, int indent) {
                std::string result;
                for (const auto& shape : shapes) {
                    result += emit(shape, scope, nullptr, indent);
                }
                return result;
            }

            // 연결선이 잇는 개체에 이름을 붙인다
            void name_connected(const std::vector<Shape>& shapes, Scope& scope) {
                std::set<std::string> taken;
                const std::function<void(const std::vector<Shape>&)> visit = [&](const std::vector<Shape>& list) {
                    for (const auto& shape : list) {
                        if (shape.object == "connector" && selected(scope.prefix + "/e" + std::to_string(shape.id))) {
                            const auto* properties = shape.node->path({"p:nvCxnSpPr", "p:cNvCxnSpPr"});
                            const auto* start = properties != nullptr ? properties->child("a:stCxn") : nullptr;
                            const auto* end = properties != nullptr ? properties->child("a:endCxn") : nullptr;
                            if (start == nullptr || end == nullptr) {
                                continue;
                            }
                            std::vector<const Shape*> targets;
                            for (const auto* connection : {start, end}) {
                                const Shape* target = find_in(shapes, static_cast<int>(connection->integer("id").value_or(-1)));
                                const bool usable = target != nullptr && target->reason.empty() && target->object != "group" && target->object != "line"
                                    && target->object != "connector" && selected(scope.prefix + "/e" + std::to_string(target->id))
                                    && !(target->object == "placeholder" && scope.slide != nullptr && !slide_role(presentation_, *scope.slide, *target).empty());
                                if (usable) {
                                    targets.push_back(target);
                                }
                            }
                            if (targets.size() != 2) {
                                continue;
                            }
                            for (const Shape* target : targets) {
                                if (!scope.names.contains(target->id)) {
                                    scope.names[target->id] = unique(identifier(target->name), target->object == "placeholder" ? "text_box" : target->object, taken);
                                }
                            }
                        }
                        visit(shape.children);
                    }
                };
                visit(shapes);
            }

            // ---- 배경

            std::optional<std::string> background_code(const Part& part, const Scope& scope, const std::string& where) {
                const auto* background = part.root.path({"p:cSld", "p:bg"});
                if (background == nullptr) {
                    return std::nullopt;
                }
                std::optional<Fill> fill;
                if (const auto* properties = background->child("p:bgPr")) {
                    fill = read_fill(properties, part, scope.colors);
                } else if (const auto* reference = background->child("p:bgRef")) {
                    fill = theme_fill(reference, part, scope.colors);
                }
                if (!fill || fill->kind == Fill::Kind::None || fill->kind == Fill::Kind::Group) {
                    return std::nullopt;
                }
                const std::string code = fill_code(*fill, scope, where);
                return code.empty() ? std::nullopt : std::optional(code);
            }

            // ---- 테마, 마스터, 레이아웃

            std::string emit_theme(std::size_t index) {
                const Master& master = presentation_.masters[index];
                const Theme& theme = master.theme;
                const std::string base = identifier(theme.name.empty() ? master.name : theme.name);
                const std::string name = unique((base.empty() ? "master" + std::to_string(index + 1) : base) + "_theme", "theme", taken_);
                theme_names_[static_cast<int>(index)] = name;
                Properties props;
                static const std::vector<std::pair<std::string, std::string>> slots = {
                    {"dk1", "dark1"}, {"lt1", "light1"}, {"dk2", "dark2"}, {"lt2", "light2"}, {"accent1", "accent1"}, {"accent2", "accent2"}, {"accent3", "accent3"},
                    {"accent4", "accent4"}, {"accent5", "accent5"}, {"accent6", "accent6"}, {"hlink", "hyperlink"}, {"folHlink", "followed_hyperlink"},
                };
                for (const auto& [key, slot] : slots) {
                    if (const auto it = theme.colors.find(key); it != theme.colors.end()) {
                        Color color = it->second;
                        color.scheme.clear();
                        color.a = 1;
                        props.add(slot, color_code(color));
                    }
                }
                // 한글이 있는 발표 자료면 한글 글꼴을 고른다 (templide의 테마 글꼴은 하나다)
                if (const std::string heading = pick_font(theme.major_latin, theme.major_ea); !heading.empty()) {
                    props.add("heading_font", "font(" + quote(heading) + ")");
                }
                if (const std::string body = pick_font(theme.minor_latin, theme.minor_ea); !body.empty()) {
                    props.add("body_font", "font(" + quote(body) + ")");
                }
                return "/* " + (theme.name.empty() ? master.name : theme.name) + " */\n" + block("theme " + name, props.lines, 0);
            }

            std::string layout_case_name(const Layout& layout, std::set<std::string>& taken) {
                static const std::map<std::string, std::string> types = {
                    {"title", "title_slide"}, {"obj", "title_and_content"}, {"secHead", "section_header"}, {"twoObj", "two_content"}, {"twoTxTwoObj", "comparison"},
                    {"titleOnly", "title_only"}, {"blank", "blank"}, {"objTx", "content_with_caption"}, {"picTx", "picture_with_caption"},
                    {"vertTx", "title_and_vertical_text"}, {"vertTitleAndTx", "vertical_title_and_text"}, {"tx", "title_and_text"},
                };
                std::string base = identifier(layout.name);
                if (const auto it = types.find(layout.type); (base.empty() || has_hangul(layout.name)) && it != types.end()) {
                    base = it->second;
                } else if (base.empty() || has_hangul(layout.name)) {
                    base = "custom";
                }
                return unique(base, "layout", taken);
            }

            std::string emit_master(std::size_t index) {
                const Master& master = presentation_.masters[index];
                const std::string id = master_id(index);
                Scope scope;
                scope.part = master.part.get();
                scope.colors = {&master.theme, &master.color_map};
                scope.master = &master;
                scope.prefix = id;
                scope.where = "마스터 " + master.name;
                scope.template_body = true;
                std::string result;
                // 마스터의 도형은 template 하나로 묶어 레이아웃마다 넣는다
                const std::string shapes = emit_all(master.shapes, scope, 1);
                if (!shapes.empty()) {
                    const std::string name = unique(master_names_.at(static_cast<int>(index)) + "_shapes", "master_shapes", taken_);
                    template_names_[static_cast<int>(index)] = name;
                    result += "/* 마스터 " + master.name + "의 도형. 레이아웃마다 넣는다 */\ntemplate " + name + " {\n" + shapes + "}\n\n";
                }
                const std::string name = master_names_.at(static_cast<int>(index));
                std::vector<std::string> lines;
                lines.push_back("theme = " + theme_names_.at(static_cast<int>(index)) + ";");
                std::set<std::string> cases;
                for (const int layout_index : master.layouts) {
                    const Layout& layout = presentation_.layouts[static_cast<std::size_t>(layout_index)];
                    const std::string lid = layout_id(presentation_, static_cast<std::size_t>(layout_index));
                    if (!selected(lid)) {
                        continue;
                    }
                    const std::string case_name = layout_case_name(layout, cases);
                    case_names_[layout_index] = case_name;
                    Scope layout_scope;
                    layout_scope.part = layout.part.get();
                    layout_scope.colors = {&master.theme, &master.color_map};
                    layout_scope.master = &master;
                    layout_scope.layout = &layout;
                    layout_scope.prefix = lid;
                    layout_scope.where = "레이아웃 " + layout.name;
                    name_connected(layout.shapes, layout_scope);
                    std::vector<std::string> body;
                    auto background = background_code(*layout.part, layout_scope, layout_scope.where);
                    if (!background) {
                        background = background_code(*master.part, scope, scope.where);
                    }
                    if (background) {
                        body.push_back("background = " + *background + ";");
                    }
                    if (layout.show_master_shapes && template_names_.contains(static_cast<int>(index))) {
                        body.push_back("put " + template_names_.at(static_cast<int>(index)) + " {}");
                    }
                    std::string shapes_code = emit_all(layout.shapes, layout_scope, 0);
                    if (!shapes_code.empty() && shapes_code.back() == '\n') {
                        shapes_code.pop_back();
                    }
                    if (!shapes_code.empty()) {
                        body.push_back(shapes_code);
                    }
                    std::string case_code = block("case " + case_name, body, 0);
                    case_code.pop_back();
                    lines.push_back("/* " + layout.name + " */\n" + case_code);
                }
                result += "/* 슬라이드 마스터: " + master.name + " */\n" + block("master " + name, lines, 0);
                return result;
            }

            // ---- 슬라이드

            std::string emit_slide(std::size_t index) {
                const Slide& slide = presentation_.slides[index];
                const Layout& layout = presentation_.layouts[static_cast<std::size_t>(slide.layout)];
                const Master& master = presentation_.masters[static_cast<std::size_t>(layout.master)];
                const std::string id = slide_id(index);
                Scope scope;
                scope.part = slide.part.get();
                std::map<std::string, std::string> color_map = master.color_map;
                if (const auto* override_map = slide.part->root.path({"p:clrMapOvr", "a:overrideClrMapping"})) {
                    for (const auto& [key, value] : override_map->attributes) {
                        color_map[key] = value;
                    }
                }
                scope.colors = {&master.theme, &color_map};
                scope.master = &master;
                scope.layout = &layout;
                scope.slide = &slide;
                scope.slide_index = index;
                scope.prefix = id;
                scope.where = "슬라이드 " + std::to_string(index + 1);
                // 고른 애니메이션에 재생 차례를 붙인다
                int order = 0;
                for (std::size_t i = 0; i < slide.animations.size(); ++i) {
                    const Animation& animation = slide.animations[i];
                    if (!animation.reason.empty() || !selected(id + "/a" + std::to_string(i + 1)) || !selected(id + "/e" + std::to_string(animation.target))) {
                        continue;
                    }
                    scope.animations[animation.target].emplace_back(&animation, ++order);
                }
                name_connected(slide.shapes, scope);
                std::string shapes = emit_all(slide.shapes, scope, 0);
                if (!shapes.empty() && shapes.back() == '\n') {
                    shapes.pop_back();
                }
                std::vector<std::string> lines;
                lines.push_back("layout = " + master_names_.at(layout.master) + "." + case_names_.at(slide.layout) + ";");
                if (selected(id + "/bg")) {
                    if (const auto background = background_code(*slide.part, scope, scope.where)) {
                        lines.push_back("background = " + *background + ";");
                    }
                }
                if (slide.hidden) {
                    lines.push_back("hidden = true;");
                }
                const TransitionInfo transition = read_transition(slide);
                const bool with_transition = transition.present && selected(id + "/tr");
                if (with_transition && transition.advance) {
                    lines.push_back("advance_after = " + seconds(*transition.advance) + ";");
                }
                if (with_transition && !transition.sound.empty()) {
                    if (transition.sound.ends_with(".wav") || transition.sound.ends_with(".WAV")) {
                        lines.push_back("transition_sound = " + media(transition.sound, "audio") + ";");
                    } else {
                        warn(scope.where, "화면 전환 소리는 wav만 쓸 수 있어 뺐습니다");
                    }
                }
                for (const char* role : {"title", "subtitle", "body"}) {
                    if (const auto it = scope.roles.find(role); it != scope.roles.end()) {
                        lines.push_back(std::string(role) + " = " + it->second + ";");
                    }
                }
                if (!shapes.empty()) {
                    lines.push_back(shapes);
                }
                if (with_transition && !transition.kind.empty()) {
                    lines.push_back("transition " + transition.kind + (transition.option.empty() ? "" : "." + transition.option)
                                    + (transition.duration ? " " + seconds(*transition.duration) : "") + ";");
                } else if (with_transition && !transition.reason.empty()) {
                    warn(scope.where, transition.reason);
                }
                if (selected(id + "/notes")) {
                    if (const std::string notes = notes_code(slide); !notes.empty()) {
                        lines.push_back("comment " + notes + ";");
                    }
                }
                if (selected(id + "/rv")) {
                    for (const auto& review : slide.reviews) {
                        std::vector<std::string> fields = {"text = " + quote(review.text) + ";"};
                        if (!review.author.empty()) {
                            fields.push_back("author = " + quote(review.author) + ";");
                        }
                        fields.push_back("x = " + px(static_cast<double>(review.x)) + "; y = " + px(static_cast<double>(review.y)) + ";");
                        std::string code = block("review", fields, 0);
                        code.pop_back();
                        lines.push_back(code);
                    }
                }
                return "/* 슬라이드 " + std::to_string(index + 1) + (slide.title.empty() ? "" : ": " + slide.title) + " */\n" + block("slide", lines, 0);
            }

            std::string notes_code(const Slide& slide) const {
                if (!slide.notes) {
                    return "";
                }
                const auto* tree = slide.notes->root.path({"p:cSld", "p:spTree"});
                if (tree == nullptr) {
                    return "";
                }
                for (const auto& shape : tree->children) {
                    const auto* placeholder = placeholder_of(shape);
                    if (placeholder == nullptr || placeholder->get("type", "obj") != "body") {
                        continue;
                    }
                    std::vector<std::string> paragraphs;
                    if (const auto* body = text_body(shape)) {
                        for (const auto* paragraph : body->all("a:p")) {
                            std::string line;
                            for (const auto& child : paragraph->children) {
                                if (child.name == "a:r" || child.name == "a:fld") {
                                    if (const auto* t = child.child("a:t")) {
                                        line += t->text;
                                    }
                                } else if (child.name == "a:br") {
                                    line += '\n';
                                }
                            }
                            paragraphs.push_back(quote(line));
                        }
                    }
                    while (!paragraphs.empty() && paragraphs.back() == "\"\"") {
                        paragraphs.pop_back();
                    }
                    if (paragraphs.empty()) {
                        return "";
                    }
                    if (paragraphs.size() == 1) {
                        return paragraphs.front();
                    }
                    std::string joined;
                    for (const auto& paragraph : paragraphs) {
                        joined += (joined.empty() ? "" : ", ") + paragraph;
                    }
                    return "paragraphs [" + joined + "]";
                }
                return "";
            }
        };

        ImportResult Converter::run(const std::filesystem::path& packages_dir) {
            ImportResult result;
            // 고른 슬라이드의 레이아웃과 마스터도 골라야 한다
            int number = 0;
            for (std::size_t i = 0; i < presentation_.slides.size(); ++i) {
                slide_parts_[presentation_.slides[i].part->name] = i;
                if (!selected(slide_id(i))) {
                    continue;
                }
                const std::size_t layout = static_cast<std::size_t>(presentation_.slides[i].layout);
                if (!selected(layout_id(presentation_, layout)) || !selected(master_id(static_cast<std::size_t>(presentation_.layouts[layout].master)))) {
                    result.error = "슬라이드 " + std::to_string(i + 1) + "의 레이아웃 '" + presentation_.layouts[layout].name + "'를 함께 골라야 합니다";
                    return result;
                }
                slide_numbers_[i] = ++number;
            }
            for (const auto& name : reserved_names()) {
                taken_.insert(name);
            }
            // 마스터 이름을 먼저 정한다 (슬라이드가 쓴다)
            std::string themes;
            std::string masters;
            for (std::size_t m = 0; m < presentation_.masters.size(); ++m) {
                if (!selected(master_id(m))) {
                    continue;
                }
                themes += emit_theme(m) + "\n";
                const std::string base = identifier(presentation_.masters[m].name);
                master_names_[static_cast<int>(m)] = unique(base.empty() ? "master" + std::to_string(m + 1) : base, "master", taken_);
            }
            for (std::size_t m = 0; m < presentation_.masters.size(); ++m) {
                if (selected(master_id(m))) {
                    masters += emit_master(m) + "\n";
                }
            }
            std::string slides;
            std::string section;
            for (std::size_t i = 0; i < presentation_.slides.size(); ++i) {
                if (!selected(slide_id(i))) {
                    continue;
                }
                const Slide& slide = presentation_.slides[i];
                if (!slide.section.empty() && slide.section != section) {
                    slides += "section " + quote(slide.section) + ";\n\n";
                    section = slide.section;
                }
                slides += emit_slide(i) + "\n";
            }

            const std::u8string source_name = presentation_.file.filename().u8string();
            std::string code = "/* " + std::string(source_name.begin(), source_name.end()) + "에서 불러왔다 */\n#include <std/stddef>\n\n";
            if (!media_.empty()) {
                const std::u8string bundle_name = bundle_.filename().u8string();
                code += "asset " + quote(std::string(bundle_name.begin(), bundle_name.end())) + " default;\n\n";
                for (const auto& part : media_order_) {
                    const Media& item = media_.at(part);
                    code += item.kind + " " + item.name + " = asset(" + quote(item.entry) + ");\n";
                }
                code += "\n";
            }
            code += themes + masters + slides;
            // 슬라이드 크기는 target에 있다. 원본 pptx를 덮어쓰지 않게 다른 이름으로 만든다
            const std::u8string stem = output_.stem().u8string();
            const std::string stem_text(stem.begin(), stem.end());
            const std::string target = unique(identifier(stem_text), "deck", taken_);
            code += block("target " + target, {"path = " + quote(stem_text + "_templide.pptx") + ";", "type = pptx;",
                                               "width = " + px_float(static_cast<double>(presentation_.width)) + "; height = " + px_float(static_cast<double>(presentation_.height)) + ";"}, 0);

            // 그림과 미디어를 묶음에 넣는다
            if (!media_.empty()) {
                tasset::Writer writer(bundle_);
                for (const auto& part : media_order_) {
                    const Media& item = media_.at(part);
                    const auto bytes = item.bytes.empty() ? presentation_.package.read(part) : std::optional(item.bytes);
                    if (!bytes) {
                        result.error = "pptx에서 " + part + "을 읽을 수 없습니다";
                        return result;
                    }
                    if (!writer.add(item.entry, *bytes)) {
                        result.error = writer.error();
                        return result;
                    }
                }
                if (!writer.finish()) {
                    result.error = writer.error();
                    return result;
                }
                result.tasset = bundle_;
            }
            {
                std::ofstream file(output_, std::ios::binary);
                file << code;
                if (!file) {
                    const std::u8string text = output_.u8string();
                    result.error = "파일을 쓸 수 없습니다: " + std::string(text.begin(), text.end());
                    return result;
                }
            }
            result.tlide = output_;
            result.warnings = warnings_;
            // 만든 코드를 컴파일해 본다
            const middleend::Result analyzed = middleend::analyze(output_, packages_dir);
            for (const auto& diagnostic : analyzed.diagnostics) {
                result.errors.push_back((diagnostic.line > 0 ? "줄 " + std::to_string(diagnostic.line) + ": " : std::string()) + diagnostic.message);
            }
            return result;
        }
    }

    ImportResult convert(const std::filesystem::path& pptx, const std::set<std::string>& selection, const std::filesystem::path& output,
                         const std::filesystem::path& packages_dir) {
        std::string error;
        const auto presentation = load(pptx, error);
        if (!presentation) {
            ImportResult result;
            result.error = error;
            return result;
        }
        Converter converter(*presentation, selection, output);
        return converter.run(packages_dir);
    }
}

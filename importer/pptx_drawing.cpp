#include "pptx_drawing.h"

#include <algorithm>
#include <cmath>

namespace templide::importer {
    namespace {
        struct Rgb {
            double r;
            double g;
            double b;
        };

        double to_linear(double c) {
            return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double to_srgb(double c) {
            c = std::clamp(c, 0.0, 1.0);
            return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
        }

        void to_hsl(const Rgb& rgb, double& h, double& s, double& l) {
            const double max = std::max({rgb.r, rgb.g, rgb.b});
            const double min = std::min({rgb.r, rgb.g, rgb.b});
            l = (max + min) / 2;
            if (max == min) {
                h = 0;
                s = 0;
                return;
            }
            const double d = max - min;
            s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
            if (max == rgb.r) {
                h = (rgb.g - rgb.b) / d + (rgb.g < rgb.b ? 6 : 0);
            } else if (max == rgb.g) {
                h = (rgb.b - rgb.r) / d + 2;
            } else {
                h = (rgb.r - rgb.g) / d + 4;
            }
            h /= 6;
        }

        double hue_channel(double p, double q, double t) {
            if (t < 0) {
                t += 1;
            }
            if (t > 1) {
                t -= 1;
            }
            if (t < 1.0 / 6) {
                return p + (q - p) * 6 * t;
            }
            if (t < 0.5) {
                return q;
            }
            if (t < 2.0 / 3) {
                return p + (q - p) * (2.0 / 3 - t) * 6;
            }
            return p;
        }

        Rgb from_hsl(double h, double s, double l) {
            h = h - std::floor(h);
            s = std::clamp(s, 0.0, 1.0);
            l = std::clamp(l, 0.0, 1.0);
            if (s == 0) {
                return {l, l, l};
            }
            const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const double p = 2 * l - q;
            return {hue_channel(p, q, h + 1.0 / 3), hue_channel(p, q, h), hue_channel(p, q, h - 1.0 / 3)};
        }

        int channel(double value) {
            return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
        }

        double percent(const xml::Node& node) {
            return static_cast<double>(node.integer("val").value_or(100000)) / 100000;
        }

        const std::map<std::string, std::string>& theme_names() {
            static const std::map<std::string, std::string> names = {
                {"dk1", "dark1"}, {"lt1", "light1"}, {"dk2", "dark2"}, {"lt2", "light2"}, {"accent1", "accent1"}, {"accent2", "accent2"},
                {"accent3", "accent3"}, {"accent4", "accent4"}, {"accent5", "accent5"}, {"accent6", "accent6"}, {"hlink", "hyperlink"}, {"folHlink", "followed_hyperlink"},
            };
            return names;
        }

        // Office 기본 테마. 테마에 없는 색을 찾을 때 쓴다
        Color default_theme_color(const std::string& key) {
            static const std::map<std::string, std::array<int, 3>> defaults = {
                {"dk1", {0, 0, 0}}, {"lt1", {255, 255, 255}}, {"dk2", {0x44, 0x54, 0x6A}}, {"lt2", {0xE7, 0xE6, 0xE6}}, {"accent1", {0x44, 0x72, 0xC4}},
                {"accent2", {0xED, 0x7D, 0x31}}, {"accent3", {0xA5, 0xA5, 0xA5}}, {"accent4", {0xFF, 0xC0, 0x00}}, {"accent5", {0x5B, 0x9B, 0xD5}},
                {"accent6", {0x70, 0xAD, 0x47}}, {"hlink", {0x05, 0x63, 0xC1}}, {"folHlink", {0x95, 0x4F, 0x72}},
            };
            const auto it = defaults.find(key);
            Color color;
            if (it != defaults.end()) {
                color.r = it->second[0];
                color.g = it->second[1];
                color.b = it->second[2];
            }
            return color;
        }

        Color preset_color(const std::string& name) {
            static const std::map<std::string, std::array<int, 3>> table = {
                {"black", {0, 0, 0}}, {"white", {255, 255, 255}}, {"red", {255, 0, 0}}, {"green", {0, 128, 0}}, {"blue", {0, 0, 255}}, {"yellow", {255, 255, 0}},
                {"cyan", {0, 255, 255}}, {"magenta", {255, 0, 255}}, {"gray", {128, 128, 128}}, {"grey", {128, 128, 128}}, {"dkGray", {169, 169, 169}},
                {"darkGray", {169, 169, 169}}, {"ltGray", {211, 211, 211}}, {"lightGray", {211, 211, 211}}, {"silver", {192, 192, 192}}, {"orange", {255, 165, 0}},
                {"purple", {128, 0, 128}}, {"navy", {0, 0, 128}}, {"maroon", {128, 0, 0}}, {"olive", {128, 128, 0}}, {"teal", {0, 128, 128}}, {"lime", {0, 255, 0}},
                {"aqua", {0, 255, 255}}, {"fuchsia", {255, 0, 255}}, {"brown", {165, 42, 42}}, {"pink", {255, 192, 203}}, {"gold", {255, 215, 0}},
                {"dkBlue", {0, 0, 139}}, {"darkBlue", {0, 0, 139}}, {"dkRed", {139, 0, 0}}, {"darkRed", {139, 0, 0}}, {"dkGreen", {0, 100, 0}}, {"darkGreen", {0, 100, 0}},
            };
            Color color;
            if (const auto it = table.find(name); it != table.end()) {
                color.r = it->second[0];
                color.g = it->second[1];
                color.b = it->second[2];
            }
            return color;
        }

        // 색 요소의 자식(alpha, lumMod 등)을 차례로 적용한다. 테마 색 그대로가 아니면 scheme을 지운다
        void apply_modifiers(Color& color, const xml::Node& element) {
            Rgb rgb{color.r / 255.0, color.g / 255.0, color.b / 255.0};
            bool changed = false;
            for (const auto& child : element.children) {
                const std::string& name = child.name;
                if (name == "a:alpha") {
                    color.a = percent(child);
                    color.scheme.clear();
                    continue;
                }
                if (name == "a:alphaMod") {
                    color.a *= percent(child);
                    color.scheme.clear();
                    continue;
                }
                if (name == "a:alphaOff") {
                    color.a = std::clamp(color.a + percent(child), 0.0, 1.0);
                    color.scheme.clear();
                    continue;
                }
                double h = 0;
                double s = 0;
                double l = 0;
                if (name == "a:lumMod" || name == "a:lumOff" || name == "a:satMod" || name == "a:satOff" || name == "a:hueMod" || name == "a:hueOff" || name == "a:comp") {
                    to_hsl(rgb, h, s, l);
                    if (name == "a:lumMod") {
                        l *= percent(child);
                    } else if (name == "a:lumOff") {
                        l += percent(child);
                    } else if (name == "a:satMod") {
                        s *= percent(child);
                    } else if (name == "a:satOff") {
                        s += percent(child);
                    } else if (name == "a:hueMod") {
                        h *= percent(child);
                    } else if (name == "a:hueOff") {
                        h += static_cast<double>(child.integer("val").value_or(0)) / 60000 / 360;
                    } else {
                        h += 0.5;
                    }
                    rgb = from_hsl(h, s, l);
                    changed = true;
                } else if (name == "a:tint") {
                    // 흰색과 섞는다 (선형광에서)
                    const double t = percent(child);
                    rgb = {to_srgb(to_linear(rgb.r) * t + (1 - t)), to_srgb(to_linear(rgb.g) * t + (1 - t)), to_srgb(to_linear(rgb.b) * t + (1 - t))};
                    changed = true;
                } else if (name == "a:shade") {
                    const double t = percent(child);
                    rgb = {to_srgb(to_linear(rgb.r) * t), to_srgb(to_linear(rgb.g) * t), to_srgb(to_linear(rgb.b) * t)};
                    changed = true;
                } else if (name == "a:inv") {
                    rgb = {1 - rgb.r, 1 - rgb.g, 1 - rgb.b};
                    changed = true;
                } else if (name == "a:gray") {
                    const double gray = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
                    rgb = {gray, gray, gray};
                    changed = true;
                }
            }
            if (changed) {
                color.r = channel(rgb.r);
                color.g = channel(rgb.g);
                color.b = channel(rgb.b);
                color.scheme.clear();
            }
        }

        bool is_color_element(const std::string& name) {
            return name == "a:srgbClr" || name == "a:schemeClr" || name == "a:sysClr" || name == "a:prstClr" || name == "a:hslClr" || name == "a:scrgbClr";
        }

        double points(long long emu) {
            return static_cast<double>(emu) / emu_per_pt;
        }
    }

    bool same_color(const Color& a, const Color& b) {
        if (!a.scheme.empty() || !b.scheme.empty()) {
            return a.scheme == b.scheme;
        }
        return a.r == b.r && a.g == b.g && a.b == b.b && std::abs(a.a - b.a) < 0.005;
    }

    std::optional<Color> color_element(const xml::Node& element, const ColorContext& context, const std::optional<Color>& placeholder) {
        Color color;
        const std::string& name = element.name;
        if (name == "a:srgbClr") {
            const std::string hex = element.get("val");
            try {
                color.r = std::stoi(hex.substr(0, 2), nullptr, 16);
                color.g = std::stoi(hex.substr(2, 2), nullptr, 16);
                color.b = std::stoi(hex.substr(4, 2), nullptr, 16);
            } catch (...) {
            }
        } else if (name == "a:schemeClr") {
            std::string key = element.get("val");
            if (key == "phClr") {
                if (!placeholder) {
                    return std::nullopt;
                }
                color = *placeholder;
            } else {
                static const std::map<std::string, std::string> fallback = {{"bg1", "lt1"}, {"tx1", "dk1"}, {"bg2", "lt2"}, {"tx2", "dk2"}};
                if (fallback.contains(key)) {
                    const auto mapped = context.color_map != nullptr ? context.color_map->find(key) : std::map<std::string, std::string>::const_iterator{};
                    key = context.color_map != nullptr && mapped != context.color_map->end() ? mapped->second : fallback.at(key);
                }
                const auto found = context.theme != nullptr ? context.theme->colors.find(key) : std::map<std::string, Color>::const_iterator{};
                color = context.theme != nullptr && found != context.theme->colors.end() ? found->second : default_theme_color(key);
                const auto theme_name = theme_names().find(key);
                color.scheme = theme_name != theme_names().end() ? theme_name->second : "";
                color.a = 1;
            }
        } else if (name == "a:sysClr") {
            const std::string hex = element.get("lastClr", element.get("val") == "window" ? "FFFFFF" : "000000");
            try {
                color.r = std::stoi(hex.substr(0, 2), nullptr, 16);
                color.g = std::stoi(hex.substr(2, 2), nullptr, 16);
                color.b = std::stoi(hex.substr(4, 2), nullptr, 16);
            } catch (...) {
            }
        } else if (name == "a:prstClr") {
            color = preset_color(element.get("val"));
        } else if (name == "a:hslClr") {
            const Rgb rgb = from_hsl(static_cast<double>(element.integer("hue").value_or(0)) / 60000 / 360, static_cast<double>(element.integer("sat").value_or(0)) / 100000,
                                     static_cast<double>(element.integer("lum").value_or(0)) / 100000);
            color.r = channel(rgb.r);
            color.g = channel(rgb.g);
            color.b = channel(rgb.b);
        } else if (name == "a:scrgbClr") {
            color.r = channel(to_srgb(static_cast<double>(element.integer("r").value_or(0)) / 100000));
            color.g = channel(to_srgb(static_cast<double>(element.integer("g").value_or(0)) / 100000));
            color.b = channel(to_srgb(static_cast<double>(element.integer("b").value_or(0)) / 100000));
        } else {
            return std::nullopt;
        }
        apply_modifiers(color, element);
        return color;
    }

    std::optional<Color> read_color(const xml::Node* node, const ColorContext& context, const std::optional<Color>& placeholder) {
        if (node == nullptr) {
            return std::nullopt;
        }
        for (const auto& child : node->children) {
            if (is_color_element(child.name)) {
                return color_element(child, context, placeholder);
            }
        }
        return std::nullopt;
    }

    ImageEffects read_image_effects(const xml::Node* blip, const ColorContext& context, const std::optional<Color>& placeholder) {
        ImageEffects effects;
        if (blip == nullptr) {
            return effects;
        }
        for (const auto& child : blip->children) {
            if (child.name == "a:duotone") {
                std::vector<Color> colors;
                for (const auto& color : child.children) {
                    if (const auto resolved = color_element(color, context, placeholder)) {
                        colors.push_back(*resolved);
                    }
                }
                if (colors.size() == 2) {
                    effects.duotone = std::pair{colors[0], colors[1]};
                }
            } else if (child.name == "a:grayscl") {
                effects.grayscale = true;
            } else if (child.name == "a:lum") {
                effects.bright = static_cast<double>(child.integer("bright").value_or(0)) / 100000;
                effects.contrast = static_cast<double>(child.integer("contrast").value_or(0)) / 100000;
            } else if (child.name == "a:clrChange" || child.name == "a:biLevel" || child.name == "a:alphaBiLevel" || child.name == "a:clrRepl" || child.name == "a:hsl"
                       || child.name == "a:tint") {
                effects.dropped.push_back(child.name.substr(2));
            }
        }
        return effects;
    }

    std::optional<Fill> fill_element(const xml::Node& element, const Part& part, const ColorContext& context, const std::optional<Color>& placeholder) {
        Fill fill;
        fill.part = &part;
        if (element.name == "a:noFill") {
            fill.kind = Fill::Kind::None;
            return fill;
        }
        if (element.name == "a:solidFill") {
            const auto color = read_color(&element, context, placeholder);
            if (!color) {
                return std::nullopt;
            }
            fill.kind = Fill::Kind::Solid;
            fill.color = *color;
            return fill;
        }
        if (element.name == "a:gradFill") {
            fill.kind = Fill::Kind::Gradient;
            if (const auto* stops = element.child("a:gsLst")) {
                for (const auto* stop : stops->all("a:gs")) {
                    if (const auto color = read_color(stop, context, placeholder)) {
                        fill.stops.push_back({*color, static_cast<double>(stop->integer("pos").value_or(0)) / 1000});
                    }
                }
            }
            std::sort(fill.stops.begin(), fill.stops.end(), [](const GradientStop& a, const GradientStop& b) { return a.position < b.position; });
            if (const auto* linear = element.child("a:lin")) {
                fill.angle = static_cast<double>(linear->integer("ang").value_or(0)) / 60000;
            } else if (element.child("a:path") != nullptr) {
                fill.radial = true;
            }
            if (fill.stops.empty()) {
                return std::nullopt;
            }
            if (fill.stops.size() == 1) {
                fill.kind = Fill::Kind::Solid;
                fill.color = fill.stops.front().color;
            }
            return fill;
        }
        if (element.name == "a:pattFill") {
            fill.kind = Fill::Kind::Pattern;
            fill.pattern = element.get("prst", "pct5");
            fill.foreground = read_color(element.child("a:fgClr"), context, placeholder).value_or(Color{});
            fill.background = read_color(element.child("a:bgClr"), context, placeholder).value_or(Color{255, 255, 255, 1, ""});
            return fill;
        }
        if (element.name == "a:blipFill") {
            const auto* blip = element.child("a:blip");
            const auto* relationship = blip != nullptr ? part.rel(blip->get("r:embed")) : nullptr;
            if (relationship == nullptr || relationship->external) {
                return std::nullopt;
            }
            fill.kind = Fill::Kind::Image;
            fill.image = relationship->target;
            fill.blip = blip;
            fill.image_effects = read_image_effects(blip, context, placeholder);
            return fill;
        }
        if (element.name == "a:grpFill") {
            fill.kind = Fill::Kind::Group;
            return fill;
        }
        return std::nullopt;
    }

    std::optional<Fill> read_fill(const xml::Node* properties, const Part& part, const ColorContext& context, const std::optional<Color>& placeholder) {
        if (properties == nullptr) {
            return std::nullopt;
        }
        for (const auto& child : properties->children) {
            if (child.name == "a:noFill" || child.name == "a:solidFill" || child.name == "a:gradFill" || child.name == "a:pattFill" || child.name == "a:blipFill"
                || child.name == "a:grpFill") {
                return fill_element(child, part, context, placeholder);
            }
        }
        return std::nullopt;
    }

    std::optional<Fill> theme_fill(const xml::Node* reference, const Part& part, const ColorContext& context) {
        if (reference == nullptr || context.theme == nullptr) {
            return std::nullopt;
        }
        const long long index = reference->integer("idx").value_or(0);
        const auto color = read_color(reference, context);
        if (index == 0) {
            Fill none;
            none.kind = Fill::Kind::None;
            return none;
        }
        const auto& list = index >= 1001 ? context.theme->backgrounds : context.theme->fills;
        const long long position = index >= 1001 ? index - 1001 : index - 1;
        if (position < 0 || position >= static_cast<long long>(list.size())) {
            return std::nullopt;
        }
        // 테마 파트의 그림을 쓰는 채우기는 테마 파트의 관계로 찾는다
        return fill_element(*list[static_cast<std::size_t>(position)], context.theme->part ? *context.theme->part : part, context, color);
    }

    Line read_line(const xml::Node* ln, const ColorContext& context, Line base, const std::optional<Color>& placeholder) {
        if (ln == nullptr) {
            return base;
        }
        base.present = true;
        if (const auto width = ln->integer("w")) {
            base.width = width;
        }
        static const std::map<std::string, std::string> caps = {{"flat", "flat"}, {"rnd", "round"}, {"sq", "square"}};
        static const std::map<std::string, std::string> compounds = {{"sng", "single"}, {"dbl", "double"}, {"thickThin", "thick_thin"}, {"thinThick", "thin_thick"}, {"tri", "triple"}};
        if (const auto it = caps.find(ln->get("cap")); it != caps.end()) {
            base.cap = it->second;
        }
        if (const auto it = compounds.find(ln->get("cmpd")); it != compounds.end()) {
            base.compound = it->second;
        }
        for (const auto& child : ln->children) {
            if (child.name == "a:noFill") {
                base.none = true;
                base.color.reset();
            } else if (child.name == "a:solidFill") {
                if (const auto color = read_color(&child, context, placeholder)) {
                    base.none = false;
                    base.color = color;
                }
            } else if (child.name == "a:gradFill") {
                if (const auto* stop = child.path({"a:gsLst", "a:gs"})) {
                    if (const auto color = read_color(stop, context, placeholder)) {
                        base.none = false;
                        base.color = color;
                    }
                }
            } else if (child.name == "a:pattFill") {
                if (const auto color = read_color(child.child("a:fgClr"), context, placeholder)) {
                    base.none = false;
                    base.color = color;
                }
            } else if (child.name == "a:prstDash") {
                base.dash = child.get("val", "solid");
            } else if (child.name == "a:custDash") {
                base.dash = "dash";
            } else if (child.name == "a:round") {
                base.join = "round";
            } else if (child.name == "a:bevel") {
                base.join = "bevel";
            } else if (child.name == "a:miter") {
                base.join = "miter";
            } else if (child.name == "a:headEnd") {
                base.head = child.get("type", "none");
            } else if (child.name == "a:tailEnd") {
                base.tail = child.get("type", "none");
            }
        }
        return base;
    }

    Line theme_line(const xml::Node* reference, const ColorContext& context) {
        Line line;
        if (reference == nullptr || context.theme == nullptr) {
            return line;
        }
        const long long index = reference->integer("idx").value_or(0);
        if (index == 0) {
            line.none = true;
            line.present = true;
            return line;
        }
        if (index < 1 || index > static_cast<long long>(context.theme->lines.size())) {
            return line;
        }
        return read_line(context.theme->lines[static_cast<std::size_t>(index - 1)], context, line, read_color(reference, context));
    }

    namespace {
        Shadow read_shadow(const xml::Node& node, const ColorContext& context) {
            Shadow shadow;
            shadow.color = read_color(&node, context).value_or(Color{0, 0, 0, 0.4, ""});
            shadow.blur = points(node.integer("blurRad").value_or(0));
            shadow.distance = points(node.integer("dist").value_or(0));
            shadow.angle = static_cast<double>(node.integer("dir").value_or(0)) / 60000;
            return shadow;
        }

        void read_effect_list(const xml::Node& list, const ColorContext& context, Effects& effects) {
            for (const auto& child : list.children) {
                if (child.name == "a:outerShdw") {
                    effects.shadow = read_shadow(child, context);
                } else if (child.name == "a:innerShdw") {
                    effects.inner_shadow = read_shadow(child, context);
                } else if (child.name == "a:prstShdw") {
                    Shadow shadow = read_shadow(child, context);
                    shadow.blur = 0;
                    effects.shadow = shadow;
                    effects.dropped.push_back("미리 정한 그림자 모양");
                } else if (child.name == "a:glow") {
                    effects.glow = read_color(&child, context);
                    effects.glow_size = points(child.integer("rad").value_or(0));
                } else if (child.name == "a:softEdge") {
                    effects.soft_edge = points(child.integer("rad").value_or(0));
                } else if (child.name == "a:reflection") {
                    effects.reflection = static_cast<double>(child.integer("stA").value_or(100000)) / 100000;
                    effects.reflection_size = static_cast<double>(child.integer("endPos").value_or(100000)) / 100000;
                    effects.reflection_distance = points(child.integer("dist").value_or(0));
                    effects.reflection_blur = points(child.integer("blurRad").value_or(0));
                } else if (child.name == "a:blur" || child.name == "a:fillOverlay") {
                    effects.dropped.push_back(child.name == "a:blur" ? "흐리게" : "채우기 겹치기");
                }
            }
        }

        void read_three_d(const xml::Node* scene, const xml::Node* shape3d, const ColorContext& context, Effects& effects) {
            if (scene != nullptr) {
                if (const auto* camera = scene->child("a:camera")) {
                    const std::string preset = camera->get("prst");
                    if (preset == "perspectiveFront" || preset == "orthographicFront" || camera->child("a:rot") != nullptr) {
                        if (const auto* rotation = camera->child("a:rot")) {
                            const double latitude = static_cast<double>(rotation->integer("lat").value_or(0)) / 60000;
                            const double longitude = static_cast<double>(rotation->integer("lon").value_or(0)) / 60000;
                            if (longitude != 0) {
                                effects.rotation_x = std::fmod(360 - longitude, 360);
                            }
                            if (latitude != 0) {
                                effects.rotation_y = latitude;
                            }
                        }
                        if (preset.starts_with("perspective")) {
                            effects.perspective = static_cast<double>(camera->integer("fov").value_or(2700000)) / 60000;
                        }
                    } else if (preset != "orthographicFront") {
                        effects.dropped.push_back("3차원 회전 (" + preset + ")");
                    }
                }
            }
            if (shape3d != nullptr) {
                if (const auto height = shape3d->integer("extrusionH"); height && *height > 0) {
                    effects.depth = points(*height);
                }
                if (const auto* bevel = shape3d->child("a:bevelT")) {
                    static const std::map<std::string, std::string> kinds = {
                        {"circle", "circle"}, {"relaxedInset", "relaxed_inset"}, {"cross", "cross"}, {"coolSlant", "cool_slant"}, {"angle", "angle"},
                        {"softRound", "soft_round"}, {"convex", "convex"}, {"slope", "slope"}, {"divot", "divot"}, {"riblet", "riblet"}, {"hardEdge", "hard_edge"}, {"artDeco", "art_deco"},
                    };
                    const auto it = kinds.find(bevel->get("prst", "circle"));
                    effects.bevel = it != kinds.end() ? it->second : "circle";
                    effects.bevel_width = points(bevel->integer("w").value_or(76200));
                    effects.bevel_height = points(bevel->integer("h").value_or(76200));
                }
                if (const auto* color = shape3d->child("a:extrusionClr")) {
                    effects.depth_color = read_color(color, context);
                }
            }
        }
    }

    Effects read_effects(const xml::Node* properties, const xml::Node* reference, const ColorContext& context) {
        Effects effects;
        const xml::Node* list = properties != nullptr ? properties->child("a:effectLst") : nullptr;
        const xml::Node* scene = properties != nullptr ? properties->child("a:scene3d") : nullptr;
        const xml::Node* shape3d = properties != nullptr ? properties->child("a:sp3d") : nullptr;
        // spPr에 효과가 없으면 p:style의 effectRef가 가리키는 테마 효과를 쓴다
        if (list == nullptr && properties != nullptr && properties->child("a:effectDag") == nullptr && reference != nullptr && context.theme != nullptr) {
            const long long index = reference->integer("idx").value_or(0);
            if (index >= 1 && index <= static_cast<long long>(context.theme->effects.size())) {
                const xml::Node* style = context.theme->effects[static_cast<std::size_t>(index - 1)];
                list = style->child("a:effectLst");
                if (scene == nullptr) {
                    scene = style->child("a:scene3d");
                }
                if (shape3d == nullptr) {
                    shape3d = style->child("a:sp3d");
                }
                // 테마 효과의 phClr는 effectRef의 색이다
                if (list != nullptr) {
                    const auto color = read_color(reference, context);
                    for (const auto& child : list->children) {
                        if (child.name == "a:outerShdw" || child.name == "a:innerShdw") {
                            Shadow shadow = read_shadow(child, context);
                            if (const auto resolved = read_color(&child, context, color)) {
                                shadow.color = *resolved;
                            }
                            (child.name == "a:outerShdw" ? effects.shadow : effects.inner_shadow) = shadow;
                        }
                    }
                    list = nullptr;
                }
            }
        }
        if (list != nullptr) {
            read_effect_list(*list, context, effects);
        }
        read_three_d(scene, shape3d, context, effects);
        return effects;
    }

    // ---- 글자

    void merge_run(RunStyle& style, const xml::Node* properties, const ColorContext& context) {
        if (properties == nullptr) {
            return;
        }
        if (!style.size) {
            if (const auto size = properties->integer("sz")) {
                style.size = static_cast<double>(*size) / 100;
            }
        }
        if (!style.bold && properties->attribute("b") != nullptr) {
            style.bold = properties->flag("b");
        }
        if (!style.italic && properties->attribute("i") != nullptr) {
            style.italic = properties->flag("i");
        }
        if (!style.underline && properties->attribute("u") != nullptr) {
            style.underline = properties->get("u");
        }
        if (!style.strike && properties->attribute("strike") != nullptr) {
            style.strike = properties->get("strike");
        }
        if (!style.cap && properties->attribute("cap") != nullptr) {
            style.cap = properties->get("cap");
        }
        if (!style.spacing) {
            if (const auto spacing = properties->integer("spc")) {
                style.spacing = static_cast<double>(*spacing) / 100;
            }
        }
        if (!style.baseline) {
            if (const auto baseline = properties->integer("baseline")) {
                style.baseline = static_cast<double>(*baseline) / 1000;
            }
        }
        for (const auto& child : properties->children) {
            if (!style.color && child.name == "a:solidFill") {
                style.color = read_color(&child, context);
            } else if (!style.color && child.name == "a:gradFill") {
                if (const auto* stop = child.path({"a:gsLst", "a:gs"})) {
                    style.color = read_color(stop, context);
                }
            } else if (!style.highlight && child.name == "a:highlight") {
                style.highlight = read_color(&child, context);
            } else if (!style.latin && child.name == "a:latin") {
                style.latin = child.get("typeface");
            } else if (!style.ea && child.name == "a:ea") {
                style.ea = child.get("typeface");
            }
        }
    }

    void merge_paragraph(ParagraphStyle& style, const xml::Node* properties, const ColorContext& context) {
        if (properties == nullptr) {
            return;
        }
        if (!style.align && properties->attribute("algn") != nullptr) {
            style.align = properties->get("algn");
        }
        if (!style.margin) {
            style.margin = properties->integer("marL");
        }
        if (!style.indent) {
            style.indent = properties->integer("indent");
        }
        const auto spacing = [&](const char* tag, std::optional<double>& in_points, std::optional<double>& in_percent) {
            if (in_points || in_percent) {
                return;
            }
            if (const auto* node = properties->child(tag)) {
                if (const auto* value = node->child("a:spcPts")) {
                    in_points = static_cast<double>(value->integer("val").value_or(0)) / 100;
                } else if (const auto* value = node->child("a:spcPct")) {
                    in_percent = static_cast<double>(value->integer("val").value_or(0)) / 100000;
                }
            }
        };
        spacing("a:lnSpc", style.line_points, style.line_percent);
        spacing("a:spcBef", style.before_points, style.before_percent);
        spacing("a:spcAft", style.after_points, style.after_percent);
        if (!style.bullet) {
            if (properties->child("a:buNone") != nullptr) {
                style.bullet = "none";
            } else if (const auto* character = properties->child("a:buChar")) {
                style.bullet = "char";
                style.bullet_char = character->get("char", "\xE2\x80\xA2");
            } else if (const auto* number = properties->child("a:buAutoNum")) {
                style.bullet = "auto";
                style.auto_type = number->get("type", "arabicPeriod");
                style.start_at = number->integer("startAt");
            } else if (properties->child("a:buBlip") != nullptr) {
                style.bullet = "blip";
            }
        }
        if (!style.bullet_color) {
            if (const auto* color = properties->child("a:buClr")) {
                style.bullet_color = read_color(color, context);
            }
        }
    }

    namespace {
        const xml::Node* level_node(const xml::Node* style, int level) {
            if (style == nullptr) {
                return nullptr;
            }
            return style->child("a:lvl" + std::to_string(std::clamp(level, 0, 8) + 1) + "pPr");
        }
    }

    ParagraphStyle resolve_paragraph(const xml::Node* paragraph_properties, const StyleChain& chain, int level, const ColorContext& context) {
        ParagraphStyle style;
        merge_paragraph(style, paragraph_properties, context);
        for (const auto* node : chain) {
            merge_paragraph(style, level_node(node, level), context);
        }
        return style;
    }

    RunStyle resolve_run(const xml::Node* run_properties, const StyleChain& chain, int level, const ColorContext& context) {
        RunStyle style;
        merge_run(style, run_properties, context);
        for (const auto* node : chain) {
            const auto* level_properties = level_node(node, level);
            merge_run(style, level_properties != nullptr ? level_properties->child("a:defRPr") : nullptr, context);
        }
        return style;
    }
}

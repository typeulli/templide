#include "backend.h"

#include "html.h"
#include "pptx.h"
#include "../middleend/tasset.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <iterator>

namespace templide::backend {
    std::vector<std::string> write_target(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                          const std::filesystem::path& libs_dir, std::vector<std::string>& warnings) {
        for (const auto& warning : target_warnings(document, target)) {
            warnings.push_back(warning.where + ": warning: " + warning.message);
        }
        if (target.type == "pptx") {
            return pptx::write(document, target, base_dir);
        }
        if (target.type == "html" || target.type == "web") {
            return html::write(document, target, base_dir, libs_dir);
        }
        return {"Unsupported target type: " + target.type};
    }

    std::vector<Warning> target_warnings(const ir::Document& document, const ir::Target& target) {
        std::vector<Warning> result;
        const bool pptx = target.type == "pptx";
        const auto add = [&](const std::string& where, const std::string& message) {
            if (std::none_of(result.begin(), result.end(), [&](const Warning& each) { return each.where == where && each.message == message; })) {
                result.push_back({where, message});
            }
        };
        const auto check = [&](const ir::Action& action) {
            if (pptx && action.kind == "run") {
                add(action.where, "run(\"" + action.target + "\") calls a JS function, which only works in html and web; the pptx target '" + target.name + "' ignores it");
            } else if (!pptx && (action.kind == "program" || action.kind == "macro")) {
                add(action.where, action.kind + "(\"" + action.target + "\") only works in PowerPoint; the " + target.type + " target '" + target.name + "' ignores it");
            }
        };
        const auto check_text = [&](const ir::Text& text) {
            for (const auto& paragraph : text.paragraphs) {
                for (const auto& run : paragraph.runs) {
                    for (const auto* action : {&run.style.action, &run.style.hover_action}) {
                        if (*action) {
                            check(**action);
                        }
                    }
                }
            }
        };
        std::function<void(const std::vector<ir::Element>&)> check_elements = [&](const std::vector<ir::Element>& elements) {
            for (const auto& element : elements) {
                for (const auto& property : element.properties) {
                    if (const auto* action = std::get_if<ir::Action>(&property.value)) {
                        check(*action);
                    } else if (const auto* text = std::get_if<ir::Text>(&property.value)) {
                        check_text(*text);
                    }
                }
                check_elements(element.children);
            }
        };
        if (pptx && !target.script.empty()) {
            add(target.script_where, "script is only loaded by html and web targets; the pptx target '" + target.name + "' ignores it");
        }
        for (const std::size_t index : target.masters) {
            for (const auto& layout : document.masters.at(index).layouts) {
                check_elements(layout.elements);
            }
        }
        for (const auto& slide : document.slides) {
            check_elements(slide.elements);
            for (const auto& [role, text] : slide.placeholders) {
                check_text(text);
            }
        }
        return result;
    }

    // mp4, m4a(mvhd 상자), wav(data 크기 / 초당 바이트), mp3(첫 프레임의 비트율)의 길이(ms). 알 수 없으면 0
    long long media_duration(const std::string& bytes, const std::string& name) {
        const auto u32 = [&](std::size_t at) -> unsigned long long {
            if (at + 4 > bytes.size()) {
                return 0;
            }
            return (static_cast<unsigned long long>(static_cast<unsigned char>(bytes[at])) << 24) | (static_cast<unsigned char>(bytes[at + 1]) << 16)
                | (static_cast<unsigned char>(bytes[at + 2]) << 8) | static_cast<unsigned char>(bytes[at + 3]);
        };
        const auto le32 = [&](std::size_t at) -> unsigned long long {
            if (at + 4 > bytes.size()) {
                return 0;
            }
            return static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1]) << 8) | (static_cast<unsigned char>(bytes[at + 2]) << 16)
                | (static_cast<unsigned long long>(static_cast<unsigned char>(bytes[at + 3])) << 24);
        };
        if (name.ends_with(".mp4") || name.ends_with(".m4a")) {
            // moov 상자 안의 mvhd. 상자는 크기(4) + 종류(4)로 시작한다
            std::function<long long(std::size_t, std::size_t)> find = [&](std::size_t begin, std::size_t end) -> long long {
                for (std::size_t at = begin; at + 8 <= end;) {
                    unsigned long long size = u32(at);
                    const std::string type = bytes.substr(at + 4, 4);
                    std::size_t header = 8;
                    if (size == 1) {
                        size = (u32(at + 8) << 32) | u32(at + 12);
                        header = 16;
                    } else if (size == 0) {
                        size = end - at;
                    }
                    if (size < header || at + size > end) {
                        return 0;
                    }
                    if (type == "moov") {
                        return find(at + header, at + size);
                    }
                    if (type == "mvhd") {
                        const std::size_t body = at + header;
                        const bool long_form = body < bytes.size() && bytes[body] == 1;
                        const unsigned long long scale = u32(body + (long_form ? 20 : 12));
                        const unsigned long long length = long_form ? (u32(body + 24) << 32) | u32(body + 28) : u32(body + 16);
                        return scale > 0 ? static_cast<long long>(length * 1000 / scale) : 0;
                    }
                    at += size;
                }
                return 0;
            };
            return find(0, bytes.size());
        }
        if (name.ends_with(".wav") && bytes.size() > 12 && bytes.compare(0, 4, "RIFF") == 0) {
            unsigned long long rate = 0;
            for (std::size_t at = 12; at + 8 <= bytes.size();) {
                const std::string type = bytes.substr(at, 4);
                const unsigned long long size = le32(at + 4);
                if (type == "fmt ") {
                    rate = le32(at + 16);
                } else if (type == "data") {
                    return rate > 0 ? static_cast<long long>(size * 1000 / rate) : 0;
                }
                at += 8 + size + (size % 2);
            }
            return 0;
        }
        if (name.ends_with(".mp3")) {
            std::size_t at = 0;
            if (bytes.size() > 10 && bytes.compare(0, 3, "ID3") == 0) {
                at = 10 + ((static_cast<unsigned char>(bytes[6]) & 0x7F) << 21 | (static_cast<unsigned char>(bytes[7]) & 0x7F) << 14
                           | (static_cast<unsigned char>(bytes[8]) & 0x7F) << 7 | (static_cast<unsigned char>(bytes[9]) & 0x7F));
            }
            // MPEG-1 Layer III의 비트율(kbps)
            static const int rates[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
            for (; at + 4 <= bytes.size(); ++at) {
                const auto first = static_cast<unsigned char>(bytes[at]);
                const auto second = static_cast<unsigned char>(bytes[at + 1]);
                if (first == 0xFF && (second & 0xFE) == 0xFA) {
                    const int rate = rates[static_cast<unsigned char>(bytes[at + 2]) >> 4];
                    return rate > 0 ? static_cast<long long>((bytes.size() - at) * 8 / rate) : 0;
                }
            }
        }
        return 0;
    }

    std::filesystem::path utf8_path(const std::string& text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    std::string display(const std::filesystem::path& path) {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::optional<std::string> read_file(const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            if (const auto inside = tasset::split(file)) {
                return tasset::read(inside->first, inside->second);
            }
            return std::nullopt;
        }
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::optional<std::filesystem::path> disk_file(const std::filesystem::path& file) {
        std::error_code code;
        if (std::filesystem::is_regular_file(file, code)) {
            return file;
        }
        if (const auto inside = tasset::split(file)) {
            return tasset::extract(inside->first, inside->second);
        }
        return std::nullopt;
    }

    const ir::Value* find_property(const ir::Element& element, const std::string& name) {
        for (const auto& property : element.properties) {
            if (property.name == name) {
                return &property.value;
            }
        }
        return nullptr;
    }

    std::string enum_member(const ir::Element& element, const std::string& name) {
        const auto* value = std::get_if<ir::EnumValue>(find_property(element, name));
        return value != nullptr ? value->member : "";
    }

    double srgb_to_linear(double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    }

    double linear_to_srgb(double value) {
        return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
    }

    bool smooth_gradient(const ir::Gradient& gradient) {
        const auto same = [](const ir::Color& a, const ir::Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a && a.scheme == b.scheme;
        };
        const auto positions = gradient_stops(gradient);
        if (positions.front() != 0 || positions.back() != 1) {
            return false;
        }
        return gradient.colors.size() == 2 || (gradient.colors.size() == 3 && same(gradient.colors.front(), gradient.colors.back()));
    }

    double gradient_curve(double ratio) {
        static const double sigma = 0.255 * std::sqrt(2.0);
        static const double edge = std::erf(0.5 / sigma);
        return (std::erf((ratio - 0.5) / sigma) + edge) / (2 * edge);
    }

    std::vector<double> gradient_stops(const ir::Gradient& gradient) {
        const std::size_t count = gradient.colors.size();
        std::vector<std::optional<double>> known(count);
        for (std::size_t i = 0; i < count && i < gradient.positions.size(); ++i) {
            if (gradient.positions[i]) {
                double percent = 0;
                for (const auto& [unit, value] : gradient.positions[i]->terms) {
                    percent += value;
                }
                known[i] = percent / 100;
            }
        }
        if (!known.front()) {
            known.front() = 0.0;
        }
        if (!known.back()) {
            known.back() = 1.0;
        }
        std::vector<double> result(count);
        std::size_t previous = 0;
        result[0] = *known[0];
        for (std::size_t i = 1; i < count; ++i) {
            if (!known[i]) {
                continue;
            }
            const double value = std::max(*known[i], result[previous]);
            for (std::size_t j = previous + 1; j < i; ++j) {
                result[j] = result[previous] + (value - result[previous]) * static_cast<double>(j - previous) / static_cast<double>(i - previous);
            }
            result[i] = value;
            previous = i;
        }
        return result;
    }
}

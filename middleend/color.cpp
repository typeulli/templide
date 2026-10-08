#include "color.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace templide::color {
    namespace {
        constexpr double pi = 3.14159265358979323846;

        using Vec = std::array<double, 3>;
        using Mat = std::array<Vec, 3>;

        Vec multiply(const Mat& m, const Vec& v) {
            return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
                    m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
                    m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
        }

        // sRGB 감마
        double to_linear(double c) {
            const double a = std::abs(c);
            const double v = a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4);
            return std::copysign(v, c);
        }
        double to_gamma(double c) {
            const double a = std::abs(c);
            const double v = a <= 0.0031308 ? a * 12.92 : 1.055 * std::pow(a, 1 / 2.4) - 0.055;
            return std::copysign(v, c);
        }

        Vec linear_of(const Rgb& rgb) { return {to_linear(rgb.r), to_linear(rgb.g), to_linear(rgb.b)}; }
        Rgb gamma_of(const Vec& v) { return {to_gamma(v[0]), to_gamma(v[1]), to_gamma(v[2])}; }

        // CSS Color 4의 행렬들
        constexpr Mat linear_srgb_to_xyz65 = {{{0.41239079926595934, 0.357584339383878, 0.1804807884018343},
                                               {0.21263900587151027, 0.715168678767756, 0.07219231536073371},
                                               {0.01933081871559182, 0.11919477979462598, 0.9505321522496607}}};
        constexpr Mat xyz65_to_linear_srgb = {{{3.2409699419045226, -1.537383177570094, -0.4986107602930034},
                                               {-0.9692436362808796, 1.8759675015077202, 0.04155505740717559},
                                               {0.05563007969699366, -0.20397695888897652, 1.0569715142428786}}};
        constexpr Mat d65_to_d50 = {{{1.0479297925449969, 0.022946870601609652, -0.05019226628920524},
                                     {0.02962780877005599, 0.9904344267538799, -0.017073799063418826},
                                     {-0.009243040646204504, 0.015055191490298152, 0.7518742814281371}}};
        constexpr Mat d50_to_d65 = {{{0.955473421488075, -0.02309845494876471, 0.06325924320057072},
                                     {-0.0283697093338637, 1.0099953980813041, 0.021041441191917323},
                                     {0.012314014864481998, -0.020507649298898964, 1.330365926242124}}};
        constexpr Mat xyz65_to_lms = {{{0.8190224379967030, 0.3619062600528904, -0.1288737815209879},
                                       {0.0329836539323885, 0.9292868615863434, 0.0361446663506424},
                                       {0.0481771893596242, 0.2642395317527308, 0.6335478284694309}}};
        constexpr Mat lms_to_xyz65 = {{{1.2268798758459243, -0.5578149944602171, 0.2813910456659647},
                                       {-0.0405757452148008, 1.1122868032803170, -0.0717110580655164},
                                       {-0.0763729366746601, -0.4214933324022432, 1.5869240198367816}}};
        constexpr Mat lms_to_oklab = {{{0.2104542683093140, 0.7936177747023054, -0.0040720430116193},
                                       {1.9779985324311684, -2.4285922420485799, 0.4505937096174110},
                                       {0.0259040424655478, 0.7827717124575296, -0.8086757549230774}}};
        constexpr Mat oklab_to_lms = {{{1.0, 0.3963377773761749, 0.2158037573099136},
                                       {1.0, -0.1055613458156586, -0.0638541728258133},
                                       {1.0, -0.0894841775298119, -1.2914855480194092}}};
        constexpr Vec d50_white = {0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};

        Vec oklab_to_xyz65(const Vec& lab) {
            Vec lms = multiply(oklab_to_lms, lab);
            for (auto& c : lms) {
                c = c * c * c;
            }
            return multiply(lms_to_xyz65, lms);
        }
        Vec xyz65_to_oklab(const Vec& xyz) {
            Vec lms = multiply(xyz65_to_lms, xyz);
            for (auto& c : lms) {
                c = std::cbrt(c);
            }
            return multiply(lms_to_oklab, lms);
        }

        Vec lab_to_xyz50(const Vec& lab) {
            constexpr double kappa = 24389.0 / 27;
            constexpr double epsilon = 216.0 / 24389;
            const double fy = (lab[0] + 16) / 116;
            const double fx = lab[1] / 500 + fy;
            const double fz = fy - lab[2] / 200;
            const Vec xyz = {fx * fx * fx > epsilon ? fx * fx * fx : (116 * fx - 16) / kappa,
                             lab[0] > kappa * epsilon ? std::pow((lab[0] + 16) / 116, 3) : lab[0] / kappa,
                             fz * fz * fz > epsilon ? fz * fz * fz : (116 * fz - 16) / kappa};
            return {xyz[0] * d50_white[0], xyz[1] * d50_white[1], xyz[2] * d50_white[2]};
        }
        Vec xyz50_to_lab(const Vec& xyz) {
            constexpr double kappa = 24389.0 / 27;
            constexpr double epsilon = 216.0 / 24389;
            Vec f;
            for (int i = 0; i < 3; ++i) {
                const double v = xyz[i] / d50_white[i];
                f[i] = v > epsilon ? std::cbrt(v) : (kappa * v + 16) / 116;
            }
            return {116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])};
        }

        // (L, C, h) <-> (L, a, b)
        Vec polar_to_lab(const Vec& lch) {
            const double h = lch[2] * pi / 180;
            return {lch[0], lch[1] * std::cos(h), lch[1] * std::sin(h)};
        }
        Vec lab_to_polar(const Vec& lab) {
            const double c = std::hypot(lab[1], lab[2]);
            double h = std::atan2(lab[2], lab[1]) * 180 / pi;
            if (h < 0) {
                h += 360;
            }
            return {lab[0], c, c < 1e-6 ? 0 : h};
        }

        Rgb hsl_to_srgb(double h, double s, double l) {
            h = std::fmod(std::fmod(h, 360) + 360, 360);
            const auto f = [&](double n) {
                const double k = std::fmod(n + h / 30, 12);
                const double a = s * std::min(l, 1 - l);
                return l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0}));
            };
            return {f(0), f(8), f(4)};
        }
        Rgb hwb_to_srgb(double h, double w, double b) {
            if (w + b >= 1) {
                const double gray = w / (w + b);
                return {gray, gray, gray};
            }
            const Rgb pure = hsl_to_srgb(h, 1, 0.5);
            const auto mix = [&](double c) { return c * (1 - w - b) + w; };
            return {mix(pure.r), mix(pure.g), mix(pure.b)};
        }

        Vec srgb_to_hsl(const Rgb& rgb) {
            const double max = std::max({rgb.r, rgb.g, rgb.b});
            const double min = std::min({rgb.r, rgb.g, rgb.b});
            const double l = (min + max) / 2;
            const double d = max - min;
            double h = 0;
            double s = 0;
            if (d > 1e-9) {
                s = (l == 0 || l == 1) ? 0 : (max - l) / std::min(l, 1 - l);
                if (max == rgb.r) {
                    h = (rgb.g - rgb.b) / d + (rgb.g < rgb.b ? 6 : 0);
                } else if (max == rgb.g) {
                    h = (rgb.b - rgb.r) / d + 2;
                } else {
                    h = (rgb.r - rgb.g) / d + 4;
                }
                h *= 60;
            }
            return {h, s, l};
        }

        Rgb oklab_to_srgb(const Vec& lab) { return gamma_of(multiply(xyz65_to_linear_srgb, oklab_to_xyz65(lab))); }
        Vec srgb_to_oklab(const Rgb& rgb) { return xyz65_to_oklab(multiply(linear_srgb_to_xyz65, linear_of(rgb))); }

        Rgb clip(const Rgb& rgb) {
            const auto c = [](double v) { return std::clamp(v, 0.0, 1.0); };
            return {c(rgb.r), c(rgb.g), c(rgb.b)};
        }

        double delta_eok(const Vec& a, const Vec& b) {
            return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }
    }

    Rgb to_srgb(const std::string& space, const std::array<double, 3>& v) {
        if (space == "hsl") {
            return hsl_to_srgb(v[0], v[1], v[2]);
        }
        if (space == "hwb") {
            return hwb_to_srgb(v[0], v[1], v[2]);
        }
        if (space == "lab" || space == "lch") {
            const Vec lab = space == "lab" ? Vec(v) : polar_to_lab(v);
            return gamma_of(multiply(xyz65_to_linear_srgb, multiply(d50_to_d65, lab_to_xyz50(lab))));
        }
        if (space == "oklab" || space == "oklch") {
            return oklab_to_srgb(space == "oklab" ? Vec(v) : polar_to_lab(v));
        }
        return {};
    }

    bool in_gamut(const Rgb& rgb) {
        // 0~255로 반올림하면 같은 값이 되는 만큼은 범위 안으로 본다 (소수 둘째 자리로 적은 lab의 빨강 등)
        constexpr double tolerance = 0.5 / 255;
        return rgb.r >= -tolerance && rgb.r <= 1 + tolerance && rgb.g >= -tolerance && rgb.g <= 1 + tolerance && rgb.b >= -tolerance && rgb.b <= 1 + tolerance;
    }

    Rgb gamut_map(const Rgb& rgb) {
        if (in_gamut(rgb)) {
            return clip(rgb);
        }
        const Vec origin = lab_to_polar(srgb_to_oklab(rgb));
        if (origin[0] >= 1) {
            return {1, 1, 1};
        }
        if (origin[0] <= 0) {
            return {0, 0, 0};
        }
        constexpr double jnd = 0.02;
        constexpr double epsilon = 0.0001;
        Vec current = origin;
        Rgb clipped = clip(oklab_to_srgb(polar_to_lab(current)));
        if (delta_eok(srgb_to_oklab(clipped), polar_to_lab(current)) < jnd) {
            return clipped;
        }
        double min = 0;
        double max = origin[1];
        bool min_in_gamut = true;
        while (max - min > epsilon) {
            const double chroma = (min + max) / 2;
            current[1] = chroma;
            const Rgb candidate = oklab_to_srgb(polar_to_lab(current));
            if (min_in_gamut && in_gamut(candidate)) {
                min = chroma;
                continue;
            }
            clipped = clip(candidate);
            const double e = delta_eok(srgb_to_oklab(clipped), polar_to_lab(current));
            if (e < jnd) {
                if (jnd - e < epsilon) {
                    return clipped;
                }
                min_in_gamut = false;
                min = chroma;
            } else {
                max = chroma;
            }
        }
        return clipped;
    }

    std::array<double, 3> from_srgb(const std::string& space, const Rgb& rgb) {
        if (space == "hsl") {
            return srgb_to_hsl(rgb);
        }
        if (space == "hwb") {
            const Vec hsl = srgb_to_hsl(rgb);
            const double w = std::min({rgb.r, rgb.g, rgb.b});
            const double b = 1 - std::max({rgb.r, rgb.g, rgb.b});
            return {hsl[0], w, b};
        }
        if (space == "lab" || space == "lch") {
            const Vec lab = xyz50_to_lab(multiply(d65_to_d50, multiply(linear_srgb_to_xyz65, linear_of(rgb))));
            return space == "lab" ? lab : lab_to_polar(lab);
        }
        if (space == "oklab" || space == "oklch") {
            const Vec lab = srgb_to_oklab(rgb);
            return space == "oklab" ? lab : lab_to_polar(lab);
        }
        return {};
    }

    std::string example(const std::string& space) {
        if (space == "hsl") { return "hsl(160deg, 40%, 59%)"; }
        if (space == "hwb") { return "hwb(160deg, 30%, 20%)"; }
        if (space == "lab") { return "lab(70, -30, 10)"; }
        if (space == "lch") { return "lch(70, 30, 160deg)"; }
        if (space == "oklab") { return "oklab(70%, -0.1, 0.03)"; }
        return "oklch(70%, 0.12, 160deg)";
    }

    std::optional<Parsed> parse(const std::string& space, const std::vector<Component>& components, std::size_t& index, std::string& error) {
        if (components.size() != 3 && components.size() != 4) {
            index = 0;
            error = space + " needs three values and an optional alpha, such as " + example(space);
            return std::nullopt;
        }
        // 자리마다 받는 값: hue(도), percent(0~100% 또는 0~100), lab_l(0~100 또는 %), ok_l(0~1 또는 %),
        // axis(수, %는 scale의 비율), chroma(0 이상의 수, %는 scale의 비율), alpha(0~1 또는 %)
        enum class Slot { Hue, Percent, LabL, OkL, Axis, Chroma, Alpha };
        struct Rule {
            Slot slot;
            double scale = 1;
        };
        std::array<Rule, 3> rules;
        if (space == "hsl" || space == "hwb") {
            rules = {Rule{Slot::Hue}, Rule{Slot::Percent}, Rule{Slot::Percent}};
        } else if (space == "lab") {
            rules = {Rule{Slot::LabL}, Rule{Slot::Axis, 125}, Rule{Slot::Axis, 125}};
        } else if (space == "lch") {
            rules = {Rule{Slot::LabL}, Rule{Slot::Chroma, 150}, Rule{Slot::Hue}};
        } else if (space == "oklab") {
            rules = {Rule{Slot::OkL}, Rule{Slot::Axis, 0.4}, Rule{Slot::Axis, 0.4}};
        } else {
            rules = {Rule{Slot::OkL}, Rule{Slot::Chroma, 0.4}, Rule{Slot::Hue}};
        }
        const auto read = [&](const Component& component, Rule rule) -> std::optional<double> {
            const std::string& unit = component.unit;
            const double value = component.value;
            switch (rule.slot) {
                case Slot::Hue:
                    if (!unit.empty() && unit != "deg") {
                        error = "A hue must be an angle without a unit or with deg, such as 160deg";
                        return std::nullopt;
                    }
                    return value;
                case Slot::Percent:
                    if ((!unit.empty() && unit != "%") || value < 0 || value > 100) {
                        error = "Must be a percentage from 0% to 100%";
                        return std::nullopt;
                    }
                    return value / 100;
                case Slot::LabL:
                    if ((!unit.empty() && unit != "%") || value < 0 || value > 100) {
                        error = "Lightness must be from 0 to 100 (or 0% to 100%)";
                        return std::nullopt;
                    }
                    return value;
                case Slot::OkL:
                    if (unit == "%" ? value < 0 || value > 100 : (!unit.empty() || value < 0 || value > 1)) {
                        error = "Lightness must be from 0 to 1 (or 0% to 100%)";
                        return std::nullopt;
                    }
                    return unit == "%" ? value / 100 : value;
                case Slot::Axis:
                    if (!unit.empty() && unit != "%") {
                        error = "Must be a number without a unit or a percentage";
                        return std::nullopt;
                    }
                    return unit == "%" ? value / 100 * rule.scale : value;
                case Slot::Chroma:
                    if ((!unit.empty() && unit != "%") || value < 0) {
                        error = "Chroma must be 0 or more, without a unit or as a percentage";
                        return std::nullopt;
                    }
                    return unit == "%" ? value / 100 * rule.scale : value;
                case Slot::Alpha:
                    if (unit == "%" ? value < 0 || value > 100 : (!unit.empty() || value < 0 || value > 1)) {
                        error = "Alpha must be from 0 to 1 (or 0% to 100%)";
                        return std::nullopt;
                    }
                    return unit == "%" ? value / 100 : value;
            }
            return value;
        };
        Parsed parsed;
        for (std::size_t i = 0; i < components.size(); ++i) {
            const auto value = read(components[i], i < 3 ? rules[i] : Rule{Slot::Alpha});
            if (!value) {
                index = i;
                return std::nullopt;
            }
            (i < 3 ? parsed.values[i] : parsed.alpha) = *value;
        }
        return parsed;
    }

    namespace {
        // 소수 decimals 자리까지 적고 끝의 0은 뺀다
        std::string number(double value, int decimals) {
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

        double round_to(double value, int decimals) {
            const double scale = std::pow(10.0, decimals);
            return std::round(value * scale) / scale;
        }

        int byte(double c) {
            return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255));
        }
    }

    std::string format(const std::string& space, int r, int g, int b, double alpha) {
        alpha = std::clamp(alpha, 0.0, 1.0);
        const bool opaque = alpha >= 1 - 1e-9;
        if (space == "hex") {
            char text[16];
            if (opaque) {
                std::snprintf(text, sizeof text, "hex(%02X%02X%02X)", r, g, b);
            } else {
                std::snprintf(text, sizeof text, "hex(%02X%02X%02X%02X)", r, g, b, byte(alpha));
            }
            return text;
        }
        if (space == "rgb") {
            const std::string body = std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b);
            return opaque ? "rgb(" + body + ")" : "rgba(" + body + ", " + number(alpha, 2) + ")";
        }
        const Rgb rgb{r / 255.0, g / 255.0, b / 255.0};
        const std::array<double, 3> exact = from_srgb(space, rgb);
        // 자리마다 적는 모양: 값에 곱하는 수, 붙이는 단위, 수가 작아 더 적는 소수 자리
        struct Shape {
            double scale;
            const char* suffix;
            int extra;
        };
        std::array<Shape, 3> shapes;
        if (space == "hsl" || space == "hwb") {
            shapes = {Shape{1, "deg", 0}, Shape{100, "%", 0}, Shape{100, "%", 0}};
        } else if (space == "lab") {
            shapes = {Shape{1, "", 0}, Shape{1, "", 0}, Shape{1, "", 0}};
        } else if (space == "lch") {
            shapes = {Shape{1, "", 0}, Shape{1, "", 0}, Shape{1, "deg", 0}};
        } else if (space == "oklab") {
            shapes = {Shape{100, "%", 0}, Shape{1, "", 3}, Shape{1, "", 3}};
        } else {
            shapes = {Shape{100, "%", 0}, Shape{1, "", 3}, Shape{1, "deg", 0}};
        }
        // 다시 읽어 같은 0~255 색이 되는 가장 적은 소수 자리를 고른다
        std::string text;
        for (int decimals = 0; decimals <= 4; ++decimals) {
            std::array<double, 3> rounded{};
            text = space + "(";
            for (int i = 0; i < 3; ++i) {
                const double shown = round_to(exact[i] * shapes[i].scale, decimals + shapes[i].extra);
                rounded[i] = shown / shapes[i].scale;
                text += (i > 0 ? ", " : "") + number(shown, decimals + shapes[i].extra) + shapes[i].suffix;
            }
            const Rgb back = to_srgb(space, rounded);
            if (byte(back.r) == r && byte(back.g) == g && byte(back.b) == b) {
                break;
            }
        }
        if (!opaque) {
            text += ", " + number(alpha * 100, 1) + "%";
        }
        return text + ")";
    }
}

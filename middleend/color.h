#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// 색공간 변환. hsl, hwb, lab, lch, oklab, oklch를 sRGB로 바꾸고, sRGB를 다시 각 색공간으로 적는다 (편집기의 색 선택기).
// 수식과 범위는 CSS Color 4를 따른다. lab, lch는 D50, oklab, oklch는 D65 기준이다
namespace templide::color {
    // sRGB (0~1). 범위 밖일 수 있다
    struct Rgb {
        double r = 0;
        double g = 0;
        double b = 0;
    };

    // 색공간의 세 값을 sRGB로. space는 hsl, hwb, lab, lch, oklab, oklch이고 값은 아래 단위다
    //   hsl(h, s, l), hwb(h, w, b): h는 도, 나머지는 0~1
    //   lab(L, a, b), lch(L, C, h): L은 0~100, a, b, C는 수, h는 도
    //   oklab(L, a, b), oklch(L, C, h): L은 0~1, a, b, C는 수, h는 도
    Rgb to_srgb(const std::string& space, const std::array<double, 3>& values);

    // sRGB 범위 안인가 (0~255로 반올림해 같아지는 오차는 봐준다)
    bool in_gamut(const Rgb& rgb);

    // 범위 밖의 색을 CSS Color 4의 방법으로 sRGB 안에 넣는다: oklch에서 밝기와 색상은 두고 채도만 줄인다
    Rgb gamut_map(const Rgb& rgb);

    // sRGB (0~1)를 색공간의 세 값으로 (to_srgb의 반대). 무채색의 색상은 0이다
    std::array<double, 3> from_srgb(const std::string& space, const Rgb& rgb);

    // 색 함수에 적은 값 하나. unit은 "", "%", "deg"이고 그 밖의 단위나 식은 "?"다
    struct Component {
        double value = 0;
        std::string unit;
    };

    // 색 함수의 세 값과 투명도 (to_srgb의 단위)
    struct Parsed {
        std::array<double, 3> values{};
        double alpha = 1;
    };

    // hsl(...), oklch(...) 등에 적은 값(3개 또는 투명도까지 4개)을 읽는다. 단위와 범위는 CSS Color 4를 따른다.
    // 잘못 적었으면 nullopt이고 index에 그 자리, error에 이유가 있다
    std::optional<Parsed> parse(const std::string& space, const std::vector<Component>& components, std::size_t& index, std::string& error);

    // 오류 메시지에 쓰는 예 (oklch(70%, 0.12, 160deg) 등)
    std::string example(const std::string& space);

    // 0~255 색을 space(hex, rgb, hsl, hwb, lab, lch, oklab, oklch)의 함수로 적는다. 다시 읽으면 같은 값이 되는 가장 짧은 소수로 적는다
    std::string format(const std::string& space, int r, int g, int b, double alpha);
}

#pragma once

#include "../middleend/ir.h"

#include <optional>
#include <string>
#include <vector>

/** 백엔드가 차트를 그릴 수 있도록 chart element의 데이터를 풀어 둔 결과를 제공한다. */
namespace templide::backend::chart {
    enum class Kind { Column, Bar, Line, Area, Pie, Scatter };

    struct Series {
        std::string name;
        std::vector<double> values;
        std::vector<double> xs;         // 산점도만 있다
        std::optional<ir::Color> color; // colors로 정한 색. 없으면 테마의 강조색
    };

    struct Chart {
        Kind kind = Kind::Column;
        std::vector<std::string> categories; // 산점도는 비어 있다. 원은 조각의 이름
        std::vector<Series> series;
        std::vector<ir::Color> colors;       // 지정한 색들. 원은 조각마다 이 색을 되풀이해 쓴다
        std::string title;
        std::string x_title;
        std::string y_title;
        std::string legend = "bottom";       // none, top, bottom, left, right
        bool stacked = false;
        bool data_labels = false;
        bool smooth = false;
        bool markers = true;
        bool gridlines = true;
        std::optional<double> min;
        std::optional<double> max;
        double font_px = 14;
        ir::Color text_color{0x40, 0x40, 0x40, 1, ""};

        /** i번째 계열(원은 조각)의 지정한 색. colors가 비었으면 nullopt다. */
        std::optional<ir::Color> color_of(std::size_t index) const {
            if (colors.empty()) {
                return std::nullopt;
            }
            return colors[index % colors.size()];
        }
    };

    /** IR의 chart element를 백엔드용 차트로 변환한다. 데이터를 읽을 수 없으면 nullopt다. */
    std::optional<Chart> resolve(const ir::Element& element);

    const char* kind_name(Kind kind);
}

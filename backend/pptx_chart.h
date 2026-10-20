#pragma once

#include "chart.h"

#include <functional>
#include <optional>
#include <string>

/** pptx의 차트 파트(ppt/charts/chartN.xml)와 PowerPoint에서 데이터를 편집할 때 열리는 통합 문서를 만든다. */
namespace templide::backend::pptx_chart {
    struct Parts {
        std::string chart;                    // chartN.xml의 내용
        std::optional<std::string> workbook;  // 통합 문서(.xlsx)의 바이트. 만들지 못하면 없다 (그러면 차트는 보이지만 데이터를 편집할 수 없다)
    };

    /** color는 ir::Color를 <a:srgbClr .../>나 <a:schemeClr .../> 글로 바꾼다. 테마 색도 그대로 쓴다. */
    Parts build(const chart::Chart& chart, const std::function<std::string(const ir::Color&)>& color);
}

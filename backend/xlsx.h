#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

/** 차트에 끼워 넣는 작은 통합 문서(.xlsx). PowerPoint에서 차트 데이터를 편집할 때 열린다. */
namespace templide::backend::xlsx {
    /** 칸 하나. 비어 있거나 수이거나 글이다. */
    using Cell = std::variant<std::monostate, double, std::string>;

    /** rows를 Sheet1의 칸으로 쓴 통합 문서의 바이트(zip)를 돌려준다. 만들지 못하면 nullopt다. */
    std::optional<std::string> workbook(const std::vector<std::vector<Cell>>& rows);

    /** 0부터 센 열 번호의 이름. 0은 A, 25는 Z, 26은 AA다. */
    std::string column_name(std::size_t column);
}

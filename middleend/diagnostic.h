#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace templide::middleend {
    enum class Level { Error, Warning, Info };

    struct Diagnostic {
        std::string path;
        std::size_t line;   // 위치가 없으면 0
        std::size_t column;
        std::string message;
        std::size_t length = 0; // 칠할 바이트 수. 0이면 그 자리의 낱말 하나
        Level level = Level::Error;
        std::string code;   // E1001처럼 카탈로그의 코드. 카탈로그에 없는 진단은 비어 있다
        std::string help;   // "did you mean 'x'?"처럼 고치는 방법. 없으면 비어 있다
    };

    const char* level_name(Level level); // error, warning, info

    // 진단 코드 하나의 카탈로그 항목. english는 컴파일러가 만드는 메시지의 모양이고 {}가 바뀌는 부분이다 (test/catalog_check.py가 소스와 맞는지 본다).
    // 같은 코드에 모양이 다른 항목이 여럿 있을 수 있다. 진단 메시지는 영어뿐이다
    struct CatalogEntry {
        const char* code;
        Level level;
        const char* name;    // 경고와 접근성 검사의 이름 (끄거나 찾을 때 쓴다). 없으면 ""
        const char* english;
    };

    // 코드의 첫 항목. 없으면 nullptr
    const CatalogEntry* find_catalog(std::string_view code);

    // 카탈로그 전체 (같은 코드는 붙어 있다)
    const std::vector<CatalogEntry>& catalog();

    // candidates 중 name과 가장 비슷한 것. 너무 다르면 비어 있다 ("did you mean"에 쓴다)
    std::optional<std::string> closest_name(std::string_view name, const std::vector<std::string>& candidates);

    // closest_name을 diagnostic.help 형식("did you mean 'x'?")으로 만든다
    std::string did_you_mean(std::string_view name, const std::vector<std::string>& candidates);
}

#pragma once

#include "../middleend/analyzer.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// 코드 에디터의 자동 완성. 커서 앞의 원문만 보고 어느 블록의 어디를 쓰는 중인지 판단하고,
// 이름(template, object, style, enum 등)은 마지막으로 분석에 성공한 Symbols에서 찾는다.
// 쓰는 중인 코드는 대개 구문 오류가 있으므로 원문을 다시 파싱하지 않는다
namespace templide::server {
    struct CompletionItem {
        std::string label;
        int kind;                // LSP CompletionItemKind
        std::string detail;
        std::string insert_text; // snippet이면 ${1:...} 자리 표시를 쓴다
        bool snippet = false;
        std::string sort_text;   // 비어 있으면 label 순서
    };

    struct Completion {
        std::size_t replace_from = 0; // 바꿀 범위의 시작(바이트). 끝은 커서다
        std::vector<CompletionItem> items;
    };

    Completion complete(const middleend::Symbols& symbols, const std::string& text, std::size_t offset, const std::filesystem::path& packages_dir);
}

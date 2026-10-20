#pragma once

#include "../middleend/analyzer.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

/**
 * 코드 편집기의 자동 완성 결과와 진입점을 정의한다.
 * 커서 앞의 원문으로 현재 위치를 판단하고, 이름은 마지막 분석에 성공한
 * Symbols에서 찾는다. 입력 중인 코드는 구문 오류일 수 있으므로 다시 파싱하지 않는다.
 */
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
        std::size_t replace_from = 0; // 바꿀 범위의 시작(바이트)
        std::size_t replace_to = std::string::npos; // 바꿀 범위의 끝(바이트). npos면 커서다
        std::vector<CompletionItem> items;
    };

    /**
     * 현재 입력 위치에 대한 자동 완성 항목을 반환한다.
     * @param document 상대 경로 include를 해석할 문서 경로
     */
    Completion complete(const middleend::Symbols& symbols, const std::string& text, std::size_t offset, const std::filesystem::path& packages_dir, const std::filesystem::path& document);
}

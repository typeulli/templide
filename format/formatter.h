#pragma once

#include "options.h"
#include "../middleend/diagnostic.h"

#include <string>
#include <vector>

/**
 * tlide fmt의 입력과 결과를 정의한다.
 * 토큰 구조를 보존한 채 공백, 줄바꿈, 들여쓰기를 정리하므로 프로그램의
 * 의미는 바뀌지 않는다. 문법 오류가 있는 입력은 수정하지 않고 진단만 반환한다.
 */
namespace templide::format {
    struct Result {
        bool ok = false;           // false면 text가 비어 있고 diagnostics에 이유가 있다
        bool has_settings = false; // #fmt가 하나라도 있었다. 없으면 text는 입력 그대로다
        std::string text;          // 다시 쓴 UTF-8 글 (줄 끝은 line_ending 설정대로)
        std::vector<middleend::Diagnostic> diagnostics; // 문법 오류, 안 되는 #fmt 옵션(경고), 내부 점검 실패
    };

    /**
     * 소스를 포맷한다.
     * @param path 진단에 표시할 파일 이름
     * @param source 포맷할 UTF-8 소스
     */
    Result format_source(const std::string& path, const std::string& source);

    /**
     * 소스에서 #fmt 지시문만 읽는다.
     * 문법 오류가 있어도 읽을 수 있는 지시문까지 반환하며, 편집기가
     * 파일에 포맷 설정이 있는지 확인할 때 사용한다.
     */
    std::vector<Directive> read_directives(const std::string& source);
}

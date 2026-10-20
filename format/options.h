#pragma once

#include "../middleend/diagnostic.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// .tlide 안의 `#fmt 옵션 값` 지시문과 tlide fmt의 설정.
// 옵션 이름과 값의 목록은 option_specs() 하나가 가지고 있고, 검사(분석기), 자동 완성(편집기), 설정 읽기(포매터)가 모두 이것을 쓴다
namespace templide::format {
    enum class OptionKind {
        Integer,       // min ~ max의 정수
        Boolean,       // true, false
        Choice,        // choices 중 하나
        KeepOrInteger, // keep 또는 min ~ max의 정수
    };

    struct OptionSpec {
        const char* name;
        OptionKind kind;
        const char* choices;       // Choice일 때 "a|b|c"
        int min;
        int max;
        const char* default_value; // 지시문에 적지 않았을 때 쓰는 값 (글)
        const char* description;
    };

    const std::vector<OptionSpec>& option_specs();
    const OptionSpec* find_option(std::string_view name);

    // 값이 옵션에 맞는지. 맞지 않으면 expected에 "one of a, b, c"나 "an integer from 1 to 16" 같은 기대하는 값을 쓴다
    bool valid_value(const OptionSpec& spec, std::string_view value, std::string& expected);

    // 옵션이 가질 수 있는 값들(자동 완성용). Integer는 비어 있다
    std::vector<std::string> option_values(const OptionSpec& spec);

    // 포매터가 쓰는 설정
    struct Settings {
        bool use_tabs = false;
        int indent_size = 4;
        bool brace_next_line = false;       // 여는 중괄호를 다음 줄에 둔다 (펼친 블록만)
        bool keep_short_blocks = true;      // 한 줄로 쓴 짧은 블록은 한 줄로 둔다
        int inline_max_statements = 0;      // 한 줄 블록의 문장 수 상한. 0이면 제한 없음
        bool empty_block_space = true;      // {} 대신 { }
        bool inline_brace_space = true;     // {a;} 대신 { a; }
        int max_line_length = 120;          // 한 줄 블록이 이 길이를 넘으면 펼친다. 0이면 제한 없음
        bool space_around_assign = true;
        bool space_around_operators = true;
        bool space_after_comma = true;
        bool space_inside_parens = false;
        int max_blank_lines = 1;
        int blank_lines_before_slide = -1;  // 파일 맨 위의 slide 앞 빈 줄 수. -1이면 원문 그대로
        int blank_lines_after_includes = -1;
        bool sort_properties = false;       // NAME = VALUE;만 있는 블록의 속성을 이름순으로
        bool sort_includes = false;         // 이어진 #include 줄을 이름순으로
        enum class HexCase { Keep, Upper, Lower } hex_case = HexCase::Keep;
        bool final_newline = true;
        enum class LineEnding { Keep, Lf, Crlf } line_ending = LineEnding::Keep;
        int comment_gap = 1;                // 줄 끝 주석 앞 공백 수
    };

    // 지시문 하나. 위치는 진단에 쓴다
    struct Directive {
        std::string key;
        std::string value;
        std::size_t line = 0;
        std::size_t column = 0;
        std::size_t key_length = 0;
        std::size_t value_line = 0;
        std::size_t value_column = 0;
        std::size_t value_length = 0;
    };

    // 지시문의 문제
    struct OptionProblem {
        enum class Kind { UnknownOption, InvalidValue, DuplicateOption } kind;
        std::size_t directive; // directives 안의 순서
        std::string expected;  // InvalidValue일 때 기대하는 값
        std::string help;      // 고치는 방법 ("did you mean 'x'?"). 없으면 비어 있다
    };

    // 지시문들을 읽어 설정을 만든다. 같은 옵션을 여러 번 쓰면 나중 것을 쓰고, 문제가 있으면 problems에 남긴다. 잘못된 값은 무시한다
    Settings read_settings(const std::vector<Directive>& directives, std::vector<OptionProblem>* problems = nullptr);

    // 지시문의 문제를 경고 진단으로 (W2002 모르는 옵션, W2003 잘못된 값, W2004 같은 옵션을 또 씀)
    middleend::Diagnostic option_diagnostic(const std::string& path, const Directive& directive, const OptionProblem& problem);

    // 모든 옵션을 기본값으로 쓴 `#fmt` 줄들 (편집기의 '기본 설정 삽입'이 넣는다)
    std::string default_directives();
}

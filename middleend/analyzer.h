#pragma once

#include "ir.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace templide::middleend {
    struct Diagnostic {
        std::string path;
        std::size_t line;   // 위치가 없으면 0
        std::size_t column;
        std::string message;
    };

    // 편집기 자동 완성에 쓰는 이름들. type은 int, float, string, text, color, bool, enum 이름 등이다
    struct SymbolVar {
        std::string name;
        std::string type;
        std::string default_value; // 기본값의 원문. 없으면 빈 문자열
        bool required = false;     // 기본값이 없어 put에서 꼭 넣어야 한다
    };

    struct SymbolMaster {
        std::vector<std::string> parameters; // 매개변수의 타입
        std::vector<std::string> cases;
    };

    struct Symbols {
        bool valid = false; // 문서에 구문 오류가 없어 이름을 모을 수 있었다
        std::map<std::string, std::vector<SymbolVar>> objects;
        std::map<std::string, std::vector<SymbolVar>> templates;
        std::map<std::string, std::vector<std::string>> styles; // style 이름 -> 매개변수 타입
        std::map<std::string, std::vector<std::string>> enums;
        std::map<std::string, SymbolMaster> masters;
        std::vector<std::string> themes;
        std::vector<SymbolVar> common_properties; // 모든 object에 붙는 속성
        std::vector<SymbolVar> text_properties;   // 글이 있는 object에 붙는 속성
        std::vector<SymbolVar> style_properties;  // style 선언과 style(...) 안의 속성
        std::vector<SymbolVar> slide_properties;
        std::map<std::string, std::vector<std::string>> transitions; // 종류 -> 옵션 (옵션이 없으면 비어 있다)
        std::map<std::string, std::vector<std::string>> animations;  // "종류.효과" -> 옵션 (첫 옵션이 기본값, 옵션이 없으면 비어 있다)
        std::vector<std::string> endless_animations;                 // 끝나는 시간이 없어 길이를 정할 수 없는 "종류.효과"
        std::vector<std::string> theme_colors;
    };

    struct Result {
        std::optional<ir::Document> document; // 에러가 하나라도 있으면 비어 있다
        std::vector<Diagnostic> diagnostics;
        // 읽은 파일의 경로와 내용. IR의 SourceRange와 Diagnostic이 이 경로를 쓴다
        std::vector<std::pair<std::string, std::string>> sources;
        Symbols symbols;
    };

    // 디스크 대신 쓸 파일 내용 (편집기에서 저장하지 않은 문서 등). 파일 경로 -> 내용
    using Overlays = std::map<std::filesystem::path, std::string>;

    // path 파일과 그 파일이 include하는 파일을 읽어 IR을 만든다.
    // #include <이름>은 packages_dir/<이름>.tlide, #include "이름"은 include한 파일의 폴더/<이름>.tlide에서 찾는다.
    // overlays에 있는 파일은 디스크에서 읽지 않고 그 내용을 쓴다
    Result analyze(const std::filesystem::path& path, const std::filesystem::path& packages_dir, const Overlays& overlays = {});
}

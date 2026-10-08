#pragma once

#include "../middleend/analyzer.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// 코드 에디터의 이름 찾기: 마우스를 올린 이름의 정보, 정의로 이동, 참조 찾기, 같은 이름 강조, 이름 바꾸기, 개요.
// 분석기가 읽은 파일들을 다시 파싱해 선언과 그 이름을 쓰는 곳을 모은다. 구문 오류가 있는 파일은 빠진다
namespace templide::server {
    // CONSTANT는 image, video, audio로 이름 붙인 값이다
    enum class SymbolKind { TEMPLATE, OBJECT, STYLE, COLOR, ENUM, ENUM_MEMBER, MASTER, LAYOUT, THEME, TARGET, VAR, PARAMETER, LOOP, ALIAS, FILE, CONSTANT };

    struct NavSymbol {
        SymbolKind kind;
        std::string name;
        std::string path;           // 선언이 있는 파일 (분석기가 쓴 경로)
        std::size_t begin = 0;      // 이름의 위치(바이트). FILE이면 파일의 처음
        std::size_t end = 0;
        std::size_t line = 0;       // 이름이 있는 줄 (1부터)
        std::string file;           // 정보 창에 보일 파일 이름. package면 <std/...>
        std::string type;           // VAR, PARAMETER, LOOP의 타입 이름. ALIAS면 put한 template이나 object
        std::string code;           // 정보 창에 보일 선언
        std::string doc;            // 선언 바로 앞의 /* */ 설명
        int owner = -1;             // VAR, ENUM_MEMBER, PARAMETER, LAYOUT이 속한 선언
        int block = -1;             // ALIAS, LOOP가 보이는 블록(slide, template, case 등)의 번호
        bool required = false;      // 기본값이 없는 VAR
        bool package = false;       // packages 폴더의 파일이라 바꿀 수 없다
    };

    // 원문에서 이름 하나가 쓰인 곳. symbol이 -1이면 컴파일러에 내장된 속성이라 정보만 있다
    struct Occurrence {
        std::string path;
        std::size_t begin = 0;
        std::size_t end = 0;
        int symbol = -1;
        bool definition = false;
        std::string hover; // 내장 속성의 정보
    };

    // 개요의 항목. kind는 LSP SymbolKind
    struct OutlineItem {
        std::string name;
        std::string detail;
        int kind = 0;
        std::size_t begin = 0, end = 0;           // 선언 전체
        std::size_t name_begin = 0, name_end = 0; // 고를 때 칠할 이름
        std::vector<OutlineItem> children;
    };

    struct NavSource {
        std::string path;
        const std::string* text;
        bool package;
    };

    struct NavIndex {
        std::vector<NavSymbol> symbols;
        std::vector<Occurrence> occurrences;
        std::map<std::string, std::vector<OutlineItem>> outlines; // 경로 -> 그 파일의 개요
    };

    NavIndex build_index(const std::vector<NavSource>& sources, const middleend::Symbols& symbols, const std::filesystem::path& packages_dir);

    // path의 offset 위치에 있는 이름. 없으면 nullptr
    const Occurrence* occurrence_at(const NavIndex& index, const std::string& path, std::size_t offset);

    // 정보 창의 내용 (markdown)
    std::string hover_text(const NavIndex& index, const Occurrence& occurrence);

    // 이름을 바꿀 수 없으면 그 이유. 바꿀 수 있으면 빈 값
    std::optional<std::string> rename_problem(const NavIndex& index, int symbol, const std::string& main_path);
    std::optional<std::string> new_name_problem(const NavIndex& index, int symbol, const std::string& name);
}

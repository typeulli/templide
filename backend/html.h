#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

// type = html은 모든 것을 넣은 html 파일 하나를, type = web은 index.html과 templide.js, reveal.js, media가 든 폴더를 만든다.
// 슬라이드는 JSON으로 적고 libs/templide.js가 브라우저에서 그린다
namespace templide::backend::html {
    // dry_run이면 파일을 쓰지 않고 에러만 알아본다
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir, bool dry_run = false);

    // type = pdf의 target을 PDF로 인쇄할 html 문서. 파일은 쓰지 않는다. 에러가 있으면 errors에 넣고 빈 문자열을 돌려준다
    std::string print_page(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                           const std::filesystem::path& libs_dir, std::vector<std::string>& errors);

    // 편집기 미리보기에 쓰는 덱 JSON(templide.js가 그리는 것). element마다 "src"에 IR의 id를 넣고, 그림은 data URI로 넣는다.
    // html로 그릴 수 없는 것이 있어도 덱을 만들고 그 문제를 errors에 넣는다
    std::string deck_json(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir, std::vector<std::string>& errors);
}

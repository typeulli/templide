#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

// type = html은 모든 것을 넣은 html 파일 하나를, type = web은 index.html과 templide.js, reveal.js, media가 든 폴더를 만든다.
// 슬라이드는 JSON으로 적고 libs/templide.js가 브라우저에서 그린다
namespace templide::backend::html {
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir);

    // 편집기 미리보기에 쓰는 덱 JSON(templide.js가 그리는 것). element마다 "src"에 IR의 id를 넣고, 그림은 data URI로 넣는다.
    // html로 그릴 수 없는 것이 있어도 덱을 만들고 그 문제를 errors에 넣는다
    std::string deck_json(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir, std::vector<std::string>& errors);
}

#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// type = html은 모든 것을 넣은 html 파일 하나를, type = web은 index.html과 templide.js, reveal.js, media가 든 폴더를 만든다.
// 슬라이드는 JSON으로 적고 libs/templide.js가 브라우저에서 그린다
namespace templide::backend::html {
    /** HTML 또는 웹 출력을 만든다. dry_run이면 파일을 쓰지 않고 오류만 검사한다. */
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir, bool dry_run = false);

    /** target의 슬라이드 크기(px)를 반환한다. 크기를 생략하면 1280 x 720이다. */
    std::pair<double, double> slide_size(const ir::Target& target);

    /** PDF 또는 PNG 렌더링에 사용할 HTML을 반환한다. 오류는 errors에 기록한다. */
    std::string print_page(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                           const std::filesystem::path& libs_dir, std::vector<std::string>& errors);

    /** 편집기 미리보기용 덱 JSON을 만든다. 렌더링할 수 없는 요소는 errors에 기록한다. */
    std::string deck_json(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir, std::vector<std::string>& errors);
}

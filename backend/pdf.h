#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

/** 슬라이드를 헤드리스 브라우저로 인쇄해 PDF로 저장한다. 슬라이드 하나가 한 페이지가 된다. */
namespace templide::backend::pdf {
    /** dry_run이면 PDF와 브라우저를 만들지 않고 오류만 검사한다. */
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir, bool dry_run = false);
}

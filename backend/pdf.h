#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

// type = pdf는 슬라이드를 html로 그려 Chrome이나 Edge(헤드리스)로 인쇄해 PDF를 만든다. 슬라이드 하나가 한 쪽이고 쪽 크기는 슬라이드 크기다.
// 전환과 애니메이션 없이 모든 개체가 보이고, 숨긴 슬라이드는 뺀다. 브라우저는 TEMPLIDE_BROWSER 환경 변수나 흔한 설치 위치에서 찾는다
namespace templide::backend::pdf {
    // dry_run이면 html만 만들어 에러를 알아보고 브라우저는 부르지 않는다
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir, bool dry_run = false);
}

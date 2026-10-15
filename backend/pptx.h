#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace templide::backend::pptx {
    // dry_run이면 파일을 쓰지 않고 에러만 알아본다
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir, bool dry_run = false);

    // pattern(...)의 종류 -> PowerPoint의 무늬 이름과 앞색이 차지하는 비율. pptx 불러오기도 거꾸로 쓴다
    const std::map<std::string, std::pair<std::string, double>>& pattern_presets();

    struct TransitionXml {
        std::string ns; // p, p14, p15, p159
        std::string element;
    };

    // "종류.옵션" -> 전환 요소. pptx 불러오기도 거꾸로 쓴다
    const std::map<std::string, TransitionXml>& transition_table();
}

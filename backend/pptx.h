#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace templide::backend::pptx {
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir);

    // pattern(...)의 종류 -> PowerPoint의 무늬 이름과 앞색이 차지하는 비율. pptx 불러오기도 거꾸로 쓴다
    const std::map<std::string, std::pair<std::string, double>>& pattern_presets();

    struct TransitionXml {
        std::string ns; // p, p14, p15, p159
        std::string element;
    };

    // "종류.옵션" -> 전환 요소. pptx 불러오기도 거꾸로 쓴다
    const std::map<std::string, TransitionXml>& transition_table();
}

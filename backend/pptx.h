#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

namespace templide::backend::pptx {
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir);
}

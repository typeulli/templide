#pragma once

#include <filesystem>

namespace templide::middleend {
    // templide.exe가 있는 폴더
    std::filesystem::path executable_dir(const char* argv0);

    // templide.exe가 있는 폴더의 packages. #include <이름>을 찾는 곳
    std::filesystem::path default_packages_dir(const char* argv0);

    // templide.exe가 있는 폴더의 libs. html 출력에 넣는 templide.js와 reveal.js가 있다
    std::filesystem::path default_libs_dir(const char* argv0);
}

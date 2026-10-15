#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace templide::middleend {
    // 명령줄 인자를 UTF-8로. Windows의 argv는 시스템 코드 페이지라 한글 경로를 path로 바꿀 수 없다
    std::vector<std::string> command_line_arguments(int argc, char* argv[]);

    // 출력이 콘솔이면 그 콘솔이 UTF-8로 읽게 한다 (컴파일러의 출력은 모두 UTF-8이다). 끝나면 원래대로 되돌린다. Windows 밖에서는 하는 일이 없다
    void use_utf8_console();

    // templide.exe가 있는 폴더
    std::filesystem::path executable_dir(const char* argv0);

    // templide.exe가 있는 폴더의 packages. #include <이름>을 찾는 곳
    std::filesystem::path default_packages_dir(const char* argv0);

    // templide.exe가 있는 폴더의 libs. html 출력에 넣는 templide.js와 reveal.js가 있다
    std::filesystem::path default_libs_dir(const char* argv0);
}

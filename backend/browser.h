#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

/** PDF와 PNG 출력에서 헤드리스 브라우저를 실행하는 공통 기능. */
namespace templide::backend::browser {
    namespace fs = std::filesystem;

    constexpr int timeout_seconds = 120;

    bool is_file(const fs::path& file);

    /** 사용할 브라우저 실행 파일을 찾는다. */
    std::optional<fs::path> find_browser();

    /** 브라우저 실행 결과. */
    struct Outcome {
        bool started = false;
        bool timed_out = false;
        int exit_code = 0;
        std::string message; // 실행하지 못한 까닭
    };

    /** 프로그램을 실행하고 종료를 기다린다. 출력은 log에 기록한다. */
    Outcome run_program(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& log);

    /** 파일 경로를 file URL로 변환한다. */
    std::string file_url(const fs::path& file);

    /** 로그의 마지막 부분을 반환한다. */
    std::string log_tail(const fs::path& log);

    std::string unique_name();

    // 임시 폴더. 쓰고 나면 지운다
    struct Scratch {
        fs::path path;
        ~Scratch() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };
}

#include "library.h"

#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

namespace templide::middleend {
    namespace {
#ifdef _WIN32
        UINT saved_output_code_page = 0;

        void restore_console() {
            if (const UINT code_page = std::exchange(saved_output_code_page, 0)) {
                SetConsoleOutputCP(code_page);
            }
        }

        // Ctrl+C나 창 닫기로 끝나면 atexit이 불리지 않으므로 여기서도 되돌린다. 끝내는 일은 기본 처리기에 맡긴다
        BOOL WINAPI on_console_event(DWORD) {
            restore_console();
            return FALSE;
        }
#endif

        std::filesystem::path executable_path(const char* argv0) {
#ifdef _WIN32
            std::wstring buffer(MAX_PATH, L'\0');
            while (true) {
                const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0) {
                    break;
                }
                if (length < buffer.size()) {
                    buffer.resize(length);
                    return buffer;
                }
                buffer.resize(buffer.size() * 2);
            }
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
                std::error_code error;
                const auto path = std::filesystem::weakly_canonical(buffer.c_str(), error);
                if (!error) {
                    return path;
                }
            }
#elif defined(__linux__)
            {
                std::error_code error;
                const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
                if (!error) {
                    return path;
                }
            }
#endif
            std::error_code error;
            auto path = std::filesystem::absolute(argv0, error);
            return error ? std::filesystem::path(argv0) : path;
        }
    }

    std::vector<std::string> command_line_arguments(int argc, char* argv[]) {
        std::vector<std::string> arguments;
#ifdef _WIN32
        int count = 0;
        if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
            for (int i = 0; i < count; ++i) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
                std::string text(size > 1 ? size - 1 : 0, '\0');
                if (size > 1) {
                    WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
                }
                arguments.push_back(std::move(text));
            }
            LocalFree(wide);
            if (count == argc) {
                return arguments;
            }
            arguments.clear();
        }
#endif
        for (int i = 0; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }
        return arguments;
    }

    void use_utf8_console() {
#ifdef _WIN32
        DWORD mode = 0;
        const bool console = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) || GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode);
        const UINT current = GetConsoleOutputCP();
        if (!console || current == 0 || current == CP_UTF8 || !SetConsoleOutputCP(CP_UTF8)) {
            return;
        }
        saved_output_code_page = current;
        SetConsoleCtrlHandler(on_console_event, TRUE);
        std::atexit(restore_console);
#endif
    }

    std::filesystem::path executable_dir(const char* argv0) {
        return executable_path(argv0).parent_path();
    }

    std::filesystem::path default_packages_dir(const char* argv0) {
        return executable_dir(argv0) / "packages";
    }

    std::filesystem::path default_libs_dir(const char* argv0) {
        return executable_dir(argv0) / "libs";
    }
}

#include "library.h"

#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace templide::middleend {
    namespace {
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
#endif
            std::error_code error;
            auto path = std::filesystem::absolute(argv0, error);
            return error ? std::filesystem::path(argv0) : path;
        }
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

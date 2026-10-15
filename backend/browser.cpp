#include "browser.h"

#include "backend.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace templide::backend::browser {
    bool is_file(const fs::path& file) {
        std::error_code code;
        return fs::is_regular_file(file, code);
    }

    std::optional<fs::path> environment_path(const char* name) {
#ifdef _WIN32
        const std::string ascii(name);
        const std::wstring wide_name(ascii.begin(), ascii.end());
        if (const wchar_t* value = _wgetenv(wide_name.c_str()); value != nullptr && *value != L'\0') {
            return fs::path(value);
        }
#else
        if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
            return fs::path(value);
        }
#endif
        return std::nullopt;
    }

    // 인쇄에 쓸 브라우저. TEMPLIDE_BROWSER가 먼저이고, 없으면 흔한 설치 위치와 PATH에서 찾는다 (Windows는 Edge, 나머지는 Chrome이 먼저)
    std::optional<fs::path> find_browser() {
        if (const auto chosen = environment_path("TEMPLIDE_BROWSER")) {
            return is_file(*chosen) ? chosen : std::nullopt;
        }
        std::vector<fs::path> candidates;
#ifdef _WIN32
        for (const char* relative : {"Microsoft/Edge/Application/msedge.exe", "Google/Chrome/Application/chrome.exe"}) {
            for (const char* variable : {"ProgramFiles(x86)", "ProgramFiles", "LocalAppData"}) {
                if (const auto base = environment_path(variable)) {
                    candidates.push_back(*base / relative);
                }
            }
        }
#elif defined(__APPLE__)
        candidates = {"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
                      "/Applications/Chromium.app/Contents/MacOS/Chromium"};
#else
        if (const auto path = environment_path("PATH")) {
            const std::string text = path->string();
            for (const char* name : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge", "microsoft-edge-stable"}) {
                for (std::size_t begin = 0; begin <= text.size();) {
                    const std::size_t end = std::min(text.find(':', begin), text.size());
                    if (end > begin) {
                        candidates.push_back(fs::path(text.substr(begin, end - begin)) / name);
                    }
                    begin = end + 1;
                }
            }
        }
#endif
        for (const auto& candidate : candidates) {
            if (is_file(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

#ifdef _WIN32
    std::wstring widen(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    // CommandLineToArgvW가 풀어 읽는 규칙대로 인수를 감싼다
    std::wstring quote_argument(const std::wstring& argument) {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
            return argument;
        }
        std::wstring result = L"\"";
        for (auto it = argument.begin();; ++it) {
            std::size_t backslashes = 0;
            while (it != argument.end() && *it == L'\\') {
                ++it;
                ++backslashes;
            }
            if (it == argument.end()) {
                result.append(backslashes * 2, L'\\');
                break;
            }
            if (*it == L'"') {
                result.append(backslashes * 2 + 1, L'\\');
            } else {
                result.append(backslashes, L'\\');
            }
            result.push_back(*it);
        }
        result.push_back(L'"');
        return result;
    }

    // 출력은 log에 쓴다. 작업 개체(job)에 넣어 두어 시간이 지나거나 끝난 뒤에도 남은 자식 프로세스를 함께 끝낸다
    Outcome run_program(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& log) {
        Outcome outcome;
        std::wstring command = quote_argument(program.wstring());
        for (const auto& argument : arguments) {
            command += L" " + quote_argument(widen(argument));
        }
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = output;
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                            nullptr, nullptr, &startup, &process);
        if (output != INVALID_HANDLE_VALUE) {
            CloseHandle(output);
        }
        if (input != INVALID_HANDLE_VALUE) {
            CloseHandle(input);
        }
        if (!created) {
            outcome.message = "error " + std::to_string(GetLastError());
            if (job != nullptr) {
                CloseHandle(job);
            }
            return outcome;
        }
        outcome.started = true;
        if (job != nullptr) {
            AssignProcessToJobObject(job, process.hProcess);
        }
        ResumeThread(process.hThread);
        if (WaitForSingleObject(process.hProcess, timeout_seconds * 1000) == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            outcome.timed_out = true;
        }
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        outcome.exit_code = static_cast<int>(code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (job != nullptr) {
            CloseHandle(job);
        }
        return outcome;
    }
#else
    // 출력은 log에 쓴다. 새 프로세스 그룹으로 띄워 시간이 지나면 자식까지 함께 끝낸다
    Outcome run_program(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& log) {
        Outcome outcome;
        std::vector<std::string> storage{program.string()};
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& each : storage) {
            argv.push_back(each.data());
        }
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, 1, 2);
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);
        pid_t pid = 0;
        const int code = posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (code != 0) {
            outcome.message = std::strerror(code);
            return outcome;
        }
        outcome.started = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
        int status = 0;
        for (;;) {
            const pid_t done = waitpid(pid, &status, WNOHANG);
            if (done == pid) {
                outcome.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                break;
            }
            if (done < 0) {
                outcome.exit_code = -1;
                break;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
                outcome.timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return outcome;
    }
#endif

    // 경로를 file: 주소로. 영문자, 숫자와 -._~/: 말고는 UTF-8 바이트마다 %XX로 쓴다
    std::string file_url(const fs::path& file) {
        std::string path = display(file);
        std::replace(path.begin(), path.end(), '\\', '/');
        std::string url = !path.empty() && path.front() == '/' ? "file://" : "file:///";
        static constexpr std::string_view hex = "0123456789ABCDEF";
        for (const unsigned char c : path) {
            if ((std::isalnum(c) != 0 && c < 0x80) || std::string_view("-._~/:").find(static_cast<char>(c)) != std::string_view::npos) {
                url += static_cast<char>(c);
            } else {
                url += '%';
                url += hex[c >> 4];
                url += hex[c & 15];
            }
        }
        return url;
    }

    // 브라우저가 남긴 출력의 끝부분. 실패한 까닭을 알리는 데 쓴다
    std::string log_tail(const fs::path& log) {
        std::ifstream input(log, std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
            text.pop_back();
        }
        constexpr std::size_t limit = 600;
        if (text.size() > limit) {
            text = "..." + text.substr(text.size() - limit);
        }
        return text;
    }

    std::string unique_name() {
        std::random_device device;
        return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(device());
    }
}

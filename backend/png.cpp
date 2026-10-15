#include "png.h"

#include "backend.h"
#include "browser.h"
#include "html.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>

namespace templide::backend::png {
    using namespace browser;

    std::filesystem::path numbered_path(const std::filesystem::path& path, std::size_t number) {
        const std::string suffix = "-" + std::to_string(number);
        const std::u8string name = path.stem().u8string() + std::u8string(suffix.begin(), suffix.end()) + path.extension().u8string();
        fs::path result = path;
        result.replace_filename(fs::path(name));
        return result;
    }

    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const fs::path& base_dir, const fs::path& libs_dir, bool dry_run) {
        std::vector<std::string> errors;
        const fs::path wanted = utf8_path(target.path);
        const std::u8string extension_text = wanted.extension().u8string();
        std::string extension(extension_text.begin(), extension_text.end());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string page = html::print_page(document, target, base_dir, libs_dir, errors);
        if (extension != ".png") {
            errors.insert(errors.begin(), "the path of a png target must end with .png, such as \"slides.png\" (slide n is written as slides-n.png)");
        }
        if (!errors.empty() || dry_run) {
            return errors;
        }
        // 숨긴 슬라이드는 뺀다. 파일 번호는 만드는 차례다
        std::vector<std::size_t> slides;
        for (std::size_t i = 0; i < document.slides.size(); ++i) {
            if (!document.slides[i].hidden) {
                slides.push_back(i);
            }
        }
        if (slides.empty()) {
            return {"there is no slide to draw; every slide is hidden"};
        }
        const auto browser = find_browser();
        if (!browser) {
            return {"cannot find a browser to make the images with; install Chrome or Edge, or set TEMPLIDE_BROWSER to the path of one"};
        }
        std::error_code code;
        const fs::path root = fs::temp_directory_path(code);
        if (code) {
            return {"cannot find the temporary folder: " + code.message()};
        }
        const Scratch scratch{root / ("templide-png-" + unique_name())};
        fs::create_directories(scratch.path, code);
        if (code) {
            return {"cannot create folder " + display(scratch.path) + ": " + code.message()};
        }
        const fs::path source = scratch.path / "deck.html";
        {
            std::ofstream file(source, std::ios::binary);
            file.write(page.data(), static_cast<std::streamsize>(page.size()));
            if (!file) {
                return {"cannot write " + display(source)};
            }
        }
        const auto [width, height] = html::slide_size(target);
        const std::string size = std::to_string(std::llround(width)) + "," + std::to_string(std::llround(height));
        const fs::path output = wanted.is_absolute() ? wanted : base_dir / wanted;
        if (output.has_parent_path()) {
            fs::create_directories(output.parent_path(), code);
        }
        for (std::size_t i = 0; i < slides.size(); ++i) {
            // 이미 떠 있는 브라우저와 섞이지 않도록 프로필을 따로 둔다. 슬라이드는 주소의 #page=번호로 고른다
            const fs::path produced = scratch.path / ("slide-" + std::to_string(i + 1) + ".png");
            const fs::path log = scratch.path / "browser.log";
            std::vector<std::string> arguments = {
                "--headless", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--hide-scrollbars",
                "--user-data-dir=" + display(scratch.path / "profile"),
                "--force-device-scale-factor=" + std::to_string(target.scale),
                "--window-size=" + size,
            };
            if (target.transparent) {
                arguments.push_back("--default-background-color=00000000");
            }
            arguments.push_back("--screenshot=" + display(produced));
            arguments.push_back(file_url(source) + "#page=" + std::to_string(slides[i]) + (target.transparent ? "&transparent=1" : ""));
            const Outcome outcome = run_program(*browser, arguments, log);
            if (!outcome.started) {
                return {"cannot start " + display(*browser) + ": " + outcome.message};
            }
            if (outcome.timed_out) {
                return {"the browser did not finish slide " + std::to_string(slides[i] + 1) + " within " + std::to_string(timeout_seconds) + " seconds"};
            }
            if (!is_file(produced) || fs::file_size(produced, code) == 0) {
                const std::string tail = log_tail(log);
                return {"the browser made no image for slide " + std::to_string(slides[i] + 1) + " (exit code " + std::to_string(outcome.exit_code) + ")" + (tail.empty() ? "" : ": " + tail)};
            }
            const fs::path destination = numbered_path(output, i + 1);
            fs::copy_file(produced, destination, fs::copy_options::overwrite_existing, code);
            if (code) {
                return {"cannot write " + display(destination) + ": " + code.message()};
            }
        }
        return {};
    }
}

#include "pdf.h"

#include "backend.h"
#include "browser.h"
#include "html.h"

#include <fstream>
#include <optional>
#include <system_error>

namespace templide::backend::pdf {
    using namespace browser;

    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const fs::path& base_dir, const fs::path& libs_dir, bool dry_run) {
        std::vector<std::string> errors;
        const std::string page = html::print_page(document, target, base_dir, libs_dir, errors);
        if (!errors.empty() || dry_run) {
            return errors;
        }
        const auto browser = find_browser();
        if (!browser) {
            return {"cannot find a browser to make the PDF with; install Chrome or Edge, or set TEMPLIDE_BROWSER to the path of one"};
        }
        std::error_code code;
        const fs::path root = fs::temp_directory_path(code);
        if (code) {
            return {"cannot find the temporary folder: " + code.message()};
        }
        const Scratch scratch{root / ("templide-pdf-" + unique_name())};
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
        // 이미 떠 있는 브라우저와 섞이지 않도록 프로필을 따로 둔다. 옛 버전은 --print-to-pdf-no-header로 머리글과 바닥글을 뺀다
        const fs::path produced = scratch.path / "out.pdf";
        const fs::path log = scratch.path / "browser.log";
        const std::vector<std::string> arguments = {
            "--headless", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions",
            "--user-data-dir=" + display(scratch.path / "profile"),
            "--no-pdf-header-footer", "--print-to-pdf-no-header",
            "--print-to-pdf=" + display(produced),
            file_url(source),
        };
        const Outcome outcome = run_program(*browser, arguments, log);
        if (!outcome.started) {
            return {"cannot start " + display(*browser) + ": " + outcome.message};
        }
        if (outcome.timed_out) {
            return {"the browser did not finish the PDF within " + std::to_string(timeout_seconds) + " seconds"};
        }
        if (!is_file(produced) || fs::file_size(produced, code) == 0) {
            const std::string tail = log_tail(log);
            return {"the browser made no PDF (exit code " + std::to_string(outcome.exit_code) + ")" + (tail.empty() ? "" : ": " + tail)};
        }
        const fs::path wanted = utf8_path(target.path);
        const fs::path output = wanted.is_absolute() ? wanted : base_dir / wanted;
        if (output.has_parent_path()) {
            fs::create_directories(output.parent_path(), code);
        }
        fs::copy_file(produced, output, fs::copy_options::overwrite_existing, code);
        if (code) {
            return {"cannot write " + display(output) + ": " + code.message()};
        }
        return {};
    }
}

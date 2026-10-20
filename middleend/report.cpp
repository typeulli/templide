#include "report.h"

#include "json.hpp"

namespace templide::middleend {
    std::optional<ReportFormat> report_format_from_name(std::string_view name) {
        if (name == "text") {
            return ReportFormat::Text;
        }
        if (name == "json") {
            return ReportFormat::Json;
        }
        if (name == "github") {
            return ReportFormat::Github;
        }
        return std::nullopt;
    }

    namespace {
        std::string code_suffix(const Diagnostic& diagnostic) {
            return diagnostic.code.empty() ? "" : "[" + diagnostic.code + "]";
        }

        // GitHub 워크플로 명령의 속성 값과 메시지에서 따로 막아야 하는 글자
        std::string github_escape(const std::string& text, bool property) {
            std::string result;
            for (const char c : text) {
                switch (c) {
                    case '%': result += "%25"; break;
                    case '\r': result += "%0D"; break;
                    case '\n': result += "%0A"; break;
                    case ':':
                        if (property) {
                            result += "%3A";
                        } else {
                            result += c;
                        }
                        break;
                    case ',':
                        if (property) {
                            result += "%2C";
                        } else {
                            result += c;
                        }
                        break;
                    default: result += c;
                }
            }
            return result;
        }

        // GitHub는 note, warning, error만 안다. 정보는 note다
        const char* github_command(Level level) {
            switch (level) {
                case Level::Error: return "error";
                case Level::Warning: return "warning";
                case Level::Info: return "notice";
            }
            return "error";
        }
    }

    std::string format_text(const Diagnostic& diagnostic) {
        std::string result;
        if (!diagnostic.path.empty()) {
            result += diagnostic.path;
            if (diagnostic.line > 0) {
                result += ":" + std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column);
            }
            result += ": ";
        }
        result += level_name(diagnostic.level) + code_suffix(diagnostic) + ": " + diagnostic.message;
        if (!diagnostic.help.empty()) {
            result += "\n  help: " + diagnostic.help;
        }
        return result;
    }

    std::string format_github(const Diagnostic& diagnostic) {
        std::string result = std::string("::") + github_command(diagnostic.level);
        std::string properties;
        const auto add = [&](const std::string& key, const std::string& value) {
            properties += (properties.empty() ? " " : ",") + key + "=" + github_escape(value, true);
        };
        if (!diagnostic.path.empty()) {
            add("file", diagnostic.path);
        }
        if (diagnostic.line > 0) {
            add("line", std::to_string(diagnostic.line));
            add("col", std::to_string(diagnostic.column));
        }
        if (!diagnostic.code.empty()) {
            add("title", diagnostic.code);
        }
        std::string message = diagnostic.message;
        if (!diagnostic.help.empty()) {
            message += "\n" + diagnostic.help;
        }
        return result + properties + "::" + github_escape(message, false);
    }

    namespace {
        using json = nlohmann::ordered_json;

        json entry_json(const Diagnostic& diagnostic) {
            json entry = json::object();
            if (!diagnostic.path.empty()) {
                entry["file"] = diagnostic.path;
            }
            if (diagnostic.line > 0) {
                entry["line"] = diagnostic.line;
                entry["column"] = diagnostic.column;
                if (diagnostic.length > 0) {
                    entry["length"] = diagnostic.length;
                }
            }
            entry["level"] = level_name(diagnostic.level);
            if (!diagnostic.code.empty()) {
                entry["code"] = diagnostic.code;
                if (const CatalogEntry* catalog_entry = find_catalog(diagnostic.code); catalog_entry != nullptr && catalog_entry->name[0] != '\0') {
                    entry["name"] = catalog_entry->name;
                }
            }
            entry["message"] = diagnostic.message;
            if (!diagnostic.help.empty()) {
                entry["help"] = diagnostic.help;
            }
            return entry;
        }
    }

    std::string format_json_entry(const Diagnostic& diagnostic) {
        return entry_json(diagnostic).dump();
    }

    std::string format_json(const std::vector<Diagnostic>& diagnostics) {
        json list = json::array();
        std::size_t errors = 0;
        std::size_t warnings = 0;
        std::size_t infos = 0;
        for (const auto& diagnostic : diagnostics) {
            list.push_back(entry_json(diagnostic));
            switch (diagnostic.level) {
                case Level::Error: ++errors; break;
                case Level::Warning: ++warnings; break;
                case Level::Info: ++infos; break;
            }
        }
        const json document = {{"version", 1}, {"diagnostics", list}, {"summary", {{"errors", errors}, {"warnings", warnings}, {"infos", infos}}}};
        return document.dump(2);
    }
}

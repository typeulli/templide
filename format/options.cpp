#include "options.h"

#include "../middleend/diagnostic.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace templide::format {
    const std::vector<OptionSpec>& option_specs() {
        static const std::vector<OptionSpec> specs = {
            {"indent_style", OptionKind::Choice, "space|tab", 0, 0, "space", "Indent with spaces or tabs"},
            {"indent_size", OptionKind::Integer, "", 1, 16, "4", "Columns per indent level (also the tab width when measuring line length)"},
            {"brace_style", OptionKind::Choice, "same_line|next_line", 0, 0, "same_line", "Put the opening brace of a multi-line block on the same line or on its own line"},
            {"short_blocks", OptionKind::Choice, "keep|expand", 0, 0, "keep", "keep leaves a block written on one line as it is; expand always breaks blocks into lines"},
            {"inline_max_statements", OptionKind::Integer, "", 0, 100, "0", "Most statements a one-line block may have (0 = no limit)"},
            {"empty_blocks", OptionKind::Choice, "space|tight", 0, 0, "space", "Write an empty block as { } or {}"},
            {"inline_brace_space", OptionKind::Boolean, "", 0, 0, "true", "Write one-line blocks as { a; } instead of {a;}"},
            {"max_line_length", OptionKind::Integer, "", 0, 500, "120", "A one-line block longer than this is broken into lines (0 = no limit)"},
            {"space_around_assign", OptionKind::Boolean, "", 0, 0, "true", "Put spaces around ="},
            {"space_around_operators", OptionKind::Boolean, "", 0, 0, "true", "Put spaces around + - * / == < >"},
            {"space_after_comma", OptionKind::Boolean, "", 0, 0, "true", "Put a space after a comma"},
            {"space_inside_parens", OptionKind::Boolean, "", 0, 0, "false", "Put spaces inside ( )"},
            {"max_blank_lines", OptionKind::Integer, "", 0, 10, "1", "Most blank lines in a row to keep"},
            {"blank_lines_before_slide", OptionKind::KeepOrInteger, "", 0, 10, "keep", "Blank lines before each top-level slide (keep = as written)"},
            {"blank_lines_after_includes", OptionKind::KeepOrInteger, "", 0, 10, "keep", "Blank lines after the last #include of a group (keep = as written)"},
            {"sort_properties", OptionKind::Choice, "keep|alphabetical", 0, 0, "keep", "Order of NAME = VALUE; lines in a block that has nothing else"},
            {"sort_includes", OptionKind::Choice, "keep|alphabetical", 0, 0, "keep", "Order of consecutive #include lines (packages first)"},
            {"hex_case", OptionKind::Choice, "keep|upper|lower", 0, 0, "keep", "Letter case of the digits in hex(...)"},
            {"final_newline", OptionKind::Boolean, "", 0, 0, "true", "End the file with a line break"},
            {"line_ending", OptionKind::Choice, "keep|lf|crlf", 0, 0, "keep", "Line break of the file (keep = the one most lines use)"},
            {"comment_gap", OptionKind::Integer, "", 1, 8, "1", "Spaces before a comment that follows code on the same line"},
        };
        return specs;
    }

    const OptionSpec* find_option(std::string_view name) {
        for (const auto& spec : option_specs()) {
            if (name == spec.name) {
                return &spec;
            }
        }
        return nullptr;
    }

    namespace {
        std::vector<std::string> split_choices(const char* choices) {
            std::vector<std::string> result;
            std::string current;
            for (const char* p = choices; *p != '\0'; ++p) {
                if (*p == '|') {
                    result.push_back(current);
                    current.clear();
                } else {
                    current += *p;
                }
            }
            if (!current.empty()) {
                result.push_back(current);
            }
            return result;
        }

        bool all_digits(std::string_view text) {
            return !text.empty() && text.size() <= 6 && std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        }

        bool in_range(std::string_view text, const OptionSpec& spec) {
            if (!all_digits(text)) {
                return false;
            }
            const int number = std::stoi(std::string(text));
            return number >= spec.min && number <= spec.max;
        }

        std::string integer_expected(const OptionSpec& spec) {
            return "an integer from " + std::to_string(spec.min) + " to " + std::to_string(spec.max);
        }
    }

    bool valid_value(const OptionSpec& spec, std::string_view value, std::string& expected) {
        switch (spec.kind) {
            case OptionKind::Integer:
                expected = integer_expected(spec);
                return in_range(value, spec);
            case OptionKind::Boolean:
                expected = "true or false";
                return value == "true" || value == "false";
            case OptionKind::Choice: {
                const auto choices = split_choices(spec.choices);
                expected = "one of ";
                for (std::size_t i = 0; i < choices.size(); ++i) {
                    expected += (i > 0 ? ", " : "") + choices[i];
                }
                return std::find(choices.begin(), choices.end(), value) != choices.end();
            }
            case OptionKind::KeepOrInteger:
                expected = "keep or " + integer_expected(spec);
                return value == "keep" || in_range(value, spec);
        }
        return false;
    }

    std::vector<std::string> option_values(const OptionSpec& spec) {
        switch (spec.kind) {
            case OptionKind::Boolean:
                return {"true", "false"};
            case OptionKind::Choice:
                return split_choices(spec.choices);
            case OptionKind::KeepOrInteger:
                return {"keep"};
            case OptionKind::Integer:
                return {};
        }
        return {};
    }

    namespace {
        void apply(Settings& settings, const std::string& name, const std::string& value) {
            const bool on = value == "true";
            const int number = all_digits(value) ? std::stoi(value) : 0;
            if (name == "indent_style") { settings.use_tabs = value == "tab"; }
            else if (name == "indent_size") { settings.indent_size = number; }
            else if (name == "brace_style") { settings.brace_next_line = value == "next_line"; }
            else if (name == "short_blocks") { settings.keep_short_blocks = value == "keep"; }
            else if (name == "inline_max_statements") { settings.inline_max_statements = number; }
            else if (name == "empty_blocks") { settings.empty_block_space = value == "space"; }
            else if (name == "inline_brace_space") { settings.inline_brace_space = on; }
            else if (name == "max_line_length") { settings.max_line_length = number; }
            else if (name == "space_around_assign") { settings.space_around_assign = on; }
            else if (name == "space_around_operators") { settings.space_around_operators = on; }
            else if (name == "space_after_comma") { settings.space_after_comma = on; }
            else if (name == "space_inside_parens") { settings.space_inside_parens = on; }
            else if (name == "max_blank_lines") { settings.max_blank_lines = number; }
            else if (name == "blank_lines_before_slide") { settings.blank_lines_before_slide = value == "keep" ? -1 : number; }
            else if (name == "blank_lines_after_includes") { settings.blank_lines_after_includes = value == "keep" ? -1 : number; }
            else if (name == "sort_properties") { settings.sort_properties = value == "alphabetical"; }
            else if (name == "sort_includes") { settings.sort_includes = value == "alphabetical"; }
            else if (name == "hex_case") {
                settings.hex_case = value == "upper" ? Settings::HexCase::Upper : value == "lower" ? Settings::HexCase::Lower : Settings::HexCase::Keep;
            }
            else if (name == "final_newline") { settings.final_newline = on; }
            else if (name == "line_ending") {
                settings.line_ending = value == "lf" ? Settings::LineEnding::Lf : value == "crlf" ? Settings::LineEnding::Crlf : Settings::LineEnding::Keep;
            }
            else if (name == "comment_gap") { settings.comment_gap = number; }
        }
    }

    Settings read_settings(const std::vector<Directive>& directives, std::vector<OptionProblem>* problems) {
        Settings settings;
        std::map<std::string, std::size_t> seen;
        for (std::size_t i = 0; i < directives.size(); ++i) {
            const Directive& directive = directives[i];
            const OptionSpec* spec = find_option(directive.key);
            if (spec == nullptr) {
                if (problems != nullptr) {
                    std::vector<std::string> names;
                    for (const auto& known : option_specs()) {
                        names.push_back(known.name);
                    }
                    problems->push_back({OptionProblem::Kind::UnknownOption, i, "", middleend::did_you_mean(directive.key, names)});
                }
                continue;
            }
            if (seen.contains(directive.key) && problems != nullptr) {
                problems->push_back({OptionProblem::Kind::DuplicateOption, i, "", ""});
            }
            std::string expected;
            if (!valid_value(*spec, directive.value, expected)) {
                if (problems != nullptr) {
                    problems->push_back({OptionProblem::Kind::InvalidValue, i, expected, ""});
                }
                continue;
            }
            seen[directive.key] = i;
            apply(settings, directive.key, directive.value);
        }
        return settings;
    }

    middleend::Diagnostic option_diagnostic(const std::string& path, const Directive& directive, const OptionProblem& problem) {
        using middleend::Level;
        switch (problem.kind) {
            case OptionProblem::Kind::UnknownOption:
                return {path, directive.line, directive.column, "Unknown #fmt option: " + directive.key, directive.key_length, Level::Warning, "W2002", problem.help};
            case OptionProblem::Kind::InvalidValue:
                return {path, directive.value_line, directive.value_column, "Invalid value '" + directive.value + "' for #fmt option '" + directive.key + "'; expected " + problem.expected,
                        directive.value_length, Level::Warning, "W2003"};
            case OptionProblem::Kind::DuplicateOption:
                return {path, directive.line, directive.column, "Duplicate #fmt option: " + directive.key + "; the last one is used", directive.key_length, Level::Warning, "W2004"};
        }
        return {path, directive.line, directive.column, "Invalid #fmt option", directive.key_length, Level::Warning, "W2003"};
    }

    std::string default_directives() {
        std::string result;
        for (const auto& spec : option_specs()) {
            result += std::string("#fmt ") + spec.name + " " + spec.default_value + "\n";
        }
        return result;
    }
}

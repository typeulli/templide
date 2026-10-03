#include "edits.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <tuple>

namespace templide::server {
    namespace {
        constexpr const char* indent_unit = "    ";

        bool is_space(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        // 정수만 받는 값이면 정수로, 아니면 소수 둘째 자리까지
        std::string format_amount(double value, bool integer) {
            value = integer ? std::round(value) : std::round(value * 100) / 100;
            if (value == 0) {
                value = 0; // -0을 0으로
            }
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.10g", value);
            return buffer;
        }

        // px 하나가 unit으로 얼마인지. %는 reference에 대한 비율이다
        std::optional<double> unit_per_px(const std::string& unit, double reference) {
            if (unit.empty() || unit == "px") {
                return 1.0;
            }
            if (unit == "pt") {
                return 0.75;
            }
            if (unit == "in") {
                return 1.0 / 96;
            }
            if (unit == "cm") {
                return 2.54 / 96;
            }
            if (unit == "mm") {
                return 25.4 / 96;
            }
            if (unit == "%" && reference > 0) {
                return 100 / reference;
            }
            return std::nullopt;
        }

        // %를 풀 기준. x, width는 슬라이드 가로, y, height는 세로이고 나머지는 %로 쓸 수 없다
        double reference_of(const std::string& property, SlideSize size) {
            if (property == "x" || property == "width") {
                return size.width;
            }
            if (property == "y" || property == "height") {
                return size.height;
            }
            return 0;
        }

        // backend와 같게 푼다. int 값의 %는 px 정수로 자른다
        double to_px(const ir::Number& number, double reference) {
            double total = 0;
            for (const auto& [unit, value] : number.terms) {
                if (unit == "%") {
                    const double px = value / 100 * reference;
                    total += number.is_float ? px : std::trunc(px);
                } else {
                    total += value;
                }
            }
            return total;
        }

        // px을 %로. backend가 int(reference * % / 100)로 자르므로 소수 둘째 자리에서 0에서 먼 쪽으로 올려
        // 다시 풀었을 때 같은 px이 나오게 한다 (reference가 10000px보다 작으면 맞다)
        std::string format_percent(double px, double reference) {
            const double exact = px / reference * 100;
            const double scaled = std::ceil(std::abs(exact) * 100 - 1e-6) / 100;
            return format_amount(exact < 0 ? -scaled : scaled, false);
        }

        const ir::Property* find(const std::vector<ir::Property>& properties, const std::string& name) {
            for (const auto& property : properties) {
                if (property.name == name) {
                    return &property;
                }
            }
            return nullptr;
        }

        bool is_hex(const std::string& text) {
            for (const char c : text) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    return false;
                }
            }
            return true;
        }

        bool is_name(const std::string& text) {
            if (text.empty() || std::isdigit(static_cast<unsigned char>(text[0]))) {
                return false;
            }
            for (const char c : text) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                    return false;
                }
            }
            return true;
        }

        EditResult failure(std::string message) {
            return {{}, std::move(message)};
        }
    }

    std::string quote_string(const std::string& text) {
        std::string result = "\"";
        for (const char c : text) {
            switch (c) {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                case '\r': break;
                default: result += c; break;
            }
        }
        return result + "\"";
    }

    std::string lock_message(const std::string& reason) {
        if (reason == "template") {
            return "This value is written inside a template definition; change it in the code";
        }
        if (reason == "computed") {
            return "This value is computed by an expression; change it in the code";
        }
        if (reason == "for") {
            return "This value is made by a for loop; change it in the code";
        }
        if (reason == "package") {
            return "This value comes from a package file, which cannot be edited";
        }
        if (reason == "layout") {
            return "This value belongs to a master layout; change it in the code";
        }
        return "This value cannot be edited";
    }

    const std::string* Editor::text_of(const std::string& path) const {
        const auto it = sources_.find(path);
        return it == sources_.end() ? nullptr : &it->second;
    }

    // 원문에 쓸 값. LITERAL은 원래 단위를 지키고, BLOCK에 새로 넣는 길이는 px로 쓴다
    std::string Editor::code(const ir::Origin& origin, const std::string& property, const NewValue& value, std::string& error) const {
        switch (value.kind) {
            case NewValue::Kind::LENGTH:
            case NewValue::Kind::NUMBER: {
                const bool literal = origin.kind == ir::Origin::Kind::LITERAL;
                std::string unit = literal ? origin.unit : value.kind == NewValue::Kind::LENGTH ? "px" : value.unit;
                double amount = value.number;
                if (value.kind == NewValue::Kind::LENGTH) {
                    const double reference = reference_of(property, size_);
                    const auto factor = unit_per_px(unit, reference);
                    if (!factor) {
                        error = "Cannot write a length in '" + unit + "' for '" + property + "'";
                        return "";
                    }
                    // %는 int 자리에서도 소수로 쓴다
                    if (unit == "%") {
                        return format_percent(amount, reference) + unit;
                    }
                    amount *= *factor;
                }
                return format_amount(amount, origin.integer) + unit;
            }
            case NewValue::Kind::STRING:
                return quote_string(value.text);
            case NewValue::Kind::COLOR: {
                const std::string digits = value.text.starts_with("#") ? value.text.substr(1) : value.text;
                if ((digits.size() != 6 && digits.size() != 8) || !is_hex(digits)) {
                    error = "A color must be #RRGGBB or #RRGGBBAA";
                    return "";
                }
                if (digits.size() == 6 || digits.substr(6) == "FF" || digits.substr(6) == "ff") {
                    std::string upper = digits.substr(0, 6);
                    for (auto& c : upper) {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    }
                    return "hex(" + upper + ")";
                }
                const auto channel = [&](std::size_t i) { return std::stoi(digits.substr(i, 2), nullptr, 16); };
                return "rgba(" + std::to_string(channel(0)) + ", " + std::to_string(channel(2)) + ", " + std::to_string(channel(4)) + ", " +
                       format_amount(channel(6) / 255.0, false) + ")";
            }
            case NewValue::Kind::NAME:
                if (!is_name(value.text)) {
                    error = "'" + value.text + "' is not a name";
                    return "";
                }
                return value.text;
            case NewValue::Kind::BOOL:
                return value.flag ? "true" : "false";
            case NewValue::Kind::CODE: {
                // 문장이나 블록을 끝내는 글자가 따옴표 밖에 있으면 식이 아니다
                bool quoted = false;
                for (std::size_t i = 0; i < value.text.size(); ++i) {
                    const char c = value.text[i];
                    if (quoted && c == '\\') {
                        ++i;
                    } else if (c == '"') {
                        quoted = !quoted;
                    } else if (!quoted && (c == ';' || c == '{' || c == '}' || c == '\n')) {
                        error = "'" + value.text + "' is not a value";
                        return "";
                    }
                }
                if (value.text.empty() || quoted) {
                    error = "'" + value.text + "' is not a value";
                    return "";
                }
                return value.text;
            }
        }
        return "";
    }

    EditResult Editor::set(const ir::Origin& origin, const std::string& property, const NewValue& value) const {
        if (origin.kind == ir::Origin::Kind::LOCKED) {
            return failure(lock_message(origin.reason));
        }
        std::string error;
        const std::string text = code(origin, property, value, error);
        if (!error.empty()) {
            return failure(error);
        }
        if (origin.kind == ir::Origin::Kind::LITERAL) {
            return {{{origin.range.path, origin.range.begin, origin.range.end, text}}, ""};
        }
        if (origin.name.empty()) {
            return failure("No property to set");
        }
        return insert_statement(origin, origin.name + " = " + text + ";");
    }

    EditResult Editor::set_property(const ir::Element& element, const std::string& property, const NewValue& value) const {
        if (const auto* found = find(element.properties, property)) {
            return set(found->origin, property, value);
        }
        if (element.from_template) {
            return failure(lock_message("template"));
        }
        ir::Origin origin = element.source;
        origin.name = property;
        return set(origin, property, value);
    }

    EditResult Editor::move(const ir::Element& element, double dx, double dy) const {
        const auto& properties = element.from_template ? element.instance : element.properties;
        EditResult result;
        for (const auto& [name, delta, reference] : {std::tuple{"x", dx, size_.width}, std::tuple{"y", dy, size_.height}}) {
            if (delta == 0) {
                continue;
            }
            const auto* property = find(properties, name);
            const auto* number = property != nullptr ? std::get_if<ir::Number>(&property->value) : nullptr;
            if (number == nullptr) {
                return failure(element.from_template ? std::string("The template has no '") + name + "' to move it with" : std::string("This object has no '") + name + "'");
            }
            EditResult part = set(property->origin, name, {NewValue::Kind::LENGTH, to_px(*number, reference) + delta});
            if (!part.error.empty()) {
                return part;
            }
            result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
        }
        return result;
    }

    EditResult Editor::set_text(const ir::Run& run, const std::string& text) const {
        const ir::Origin& origin = run.origin;
        if (origin.kind == ir::Origin::Kind::LITERAL) {
            const std::string* source = text_of(origin.range.path);
            if (source == nullptr || origin.range.begin >= source->size() || (*source)[origin.range.begin] != '"') {
                return failure("This text is not written as a string literal");
            }
        }
        return set(origin, "text", {NewValue::Kind::STRING, 0, text});
    }

    EditResult Editor::style(const ir::Run& run, std::size_t begin, std::size_t end, const std::vector<std::pair<std::string, NewValue>>& properties) const {
        const ir::Origin& origin = run.origin;
        if (origin.kind != ir::Origin::Kind::LITERAL) {
            return failure(lock_message(origin.reason));
        }
        const std::string* source = text_of(origin.range.path);
        if (source == nullptr || (*source)[origin.range.begin] != '"') {
            return failure("This text is not written as a string literal");
        }
        if (!origin.text) {
            return failure("This text is passed as a string, so part of it cannot be styled; change the var to text");
        }
        end = std::min(end, run.text.size());
        if (begin >= end || properties.empty()) {
            return failure("Nothing to style");
        }
        // run 전체가 이미 (style(...) "글자")로 감싸여 있으면 그 style에 합친다
        if (begin == 0 && end == run.text.size() && !origin.style.path.empty()) {
            return merge_style(origin, properties);
        }
        std::string style = "style(";
        ir::Origin fresh;
        fresh.kind = ir::Origin::Kind::BLOCK;
        for (std::size_t i = 0; i < properties.size(); ++i) {
            std::string error;
            const std::string value = code(fresh, properties[i].first, properties[i].second, error);
            if (!error.empty()) {
                return failure(error);
            }
            style += (i > 0 ? ", " : "") + properties[i].first + " = " + value;
        }
        // 감싼 style이 없는데 끄라고 하면 끄는 값을 그대로 넣는다 (바깥 style이 켠 것을 이 부분만 끈다)
        // 괄호로 감싸야 style이 뒤의 글자까지 이어지지 않는다
        std::string text = "(" + style + ") " + quote_string(run.text.substr(begin, end - begin)) + ")";
        if (begin > 0) {
            text = quote_string(run.text.substr(0, begin)) + " " + text;
        }
        if (end < run.text.size()) {
            text += " " + quote_string(run.text.substr(end));
        }
        return {{{origin.range.path, origin.range.begin, origin.range.end, text}}, ""};
    }

    // style(이름 = 값, ...)의 속성들. 괄호와 따옴표 안의 쉼표는 나누지 않는다
    static std::vector<std::pair<std::string, std::string>> style_arguments(const std::string& call) {
        std::vector<std::pair<std::string, std::string>> result;
        const std::size_t open = call.find('(');
        const std::size_t close = call.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) {
            return result;
        }
        const auto trim = [](std::string text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        };
        const auto add = [&](const std::string& argument) {
            const std::size_t equal = argument.find('=');
            if (equal != std::string::npos) {
                result.emplace_back(trim(argument.substr(0, equal)), trim(argument.substr(equal + 1)));
            }
        };
        int depth = 0;
        bool quoted = false;
        std::string current;
        for (std::size_t i = open + 1; i < close; ++i) {
            const char c = call[i];
            if (quoted) {
                current += c;
                if (c == '\\' && i + 1 < close) {
                    current += call[++i];
                } else if (c == '"') {
                    quoted = false;
                }
                continue;
            }
            if (c == '"') {
                quoted = true;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            } else if (c == ',' && depth == 0) {
                add(current);
                current.clear();
                continue;
            }
            current += c;
        }
        add(current);
        return result;
    }

    EditResult Editor::merge_style(const ir::Origin& origin, const std::vector<std::pair<std::string, NewValue>>& properties) const {
        const std::string* source = text_of(origin.style.path);
        if (source == nullptr) {
            return failure("Cannot find the source file");
        }
        auto arguments = style_arguments(source->substr(origin.style.begin, origin.style.end - origin.style.begin));
        ir::Origin fresh;
        fresh.kind = ir::Origin::Kind::BLOCK;
        for (const auto& [name, value] : properties) {
            const auto found = std::find_if(arguments.begin(), arguments.end(), [&](const auto& argument) { return argument.first == name; });
            if (value.unset && found != arguments.end()) {
                arguments.erase(found);
                continue;
            }
            std::string error;
            const std::string text = code(fresh, name, value, error);
            if (!error.empty()) {
                return failure(error);
            }
            if (found != arguments.end()) {
                found->second = text;
            } else {
                arguments.emplace_back(name, text);
            }
        }
        // 속성이 모두 없어지면 괄호와 style을 풀고 리터럴만 남긴다
        if (arguments.empty()) {
            return {{{origin.group.path, origin.group.begin, origin.group.end, source->substr(origin.range.begin, origin.range.end - origin.range.begin)}}, ""};
        }
        std::string call = "style(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            call += (i > 0 ? ", " : "") + arguments[i].first + " = " + arguments[i].second;
        }
        return {{{origin.style.path, origin.style.begin, origin.style.end, call + ")"}}, ""};
    }

    EditResult Editor::unstyle(const ir::Run& run) const {
        const ir::Origin& origin = run.origin;
        if (origin.kind != ir::Origin::Kind::LITERAL) {
            return failure(lock_message(origin.reason));
        }
        if (origin.group.path.empty()) {
            return failure("This text has no inline style to clear");
        }
        const std::string* source = text_of(origin.range.path);
        if (source == nullptr) {
            return failure("Cannot find the source file");
        }
        return {{{origin.group.path, origin.group.begin, origin.group.end, source->substr(origin.range.begin, origin.range.end - origin.range.begin)}}, ""};
    }

    std::optional<std::pair<std::size_t, std::size_t>> Editor::whole_lines(const ir::SourceRange& range) const {
        const std::string* text = text_of(range.path);
        if (text == nullptr) {
            return std::nullopt;
        }
        std::size_t begin = range.begin;
        while (begin > 0 && ((*text)[begin - 1] == ' ' || (*text)[begin - 1] == '\t')) {
            --begin;
        }
        std::size_t end = range.end;
        while (end < text->size() && ((*text)[end] == ' ' || (*text)[end] == '\t' || (*text)[end] == '\r')) {
            ++end;
        }
        if ((begin != 0 && (*text)[begin - 1] != '\n') || (end != text->size() && (*text)[end] != '\n')) {
            return std::nullopt;
        }
        return std::pair{begin, end < text->size() ? end + 1 : end};
    }

    EditResult Editor::swap(const ir::SourceRange& a, const ir::SourceRange& b) const {
        const std::string* text = text_of(a.path);
        if (text == nullptr || a.path != b.path) {
            return failure("Cannot swap statements in different files");
        }
        return {{{a.path, a.begin, a.end, text->substr(b.begin, b.end - b.begin)}, {b.path, b.begin, b.end, text->substr(a.begin, a.end - a.begin)}}, ""};
    }

    EditResult Editor::move_lines(const ir::SourceRange& statement, const ir::SourceRange& target, bool after) const {
        const std::string* text = text_of(statement.path);
        const auto from = whole_lines(statement);
        const auto to = whole_lines(target);
        if (text == nullptr || statement.path != target.path || !from || !to) {
            return failure("Only statements on their own lines can be moved this way; move it in the code");
        }
        std::string lines = text->substr(from->first, from->second - from->first);
        if (!lines.ends_with("\n")) {
            lines += "\n";
        }
        const std::size_t at = after ? to->second : to->first;
        if (after && at == text->size() && !text->ends_with("\n")) {
            lines = "\n" + lines.substr(0, lines.size() - 1);
        }
        return {{{statement.path, from->first, from->second, ""}, {statement.path, at, at, lines}}, ""};
    }

    // 같은 블록 안의 문장들. template이 만든 element들은 그 template을 넣은 put 하나로 센다
    EditResult Editor::reorder(const std::vector<ir::Element>& siblings, const ir::Element& element, const std::string& direction) const {
        if (element.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(element.source.reason));
        }
        std::vector<ir::SourceRange> statements;
        for (const auto& sibling : siblings) {
            const auto& range = sibling.source.range;
            if (sibling.source.kind == ir::Origin::Kind::BLOCK && range.path == element.source.range.path &&
                std::none_of(statements.begin(), statements.end(), [&](const auto& other) { return other.begin == range.begin; })) {
                statements.push_back(range);
            }
        }
        const auto here = std::find_if(statements.begin(), statements.end(), [&](const auto& range) { return range.begin == element.source.range.begin; });
        if (here == statements.end()) {
            return failure("Cannot find this object among its neighbours");
        }
        const auto index = static_cast<std::size_t>(here - statements.begin());
        const bool up = direction == "forward" || direction == "front";
        if ((up && index + 1 == statements.size()) || (!up && index == 0)) {
            return failure(up ? "It is already in front" : "It is already at the back");
        }
        if (direction == "forward" || direction == "backward") {
            return swap(*here, statements[up ? index + 1 : index - 1]);
        }
        return move_lines(*here, up ? statements.back() : statements.front(), up);
    }

    EditResult Editor::add_slide(const std::vector<ir::Slide>& slides, std::size_t after, const std::string& main) const {
        if (slides.empty() || after == 0) {
            // 첫 slide 앞, 또는 slide가 없으면 main 파일 끝에 넣는다
            if (!slides.empty() && slides.front().source.kind == ir::Origin::Kind::BLOCK) {
                const auto lines = whole_lines(slides.front().source.range);
                if (lines) {
                    return {{{slides.front().source.range.path, lines->first, lines->first, "slide {\n}\n\n"}}, ""};
                }
            }
            const std::string* text = text_of(main);
            if (text == nullptr) {
                return failure("Cannot find the main file");
            }
            const std::string prefix = text->empty() || text->ends_with("\n") ? "" : "\n";
            return {{{main, text->size(), text->size(), prefix + "\nslide {\n}\n"}}, ""};
        }
        if (after > slides.size()) {
            return failure("No slide " + std::to_string(after));
        }
        const ir::Origin& source = slides[after - 1].source;
        if (source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(source.reason));
        }
        if (const auto lines = whole_lines(source.range)) {
            return {{{source.range.path, lines->second, lines->second, "\nslide {\n}\n"}}, ""};
        }
        return {{{source.range.path, source.range.end, source.range.end, " slide {}"}}, ""};
    }

    EditResult Editor::remove_slide(const ir::Slide& slide) const {
        if (slide.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(slide.source.reason));
        }
        const std::string* text = text_of(slide.source.range.path);
        auto [begin, end] = whole_lines(slide.source.range).value_or(std::pair{slide.source.range.begin, slide.source.range.end});
        // slide 사이의 빈 줄 하나도 함께 지운다
        if (text != nullptr && end < text->size() && (*text)[end] == '\n' && begin > 0 && (*text)[begin - 1] == '\n') {
            ++end;
        }
        return {{{slide.source.range.path, begin, end, ""}}, ""};
    }

    EditResult Editor::duplicate_slide(const ir::Slide& slide) const {
        if (slide.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(slide.source.reason));
        }
        const std::string* text = text_of(slide.source.range.path);
        const auto lines = whole_lines(slide.source.range);
        if (text == nullptr || !lines) {
            return failure("Only a slide on its own lines can be duplicated; copy it in the code");
        }
        std::string copy = text->substr(lines->first, lines->second - lines->first);
        if (!copy.ends_with("\n")) {
            copy += "\n";
        }
        return {{{slide.source.range.path, lines->second, lines->second, "\n" + copy}}, ""};
    }

    EditResult Editor::move_slide(const std::vector<ir::Slide>& slides, std::size_t page, int step) const {
        const std::size_t other = page + step;
        if (page == 0 || page > slides.size() || other == 0 || other > slides.size()) {
            return failure(step < 0 ? "It is already the first slide" : "It is already the last slide");
        }
        const ir::Origin& a = slides[page - 1].source;
        const ir::Origin& b = slides[other - 1].source;
        if (a.kind != ir::Origin::Kind::BLOCK || b.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(a.kind != ir::Origin::Kind::BLOCK ? a.reason : b.reason));
        }
        return swap(a.range, b.range);
    }

    EditResult Editor::move_slide_to(const std::vector<ir::Slide>& slides, std::size_t page, std::size_t to) const {
        if (page == 0 || page > slides.size() || to == 0 || to > slides.size()) {
            return failure("No slide " + std::to_string(page == 0 || page > slides.size() ? page : to));
        }
        if (page == to) {
            return {{}, ""};
        }
        const ir::Origin& a = slides[page - 1].source;
        const ir::Origin& b = slides[to - 1].source;
        if (a.kind != ir::Origin::Kind::BLOCK || b.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(a.kind != ir::Origin::Kind::BLOCK ? a.reason : b.reason));
        }
        // 앞으로 옮기면 to번째 slide 앞에, 뒤로 옮기면 그 뒤에 둔다
        return move_lines(a.range, b.range, to > page);
    }

    EditResult Editor::copy(const ir::Element& element, const ir::Slide& slide, double dx, double dy) const {
        const ir::Origin& source = element.source;
        if (source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(source.reason));
        }
        const std::string* text = text_of(source.range.path);
        if (text == nullptr) {
            return failure("Cannot find the source file");
        }
        std::string statement = text->substr(source.range.begin, source.range.end - source.range.begin);
        // 문장 안에 적힌 x, y를 뒤에서부터 바꿔야 앞의 위치가 밀리지 않는다
        std::vector<std::pair<const ir::Property*, double>> moves;
        const auto& properties = element.from_template ? element.instance : element.properties;
        for (const auto& [name, delta, reference] : {std::tuple{"x", dx, size_.width}, std::tuple{"y", dy, size_.height}}) {
            const auto* property = find(properties, name);
            if (delta != 0 && property != nullptr && property->origin.kind == ir::Origin::Kind::LITERAL && property->origin.range.path == source.range.path &&
                property->origin.range.begin >= source.range.begin && property->origin.range.end <= source.range.end) {
                if (const auto* number = std::get_if<ir::Number>(&property->value)) {
                    moves.emplace_back(property, to_px(*number, reference) + delta);
                }
            }
        }
        std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) { return a.first->origin.range.begin > b.first->origin.range.begin; });
        for (const auto& [property, px] : moves) {
            std::string error;
            const std::string value = code(property->origin, property->name, {NewValue::Kind::LENGTH, px}, error);
            if (!error.empty()) {
                return failure(error);
            }
            statement.replace(property->origin.range.begin - source.range.begin, property->origin.range.end - property->origin.range.begin, value);
        }
        return insert_statement(slide.source, statement);
    }

    EditResult Editor::paste(const ir::Slide& slide, const std::string& statement) const {
        if (slide.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(slide.source.reason));
        }
        return insert_statement(slide.source, statement);
    }

    EditResult Editor::insert(const ir::Slide& slide, const std::string& object, const std::vector<std::pair<std::string, NewValue>>& properties) const {
        if (slide.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(slide.source.reason));
        }
        if (!is_name(object)) {
            return failure("'" + object + "' is not a name");
        }
        std::string statement = "put " + object + " {";
        ir::Origin fresh;
        fresh.kind = ir::Origin::Kind::BLOCK;
        for (const auto& [name, value] : properties) {
            std::string error;
            const std::string text = code(fresh, name, value, error);
            if (!error.empty()) {
                return failure(error);
            }
            statement += " " + name + " = " + text + ";";
        }
        return insert_statement(slide.source, statement + " }");
    }

    EditResult Editor::remove(const ir::Element& element) const {
        return remove_statement(element.source);
    }

    EditResult Editor::rename(const ir::Element& element, const std::string& name) const {
        const ir::Origin& source = element.source;
        if (source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(source.reason));
        }
        if (!is_name(name)) {
            return failure("'" + name + "' is not a name");
        }
        const std::string* text = text_of(source.range.path);
        if (text == nullptr) {
            return failure("Cannot find the source file");
        }
        // put 이름 [as 별칭] { 또는 group [as 별칭] {
        std::size_t at = source.range.begin;
        const auto skip_space = [&] {
            while (at < source.range.end && is_space((*text)[at])) {
                ++at;
            }
        };
        const auto word = [&] {
            const std::size_t begin = at;
            while (at < source.range.end && (std::isalnum(static_cast<unsigned char>((*text)[at])) || (*text)[at] == '_')) {
                ++at;
            }
            return text->substr(begin, at - begin);
        };
        const std::string keyword = word();
        if (keyword == "put") {
            skip_space();
            word();
        } else if (keyword != "group") {
            return failure("Cannot find the put statement");
        }
        const std::size_t after = at;
        skip_space();
        if (word() == "as") {
            skip_space();
            const std::size_t begin = at;
            word();
            return {{{source.range.path, begin, at, name}}, ""};
        }
        return {{{source.range.path, after, after, " as " + name}}, ""};
    }

    EditResult Editor::unset(const ir::Origin& origin) const {
        if (origin.kind == ir::Origin::Kind::LOCKED) {
            return failure(lock_message(origin.reason));
        }
        if (origin.kind != ir::Origin::Kind::LITERAL || origin.statement.path.empty()) {
            return {}; // 적지 않은 값은 이미 기본값이다
        }
        ir::Origin statement;
        statement.kind = ir::Origin::Kind::BLOCK;
        statement.range = origin.statement;
        return remove_statement(statement);
    }

    EditResult Editor::unset_property(const ir::Element& element, const std::string& property) const {
        const auto* found = find(element.properties, property);
        if (found == nullptr) {
            return {};
        }
        return unset(found->origin);
    }

    EditResult Editor::set_named(const std::map<std::string, ir::Origin>& origins, const ir::Origin& block, const std::string& name, const NewValue& value) const {
        if (const auto it = origins.find(name); it != origins.end()) {
            return set(it->second, name, value);
        }
        if (block.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(block.reason));
        }
        ir::Origin origin = block;
        origin.name = name;
        return set(origin, name, value);
    }

    EditResult Editor::unset_named(const std::map<std::string, ir::Origin>& origins, const std::string& name) const {
        const auto it = origins.find(name);
        return it == origins.end() ? EditResult{} : unset(it->second);
    }

    EditResult Editor::replace_statement(const ir::Origin& statement, const std::string& text) const {
        if (statement.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(statement.reason));
        }
        return {{{statement.range.path, statement.range.begin, statement.range.end, text}}, ""};
    }

    EditResult Editor::insert_into(const ir::Origin& block, const std::string& statement) const {
        if (block.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(block.reason));
        }
        return insert_statement(block, statement);
    }

    EditResult Editor::remove_statement(const ir::Origin& origin) const {
        if (origin.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(origin.reason));
        }
        const std::string* text = text_of(origin.range.path);
        if (text == nullptr) {
            return failure("Cannot find the source file");
        }
        std::size_t begin = origin.range.begin;
        std::size_t end = origin.range.end;
        // 문장만 있는 줄이면 줄 전체를 지운다
        std::size_t line_begin = begin;
        while (line_begin > 0 && ((*text)[line_begin - 1] == ' ' || (*text)[line_begin - 1] == '\t')) {
            --line_begin;
        }
        std::size_t line_end = end;
        while (line_end < text->size() && ((*text)[line_end] == ' ' || (*text)[line_end] == '\t' || (*text)[line_end] == '\r')) {
            ++line_end;
        }
        if ((line_begin == 0 || (*text)[line_begin - 1] == '\n') && (line_end == text->size() || (*text)[line_end] == '\n')) {
            begin = line_begin;
            end = line_end < text->size() ? line_end + 1 : line_end;
        } else if (begin > 0 && (*text)[begin - 1] == ' ') {
            --begin; // 한 줄에 여러 문장이 있으면 앞의 공백 하나도 지운다
        }
        return {{{origin.range.path, begin, end, ""}}, ""};
    }

    // 블록의 닫는 '}' 앞에 문장을 넣는다. 여러 줄 블록이면 앞 문장의 들여쓰기로 새 줄에, 한 줄 블록이면 같은 줄에 넣는다
    EditResult Editor::insert_statement(const ir::Origin& block, const std::string& statement) const {
        const std::string* text = text_of(block.range.path);
        if (text == nullptr || block.range.end == 0 || block.range.end > text->size() || (*text)[block.range.end - 1] != '}') {
            return failure("Cannot find the block to change");
        }
        const std::size_t close = block.range.end - 1;
        std::size_t last = close;
        while (last > block.range.begin && is_space((*text)[last - 1])) {
            --last;
        }
        // last는 '}' 앞의 마지막 글자 바로 뒤
        if (text->find('\n', last) < close) {
            std::size_t close_line = close;
            while (close_line > 0 && (*text)[close_line - 1] != '\n') {
                --close_line;
            }
            std::size_t last_line = last - 1;
            while (last_line > 0 && (*text)[last_line - 1] != '\n') {
                --last_line;
            }
            std::string indent;
            if (last_line > block.range.begin) {
                for (std::size_t i = last_line; i < text->size() && ((*text)[i] == ' ' || (*text)[i] == '\t'); ++i) {
                    indent += (*text)[i];
                }
            } else {
                indent = text->substr(close_line, close - close_line) + indent_unit;
            }
            return {{{block.range.path, close_line, close_line, indent + statement + "\n"}}, ""};
        }
        return {{{block.range.path, last, last, " " + statement + (last == close ? " " : "")}}, ""};
    }
}

#include "xml.h"

#include <cstdint>
#include <map>

namespace templide::importer::xml {
    namespace {
        // namespace 주소 -> 이 파서가 쓰는 접두사
        const std::map<std::string, std::string, std::less<>>& canonical_prefixes() {
            static const std::map<std::string, std::string, std::less<>> table = {
                {"http://schemas.openxmlformats.org/presentationml/2006/main", "p"},
                {"http://purl.oclc.org/ooxml/presentationml/main", "p"},
                {"http://schemas.openxmlformats.org/drawingml/2006/main", "a"},
                {"http://purl.oclc.org/ooxml/drawingml/main", "a"},
                {"http://schemas.openxmlformats.org/officeDocument/2006/relationships", "r"},
                {"http://purl.oclc.org/ooxml/officeDocument/relationships", "r"},
                {"http://schemas.openxmlformats.org/markup-compatibility/2006", "mc"},
                {"http://schemas.microsoft.com/office/powerpoint/2010/main", "p14"},
                {"http://schemas.microsoft.com/office/powerpoint/2012/main", "p15"},
                {"http://schemas.microsoft.com/office/powerpoint/2015/09/main", "p159"},
                {"http://schemas.microsoft.com/office/powerpoint/2018/8/main", "p188"},
                {"http://schemas.microsoft.com/office/powerpoint/2013/main/command", "pc"},
                {"http://schemas.microsoft.com/office/drawing/2010/main", "a14"},
                {"http://schemas.microsoft.com/office/drawing/2014/main", "a16"},
                {"http://schemas.microsoft.com/office/drawing/2016/SVG/main", "asvg"},
                {"http://schemas.microsoft.com/office/drawing/2017/model3d", "am3d"},
                {"http://schemas.openxmlformats.org/drawingml/2006/chart", "c"},
                {"http://schemas.openxmlformats.org/drawingml/2006/diagram", "dgm"},
                {"http://schemas.openxmlformats.org/drawingml/2006/picture", "pic"},
                {"http://schemas.openxmlformats.org/officeDocument/2006/math", "m"},
                {"http://schemas.openxmlformats.org/package/2006/relationships", ""},
                {"http://schemas.openxmlformats.org/package/2006/content-types", ""},
            };
            return table;
        }

        struct Parser {
            const std::string& text;
            std::size_t at = 0;
            std::string error;
            // 지금 보이는 namespace 선언. (접두사, 주소). 뒤의 것이 앞의 것을 가린다
            std::vector<std::pair<std::string, std::string>> scope;

            bool done() const { return at >= text.size(); }

            bool starts(std::string_view prefix) const {
                return text.compare(at, prefix.size(), prefix) == 0;
            }

            void skip_space() {
                while (!done() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) {
                    ++at;
                }
            }

            // <? ?>, <!-- -->, <!DOCTYPE >를 건너뛴다. 건너뛴 것이 있으면 true
            bool skip_misc() {
                if (starts("<?")) {
                    const auto end = text.find("?>", at);
                    at = end == std::string::npos ? text.size() : end + 2;
                    return true;
                }
                if (starts("<!--")) {
                    const auto end = text.find("-->", at);
                    at = end == std::string::npos ? text.size() : end + 3;
                    return true;
                }
                if (starts("<!DOCTYPE")) {
                    const auto end = text.find('>', at);
                    at = end == std::string::npos ? text.size() : end + 1;
                    return true;
                }
                return false;
            }

            static bool name_char(char c) {
                return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '=' && c != '>' && c != '/' && c != '<' && c != '"' && c != '\'';
            }

            std::string read_name() {
                const std::size_t start = at;
                while (!done() && name_char(text[at])) {
                    ++at;
                }
                return text.substr(start, at - start);
            }

            static void append_utf8(std::string& out, std::uint32_t code) {
                if (code < 0x80) {
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (code >> 18));
                    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
            }

            // &amp; 같은 참조를 푼다
            static std::string decode(std::string_view raw) {
                std::string out;
                out.reserve(raw.size());
                for (std::size_t i = 0; i < raw.size(); ++i) {
                    if (raw[i] != '&') {
                        out += raw[i];
                        continue;
                    }
                    const auto end = raw.find(';', i);
                    if (end == std::string_view::npos) {
                        out += raw[i];
                        continue;
                    }
                    const std::string_view entity = raw.substr(i + 1, end - i - 1);
                    if (entity == "lt") {
                        out += '<';
                    } else if (entity == "gt") {
                        out += '>';
                    } else if (entity == "amp") {
                        out += '&';
                    } else if (entity == "quot") {
                        out += '"';
                    } else if (entity == "apos") {
                        out += '\'';
                    } else if (!entity.empty() && entity[0] == '#') {
                        try {
                            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                            const std::uint32_t code = static_cast<std::uint32_t>(std::stoul(std::string(entity.substr(hex ? 2 : 1)), nullptr, hex ? 16 : 10));
                            append_utf8(out, code);
                        } catch (...) {
                            out += std::string(raw.substr(i, end - i + 1));
                        }
                    } else {
                        out += std::string(raw.substr(i, end - i + 1));
                    }
                    i = end;
                }
                return out;
            }

            const std::string* lookup(std::string_view prefix) const {
                for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
                    if (it->first == prefix) {
                        return &it->second;
                    }
                }
                return nullptr;
            }

            // 원문의 이름(접두사:이름)을 표준 접두사의 이름으로
            std::string canonical(const std::string& name, bool attribute) const {
                const auto colon = name.find(':');
                const std::string prefix = colon == std::string::npos ? "" : name.substr(0, colon);
                const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
                if (attribute && prefix.empty()) {
                    return name; // 접두사 없는 속성은 namespace가 없다
                }
                const std::string* uri = lookup(prefix);
                if (uri == nullptr) {
                    return name;
                }
                const auto it = canonical_prefixes().find(*uri);
                if (it == canonical_prefixes().end()) {
                    return name;
                }
                return it->second.empty() ? local : it->second + ":" + local;
            }

            // mc:Choice의 Requires처럼 접두사를 나열한 값
            std::string canonical_prefix_list(const std::string& value) const {
                std::string result;
                std::size_t start = 0;
                while (start < value.size()) {
                    const auto space = value.find(' ', start);
                    const std::string prefix = value.substr(start, space == std::string::npos ? std::string::npos : space - start);
                    start = space == std::string::npos ? value.size() : space + 1;
                    if (prefix.empty()) {
                        continue;
                    }
                    std::string mapped = prefix;
                    if (const std::string* uri = lookup(prefix)) {
                        if (const auto it = canonical_prefixes().find(*uri); it != canonical_prefixes().end() && !it->second.empty()) {
                            mapped = it->second;
                        }
                    }
                    result += (result.empty() ? "" : " ") + mapped;
                }
                return result;
            }

            bool fail(const std::string& message) {
                if (error.empty()) {
                    error = message + " at byte " + std::to_string(at);
                }
                return false;
            }

            // at은 '<' 위치
            bool element(Node& node) {
                ++at;
                const std::string raw_name = read_name();
                if (raw_name.empty()) {
                    return fail("expected an element name");
                }
                std::vector<std::pair<std::string, std::string>> raw_attributes;
                bool empty = false;
                while (true) {
                    skip_space();
                    if (done()) {
                        return fail("unterminated tag <" + raw_name);
                    }
                    if (starts("/>")) {
                        at += 2;
                        empty = true;
                        break;
                    }
                    if (text[at] == '>') {
                        ++at;
                        break;
                    }
                    const std::string attribute_name = read_name();
                    if (attribute_name.empty()) {
                        return fail("bad attribute in <" + raw_name);
                    }
                    skip_space();
                    if (done() || text[at] != '=') {
                        return fail("expected '=' after " + attribute_name);
                    }
                    ++at;
                    skip_space();
                    if (done() || (text[at] != '"' && text[at] != '\'')) {
                        return fail("expected a quoted value for " + attribute_name);
                    }
                    const char quote = text[at++];
                    const auto end = text.find(quote, at);
                    if (end == std::string::npos) {
                        return fail("unterminated value for " + attribute_name);
                    }
                    raw_attributes.emplace_back(attribute_name, decode(std::string_view(text).substr(at, end - at)));
                    at = end + 1;
                }
                // 이 요소의 namespace 선언을 먼저 넣어야 이름을 풀 수 있다
                const std::size_t scope_size = scope.size();
                for (const auto& [key, value] : raw_attributes) {
                    if (key == "xmlns") {
                        scope.emplace_back("", value);
                    } else if (key.starts_with("xmlns:")) {
                        scope.emplace_back(key.substr(6), value);
                    }
                }
                node.name = canonical(raw_name, false);
                for (const auto& [key, value] : raw_attributes) {
                    if (key == "xmlns" || key.starts_with("xmlns:")) {
                        continue;
                    }
                    const std::string name = canonical(key, true);
                    node.attributes.emplace_back(name, name == "Requires" || name == "mc:Ignorable" ? canonical_prefix_list(value) : value);
                }
                if (!empty) {
                    while (true) {
                        if (done()) {
                            scope.resize(scope_size);
                            return fail("unterminated element <" + raw_name + ">");
                        }
                        if (starts("</")) {
                            const auto end = text.find('>', at);
                            if (end == std::string::npos) {
                                return fail("unterminated closing tag");
                            }
                            at = end + 1;
                            break;
                        }
                        if (starts("<![CDATA[")) {
                            const auto end = text.find("]]>", at);
                            if (end == std::string::npos) {
                                return fail("unterminated CDATA");
                            }
                            node.text += text.substr(at + 9, end - at - 9);
                            at = end + 3;
                            continue;
                        }
                        if (skip_misc()) {
                            continue;
                        }
                        if (text[at] == '<') {
                            Node child;
                            if (!element(child)) {
                                return false;
                            }
                            node.children.push_back(std::move(child));
                            continue;
                        }
                        const auto next = text.find('<', at);
                        const std::size_t end = next == std::string::npos ? text.size() : next;
                        node.text += decode(std::string_view(text).substr(at, end - at));
                        at = end;
                    }
                }
                scope.resize(scope_size);
                return true;
            }
        };
    }

    const Node* Node::child(std::string_view wanted) const {
        for (const auto& each : children) {
            if (each.name == wanted) {
                return &each;
            }
        }
        return nullptr;
    }

    std::vector<const Node*> Node::all(std::string_view wanted) const {
        std::vector<const Node*> result;
        for (const auto& each : children) {
            if (each.name == wanted) {
                result.push_back(&each);
            }
        }
        return result;
    }

    const Node* Node::path(std::initializer_list<std::string_view> names) const {
        const Node* current = this;
        for (const auto name : names) {
            current = current->child(name);
            if (current == nullptr) {
                return nullptr;
            }
        }
        return current;
    }

    const Node* Node::find(std::string_view wanted) const {
        for (const auto& each : children) {
            if (each.name == wanted) {
                return &each;
            }
            if (const Node* found = each.find(wanted)) {
                return found;
            }
        }
        return nullptr;
    }

    const std::string* Node::attribute(std::string_view wanted) const {
        for (const auto& [key, value] : attributes) {
            if (key == wanted) {
                return &value;
            }
        }
        return nullptr;
    }

    std::string Node::get(std::string_view wanted, const std::string& fallback) const {
        const std::string* value = attribute(wanted);
        return value != nullptr ? *value : fallback;
    }

    std::optional<long long> Node::integer(std::string_view wanted) const {
        const std::string* value = attribute(wanted);
        if (value == nullptr || value->empty()) {
            return std::nullopt;
        }
        try {
            std::size_t used = 0;
            const long long number = std::stoll(*value, &used);
            return number;
        } catch (...) {
            return std::nullopt;
        }
    }

    bool Node::flag(std::string_view wanted, bool fallback) const {
        const std::string* value = attribute(wanted);
        if (value == nullptr) {
            return fallback;
        }
        return *value == "1" || *value == "true" || *value == "on";
    }

    std::optional<Node> parse(const std::string& text, std::string& error) {
        Parser parser{text, 0, {}, {}};
        // UTF-8 BOM
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
            parser.at = 3;
        }
        while (true) {
            parser.skip_space();
            if (parser.done()) {
                error = "no root element";
                return std::nullopt;
            }
            if (!parser.skip_misc()) {
                break;
            }
        }
        if (parser.text[parser.at] != '<') {
            error = "expected '<'";
            return std::nullopt;
        }
        Node root;
        if (!parser.element(root)) {
            error = parser.error;
            return std::nullopt;
        }
        return root;
    }
}

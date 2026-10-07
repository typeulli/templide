#include "server.h"
#include "completion.h"
#include "edits.h"
#include "navigation.h"
#include "../backend/backend.h"
#include "../backend/html.h"
#include "../middleend/analyzer.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace templide::server {
    namespace {
        using json = nlohmann::json;

        constexpr double default_slide_width = 1280; // backend들의 기본 크기 (16:9)
        constexpr double default_slide_height = 720;

        // ---- 경로와 URI

        std::string percent_decode(const std::string& text) {
            std::string result;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
                    result += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    result += text[i];
                }
            }
            return result;
        }

        std::filesystem::path uri_to_path(const std::string& uri) {
            if (!uri.starts_with("file://")) {
                throw std::runtime_error("Only file URIs are supported: " + uri);
            }
            std::string path = percent_decode(uri.substr(7));
            // file:///C:/a는 /C:/a가 된다
            if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
                path = path.substr(1);
            }
            return backend::utf8_path(path).lexically_normal();
        }

        std::string path_to_uri(const std::filesystem::path& path) {
            const std::u8string generic = path.generic_u8string();
            const std::string text(generic.begin(), generic.end());
            std::string result = "file://";
            if (!text.starts_with("/")) {
                result += "/";
            }
            static const char* digits = "0123456789ABCDEF";
            for (const char c : text) {
                const auto byte = static_cast<unsigned char>(c);
                if (std::isalnum(byte) || c == '/' || c == '-' || c == '.' || c == '_' || c == '~' || c == ':') {
                    result += c;
                } else {
                    result += '%';
                    result += digits[byte >> 4];
                    result += digits[byte & 15];
                }
            }
            return result;
        }

        // 같은 파일을 가리키는 경로를 하나로 맞춘 것. Windows는 대소문자를 가리지 않는다
        std::string file_key(const std::filesystem::path& path) {
            std::error_code error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
            const std::u8string generic = (error ? path : canonical).generic_u8string();
            std::string key(generic.begin(), generic.end());
#ifdef _WIN32
            for (auto& c : key) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
#endif
            return key;
        }

        // ---- 위치. LSP는 0부터 세는 줄과 UTF-16 단위의 칸을 쓴다

        struct SourceText {
            std::string text;
            std::vector<std::size_t> line_starts;
        };

        SourceText index_source(std::string text) {
            SourceText source{std::move(text), {0}};
            for (std::size_t i = 0; i < source.text.size(); ++i) {
                if (source.text[i] == '\n') {
                    source.line_starts.push_back(i + 1);
                }
            }
            return source;
        }

        json position(const SourceText& source, std::size_t offset) {
            offset = std::min(offset, source.text.size());
            const auto line = static_cast<std::size_t>(std::upper_bound(source.line_starts.begin(), source.line_starts.end(), offset) - source.line_starts.begin()) - 1;
            std::size_t character = 0;
            for (std::size_t i = source.line_starts[line]; i < offset; ++i) {
                const auto byte = static_cast<unsigned char>(source.text[i]);
                if (byte >= 0xF0) {
                    character += 2; // UTF-16 대리 쌍
                } else if (byte < 0x80 || byte >= 0xC0) {
                    character += 1; // 이어지는 바이트(0x80 ~ 0xBF)는 세지 않는다
                }
            }
            return {{"line", line}, {"character", character}};
        }

        // 마지막으로 컴파일한 결과
        struct Compiled {
            middleend::Result result;
            std::map<std::string, SourceText> sources; // 분석기가 읽은 경로 -> 내용
            Sources texts;                            // Editor가 쓰는 같은 내용
            mutable std::optional<NavIndex> navigation; // 이름 찾기. 처음 물을 때 만든다
        };

        std::string kind_name(ir::Origin::Kind kind) {
            switch (kind) {
                case ir::Origin::Kind::LITERAL: return "literal";
                case ir::Origin::Kind::BLOCK: return "block";
                case ir::Origin::Kind::LOCKED: return "locked";
            }
            return "locked";
        }

        double to_px(const std::optional<ir::Number>& number, double fallback) {
            if (!number) {
                return fallback;
            }
            double total = 0;
            for (const auto& [unit, value] : number->terms) {
                total += value;
            }
            return total;
        }

        const ir::Element* find_element(const std::vector<ir::Element>& elements, const std::string& id) {
            for (const auto& element : elements) {
                if (element.id == id) {
                    return &element;
                }
                if (const auto* found = find_element(element.children, id)) {
                    return found;
                }
            }
            return nullptr;
        }

        // id인 element가 들어 있는 목록 (slide나 group의 children)
        const std::vector<ir::Element>* find_siblings(const std::vector<ir::Element>& elements, const std::string& id) {
            for (const auto& element : elements) {
                if (element.id == id) {
                    return &elements;
                }
                if (const auto* found = find_siblings(element.children, id)) {
                    return found;
                }
            }
            return nullptr;
        }

        NewValue new_value(const json& op) {
            if (op.contains("length")) {
                return {NewValue::Kind::LENGTH, op.at("length").get<double>()};
            }
            if (op.contains("number")) {
                NewValue value{NewValue::Kind::NUMBER, op.at("number").get<double>()};
                value.unit = op.value("unit", "");
                value.unset = op.value("unset", false);
                return value;
            }
            if (op.contains("string")) {
                return {NewValue::Kind::STRING, 0, op.at("string").get<std::string>()};
            }
            if (op.contains("color")) {
                return {NewValue::Kind::COLOR, 0, op.at("color").get<std::string>()};
            }
            if (op.contains("enum")) {
                NewValue value{NewValue::Kind::NAME, 0, op.at("enum").get<std::string>()};
                value.unset = op.value("unset", false);
                return value;
            }
            if (op.contains("bool")) {
                return {NewValue::Kind::BOOL, 0, "", op.at("bool").get<bool>()};
            }
            if (op.contains("code")) {
                return {NewValue::Kind::CODE, 0, op.at("code").get<std::string>()};
            }
            throw std::runtime_error("A value needs length, number, string, color, enum, bool or code");
        }

        // ---- 애니메이션 문장

        // ms를 s 단위 리터럴로 (0.5s)
        std::string seconds_code(double milliseconds) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.10g", std::round(milliseconds) / 1000);
            return std::string(buffer) + "s";
        }

        double milliseconds_of(const ir::Number& number) {
            double total = 0;
            for (const auto& [unit, value] : number.terms) {
                total += unit == "s" ? value * 1000 : value;
            }
            return total;
        }

        // animate 문장 하나의 내용. 시간은 ms
        struct AnimationSpec {
            std::string category;
            std::string effect;
            std::string option;
            std::string path;
            std::string start = "on_click";
            std::optional<double> duration;
            std::optional<double> delay;
            std::optional<int> order;
        };

        AnimationSpec spec_of(const ir::Animation& animation) {
            AnimationSpec spec{animation.category, animation.effect, animation.option, animation.path, animation.start, std::nullopt, std::nullopt, animation.order};
            if (animation.duration) {
                spec.duration = milliseconds_of(*animation.duration);
            }
            if (animation.delay) {
                spec.delay = milliseconds_of(*animation.delay);
            }
            return spec;
        }

        // 편집기가 보낸 바꿀 값. null인 duration, delay는 지운다
        void apply_changes(AnimationSpec& spec, const json& changes) {
            for (const char* name : {"category", "effect", "option", "path", "start"}) {
                if (changes.contains(name) && changes.at(name).is_string()) {
                    std::string& field = std::string(name) == "category" ? spec.category : std::string(name) == "effect" ? spec.effect
                        : std::string(name) == "option" ? spec.option : std::string(name) == "path" ? spec.path : spec.start;
                    field = changes.at(name).get<std::string>();
                }
            }
            if (changes.contains("effect") && !changes.contains("option")) {
                spec.option.clear(); // 효과를 바꾸면 그 효과의 기본 옵션
            }
            for (const auto& [name, field] : {std::pair{"duration", &spec.duration}, std::pair{"delay", &spec.delay}}) {
                if (changes.contains(name)) {
                    *field = changes.at(name).is_number() ? std::optional(changes.at(name).get<double>()) : std::nullopt;
                }
            }
        }

        // animate [대상] 종류 효과[.옵션] [path "..."] [길이] [시작] [delay 지연] [order 차례];
        std::string animation_statement(const AnimationSpec& spec, const std::string& target, const middleend::Symbols* symbols) {
            std::string text = "animate " + (target.empty() ? "" : target + " ") + spec.category + " " + spec.effect;
            if (spec.effect == "path" && spec.category == "move") {
                text += " " + quote_string(spec.path);
            } else if (!spec.option.empty()) {
                // 기본 옵션은 적지 않는다
                const auto it = symbols != nullptr ? symbols->animations.find(spec.category + "." + spec.effect) : decltype(symbols->animations)::const_iterator{};
                const bool is_default = symbols != nullptr && it != symbols->animations.end() && !it->second.empty() && it->second.front() == spec.option;
                if (!is_default) {
                    text += "." + spec.option;
                }
            }
            if (spec.duration) {
                text += " " + seconds_code(*spec.duration);
            }
            if (spec.start != "on_click") {
                text += " " + spec.start;
            }
            if (spec.delay && *spec.delay > 0) {
                text += " delay " + seconds_code(*spec.delay);
            }
            if (spec.order) {
                text += " order " + std::to_string(*spec.order);
            }
            return text + ";";
        }

        // 따옴표 밖의 낱말 word가 text의 어디에 있는지
        std::size_t find_word(const std::string& text, const std::string& word) {
            bool quoted = false;
            for (std::size_t i = 0; i + word.size() <= text.size(); ++i) {
                const char c = text[i];
                if (quoted) {
                    if (c == '\\') {
                        ++i;
                    } else if (c == '"') {
                        quoted = false;
                    }
                    continue;
                }
                if (c == '"') {
                    quoted = true;
                    continue;
                }
                const bool before = i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_');
                const std::size_t end = i + word.size();
                const bool after = end == text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
                if (before && after && text.compare(i, word.size(), word) == 0) {
                    return i;
                }
            }
            return std::string::npos;
        }

        // 편집기(JavaScript)의 UTF-16 위치를 UTF-8 바이트 위치로
        std::size_t utf16_to_byte(const std::string& text, std::size_t units) {
            std::size_t i = 0;
            std::size_t count = 0;
            while (i < text.size() && count < units) {
                const auto byte = static_cast<unsigned char>(text[i]);
                const std::size_t length = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
                count += length == 4 ? 2 : 1;
                i += length;
            }
            return std::min(i, text.size());
        }

        std::string hex_color(const ir::Color& color) {
            char buffer[16];
            const int alpha = static_cast<int>(std::lround(color.a * 255));
            if (alpha >= 255) {
                std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X", color.r, color.g, color.b);
            } else {
                std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X%02X", color.r, color.g, color.b, alpha);
            }
            return buffer;
        }

        json link_json(const ir::Link& link) {
            json result = json::object();
            if (!link.url.empty()) {
                result["url"] = link.url;
            } else if (link.slide > 0) {
                result["slide"] = link.slide;
            } else {
                result["jump"] = link.jump;
            }
            return result;
        }

        // 속성 패널에 보여 줄 지금 값. 길이는 px로 풀고(%는 슬라이드 크기로), 시간은 ms, 그 밖의 수는 단위를 뗀 값이다.
        // 그라데이션, 무늬, 그림, 링크, 실행 설정은 {gradient}, {pattern}, {image}, {link}, {action} 객체다
        json value_json(const std::string& name, const ir::Value& value, SlideSize size) {
            if (const auto* number = std::get_if<ir::Number>(&value)) {
                const double reference = name == "x" || name == "width" || name == "x1" || name == "x2" ? size.width : size.height;
                const bool position = name == "x" || name == "y" || name == "width" || name == "height" || name == "x1" || name == "y1" || name == "x2" || name == "y2";
                double total = 0;
                for (const auto& [unit, amount] : number->terms) {
                    total += unit == "%" && position ? amount / 100 * reference : unit == "s" ? amount * 1000 : amount;
                }
                return total;
            }
            if (const auto* gradient = std::get_if<ir::Gradient>(&value)) {
                json colors = json::array();
                json positions = json::array();
                for (std::size_t i = 0; i < gradient->colors.size(); ++i) {
                    colors.push_back(hex_color(gradient->colors[i]));
                    const auto& at = i < gradient->positions.size() ? gradient->positions[i] : std::nullopt;
                    positions.push_back(at ? json(value_json("", *at, size)) : json());
                }
                return {{"gradient", {{"radial", gradient->radial}, {"angle", value_json("", gradient->angle, size)}, {"colors", colors}, {"positions", positions}}}};
            }
            if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
                return {{"pattern", {{"kind", pattern->kind}, {"foreground", hex_color(pattern->foreground)}, {"background", hex_color(pattern->background)}}}};
            }
            if (const auto* image = std::get_if<ir::Image>(&value)) {
                return {{"image", image->path}};
            }
            if (const auto* link = std::get_if<ir::Link>(&value)) {
                return {{"link", link_json(*link)}};
            }
            if (const auto* action = std::get_if<ir::Action>(&value)) {
                json arguments = json::array();
                for (const auto& argument : action->arguments) {
                    std::visit([&](const auto& each) { arguments.push_back(each); }, argument);
                }
                json result = {{"kind", action->kind}, {"target", action->target}, {"arguments", arguments}};
                if (action->kind == "link") {
                    result["link"] = link_json(action->link);
                }
                return {{"action", result}};
            }
            if (const auto* color = std::get_if<ir::Color>(&value)) {
                return hex_color(*color);
            }
            if (const auto* member = std::get_if<ir::EnumValue>(&value)) {
                return member->member;
            }
            if (const auto* flag = std::get_if<bool>(&value)) {
                return *flag;
            }
            if (const auto* string = std::get_if<std::string>(&value)) {
                return *string;
            }
            return nullptr;
        }

        class Server {
        public:
            Server(std::filesystem::path packages_dir, std::filesystem::path libs_dir) : packages_dir_(std::move(packages_dir)), libs_dir_(std::move(libs_dir)) {}

            int run() {
#ifdef _WIN32
                _setmode(_fileno(stdin), _O_BINARY);
                _setmode(_fileno(stdout), _O_BINARY);
#endif
                while (!exit_) {
                    const auto body = read_message();
                    if (!body) {
                        break;
                    }
                    json message;
                    try {
                        message = json::parse(*body);
                    } catch (const json::exception&) {
                        send({{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "Parse error"}}}});
                        continue;
                    }
                    handle(message);
                }
                // LSP: shutdown을 받은 뒤의 exit만 정상 종료다
                return shutdown_ ? 0 : 1;
            }

        private:
            struct Document {
                std::string uri;
                std::filesystem::path path;
                std::string text;
            };

            std::filesystem::path packages_dir_;
            std::filesystem::path libs_dir_;
            std::map<std::string, middleend::Symbols> symbols_; // file_key -> 마지막으로 분석에 성공한 이름들 (쓰는 중에는 구문 오류가 잦다)
            std::map<std::string, Document> documents_; // file_key -> 열린 문서
            std::map<std::string, Compiled> compiled_;  // file_key -> 그 문서를 main으로 컴파일한 결과
            std::map<std::string, std::set<std::string>> published_; // file_key -> 오류를 보낸 URI들
            mutable std::map<std::string, std::string> uris_;        // 분석기가 쓴 경로 -> URI
            bool shutdown_ = false;
            bool exit_ = false;

            // ---- 메시지

            static std::optional<std::string> read_message() {
                std::size_t length = 0;
                bool has_length = false;
                while (true) {
                    std::string line;
                    int c;
                    while ((c = std::getchar()) != EOF && c != '\n') {
                        line += static_cast<char>(c);
                    }
                    if (c == EOF) {
                        return std::nullopt;
                    }
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty()) {
                        if (has_length) {
                            break;
                        }
                        continue;
                    }
                    if (line.starts_with("Content-Length:")) {
                        length = std::stoul(line.substr(15));
                        has_length = true;
                    }
                }
                std::string body(length, '\0');
                if (std::fread(body.data(), 1, length, stdin) != length) {
                    return std::nullopt;
                }
                return body;
            }

            static void send_text(const std::string& body) {
                std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
                std::fwrite(body.data(), 1, body.size(), stdout);
                std::fflush(stdout);
            }

            static void send(const json& message) {
                send_text(message.dump(-1, ' ', false, json::error_handler_t::replace));
            }

            static void reply(const json& id, const json& result) {
                send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
            }

            static void reply_error(const json& id, int code, const std::string& message) {
                send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
            }

            void handle(const json& message) {
                const std::string method = message.value("method", "");
                const bool request = message.contains("id");
                const json id = request ? message.at("id") : json();
                const json params = message.contains("params") ? message.at("params") : json::object();
                try {
                    if (method == "initialize") {
                        reply(id, {{"capabilities", {
                                       {"textDocumentSync", {{"openClose", true}, {"change", 1}}},
                                       {"completionProvider", {{"triggerCharacters", {".", "<", " ", "=", "("}}}},
                                       {"hoverProvider", true},
                                       {"definitionProvider", true},
                                       {"referencesProvider", true},
                                       {"documentHighlightProvider", true},
                                       {"renameProvider", {{"prepareProvider", true}}},
                                       {"documentSymbolProvider", true},
                                   }},
                                   {"serverInfo", {{"name", "templide"}}}});
                    } else if (method == "shutdown") {
                        shutdown_ = true;
                        reply(id, nullptr);
                    } else if (method == "exit") {
                        exit_ = true;
                    } else if (method == "textDocument/didOpen") {
                        const json& document = params.at("textDocument");
                        const std::string uri = document.at("uri").get<std::string>();
                        const auto path = uri_to_path(uri);
                        documents_[file_key(path)] = {uri, path, document.at("text").get<std::string>()};
                        compile_all();
                    } else if (method == "textDocument/didChange") {
                        const auto path = uri_to_path(params.at("textDocument").at("uri").get<std::string>());
                        const auto it = documents_.find(file_key(path));
                        if (it != documents_.end() && !params.at("contentChanges").empty()) {
                            // 문서 전체를 받는다 (textDocumentSync.change = 1)
                            it->second.text = params.at("contentChanges").back().at("text").get<std::string>();
                            compile_all();
                        }
                    } else if (method == "textDocument/didClose") {
                        const std::string key = file_key(uri_to_path(params.at("textDocument").at("uri").get<std::string>()));
                        documents_.erase(key);
                        compiled_.erase(key);
                        for (const auto& uri : published_[key]) {
                            send({{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", {{"uri", uri}, {"diagnostics", json::array()}}}});
                        }
                        published_.erase(key);
                        compile_all();
                    } else if (method == "textDocument/completion") {
                        reply(id, completion(params));
                    } else if (method == "textDocument/hover") {
                        reply(id, hover(params));
                    } else if (method == "textDocument/definition") {
                        reply(id, definition(params));
                    } else if (method == "textDocument/references") {
                        reply(id, references(params));
                    } else if (method == "textDocument/documentHighlight") {
                        reply(id, highlights(params));
                    } else if (method == "textDocument/prepareRename") {
                        reply(id, prepare_rename(params));
                    } else if (method == "textDocument/rename") {
                        reply(id, rename(params));
                    } else if (method == "textDocument/documentSymbol") {
                        reply(id, document_symbols(params));
                    } else if (method == "templide/build") {
                        reply(id, build(params));
                    } else if (method == "templide/targets") {
                        reply(id, targets(params));
                    } else if (method == "templide/deck") {
                        reply_deck(id, params);
                    } else if (method == "templide/edit") {
                        reply(id, edit(params));
                    } else if (method == "templide/schema") {
                        reply(id, schema(params));
                    } else if (request) {
                        reply_error(id, -32601, "Unknown method: " + method);
                    }
                } catch (const std::exception& error) {
                    if (request) {
                        reply_error(id, -32602, error.what());
                    } else {
                        std::cerr << "templide --serve: " << method << ": " << error.what() << '\n';
                    }
                }
            }

            // ---- 컴파일과 오류

            // 열린 문서마다 그 문서를 main으로 컴파일한다. 열린 다른 문서는 저장하지 않은 내용을 쓴다
            void compile_all() {
                uris_.clear();
                middleend::Overlays overlays;
                for (const auto& [key, document] : documents_) {
                    overlays[document.path] = document.text;
                }
                for (const auto& [key, document] : documents_) {
                    Compiled compiled;
                    compiled.result = middleend::analyze(document.path, packages_dir_, overlays);
                    for (auto& [path, text] : compiled.result.sources) {
                        compiled.texts[path] = text;
                        compiled.sources[path] = index_source(std::move(text));
                    }
                    compiled.result.sources.clear();
                    publish(key, compiled);
                    if (compiled.result.symbols.valid) {
                        symbols_[key] = compiled.result.symbols;
                    }
                    compiled_[key] = std::move(compiled);
                }
            }

            // 경로를 맞추는 데 파일 시스템을 거치므로 출처마다 다시 하지 않게 기억해 둔다. 열린 문서가 바뀌면 비운다
            std::string uri_of(const std::string& path) const {
                if (const auto cached = uris_.find(path); cached != uris_.end()) {
                    return cached->second;
                }
                const auto it = documents_.find(file_key(std::filesystem::path(path)));
                const std::string uri = it != documents_.end() ? it->second.uri : path_to_uri(std::filesystem::absolute(std::filesystem::path(path)));
                uris_.emplace(path, uri);
                return uri;
            }

            json range_of(const Compiled& compiled, const ir::SourceRange& range) const {
                const auto it = compiled.sources.find(range.path);
                if (it == compiled.sources.end()) {
                    return {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 0}}}};
                }
                return {{"start", position(it->second, range.begin)}, {"end", position(it->second, range.end)}};
            }

            // 진단의 줄과 칸(1부터, 바이트)을 LSP 범위로. 낱말이면 낱말 끝까지 칠한다
            json diagnostic_range(const Compiled& compiled, const middleend::Diagnostic& diagnostic) const {
                const auto it = compiled.sources.find(diagnostic.path);
                if (diagnostic.line == 0 || it == compiled.sources.end() || diagnostic.line > it->second.line_starts.size()) {
                    return range_of(compiled, {diagnostic.path, 0, 0});
                }
                const SourceText& source = it->second;
                const std::size_t begin = std::min(source.line_starts[diagnostic.line - 1] + diagnostic.column - 1, source.text.size());
                std::size_t end = begin;
                while (end < source.text.size() && (std::isalnum(static_cast<unsigned char>(source.text[end])) || source.text[end] == '_')) {
                    ++end;
                }
                if (end == begin && end < source.text.size() && source.text[end] != '\n') {
                    ++end;
                }
                return range_of(compiled, {diagnostic.path, begin, end});
            }

            // 고른 target이 빼고 만드는 것. 경고의 위치(파일:줄:칸)를 LSP 범위로 바꾼다
            json warnings_json(const Compiled& compiled, const ir::Document& document, const ir::Target& target) const {
                json result = json::array();
                for (const auto& warning : backend::target_warnings(document, target)) {
                    middleend::Diagnostic diagnostic{warning.where, 0, 0, warning.message};
                    const std::size_t column = warning.where.rfind(':');
                    const std::size_t line = column == std::string::npos || column == 0 ? std::string::npos : warning.where.rfind(':', column - 1);
                    if (line != std::string::npos) {
                        try {
                            diagnostic = {warning.where.substr(0, line), std::stoul(warning.where.substr(line + 1, column - line - 1)), std::stoul(warning.where.substr(column + 1)), warning.message};
                        } catch (const std::exception&) {
                            // 위치를 읽을 수 없으면 파일 처음을 가리킨다
                        }
                    }
                    result.push_back({{"uri", uri_of(diagnostic.path)}, {"range", diagnostic_range(compiled, diagnostic)}, {"severity", 2}, {"message", warning.message}});
                }
                return result;
            }

            void publish(const std::string& key, const Compiled& compiled) {
                std::map<std::string, json> by_uri;
                for (const auto& diagnostic : compiled.result.diagnostics) {
                    by_uri[uri_of(diagnostic.path)].push_back({
                        {"range", diagnostic_range(compiled, diagnostic)},
                        {"severity", 1},
                        {"source", "templide"},
                        {"message", diagnostic.message},
                    });
                }
                by_uri.try_emplace(documents_.at(key).uri, json::array());
                // 이번에 오류가 없어진 파일도 빈 목록을 보내 지운다
                for (const auto& uri : published_[key]) {
                    by_uri.try_emplace(uri, json::array());
                }
                published_[key].clear();
                for (const auto& [uri, diagnostics] : by_uri) {
                    send({{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}});
                    if (!diagnostics.empty()) {
                        published_[key].insert(uri);
                    }
                }
            }

            // ---- 덱과 편집

            const Compiled& compiled_for(const json& params) const {
                const auto it = compiled_.find(file_key(uri_to_path(params.at("uri").get<std::string>())));
                if (it == compiled_.end()) {
                    throw std::runtime_error("The document is not open");
                }
                return it->second;
            }

            // 고른 target. 없으면 target이 없을 때의 기본값(1280 x 720, slide들이 쓰는 master)을 fallback에 만든다
            static const ir::Target* pick_target(const ir::Document& document, const json& params, ir::Target& fallback) {
                if (params.contains("target") && !params.at("target").is_null()) {
                    const std::string name = params.at("target").get<std::string>();
                    for (const auto& target : document.targets) {
                        if (target.name == name) {
                            return &target;
                        }
                    }
                    throw std::runtime_error("Unknown target: " + name);
                }
                if (!document.targets.empty()) {
                    return &document.targets.front();
                }
                fallback = ir::Target{};
                fallback.type = "html";
                for (const auto& slide : document.slides) {
                    if (slide.layout && std::find(fallback.masters.begin(), fallback.masters.end(), slide.layout->master) == fallback.masters.end()) {
                        fallback.masters.push_back(slide.layout->master);
                    }
                }
                return nullptr;
            }

            static SlideSize size_of(const ir::Target* target) {
                if (target == nullptr || !target->width || !target->height) {
                    return {default_slide_width, default_slide_height};
                }
                return {to_px(target->width, default_slide_width), to_px(target->height, default_slide_height)};
            }

            json origin_json(const Compiled& compiled, const ir::Origin& origin) const {
                json result = {{"kind", kind_name(origin.kind)}};
                if (origin.kind == ir::Origin::Kind::LOCKED) {
                    result["reason"] = origin.reason;
                    return result;
                }
                result["uri"] = uri_of(origin.range.path);
                result["range"] = range_of(compiled, origin.range);
                if (!origin.unit.empty()) {
                    result["unit"] = origin.unit;
                }
                if (!origin.name.empty()) {
                    result["name"] = origin.name;
                }
                if (origin.integer) {
                    result["integer"] = true;
                }
                if (origin.text) {
                    result["text"] = true;
                }
                if (!origin.group.path.empty()) {
                    result["styled"] = true; // (style(...) "글자")로 감싸여 있어 서식을 지울 수 있다
                }
                if (!origin.statement.path.empty()) {
                    result["statement"] = true; // 'name = 값;' 대입을 지워 기본값으로 되돌릴 수 있다
                }
                return result;
            }

            // slide의 애니메이션(재생 차례), 전환, 속성, 메모, 검토 메모
            json slide_json(const Compiled& compiled, const ir::Slide& slide, SlideSize size) const {
                json animations = json::array();
                for (const auto& animation : slide.animations) {
                    json entry = {
                        {"target", animation.target}, {"elementId", animation.element_id}, {"category", animation.category}, {"effect", animation.effect},
                        {"option", animation.option}, {"start", animation.start}, {"source", origin_json(compiled, animation.source)},
                        {"inside", animation.inside}, {"implicit", animation.implicit},
                    };
                    if (!animation.path.empty()) {
                        entry["path"] = animation.path;
                    }
                    if (animation.duration) {
                        entry["duration"] = value_json("", *animation.duration, size);
                    }
                    if (animation.delay) {
                        entry["delay"] = value_json("", *animation.delay, size);
                    }
                    if (animation.order) {
                        entry["order"] = *animation.order;
                    }
                    animations.push_back(std::move(entry));
                }
                json properties = json::object();
                for (const auto& [name, origin] : slide.origins) {
                    properties[name] = origin_json(compiled, origin);
                }
                json values = json::object();
                if (slide.background) {
                    values["background"] = value_json("background", *slide.background, size);
                }
                values["hidden"] = slide.hidden;
                if (slide.advance_after) {
                    values["advance_after"] = value_json("", *slide.advance_after, size);
                }
                if (slide.transition_sound) {
                    values["transition_sound"] = *slide.transition_sound;
                }
                json result = {
                    {"page", slide.page}, {"source", origin_json(compiled, slide.source)}, {"animations", animations},
                    {"properties", properties}, {"values", values},
                };
                if (slide.transition) {
                    json transition = {{"kind", slide.transition->kind}, {"option", slide.transition->option}, {"source", origin_json(compiled, slide.transition->source)}};
                    if (slide.transition->duration) {
                        transition["duration"] = value_json("", *slide.transition->duration, size);
                    }
                    result["transition"] = transition;
                }
                std::string notes;
                for (std::size_t i = 0; i < slide.notes.paragraphs.size(); ++i) {
                    notes += i > 0 ? "\n" : "";
                    for (const auto& run : slide.notes.paragraphs[i].runs) {
                        notes += run.text;
                    }
                }
                result["notes"] = notes;
                json note_sources = json::array();
                for (const auto& origin : slide.note_sources) {
                    note_sources.push_back(origin_json(compiled, origin));
                }
                result["noteSources"] = note_sources;
                json reviews = json::array();
                for (const auto& review : slide.reviews) {
                    reviews.push_back({{"text", review.text}, {"author", review.author}, {"x", value_json("x", review.x, size)}, {"y", value_json("y", review.y, size)},
                                       {"source", origin_json(compiled, review.source)}});
                }
                result["reviews"] = reviews;
                if (slide.section) {
                    result["section"] = *slide.section;
                }
                return result;
            }

            // 편집기의 속성 패널과 고르는 목록이 쓰는 이름들 (마지막으로 분석에 성공한 문서의 것)
            json schema(const json& params) const {
                const auto it = symbols_.find(file_key(uri_to_path(params.at("uri").get<std::string>())));
                if (it == symbols_.end()) {
                    return {{"error", "The document has not been analyzed yet"}};
                }
                const middleend::Symbols& symbols = it->second;
                const auto vars = [](const std::vector<middleend::SymbolVar>& list) {
                    json result = json::array();
                    for (const auto& var : list) {
                        result.push_back({{"name", var.name}, {"type", var.type}, {"default", var.default_value}, {"required", var.required}});
                    }
                    return result;
                };
                json objects = json::object();
                for (const auto& [name, list] : symbols.objects) {
                    objects[name] = vars(list);
                }
                json templates = json::object();
                for (const auto& [name, list] : symbols.templates) {
                    templates[name] = vars(list);
                }
                json masters = json::object();
                for (const auto& [name, master] : symbols.masters) {
                    masters[name] = {{"parameters", master.parameters}, {"cases", master.cases}};
                }
                return {
                    {"objects", objects}, {"templates", templates}, {"enums", symbols.enums}, {"masters", masters}, {"themes", symbols.themes},
                    {"commonProperties", vars(symbols.common_properties)}, {"textProperties", vars(symbols.text_properties)},
                    {"styleProperties", vars(symbols.style_properties)}, {"slideProperties", vars(symbols.slide_properties)},
                    {"transitions", symbols.transitions}, {"animations", symbols.animations}, {"endlessAnimations", symbols.endless_animations},
                    {"themeColors", symbols.theme_colors},
                };
            }

            void add_elements(const Compiled& compiled, const std::vector<ir::Element>& elements, SlideSize size, json& out) const {
                for (const auto& element : elements) {
                    if (!element.id.empty()) {
                        json entry = {{"object", element.object}, {"source", origin_json(compiled, element.source)}, {"fromTemplate", element.from_template}};
                        if (!element.name.empty() && !element.generated_name) {
                            entry["name"] = element.name;
                        }
                        json properties = json::object();
                        json values = json::object();
                        for (const auto& property : element.properties) {
                            properties[property.name] = origin_json(compiled, property.origin);
                            if (json value = value_json(property.name, property.value, size); !value.is_null()) {
                                values[property.name] = std::move(value);
                            }
                            if (const auto* text = std::get_if<ir::Text>(&property.value)) {
                                json paragraphs = json::array();
                                for (const auto& paragraph : text->paragraphs) {
                                    json runs = json::array();
                                    for (const auto& run : paragraph.runs) {
                                        runs.push_back(origin_json(compiled, run.origin));
                                    }
                                    paragraphs.push_back(std::move(runs));
                                }
                                entry["text"] = std::move(paragraphs);
                            }
                        }
                        entry["properties"] = std::move(properties);
                        entry["values"] = std::move(values);
                        if (element.from_template) {
                            json instance = json::object();
                            json instance_values = json::object();
                            for (const auto& property : element.instance) {
                                instance[property.name] = origin_json(compiled, property.origin);
                                if (json value = value_json(property.name, property.value, size); !value.is_null()) {
                                    instance_values[property.name] = std::move(value);
                                }
                            }
                            entry["instance"] = std::move(instance);
                            entry["instanceValues"] = std::move(instance_values);
                        }
                        out[element.id] = std::move(entry);
                    }
                    add_elements(compiled, element.children, size, out);
                }
            }

            // 덱 JSON은 이미 글자로 만들어져 있으므로 다시 읽지 않고 결과에 끼워 넣는다
            void reply_deck(const json& id, const json& params) const {
                const Compiled& compiled = compiled_for(params);
                if (!compiled.result.document) {
                    reply(id, {{"error", "The document has errors"}});
                    return;
                }
                const ir::Document& document = *compiled.result.document;
                ir::Target fallback;
                const ir::Target* target = pick_target(document, params, fallback);
                const SlideSize size = size_of(target);
                std::vector<std::string> warnings;
                const std::filesystem::path main = uri_to_path(params.at("uri").get<std::string>());
                const std::string deck = backend::html::deck_json(document, target != nullptr ? *target : fallback, main.parent_path(), warnings);

                json targets = json::array();
                for (const auto& each : document.targets) {
                    targets.push_back({{"name", each.name}, {"type", each.type}});
                }
                json slides = json::array();
                json elements = json::object();
                for (const auto& slide : document.slides) {
                    slides.push_back(slide_json(compiled, slide, size));
                    add_elements(compiled, slide.elements, size, elements);
                }
                json document_info = json::object();
                if (target != nullptr) {
                    json properties = json::object();
                    for (const auto& [name, origin] : target->origins) {
                        properties[name] = origin_json(compiled, origin);
                    }
                    document_info = {{"title", target->title}, {"author", target->author}, {"loop", target->loop}, {"type", target->type},
                                     {"path", target->path}, {"source", origin_json(compiled, target->source)}, {"properties", properties}};
                }
                const json result = {
                    {"targets", targets},
                    {"target", target != nullptr ? json(target->name) : json()},
                    {"width", size.width},
                    {"height", size.height},
                    {"warnings", warnings},
                    {"targetWarnings", target != nullptr ? warnings_json(compiled, document, *target) : json::array()},
                    // 슬라이드 쇼가 run(...)에 쓰는 script의 경로. 고른 target에 없으면 script가 있는 첫 target의 것이다
                    {"script", script_path(document, target, main.parent_path())},
                    {"slides", slides},
                    {"elements", elements},
                    {"document", document_info},
                };
                const std::string rest = result.dump(-1, ' ', false, json::error_handler_t::replace);
                send_text("{\"jsonrpc\":\"2.0\",\"id\":" + id.dump() + ",\"result\":{\"deck\":" + deck + "," + rest.substr(1) + "}");
            }

            static json script_path(const ir::Document& document, const ir::Target* target, const std::filesystem::path& base) {
                const ir::Target* source = target != nullptr && !target->script.empty() ? target : nullptr;
                for (const auto& each : document.targets) {
                    if (source == nullptr && !each.script.empty()) {
                        source = &each;
                    }
                }
                if (source == nullptr) {
                    return nullptr;
                }
                const std::filesystem::path path = backend::utf8_path(source->script);
                return backend::display((path.is_absolute() ? path : base / path).lexically_normal());
            }

            // 편집 요청의 문서가 분석기에 들어간 경로. slide가 없을 때 새 slide를 이 파일에 넣는다
            static std::string main_source(const Compiled& compiled, const json& params) {
                const std::string key = file_key(uri_to_path(params.at("uri").get<std::string>()));
                for (const auto& [path, text] : compiled.texts) {
                    if (file_key(std::filesystem::path(path)) == key) {
                        return path;
                    }
                }
                return "";
            }

            json completion(const json& params) const {
                const std::string key = file_key(uri_to_path(params.at("textDocument").at("uri").get<std::string>()));
                const auto document = documents_.find(key);
                const auto symbols = symbols_.find(key);
                if (document == documents_.end()) {
                    return {{"isIncomplete", false}, {"items", json::array()}};
                }
                const SourceText source = index_source(document->second.text);
                // LSP 위치(줄, UTF-16 칸)를 바이트 위치로
                const auto line = params.at("position").at("line").get<std::size_t>();
                const auto character = params.at("position").at("character").get<std::size_t>();
                std::size_t offset = line < source.line_starts.size() ? source.line_starts[line] : source.text.size();
                for (std::size_t units = 0; units < character && offset < source.text.size() && source.text[offset] != '\n';) {
                    const auto byte = static_cast<unsigned char>(source.text[offset]);
                    const std::size_t length = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
                    units += length == 4 ? 2 : 1;
                    offset += length;
                }
                const Completion result = complete(symbols != symbols_.end() ? symbols->second : middleend::Symbols{}, source.text, offset, packages_dir_);
                const json range = {{"start", position(source, result.replace_from)}, {"end", position(source, offset)}};
                json items = json::array();
                for (const auto& item : result.items) {
                    json entry = {
                        {"label", item.label},
                        {"kind", item.kind},
                        {"detail", item.detail},
                        {"textEdit", {{"range", range}, {"newText", item.insert_text.empty() ? item.label : item.insert_text}}},
                        {"insertTextFormat", item.snippet ? 2 : 1},
                    };
                    if (!item.sort_text.empty()) {
                        entry["sortText"] = item.sort_text + item.label;
                    }
                    items.push_back(std::move(entry));
                }
                return {{"isIncomplete", false}, {"items", items}};
            }

            // ---- 이름 찾기

            // 요청의 문서를 읽은 컴파일 결과와 그 안의 경로. 열린 문서가 아니면(include한 파일) 그 파일을 읽은 컴파일에서 찾는다
            struct Located {
                const Compiled* compiled = nullptr;
                std::string path;
                const SourceText* source = nullptr;
                std::size_t offset = 0;
                std::string key; // 그 컴파일의 main 문서의 file_key
            };

            std::optional<Located> locate(const json& params) const {
                const std::string key = file_key(uri_to_path(params.at("textDocument").at("uri").get<std::string>()));
                const auto find_in = [&](const std::string& main, const Compiled& compiled) -> std::optional<Located> {
                    for (const auto& [path, source] : compiled.sources) {
                        if (file_key(std::filesystem::path(path)) == key) {
                            return Located{&compiled, path, &source, 0, main};
                        }
                    }
                    return std::nullopt;
                };
                std::optional<Located> located;
                if (const auto it = compiled_.find(key); it != compiled_.end()) {
                    located = find_in(key, it->second);
                }
                for (auto it = compiled_.begin(); !located && it != compiled_.end(); ++it) {
                    located = find_in(it->first, it->second);
                }
                if (located && params.contains("position")) {
                    // LSP 위치(줄, UTF-16 칸)를 바이트 위치로
                    const SourceText& source = *located->source;
                    const auto line = params.at("position").at("line").get<std::size_t>();
                    const auto character = params.at("position").at("character").get<std::size_t>();
                    std::size_t offset = line < source.line_starts.size() ? source.line_starts[line] : source.text.size();
                    for (std::size_t units = 0; units < character && offset < source.text.size() && source.text[offset] != '\n';) {
                        const auto byte = static_cast<unsigned char>(source.text[offset]);
                        const std::size_t length = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
                        units += length == 4 ? 2 : 1;
                        offset += length;
                    }
                    located->offset = offset;
                }
                return located;
            }

            const NavIndex& navigation_of(const Located& located) const {
                const Compiled& compiled = *located.compiled;
                if (!compiled.navigation) {
                    std::vector<NavSource> sources;
                    const std::filesystem::path packages(file_key(packages_dir_));
                    for (const auto& [path, source] : compiled.sources) {
                        const std::filesystem::path relative = std::filesystem::path(file_key(std::filesystem::path(path))).lexically_relative(packages);
                        sources.push_back({path, &source.text, !relative.empty() && *relative.begin() != ".."});
                    }
                    const auto symbols = symbols_.find(located.key);
                    compiled.navigation = build_index(sources, compiled.result.symbols.valid ? compiled.result.symbols
                                                               : symbols != symbols_.end() ? symbols->second : middleend::Symbols{}, packages_dir_);
                }
                return *compiled.navigation;
            }

            json location_json(const Compiled& compiled, const std::string& path, std::size_t begin, std::size_t end) const {
                return {{"uri", uri_of(path)}, {"range", range_of(compiled, {path, begin, end})}};
            }

            // 커서 위치의 이름. 없으면 nullptr
            const Occurrence* occurrence_for(const json& params, std::optional<Located>& located) const {
                located = locate(params);
                return located ? occurrence_at(navigation_of(*located), located->path, located->offset) : nullptr;
            }

            json hover(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                if (occurrence == nullptr) {
                    return nullptr;
                }
                const std::string text = hover_text(navigation_of(*located), *occurrence);
                if (text.empty()) {
                    return nullptr;
                }
                return {{"contents", {{"kind", "markdown"}, {"value", text}}},
                        {"range", range_of(*located->compiled, {occurrence->path, occurrence->begin, occurrence->end})}};
            }

            json definition(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                if (occurrence == nullptr || occurrence->symbol < 0) {
                    return json::array();
                }
                const NavSymbol& symbol = navigation_of(*located).symbols[occurrence->symbol];
                return json::array({location_json(*located->compiled, symbol.path, symbol.begin, symbol.end)});
            }

            json references(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                json result = json::array();
                if (occurrence == nullptr || occurrence->symbol < 0) {
                    return result;
                }
                const bool declaration = !params.contains("context") || params.at("context").value("includeDeclaration", true);
                for (const auto& each : navigation_of(*located).occurrences) {
                    if (each.symbol == occurrence->symbol && (declaration || !each.definition)) {
                        result.push_back(location_json(*located->compiled, each.path, each.begin, each.end));
                    }
                }
                return result;
            }

            json highlights(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                json result = json::array();
                if (occurrence == nullptr || occurrence->symbol < 0) {
                    return result;
                }
                for (const auto& each : navigation_of(*located).occurrences) {
                    if (each.symbol == occurrence->symbol && each.path == located->path) {
                        // DocumentHighlightKind: 2 읽기, 3 쓰기(선언)
                        result.push_back({{"range", range_of(*located->compiled, {each.path, each.begin, each.end})}, {"kind", each.definition ? 3 : 2}});
                    }
                }
                return result;
            }

            // 바꿀 수 없으면 오류로 그 이유를 돌려준다 (편집기가 그대로 보여 준다)
            json prepare_rename(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                if (occurrence == nullptr) {
                    throw std::runtime_error("이름이 아니라 바꿀 수 없습니다");
                }
                const NavIndex& index = navigation_of(*located);
                if (const auto problem = rename_problem(index, occurrence->symbol, located->path)) {
                    throw std::runtime_error(*problem);
                }
                return {{"range", range_of(*located->compiled, {occurrence->path, occurrence->begin, occurrence->end})}, {"placeholder", index.symbols[occurrence->symbol].name}};
            }

            json rename(const json& params) const {
                std::optional<Located> located;
                const Occurrence* occurrence = occurrence_for(params, located);
                if (occurrence == nullptr) {
                    throw std::runtime_error("이름이 아니라 바꿀 수 없습니다");
                }
                const NavIndex& index = navigation_of(*located);
                const std::string name = params.at("newName").get<std::string>();
                if (const auto problem = rename_problem(index, occurrence->symbol, located->path)) {
                    throw std::runtime_error(*problem);
                }
                if (const auto problem = new_name_problem(index, occurrence->symbol, name)) {
                    throw std::runtime_error(*problem);
                }
                json edits = json::array();
                for (const auto& each : index.occurrences) {
                    if (each.symbol == occurrence->symbol) {
                        edits.push_back({{"range", range_of(*located->compiled, {each.path, each.begin, each.end})}, {"newText", name}});
                    }
                }
                return {{"changes", {{uri_of(located->path), edits}}}};
            }

            json outline_json(const Compiled& compiled, const std::string& path, const std::vector<OutlineItem>& items) const {
                json result = json::array();
                for (const auto& item : items) {
                    json entry = {
                        {"name", item.name.empty() ? std::string("(이름 없음)") : item.name},
                        {"kind", item.kind},
                        {"range", range_of(compiled, {path, item.begin, item.end})},
                        {"selectionRange", range_of(compiled, {path, item.name_begin, item.name_end})},
                        {"children", outline_json(compiled, path, item.children)},
                    };
                    if (!item.detail.empty()) {
                        entry["detail"] = item.detail;
                    }
                    result.push_back(std::move(entry));
                }
                return result;
            }

            json document_symbols(const json& params) const {
                const std::optional<Located> located = locate(params);
                if (!located) {
                    return json::array();
                }
                const auto& outlines = navigation_of(*located).outlines;
                const auto it = outlines.find(located->path);
                return it == outlines.end() ? json::array() : outline_json(*located->compiled, located->path, it->second);
            }

            const middleend::Symbols* symbols_for(const json& params) const {
                const auto it = symbols_.find(file_key(uri_to_path(params.at("uri").get<std::string>())));
                return it == symbols_.end() ? nullptr : &it->second;
            }

            // slide 블록의 속성, layout, 전환, 발표자 메모, 검토 메모
            static EditResult slide_edit(const Editor& editor, const ir::Slide& slide, const std::string& kind, const json& op, const middleend::Symbols* symbols) {
                if (kind == "slide_set") {
                    return editor.set_named(slide.origins, slide.source, op.at("name").get<std::string>(), new_value(op));
                }
                if (kind == "slide_unset") {
                    return editor.unset_named(slide.origins, op.at("name").get<std::string>());
                }
                if (kind == "layout") {
                    // layout = master(값).case 에서 case 이름만 바꾼다
                    const auto it = slide.origins.find("layout");
                    if (it == slide.origins.end() || it->second.kind != ir::Origin::Kind::LITERAL) {
                        return {{}, it == slide.origins.end() ? "This slide has no layout" : lock_message(it->second.reason)};
                    }
                    const std::string* text = editor.source_text(it->second.range.path);
                    if (text == nullptr) {
                        return {{}, "Cannot find the source file"};
                    }
                    const std::string expression = text->substr(it->second.range.begin, it->second.range.end - it->second.range.begin);
                    const std::size_t dot = expression.rfind('.');
                    if (dot == std::string::npos) {
                        return {{}, "Cannot read the layout"};
                    }
                    return {{{it->second.range.path, it->second.range.begin + dot + 1, it->second.range.end, op.at("case").get<std::string>()}}, ""};
                }
                if (kind == "transition") {
                    if (op.value("remove", false) || !op.contains("kind")) {
                        return slide.transition ? editor.remove_statement(slide.transition->source) : EditResult{};
                    }
                    const std::string transition_kind = op.at("kind").get<std::string>();
                    std::string statement = "transition " + transition_kind;
                    const std::string option = op.value("option", "");
                    const auto options = symbols != nullptr ? symbols->transitions.find(transition_kind) : decltype(symbols->transitions)::const_iterator{};
                    const bool is_default = symbols != nullptr && options != symbols->transitions.end() && !options->second.empty() && options->second.front() == option;
                    if (!option.empty() && !is_default) {
                        statement += "." + option;
                    }
                    if (op.contains("duration") && op.at("duration").is_number()) {
                        statement += " " + seconds_code(op.at("duration").get<double>());
                    }
                    statement += ";";
                    return slide.transition ? editor.replace_statement(slide.transition->source, statement) : editor.insert_into(slide.source, statement);
                }
                if (kind == "notes") {
                    // comment 문장들을 하나로 바꾼다. 비우면 모두 지운다
                    const std::string notes = op.at("text").get<std::string>();
                    EditResult result;
                    for (std::size_t i = 0; i < slide.note_sources.size(); ++i) {
                        EditResult part = i == 0 && !notes.empty() ? editor.replace_statement(slide.note_sources[i], "comment " + quote_string(notes) + ";")
                                                                   : editor.remove_statement(slide.note_sources[i]);
                        if (!part.error.empty()) {
                            return part;
                        }
                        result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
                    }
                    if (slide.note_sources.empty() && !notes.empty()) {
                        return editor.insert_into(slide.source, "comment " + quote_string(notes) + ";");
                    }
                    return result;
                }
                // 검토 메모 추가, 고치기, 지우기
                const std::string action = op.at("action").get<std::string>();
                const auto index = op.value("index", std::size_t{0});
                if (action != "add" && index >= slide.reviews.size()) {
                    throw std::runtime_error("No review " + std::to_string(index));
                }
                if (action == "delete") {
                    return editor.remove_statement(slide.reviews[index].source);
                }
                const auto px = [](double value) {
                    char buffer[32];
                    std::snprintf(buffer, sizeof buffer, "%.10g", std::round(value));
                    return std::string(buffer) + "px";
                };
                const std::string statement = "review { text = " + quote_string(op.value("text", "")) + "; author = " + quote_string(op.value("author", "templide"))
                    + "; x = " + px(op.value("x", 0.0)) + "; y = " + px(op.value("y", 0.0)) + "; }";
                return action == "add" ? editor.insert_into(slide.source, statement) : editor.replace_statement(slide.reviews[index].source, statement);
            }

            // 문장의 order 값을 order로 바꾸거나 order 절을 넣는다
            static EditResult set_order(const Editor& editor, const ir::Origin& statement, int order) {
                if (statement.kind != ir::Origin::Kind::BLOCK) {
                    return {{}, lock_message(statement.reason)};
                }
                const std::string* text = editor.source_text(statement.range.path);
                if (text == nullptr) {
                    return {{}, "Cannot find the source file"};
                }
                const std::string body = text->substr(statement.range.begin, statement.range.end - statement.range.begin);
                const std::size_t at = find_word(body, "order");
                if (at == std::string::npos) {
                    const std::size_t end = body.rfind(';');
                    if (end == std::string::npos) {
                        return {{}, "Cannot read the animate statement"};
                    }
                    const std::size_t insert = statement.range.begin + end;
                    return {{{statement.range.path, insert, insert, " order " + std::to_string(order)}}, ""};
                }
                std::size_t begin = at + 5;
                while (begin < body.size() && (body[begin] == ' ' || body[begin] == '\t')) {
                    ++begin;
                }
                std::size_t end = begin;
                while (end < body.size() && !std::isspace(static_cast<unsigned char>(body[end])) && body[end] != ';') {
                    ++end;
                }
                return {{{statement.range.path, statement.range.begin + begin, statement.range.begin + end, std::to_string(order)}}, ""};
            }

            // 새 애니메이션. element(template이 만든 것이면 바깥 put)의 블록에 넣는다
            struct NewAnimation {
                AnimationSpec spec;
                ir::Origin block;
            };

            // sequence의 차례대로 order 1, 2, ...를 붙인다. 값은 slide.animations의 번호, -1은 added다
            static EditResult renumber(const Editor& editor, const ir::Slide& slide, const std::vector<int>& sequence, const std::optional<NewAnimation>& added, const middleend::Symbols* symbols) {
                EditResult result;
                for (std::size_t k = 0; k < sequence.size(); ++k) {
                    const int order = static_cast<int>(k) + 1;
                    EditResult part;
                    if (sequence[k] < 0) {
                        AnimationSpec spec = added->spec;
                        spec.order = order;
                        part = editor.insert_into(added->block, animation_statement(spec, "", symbols));
                    } else {
                        const ir::Animation& animation = slide.animations.at(static_cast<std::size_t>(sequence[k]));
                        if (animation.source.kind == ir::Origin::Kind::LOCKED) {
                            return {{}, "Some animations on this slide are made by a for loop or a template, so their order cannot be changed here; change it in the code"};
                        }
                        if (animation.implicit) {
                            AnimationSpec spec = spec_of(animation);
                            spec.order = order;
                            part = editor.insert_into(animation.source, animation_statement(spec, "", symbols));
                        } else if (animation.order != order) {
                            part = set_order(editor, animation.source, order);
                        }
                    }
                    if (!part.error.empty()) {
                        return part;
                    }
                    result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
                }
                return result;
            }

            static const ir::Element* slide_element(const ir::Slide& slide, const std::string& id) {
                return find_element(slide.elements, id);
            }

            // {op: "animation", page, action: add | update | delete | move, ...}
            static EditResult animation_edit(const Editor& editor, const ir::Slide& slide, const json& op, const middleend::Symbols* symbols) {
                const std::string action = op.at("action").get<std::string>();
                const auto count = static_cast<int>(slide.animations.size());
                if (action == "add") {
                    const ir::Element* element = slide_element(slide, op.at("id").get<std::string>());
                    if (element == nullptr) {
                        throw std::runtime_error("No object with id " + op.at("id").get<std::string>());
                    }
                    if (element->source.kind != ir::Origin::Kind::BLOCK) {
                        return {{}, lock_message(element->source.reason)};
                    }
                    AnimationSpec spec;
                    apply_changes(spec, op.at("spec"));
                    if (spec.category.empty() || spec.effect.empty()) {
                        return {{}, "An animation needs a category and an effect"};
                    }
                    // 순서 번호가 없고 새 문장이 다른 애니메이션 문장들보다 뒤에 적히면 그대로 맨 끝에 재생된다. 아니면 모두 번호를 붙인다
                    const auto& block = element->source.range;
                    const bool last = std::all_of(slide.animations.begin(), slide.animations.end(), [&](const ir::Animation& animation) {
                        return !animation.order && animation.source.kind != ir::Origin::Kind::LOCKED && animation.source.range.path == block.path
                            && animation.source.range.begin < block.end;
                    });
                    if (last) {
                        return editor.insert_into(element->source, animation_statement(spec, "", symbols));
                    }
                    std::vector<int> sequence;
                    for (int i = 0; i < count; ++i) {
                        sequence.push_back(i);
                    }
                    sequence.push_back(-1);
                    return renumber(editor, slide, sequence, NewAnimation{spec, element->source}, symbols);
                }
                const auto index = op.at("index").get<int>();
                if (index < 0 || index >= count) {
                    throw std::runtime_error("No animation " + std::to_string(index));
                }
                const ir::Animation& animation = slide.animations[static_cast<std::size_t>(index)];
                if (action == "move") {
                    const int to = std::clamp(op.at("to").get<int>(), 0, count - 1);
                    std::vector<int> sequence;
                    for (int i = 0; i < count; ++i) {
                        if (i != index) {
                            sequence.push_back(i);
                        }
                    }
                    sequence.insert(sequence.begin() + to, index);
                    return renumber(editor, slide, sequence, std::nullopt, symbols);
                }
                const ir::Element* element = slide_element(slide, animation.element_id);
                if (action == "update") {
                    AnimationSpec spec = spec_of(animation);
                    apply_changes(spec, op.at("spec"));
                    if (animation.implicit) {
                        return editor.insert_into(animation.source, animation_statement(spec, "", symbols));
                    }
                    return editor.replace_statement(animation.source, animation_statement(spec, animation.inside ? "" : animation.target, symbols));
                }
                if (action != "delete") {
                    throw std::runtime_error("Unknown animation action: " + action);
                }
                // video, audio의 재생을 지우면 start도 when_clicked로 바꿔야 저절로 다시 생기지 않는다
                EditResult result;
                if (!animation.implicit) {
                    result = editor.remove_statement(animation.source);
                    if (!result.error.empty()) {
                        return result;
                    }
                }
                const bool play = animation.category == "media" && animation.effect == "play";
                const bool other_play = std::any_of(slide.animations.begin(), slide.animations.end(), [&](const ir::Animation& other) {
                    return &other != &animation && other.element_id == animation.element_id && other.category == "media" && other.effect == "play";
                });
                if (play && !other_play && element != nullptr) {
                    const ir::Value* start = backend::find_property(*element, "start");
                    const auto* member = start != nullptr ? std::get_if<ir::EnumValue>(start) : nullptr;
                    if (member == nullptr || member->member != "when_clicked") {
                        EditResult part = editor.set_property(*element, "start", {NewValue::Kind::NAME, 0, "when_clicked"});
                        if (!part.error.empty()) {
                            return part;
                        }
                        result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
                    }
                }
                return result;
            }

            // 고른 target(없으면 첫 target)의 파일을 만든다. 상대 경로는 문서의 폴더 기준이다
            json build(const json& params) const {
                const Compiled& compiled = compiled_for(params);
                if (!compiled.result.document) {
                    return {{"error", "The document has errors"}};
                }
                const ir::Document& document = *compiled.result.document;
                ir::Target fallback;
                const ir::Target* target = pick_target(document, params, fallback);
                if (target == nullptr) {
                    return {{"error", "There is no target; add one such as target out { path = \"out.pptx\"; type = pptx; }"}};
                }
                const std::filesystem::path base = uri_to_path(params.at("uri").get<std::string>()).parent_path();
                std::vector<std::string> warnings;
                const auto errors = backend::write_target(document, *target, base, libs_dir_, warnings);
                const std::filesystem::path output = (base / backend::utf8_path(target->path)).lexically_normal();
                return {{"path", backend::display(output)}, {"type", target->type}, {"errors", errors}, {"warnings", warnings}};
            }

            // 내보내기 창이 보여 주는 target. 덱 전체를 만들지 않으므로 가볍다
            json targets(const json& params) const {
                const Compiled& compiled = compiled_for(params);
                if (!compiled.result.document) {
                    return {{"error", "The document has errors"}};
                }
                const std::filesystem::path base = uri_to_path(params.at("uri").get<std::string>()).parent_path();
                json targets = json::array();
                for (const auto& target : compiled.result.document->targets) {
                    const std::filesystem::path output = (base / backend::utf8_path(target.path)).lexically_normal();
                    targets.push_back({{"name", target.name}, {"type", target.type}, {"path", backend::display(output)}});
                }
                return {{"targets", targets}};
            }

            json edit(const json& params) const {
                const Compiled& compiled = compiled_for(params);
                if (!compiled.result.document) {
                    return {{"error", "The document has errors"}};
                }
                const ir::Document& document = *compiled.result.document;
                ir::Target fallback;
                const ir::Target* target = pick_target(document, params, fallback);
                const Editor editor(compiled.texts, size_of(target));

                std::vector<TextEdit> edits;
                for (const auto& op : params.at("edits")) {
                    const std::string kind = op.at("op").get<std::string>();
                    const auto element = [&]() -> const ir::Element& {
                        const std::string element_id = op.at("id").get<std::string>();
                        for (const auto& slide : document.slides) {
                            if (const auto* found = find_element(slide.elements, element_id)) {
                                return *found;
                            }
                        }
                        throw std::runtime_error("No object with id " + element_id);
                    };
                    EditResult result;
                    if (kind == "set" && op.value("instance", false)) {
                        // template을 넣은 바깥쪽 put의 x, y, width, height
                        const ir::Element& target = element();
                        const std::string name = op.at("name").get<std::string>();
                        const auto found = std::find_if(target.instance.begin(), target.instance.end(), [&](const ir::Property& property) { return property.name == name; });
                        if (found == target.instance.end()) {
                            return {{"error", "The template has no '" + name + "'"}};
                        }
                        result = editor.set(found->origin, name, new_value(op));
                    } else if (kind == "set") {
                        result = editor.set_property(element(), op.at("name").get<std::string>(), new_value(op));
                    } else if (kind == "move") {
                        result = editor.move(element(), op.value("dx", 0.0), op.value("dy", 0.0));
                    } else if (kind == "text") {
                        const auto* text = std::get_if<ir::Text>(backend::find_property(element(), "text"));
                        const auto paragraph = op.at("paragraph").get<std::size_t>();
                        const auto run = op.at("run").get<std::size_t>();
                        if (text == nullptr || paragraph >= text->paragraphs.size() || run >= text->paragraphs[paragraph].runs.size()) {
                            throw std::runtime_error("No such text run");
                        }
                        result = editor.set_text(text->paragraphs[paragraph].runs[run], op.at("text").get<std::string>());
                    } else if (kind == "style") {
                        const auto* text = std::get_if<ir::Text>(backend::find_property(element(), "text"));
                        const auto paragraph = op.at("paragraph").get<std::size_t>();
                        const auto run = op.at("run").get<std::size_t>();
                        if (text == nullptr || paragraph >= text->paragraphs.size() || run >= text->paragraphs[paragraph].runs.size()) {
                            throw std::runtime_error("No such text run");
                        }
                        const ir::Run& target_run = text->paragraphs[paragraph].runs[run];
                        // start, end는 편집기의 UTF-16 위치이고 없으면 run 전체다
                        const std::size_t begin = utf16_to_byte(target_run.text, op.value("start", std::size_t{0}));
                        const std::size_t end = op.contains("end") ? utf16_to_byte(target_run.text, op.at("end").get<std::size_t>()) : target_run.text.size();
                        std::vector<std::pair<std::string, NewValue>> properties;
                        for (const auto& property : op.at("properties")) {
                            properties.emplace_back(property.at("name").get<std::string>(), new_value(property));
                        }
                        result = editor.style(target_run, begin, end, properties);
                    } else if (kind == "unstyle") {
                        const auto* text = std::get_if<ir::Text>(backend::find_property(element(), "text"));
                        const auto paragraph = op.at("paragraph").get<std::size_t>();
                        const auto run = op.at("run").get<std::size_t>();
                        if (text == nullptr || paragraph >= text->paragraphs.size() || run >= text->paragraphs[paragraph].runs.size()) {
                            throw std::runtime_error("No such text run");
                        }
                        result = editor.unstyle(text->paragraphs[paragraph].runs[run]);
                    } else if (kind == "order") {
                        const std::string element_id = op.at("id").get<std::string>();
                        const std::vector<ir::Element>* siblings = nullptr;
                        for (const auto& slide : document.slides) {
                            if (!siblings) {
                                siblings = find_siblings(slide.elements, element_id);
                            }
                        }
                        if (siblings == nullptr) {
                            throw std::runtime_error("No object with id " + element_id);
                        }
                        result = editor.reorder(*siblings, element(), op.at("direction").get<std::string>());
                    } else if (kind == "slide") {
                        const std::string action = op.at("action").get<std::string>();
                        const auto page = op.value("page", std::size_t{0});
                        const auto slide = [&]() -> const ir::Slide& {
                            if (page == 0 || page > document.slides.size()) {
                                throw std::runtime_error("No slide " + std::to_string(page));
                            }
                            return document.slides[page - 1];
                        };
                        if (action == "add") {
                            result = editor.add_slide(document.slides, page, main_source(compiled, params));
                        } else if (action == "delete") {
                            result = editor.remove_slide(slide());
                        } else if (action == "duplicate") {
                            result = editor.duplicate_slide(slide());
                        } else if (action == "up" || action == "down") {
                            result = editor.move_slide(document.slides, page, action == "up" ? -1 : 1);
                        } else if (action == "move") {
                            result = editor.move_slide_to(document.slides, page, op.at("to").get<std::size_t>());
                        } else {
                            throw std::runtime_error("Unknown slide action: " + action);
                        }
                    } else if (kind == "insert") {
                        const auto page = op.at("page").get<std::size_t>();
                        if (page == 0 || page > document.slides.size()) {
                            throw std::runtime_error("No slide " + std::to_string(page));
                        }
                        std::vector<std::pair<std::string, NewValue>> properties;
                        for (const auto& property : op.value("properties", json::array())) {
                            properties.emplace_back(property.at("name").get<std::string>(), new_value(property));
                        }
                        result = editor.insert(document.slides[page - 1], op.at("object").get<std::string>(), properties);
                    } else if (kind == "copy") {
                        const auto page = op.at("page").get<std::size_t>();
                        if (page == 0 || page > document.slides.size()) {
                            throw std::runtime_error("No slide " + std::to_string(page));
                        }
                        result = editor.copy(element(), document.slides[page - 1], op.value("dx", 0.0), op.value("dy", 0.0));
                    } else if (kind == "paste") {
                        const auto page = op.at("page").get<std::size_t>();
                        if (page == 0 || page > document.slides.size()) {
                            throw std::runtime_error("No slide " + std::to_string(page));
                        }
                        result = editor.paste(document.slides[page - 1], op.at("text").get<std::string>());
                    } else if (kind == "delete") {
                        result = editor.remove(element());
                    } else if (kind == "size") {
                        if (target == nullptr) {
                            return {{"error", "There is no target to set the slide size on"}};
                        }
                        for (const auto& [name, origin] : {std::pair{"width", &target->width_origin}, std::pair{"height", &target->height_origin}}) {
                            if (!op.contains(name)) {
                                continue;
                            }
                            EditResult part = editor.set(*origin, name, {NewValue::Kind::LENGTH, op.at(name).get<double>()});
                            if (!part.error.empty()) {
                                result = part;
                                break;
                            }
                            result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
                        }
                    } else if (kind == "name") {
                        result = editor.rename(element(), op.at("name").get<std::string>());
                    } else if (kind == "unset") {
                        result = editor.unset_property(element(), op.at("name").get<std::string>());
                    } else if (kind == "slide_set" || kind == "slide_unset" || kind == "layout" || kind == "transition" || kind == "notes" || kind == "review") {
                        const auto page = op.at("page").get<std::size_t>();
                        if (page == 0 || page > document.slides.size()) {
                            throw std::runtime_error("No slide " + std::to_string(page));
                        }
                        result = slide_edit(editor, document.slides[page - 1], kind, op, symbols_for(params));
                    } else if (kind == "document_set" || kind == "document_unset") {
                        if (target == nullptr) {
                            return {{"error", "There is no target to set this on; add one such as target out { path = \"out.pptx\"; type = pptx; }"}};
                        }
                        const std::string name = op.at("name").get<std::string>();
                        result = kind == "document_set" ? editor.set_named(target->origins, target->source, name, new_value(op)) : editor.unset_named(target->origins, name);
                    } else if (kind == "animation") {
                        const auto page = op.at("page").get<std::size_t>();
                        if (page == 0 || page > document.slides.size()) {
                            throw std::runtime_error("No slide " + std::to_string(page));
                        }
                        result = animation_edit(editor, document.slides[page - 1], op, symbols_for(params));
                    } else {
                        throw std::runtime_error("Unknown edit: " + kind);
                    }
                    if (!result.error.empty()) {
                        return {{"error", result.error}};
                    }
                    edits.insert(edits.end(), result.edits.begin(), result.edits.end());
                }

                // 겹치는 수정은 한 번에 적용할 수 없다
                std::vector<const TextEdit*> sorted;
                for (const auto& each : edits) {
                    sorted.push_back(&each);
                }
                std::stable_sort(sorted.begin(), sorted.end(), [](const TextEdit* a, const TextEdit* b) { return std::tie(a->path, a->begin) < std::tie(b->path, b->begin); });
                for (std::size_t i = 1; i < sorted.size(); ++i) {
                    if (sorted[i]->path == sorted[i - 1]->path && sorted[i]->begin < sorted[i - 1]->end) {
                        return {{"error", "The edits overlap"}};
                    }
                }
                json result = json::array();
                for (const auto& each : edits) {
                    result.push_back({{"uri", uri_of(each.path)}, {"range", range_of(compiled, {each.path, each.begin, each.end})}, {"newText", each.text}});
                }
                return {{"edits", result}};
            }
        };
    }

    int serve(const std::filesystem::path& packages_dir, const std::filesystem::path& libs_dir) {
        return Server(packages_dir, libs_dir).run();
    }
}

#include "completion.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <system_error>

namespace templide::server {
    namespace {
        // LSP CompletionItemKind
        enum Kind { FUNCTION = 3, VARIABLE = 6, CLASS = 7, MODULE = 9, PROPERTY = 10, VALUE = 12, KEYWORD = 14, SNIPPET = 15, COLOR = 16, FILE = 17, ENUM_MEMBER = 20, STRUCT = 22 };

        bool is_word(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80;
        }

        std::string trim(const std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? "" : text.substr(first, last - first + 1);
        }

        // 커서 앞의 원문에서 열려 있는 괄호들
        struct Open {
            char bracket;
            std::size_t position;
            std::size_t statement; // 이 괄호를 연 문장의 시작
        };

        struct Context {
            bool in_comment = false;
            bool in_string = false;
            std::vector<Open> opens;
            std::size_t statement = 0; // 지금 쓰는 문장의 시작
        };

        Context scan(const std::string& text, std::size_t offset) {
            Context context;
            std::size_t i = 0;
            while (i < offset) {
                const char c = text[i];
                if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                    const auto end = text.find("*/", i + 2);
                    if (end == std::string::npos || end + 2 > offset) {
                        context.in_comment = true;
                        return context;
                    }
                    // 문장 앞의 주석은 문장에 넣지 않는다
                    if (trim(text.substr(context.statement, i - context.statement)).empty()) {
                        context.statement = end + 2;
                    }
                    i = end + 2;
                    continue;
                }
                if (c == '"') {
                    std::size_t j = i + 1;
                    while (j < text.size() && text[j] != '"' && text[j] != '\n') {
                        j += text[j] == '\\' ? 2 : 1;
                    }
                    if (j >= offset) {
                        context.in_string = true;
                        return context;
                    }
                    i = j + 1;
                    continue;
                }
                if (c == '{' || c == '(' || c == '[') {
                    context.opens.push_back({c, i, context.statement});
                    if (c == '{') {
                        context.statement = i + 1;
                    }
                } else if (c == '}' || c == ')' || c == ']') {
                    if (!context.opens.empty()) {
                        context.opens.pop_back();
                    }
                    if (c == '}') {
                        context.statement = i + 1;
                    }
                } else if (c == ';' && (context.opens.empty() || context.opens.back().bracket == '{')) {
                    context.statement = i + 1;
                }
                ++i;
            }
            return context;
        }

        // 블록을 연 문장의 첫 낱말과 둘째 낱말. put flat_card { -> (put, flat_card)
        std::pair<std::string, std::string> header_words(const std::string& header) {
            static const std::regex words(R"(^\s*(#?[A-Za-z_]\w*)\s*([A-Za-z_]\w*)?)");
            std::smatch match;
            if (std::regex_search(header, match, words)) {
                return {match[1], match[2]};
            }
            return {"", ""};
        }

        std::string placeholder(const middleend::Symbols& symbols, const middleend::SymbolVar& var, int index) {
            const std::string n = std::to_string(index);
            if (const auto it = symbols.enums.find(var.type); it != symbols.enums.end() && it->second.size() > 8) {
                // 값이 많으면 목록 대신 기본값, rect, 첫 값 중 하나를 둔다
                const bool has_rect = std::find(it->second.begin(), it->second.end(), "rect") != it->second.end();
                return "${" + n + ":" + (!var.default_value.empty() ? var.default_value : has_rect ? std::string("rect") : it->second.front()) + "}";
            }
            if (const auto it = symbols.enums.find(var.type); it != symbols.enums.end() && !it->second.empty()) {
                std::string choices;
                for (const auto& member : it->second) {
                    choices += (choices.empty() ? "" : ",") + member;
                }
                return "${" + n + "|" + choices + "|}";
            }
            if (var.type == "int" || var.type == "float") {
                return "${" + n + ":" + (var.name.empty() ? "1" : "0px") + "}"; // 이름 없는 매개변수(layout, style)는 수
            }
            if (var.type == "text" || var.type == "string") {
                return "\"${" + n + "}\"";
            }
            if (var.type == "color") {
                return "hex(${" + n + ":000000})";
            }
            if (var.type == "bool") {
                return "${" + n + "|true,false|}";
            }
            return "${" + n + "}";
        }

        void add(Completion& completion, std::string label, int kind, std::string detail, std::string insert = "", bool snippet = false, std::string sort = "") {
            completion.items.push_back({std::move(label), kind, std::move(detail), std::move(insert), snippet, std::move(sort)});
        }

        // name = 값; 하나를 넣는 snippet
        void add_property(Completion& completion, const middleend::Symbols& symbols, const middleend::SymbolVar& var, const std::string& sort) {
            const std::string detail = var.type + (var.required ? " · 꼭 넣어야 함" : var.default_value.empty() ? "" : " = " + var.default_value);
            add(completion, var.name, PROPERTY, detail, var.name + " = " + placeholder(symbols, var, 1) + ";", true, sort);
        }

        const std::vector<middleend::SymbolVar>* signature(const middleend::Symbols& symbols, const std::string& name, bool& object) {
            if (const auto it = symbols.objects.find(name); it != symbols.objects.end()) {
                object = true;
                return &it->second;
            }
            if (const auto it = symbols.templates.find(name); it != symbols.templates.end()) {
                object = false;
                return &it->second;
            }
            return nullptr;
        }

        // 블록 안에서 이미 적은 속성 이름
        std::set<std::string> assigned_in(const std::string& block) {
            std::set<std::string> names;
            static const std::regex assignment(R"((?:^|[;{])\s*([A-Za-z_][\w-]*)\s*=)");
            for (auto it = std::sregex_iterator(block.begin(), block.end(), assignment); it != std::sregex_iterator(); ++it) {
                names.insert((*it)[1]);
            }
            return names;
        }

        // 블록 안의 속성 목록과 그 타입
        std::vector<middleend::SymbolVar> properties_of(const middleend::Symbols& symbols, const std::string& kind, const std::string& name) {
            static const std::vector<middleend::SymbolVar> target = {
                {"path", "string", "", true}, {"type", "target_type", "", true}, {"width", "float"}, {"height", "float"},
                {"title", "string"}, {"author", "string"}, {"loop", "bool"}, {"angles", "angle_convention"}, {"script", "string"},
            };
            static const std::vector<middleend::SymbolVar> review = {{"text", "text", "", true}, {"author", "string"}, {"x", "int"}, {"y", "int"}};
            if (kind == "put") {
                bool object = false;
                const auto* vars = signature(symbols, name, object);
                if (vars == nullptr) {
                    return {};
                }
                std::vector<middleend::SymbolVar> result = *vars;
                const auto add_unique = [&](const std::vector<middleend::SymbolVar>& more) {
                    for (const auto& var : more) {
                        if (std::none_of(result.begin(), result.end(), [&](const auto& existing) { return existing.name == var.name; })) {
                            result.push_back(var);
                        }
                    }
                };
                if (object) {
                    add_unique(symbols.common_properties);
                    const bool has_text = std::any_of(vars->begin(), vars->end(), [](const auto& var) { return var.name == "text"; });
                    if (has_text) {
                        add_unique(symbols.text_properties);
                    }
                }
                return result;
            }
            if (kind == "slide") {
                std::vector<middleend::SymbolVar> result = symbols.slide_properties;
                result.push_back({"layout", "layout"});
                return result;
            }
            if (kind == "style") {
                return symbols.style_properties;
            }
            if (kind == "target") {
                return target;
            }
            if (kind == "review") {
                return review;
            }
            if (kind == "case") {
                return {{"background", "color, gradient, pattern or image"}};
            }
            return {};
        }

        void add_colors(Completion& completion, const middleend::Symbols& symbols) {
            add(completion, "hex(...)", FUNCTION, "16진수 색", "hex(${1:000000})", true, "0");
            add(completion, "rgb(...)", FUNCTION, "빨강, 초록, 파랑 (0~255)", "rgb(${1:0}, ${2:0}, ${3:0})", true, "0");
            add(completion, "rgba(...)", FUNCTION, "투명도가 있는 색", "rgba(${1:0}, ${2:0}, ${3:0}, ${4:1})", true, "0");
            add(completion, "hsl(...)", FUNCTION, "색상, 채도, 밝기 (투명도는 넷째 값)", "hsl(${1:160deg}, ${2:40%}, ${3:60%})", true, "0");
            add(completion, "hwb(...)", FUNCTION, "색상, 흰색, 검은색", "hwb(${1:160deg}, ${2:30%}, ${3:20%})", true, "0");
            add(completion, "lab(...)", FUNCTION, "CIE Lab (밝기 0~100, a, b)", "lab(${1:70}, ${2:-30}, ${3:10})", true, "0");
            add(completion, "lch(...)", FUNCTION, "CIE LCh (밝기, 채도, 색상)", "lch(${1:70}, ${2:30}, ${3:160deg})", true, "0");
            add(completion, "oklab(...)", FUNCTION, "Oklab (밝기 0~100%, a, b)", "oklab(${1:70%}, ${2:-0.1}, ${3:0.03})", true, "0");
            add(completion, "oklch(...)", FUNCTION, "Oklch (밝기, 채도, 색상)", "oklch(${1:70%}, ${2:0.12}, ${3:160deg})", true, "0");
            for (const auto& name : symbols.theme_colors) {
                add(completion, "theme." + name, COLOR, "테마 색", "theme." + name, false, "1");
            }
        }

        // 그림, 비디오, 오디오 자리. type은 image, video, audio이고 그 파일 형식에 맞는 묶음 안 파일과 이름 붙인 값을 보인다
        void add_files(Completion& completion, const middleend::Symbols& symbols, const std::string& type) {
            static const std::map<std::string, std::vector<std::string>> extensions = {
                {"image", {".png", ".jpg", ".jpeg", ".gif", ".bmp"}}, {"video", {".mp4", ".webm"}}, {"audio", {".mp3", ".wav", ".m4a"}},
            };
            const auto fits = [&](const std::string& entry) {
                std::string lower = entry;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto& list = extensions.at(type);
                return std::any_of(list.begin(), list.end(), [&](const std::string& extension) { return lower.ends_with(extension); });
            };
            for (const auto& [name, constant_type] : symbols.constants) {
                if (constant_type == type) {
                    add(completion, name, VARIABLE, type, name, false, "0");
                }
            }
            for (const auto& asset : symbols.assets) {
                for (const auto& entry : asset.entries) {
                    if (!fits(entry)) {
                        continue;
                    }
                    if (!asset.has_by) {
                        add(completion, "asset(\"" + entry + "\")", FILE, asset.written, "asset(\"" + entry + "\")", false, "1");
                    }
                    for (const auto& space : asset.namespaces) {
                        add(completion, "asset(\"" + space + "." + entry + "\")", FILE, asset.written, "asset(\"" + space + "." + entry + "\")", false, "1");
                    }
                    for (const auto& [alias, file] : asset.aliases) {
                        if (file == entry) {
                            add(completion, "asset(\"" + alias + "\")", FILE, asset.written, "asset(\"" + alias + "\")", false, "1");
                        }
                    }
                }
            }
            add(completion, "file(...)", FUNCTION, "디스크의 파일 (x.tasset/이름이면 묶음 안의 파일)", "file(\"${1}\")", true, "2");
            add(completion, "asset(...)", FUNCTION, "asset으로 불러온 묶음의 파일", "asset(\"${1}\")", true, "2");
        }

        // 속성 값 자리
        void complete_value(Completion& completion, const middleend::Symbols& symbols, const std::string& type, const std::string& value) {
            if (value.ends_with("theme.") || (value.size() > 6 && value.rfind("theme.") != std::string::npos && std::all_of(value.begin() + static_cast<long>(value.rfind("theme.") + 6), value.end(), is_word))) {
                for (const auto& name : symbols.theme_colors) {
                    add(completion, name, COLOR, "테마 색");
                }
                return;
            }
            if (const auto it = symbols.enums.find(type); it != symbols.enums.end()) {
                for (const auto& member : it->second) {
                    add(completion, member, ENUM_MEMBER, type);
                }
                return;
            }
            if (type == "bool") {
                add(completion, "true", KEYWORD, "bool");
                add(completion, "false", KEYWORD, "bool");
            } else if (type == "color") {
                add_colors(completion, symbols);
            } else if (type == "color, gradient, pattern or image") {
                add_colors(completion, symbols);
                add(completion, "linear(...)", FUNCTION, "선형 그라데이션 (각도, 색, 색, ...)", "linear(${1:90}, ${2:hex(FFFFFF)}, ${3:hex(000000)})", true, "0");
                add(completion, "radial(...)", FUNCTION, "원형 그라데이션", "radial(${1:hex(FFFFFF)}, ${2:hex(000000)})", true, "0");
                add(completion, "image(...)", FUNCTION, "그림", "image(\"${1}\")", true, "0");
                add_files(completion, symbols, "image");
            } else if (type == "image" || type == "video" || type == "audio") {
                add_files(completion, symbols, type);
            } else if (type == "font") {
                add(completion, "font(...)", FUNCTION, "폰트", "font(\"${1:맑은 고딕}\")", true, "0");
                add(completion, "theme.heading_font", VARIABLE, "테마의 제목 폰트", "theme.heading_font", false, "1");
                add(completion, "theme.body_font", VARIABLE, "테마의 본문 폰트", "theme.body_font", false, "1");
            } else if (type == "text") {
                add(completion, "style(...)", FUNCTION, "이 글자에만 쓰는 서식", "(style(${1:font-weight = bold}) \"${2}\")", true, "0");
                for (const char* list : {"bullets", "numbers", "dashes", "paragraphs"}) {
                    add(completion, list, KEYWORD, "목록", std::string(list) + " [\"${1}\"]", true, "1");
                }
                for (const auto& [name, parameters] : symbols.styles) {
                    std::string insert = name;
                    if (!parameters.empty()) {
                        insert += "(";
                        for (std::size_t i = 0; i < parameters.size(); ++i) {
                            insert += (i > 0 ? ", " : "") + placeholder(symbols, {"", parameters[i]}, static_cast<int>(i + 1));
                        }
                        insert += ")";
                    }
                    add(completion, name, STRUCT, parameters.empty() ? "style" : "style (" + std::to_string(parameters.size()) + "개 매개변수)", insert, !parameters.empty(), "2");
                }
            } else if (type == "layout") {
                for (const auto& [name, master] : symbols.masters) {
                    for (const auto& layout : master.cases) {
                        std::string insert = name;
                        if (!master.parameters.empty()) {
                            insert += "(";
                            for (std::size_t i = 0; i < master.parameters.size(); ++i) {
                                insert += (i > 0 ? ", " : "") + placeholder(symbols, {"", master.parameters[i]}, static_cast<int>(i + 1));
                            }
                            insert += ")";
                        }
                        add(completion, name + "." + layout, CLASS, "layout", insert + "." + layout, !master.parameters.empty());
                    }
                }
            } else if (type == "link" || type == "action") {
                for (const char* jump : {"next_slide", "previous_slide", "first_slide", "last_slide", "last_viewed_slide", "end_show"}) {
                    add(completion, jump, ENUM_MEMBER, "slide 이동");
                }
                add(completion, "slide(...)", FUNCTION, "n번째 slide", "slide(${1:1})", true);
                if (type == "action") {
                    add(completion, "run(...)", FUNCTION, "JS 함수 실행 (html, web)", "run(\"${1:onClick}\"$2)", true);
                    add(completion, "program(...)", FUNCTION, "프로그램 실행 (pptx)", "program(\"${1:app.exe}\")", true);
                    add(completion, "macro(...)", FUNCTION, "매크로 실행 (pptx)", "macro(\"${1:Module1.Macro1}\")", true);
                    add(completion, "action_file(...)", FUNCTION, "파일 열기", "action_file(\"${1:report.pdf}\")", true);
                }
            }
        }

        // 문장을 시작하는 자리
        void complete_statement(Completion& completion, const middleend::Symbols& symbols, const std::string& kind, const std::string& name, const std::string& block) {
            const auto keyword = [&](const char* word, const char* detail, const std::string& insert, const char* sort = "1") {
                add(completion, word, KEYWORD, detail, insert, true, sort);
            };
            if (kind.empty()) {
                keyword("slide", "슬라이드", "slide {\n\t$0\n}");
                keyword("template", "template", "template ${1:name} {\n\tvar ${2:int} ${3:x};\n\t$0\n}");
                keyword("style", "이름 있는 style", "style ${1:name} {\n\t${2:font-weight} = ${3:bold};\n}");
                keyword("object", "object", "object ${1:name} {\n\t$0\n}");
                keyword("enum", "enum", "enum ${1:name} { ${2:a}, ${3:b} }");
                keyword("target", "출력 파일", "target ${1:out} { path = \"${2:out.pptx}\"; type = ${3|pptx,html,web|}; }");
                keyword("master", "master", "master ${1:name} {\n\tcase ${2:title} {\n\t\t$0\n\t}\n}");
                keyword("theme", "theme", "theme ${1:name} {\n\t$0\n}");
                keyword("section", "구역", "section \"${1}\";");
                keyword("#include", "파일 불러오기", "#include <${1:std/stddef}>");
                keyword("asset", "그림, 미디어 묶음(.tasset) 불러오기", "asset \"${1:slides.tasset}\";");
                keyword("image", "그림 파일에 이름 붙이기", "image ${1:name} = ${2:asset(\"${3}\")};");
                keyword("video", "비디오 파일에 이름 붙이기", "video ${1:name} = ${2:asset(\"${3}\")};");
                keyword("audio", "오디오 파일에 이름 붙이기", "audio ${1:name} = ${2:asset(\"${3}\")};");
                return;
            }
            if (kind == "slide" || kind == "case" || kind == "group" || kind == "template") {
                keyword("put", "개체 넣기", "put ${1:text_box} {\n\t$0\n}", "0");
                keyword("group", "묶기", "group {\n\t$0\n}");
                if (kind != "slide") {
                    keyword("if", "조건", "if (${1}) {\n\t$0\n}");
                    keyword("for", "반복", "for (${1:int} ${2:i} in ${3:1}..${4:3}) {\n\t$0\n}");
                }
                if (kind == "template") {
                    keyword("var", "template이 받는 값", "var ${1:int} ${2:name};", "0");
                }
                if (kind == "slide") {
                    keyword("transition", "전환 효과", "transition ${1:fade} ${2:0.5s};");
                    keyword("animate", "애니메이션", "animate ${1:name} ${2|enter,emphasis,exit,move,media|} ${3:fade};");
                    keyword("comment", "발표자 메모", "comment \"${1}\";");
                    keyword("review", "검토 메모", "review { text = \"${1}\"; author = \"${2}\"; x = ${3:0px}; y = ${4:0px}; }");
                }
            }
            // slide에 바로 적은 put, group 블록 안에서는 대상 없이 그 개체에 애니메이션을 건다
            if (kind == "put" || kind == "group") {
                keyword("animate", "이 개체의 애니메이션", "animate ${1|enter,emphasis,exit,move,media|} ${2:fade};", "3");
            }
            if (kind == "master") {
                keyword("case", "layout", "case ${1:name} {\n\t$0\n}");
                keyword("theme", "이 master의 theme", "theme = ${1};");
            }
            if (kind == "object") {
                keyword("var", "속성", "var ${1:int} ${2:name};");
            }
            if (kind == "theme") {
                for (const auto& color : symbols.theme_colors) {
                    add(completion, color, PROPERTY, "테마 색", color + " = hex(${1:000000});", true);
                }
            }
            const auto assigned = assigned_in(block);
            for (const auto& var : properties_of(symbols, kind, name)) {
                if (!assigned.contains(var.name)) {
                    add_property(completion, symbols, var, var.required ? "0" : "2");
                }
            }
        }

        void complete_packages(Completion& completion, const std::filesystem::path& packages_dir) {
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(packages_dir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (it->path().extension() == ".tlide") {
                    auto relative = it->path().lexically_relative(packages_dir).replace_extension().generic_u8string();
                    const std::string name(relative.begin(), relative.end());
                    add(completion, name, FILE, "package", name + ">");
                }
            }
        }
    }

    Completion complete(const middleend::Symbols& symbols, const std::string& text, std::size_t offset, const std::filesystem::path& packages_dir) {
        Completion completion;
        offset = std::min(offset, text.size());
        // 바꿀 범위는 커서 앞의 낱말
        completion.replace_from = offset;
        while (completion.replace_from > 0 && is_word(text[completion.replace_from - 1])) {
            --completion.replace_from;
        }
        const Context context = scan(text, offset);
        if (context.in_comment || context.in_string) {
            return completion;
        }
        const std::string statement = text.substr(context.statement, offset - context.statement);
        // 앞 공백만 뗀다. 뒤 공백은 'put '처럼 다음 낱말을 기다리는 자리를 알려 준다
        const auto first = statement.find_first_not_of(" \t\r\n");
        const std::string prefix = first == std::string::npos ? "" : statement.substr(first);

        // #include <...
        if (const auto include = prefix.rfind("#include"); include == 0) {
            const auto open = statement.rfind('<');
            if (open != std::string::npos) {
                completion.replace_from = context.statement + open + 1;
                complete_packages(completion, packages_dir);
            }
            return completion;
        }

        // style(...) 안: 속성 이름, 또는 속성 = 뒤의 값
        if (!context.opens.empty() && context.opens.back().bracket == '(') {
            const Open& open = context.opens.back();
            std::size_t word_end = open.position;
            while (word_end > 0 && text[word_end - 1] == ' ') {
                --word_end;
            }
            std::size_t word_begin = word_end;
            while (word_begin > 0 && is_word(text[word_begin - 1])) {
                --word_begin;
            }
            if (text.substr(word_begin, word_end - word_begin) == "style") {
                const std::string arguments = text.substr(open.position + 1, offset - open.position - 1);
                const std::string current = trim(arguments.substr(arguments.rfind(',') == std::string::npos ? 0 : arguments.rfind(',') + 1));
                if (const auto equal = current.find('='); equal != std::string::npos) {
                    const std::string name = trim(current.substr(0, equal));
                    for (const auto& property : symbols.style_properties) {
                        if (property.name == name) {
                            complete_value(completion, symbols, property.type, trim(current.substr(equal + 1)));
                        }
                    }
                } else {
                    for (const auto& property : symbols.style_properties) {
                        add(completion, property.name, PROPERTY, property.type, property.name + " = ");
                    }
                }
                return completion;
            }
            return completion;
        }

        // 지금 있는 블록. if, for, else 블록은 그 바깥 블록을 따른다
        std::string kind;
        std::string name;
        std::size_t block_begin = 0;
        for (auto it = context.opens.rbegin(); it != context.opens.rend(); ++it) {
            if (it->bracket != '{') {
                continue;
            }
            const auto [first, second] = header_words(text.substr(it->statement, it->position - it->statement));
            if (block_begin == 0) {
                block_begin = it->position + 1;
            }
            if (first == "if" || first == "for" || first == "else") {
                continue;
            }
            kind = first;
            name = second;
            break;
        }
        const std::string block = block_begin > 0 ? text.substr(block_begin, offset - block_begin) : "";

        std::smatch match;
        static const std::regex put_name(R"(^put\s+[A-Za-z_]?\w*$)");
        static const std::regex transition_kind(R"(^transition\s+[A-Za-z_]?\w*$)");
        static const std::regex transition_option(R"(^transition\s+([A-Za-z_]\w*)\.\w*$)");
        static const std::regex var_type(R"(^var\s+[A-Za-z_]?\w*$)");
        static const std::regex assignment(R"(^([A-Za-z_][\w-]*)\s*=\s*([\s\S]*)$)");
        static const std::regex word(R"(^#?[A-Za-z_]?[\w-]*$)");

        if (std::regex_match(prefix, put_name)) {
            const auto put = [&](const std::string& target, const std::vector<middleend::SymbolVar>& vars, bool object) {
                std::string insert = target + " { ";
                int index = 1;
                for (const auto& var : vars) {
                    if (var.required) {
                        insert += var.name + " = " + placeholder(symbols, var, index++) + "; ";
                    }
                }
                add(completion, target, object ? CLASS : MODULE, object ? "object" : "template", insert + "}", true, object ? "0" : "1");
            };
            for (const auto& [target, vars] : symbols.objects) {
                put(target, vars, true);
            }
            for (const auto& [target, vars] : symbols.templates) {
                put(target, vars, false);
            }
        } else if (std::regex_match(prefix, transition_kind)) {
            for (const auto& [transition, options] : symbols.transitions) {
                add(completion, transition, ENUM_MEMBER, options.empty() ? "전환" : "전환 (." + options.front() + " 등)");
            }
        } else if (std::regex_match(prefix, match, transition_option)) {
            if (const auto it = symbols.transitions.find(match[1]); it != symbols.transitions.end()) {
                for (const auto& option : it->second) {
                    add(completion, option, ENUM_MEMBER, "옵션");
                }
            }
        } else if (std::regex_match(prefix, var_type)) {
            for (const char* type : {"int", "float", "string", "text", "color", "bool", "ref", "image", "video", "audio", "font"}) {
                add(completion, type, KEYWORD, "타입", "", false, "0");
            }
            for (const auto& [enumeration, members] : symbols.enums) {
                add(completion, enumeration, VARIABLE, "enum", "", false, "1");
            }
        } else if (std::regex_match(prefix, match, assignment)) {
            const std::string property = match[1];
            for (const auto& var : properties_of(symbols, kind, name)) {
                if (var.name == property) {
                    complete_value(completion, symbols, var.type, match[2]);
                    break;
                }
            }
            if (kind == "style" || kind.empty()) {
                for (const auto& var : symbols.style_properties) {
                    if (var.name == property && kind == "style") {
                        complete_value(completion, symbols, var.type, match[2]);
                    }
                }
            }
        } else if (std::regex_match(prefix, word)) {
            if (!prefix.empty() && prefix[0] == '#') {
                --completion.replace_from; // '#'까지 바꾼다
            }
            complete_statement(completion, symbols, kind, name, block);
        }
        return completion;
    }
}

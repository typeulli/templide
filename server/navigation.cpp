#include "navigation.h"

#include "../frontend/lexor.h"
#include "../frontend/parser.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace templide::server {
    namespace {
        namespace ast = parser::ast;
        using lexor::Token;

        std::string trim(const std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? "" : text.substr(first, last - first + 1);
        }

        std::size_t indent_of(const std::string& line) {
            const auto first = line.find_first_not_of(" \t");
            return first == std::string::npos ? line.size() : first;
        }

        // 선언의 원문. 첫 줄은 선언의 시작부터라 들여쓰기가 없으므로 나머지 줄에서 닫는 '}'의 들여쓰기만큼 뺀다.
        // 줄이 많으면 앞부분과 닫는 줄만 남긴다
        std::string dedent(const std::string& text, std::size_t max_lines = 14) {
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (true) {
                const auto newline = text.find('\n', start);
                std::string line = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
                if (newline == std::string::npos) {
                    break;
                }
                start = newline + 1;
            }
            const std::size_t cut = lines.size() > 1 ? indent_of(lines.back()) : 0;
            const auto strip = [&](const std::string& line) { return line.substr(std::min(cut, indent_of(line))); };
            std::string result = lines[0];
            for (std::size_t i = 1; i < lines.size(); ++i) {
                if (i == max_lines && lines.size() > max_lines + 1) {
                    result += "\n    ...\n" + strip(lines.back());
                    break;
                }
                result += "\n" + strip(lines[i]);
            }
            return result;
        }

        const std::set<std::string>& keywords() {
            static const std::set<std::string> words = {
                "slide", "put", "template", "style", "object", "var", "master", "case", "target", "if", "else", "for", "in", "enum",
                "transition", "animate", "group", "as", "theme", "section", "review", "comment", "bullets", "numbers", "dashes",
                "paragraphs", "true", "false", "color", "include", "delay", "order", "on_click", "with_previous", "after_previous",
                "int", "float", "string", "text", "bool", "ref", "asset", "by", "default", "image", "video", "audio", "font",
            };
            return words;
        }

        // 이름을 바꿀 때 서로 겹치면 안 되는 이름들의 묶음
        std::string namespace_of(const NavSymbol& symbol) {
            switch (symbol.kind) {
                case SymbolKind::TEMPLATE:
                case SymbolKind::OBJECT: return "put";
                case SymbolKind::STYLE:
                case SymbolKind::COLOR: return "style";
                case SymbolKind::ENUM: return "enum";
                case SymbolKind::MASTER: return "master";
                case SymbolKind::THEME: return "theme";
                case SymbolKind::TARGET: return "target";
                case SymbolKind::FILE: return "file";
                case SymbolKind::CONSTANT: return "constant";
                case SymbolKind::ALIAS: return "alias:" + std::to_string(symbol.block);
                case SymbolKind::LOOP: return "loop:" + std::to_string(symbol.block);
                default: return "member:" + std::to_string(symbol.owner);
            }
        }

        struct Scope {
            std::vector<std::pair<std::string, int>> vars; // 뒤의 것이 앞의 것을 가린다
            const std::map<std::string, int>* aliases = nullptr;
            int block = -1;

            int find(const std::string& name) const {
                for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
                    if (it->first == name) {
                        return it->second;
                    }
                }
                return -1;
            }
        };

        struct Builder {
            NavIndex& index;
            const middleend::Symbols& symbols;
            std::filesystem::path packages_dir;

            const NavSource* source = nullptr;
            std::vector<std::size_t> line_starts;
            int blocks = 0;

            std::map<std::string, int> puts; // template, object
            std::map<std::string, int> styles; // style, color
            std::map<std::string, int> enums;
            std::map<std::string, int> masters;
            std::map<std::string, int> themes;
            std::map<std::string, int> targets;
            std::map<std::string, int> files;
            std::map<std::string, int> constants; // image, video, audio
            std::map<std::pair<std::string, std::size_t>, int> declared; // (경로, 이름의 위치) -> 기호
            std::map<int, std::map<std::string, int>> members; // 선언 -> 변수, 값, 매개변수, 레이아웃
            std::map<int, std::vector<int>> parameters;       // style, master -> 매개변수 차례대로

            // 위치

            void open(const NavSource& next) {
                source = &next;
                line_starts = {0};
                for (std::size_t i = 0; i < next.text->size(); ++i) {
                    if ((*next.text)[i] == '\n') {
                        line_starts.push_back(i + 1);
                    }
                }
            }

            std::size_t offset(std::string_view view) const {
                return static_cast<std::size_t>(view.data() - source->text->data());
            }

            std::size_t line_of(std::size_t at) const {
                return static_cast<std::size_t>(std::upper_bound(line_starts.begin(), line_starts.end(), at) - line_starts.begin());
            }

            std::string text_of(std::string_view view) const {
                return std::string(view);
            }

            // 선언 바로 앞(빈 줄 없이)의 /* */ 설명
            std::string doc_before(std::size_t at) const {
                const std::string& text = *source->text;
                std::size_t i = at;
                int newlines = 0;
                while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) {
                    newlines += text[i - 1] == '\n';
                    --i;
                }
                if (newlines > 1 || i < 2 || text.compare(i - 2, 2, "*/") != 0) {
                    return "";
                }
                const auto open = text.rfind("/*", i - 2);
                if (open == std::string::npos) {
                    return "";
                }
                std::string inner = text.substr(open + 2, i - 2 - open - 2);
                std::string result;
                std::size_t start = 0;
                while (start <= inner.size()) {
                    const auto newline = inner.find('\n', start);
                    std::string line = trim(inner.substr(start, newline == std::string::npos ? std::string::npos : newline - start));
                    if (line.starts_with("* ")) {
                        line = line.substr(2);
                    } else if (line == "*") {
                        line.clear();
                    }
                    result += (result.empty() ? "" : "\n") + line;
                    if (newline == std::string::npos) {
                        break;
                    }
                    start = newline + 1;
                }
                return trim(result);
            }

            // 기록

            int add(SymbolKind kind, const std::string& name, const Token& token, std::string code, std::size_t declaration) {
                NavSymbol symbol;
                symbol.kind = kind;
                symbol.name = name;
                symbol.path = source->path;
                symbol.begin = offset(token.value);
                symbol.end = symbol.begin + token.value.size();
                symbol.line = line_of(symbol.begin);
                symbol.file = display_file(source->path, source->package);
                symbol.code = std::move(code);
                symbol.doc = doc_before(declaration);
                symbol.package = source->package;
                index.symbols.push_back(std::move(symbol));
                const int id = static_cast<int>(index.symbols.size()) - 1;
                declared.try_emplace({source->path, index.symbols.back().begin}, id);
                index.occurrences.push_back({source->path, index.symbols.back().begin, index.symbols.back().end, id, true, ""});
                return id;
            }

            void use(const Token& token, int symbol) {
                if (symbol < 0 || token.value.data() == nullptr) {
                    return;
                }
                const std::size_t begin = offset(token.value);
                index.occurrences.push_back({source->path, begin, begin + token.value.size(), symbol, false, ""});
            }

            void use_builtin(const Token& token, const std::string& hover) {
                const std::size_t begin = offset(token.value);
                index.occurrences.push_back({source->path, begin, begin + token.value.size(), -1, false, hover});
            }

            std::string display_file(const std::string& path, bool package) const {
                const std::filesystem::path file(std::u8string(path.begin(), path.end()));
                if (package) {
                    std::filesystem::path relative = file.lexically_relative(packages_dir);
                    if (!relative.empty() && *relative.begin() != "..") {
                        relative.replace_extension();
                        const auto name = relative.generic_u8string();
                        return "<" + std::string(name.begin(), name.end()) + ">";
                    }
                }
                const auto name = file.filename().u8string();
                return std::string(name.begin(), name.end());
            }

            // 내장 속성(common, text, style, slide 속성)의 정보
            static std::string builtin_hover(const middleend::SymbolVar& var, const std::string& where) {
                std::string code = var.name + ": " + var.type;
                if (!var.default_value.empty()) {
                    code += " = " + var.default_value;
                }
                return "```tlide\n" + code + "\n```\n" + where;
            }

            bool builtin(const Token& token, const std::vector<middleend::SymbolVar>& list, const std::string& where, std::string* type = nullptr) {
                const std::string name(token.value);
                for (const auto& var : list) {
                    if (var.name == name) {
                        use_builtin(token, builtin_hover(var, where));
                        if (type != nullptr) {
                            *type = var.type;
                        }
                        return true;
                    }
                }
                return false;
            }

            // 1. 선언

            void declare_file(const ast::ASTFile* file) {
                declare_all(file->body);
            }

            void declare_all(const std::vector<ast::ASTNode*>& body) {
                for (const auto* statement : body) {
                    declare(statement);
                }
            }

            void declare_branch(const ast::ASTNode* branch) {
                while (branch != nullptr) {
                    if (branch->type == ast::ELSE_IF) {
                        const auto* node = static_cast<const ast::ASTElseIf*>(branch);
                        declare_all(node->body);
                        branch = node->branch;
                    } else {
                        declare_all(static_cast<const ast::ASTElse*>(branch)->body);
                        branch = nullptr;
                    }
                }
            }

            void declare(const ast::ASTNode* statement) {
                switch (statement->type) {
                    case ast::IF: {
                        const auto* node = static_cast<const ast::ASTIf*>(statement);
                        declare_all(node->body);
                        declare_branch(node->branch);
                        break;
                    }
                    case ast::ENUM: {
                        const auto* node = static_cast<const ast::ASTEnum*>(statement);
                        std::string code = "enum " + node->name + " { ";
                        for (std::size_t i = 0; i < node->members.size(); ++i) {
                            if (i == 12) {
                                code += ", ... (" + std::to_string(node->members.size()) + "개)";
                                break;
                            }
                            code += (i > 0 ? ", " : "") + node->members[i]->name;
                        }
                        code += " }";
                        const int id = add(SymbolKind::ENUM, node->name, node->name_token, code, offset(node->span));
                        enums.try_emplace(node->name, id);
                        for (const auto* member : node->members) {
                            const int value = add(SymbolKind::ENUM_MEMBER, member->name, member->token, node->name + "." + member->name, offset(member->token.value));
                            index.symbols[value].owner = id;
                            index.symbols[value].doc.clear();
                            members[id].try_emplace(member->name, value);
                        }
                        break;
                    }
                    case ast::STYLE: {
                        const auto* node = static_cast<const ast::ASTStyle*>(statement);
                        const bool color = node->token.value == "color";
                        const int id = add(color ? SymbolKind::COLOR : SymbolKind::STYLE, node->name, node->name_token, dedent(text_of(node->span)), offset(node->span));
                        styles.try_emplace(node->name, id);
                        declare_parameters(id, node->parameters);
                        break;
                    }
                    case ast::TEMPLATE:
                    case ast::OBJECT: {
                        const bool is_template = statement->type == ast::TEMPLATE;
                        const auto& name = is_template ? static_cast<const ast::ASTTemplate*>(statement)->name : static_cast<const ast::ASTObject*>(statement)->name;
                        const Token& token = is_template ? static_cast<const ast::ASTTemplate*>(statement)->name_token : static_cast<const ast::ASTObject*>(statement)->name_token;
                        const auto& body = is_template ? static_cast<const ast::ASTTemplate*>(statement)->body : static_cast<const ast::ASTObject*>(statement)->body;
                        std::string code = std::string(is_template ? "template " : "object ") + name + " {";
                        bool any = false;
                        for (const auto* inner : body) {
                            if (inner->type == ast::VAR) {
                                code += "\n    " + trim(text_of(inner->span));
                                any = true;
                            }
                        }
                        code += any ? "\n}" : " }";
                        const int id = add(is_template ? SymbolKind::TEMPLATE : SymbolKind::OBJECT, name, token, code, offset(statement->span));
                        puts.try_emplace(name, id);
                        for (const auto* inner : body) {
                            if (inner->type != ast::VAR) {
                                continue;
                            }
                            const auto* var = static_cast<const ast::ASTVar*>(inner);
                            const int member = add(SymbolKind::VAR, var->name->name, var->name->token, trim(text_of(var->span)), offset(var->span));
                            index.symbols[member].owner = id;
                            index.symbols[member].type = var->type_name->name;
                            index.symbols[member].required = var->default_value == nullptr;
                            members[id].try_emplace(var->name->name, member);
                        }
                        break;
                    }
                    case ast::MASTER: {
                        const auto* node = static_cast<const ast::ASTMaster*>(statement);
                        std::string header = text_of(node->span);
                        header = trim(header.substr(0, header.find('{')));
                        std::string cases;
                        for (const auto* inner : node->body) {
                            cases += (cases.empty() ? "" : ", ") + static_cast<const ast::ASTMasterCase*>(inner)->name;
                        }
                        const int id = add(SymbolKind::MASTER, node->name, node->name_token, header + " { " + (cases.empty() ? "" : "case " + cases + " ") + "}", offset(node->span));
                        masters.try_emplace(node->name, id);
                        declare_parameters(id, node->parameters);
                        for (const auto* inner : node->body) {
                            const auto* layout = static_cast<const ast::ASTMasterCase*>(inner);
                            const int member = add(SymbolKind::LAYOUT, layout->name, layout->name_token, node->name + "." + layout->name, offset(layout->span));
                            index.symbols[member].owner = id;
                            members[id].try_emplace(layout->name, member);
                        }
                        break;
                    }
                    case ast::THEME: {
                        const auto* node = static_cast<const ast::ASTTheme*>(statement);
                        themes.try_emplace(node->name, add(SymbolKind::THEME, node->name, node->name_token, dedent(text_of(node->span)), offset(node->span)));
                        break;
                    }
                    case ast::TARGET: {
                        const auto* node = static_cast<const ast::ASTTarget*>(statement);
                        targets.try_emplace(node->name, add(SymbolKind::TARGET, node->name, node->name_token, dedent(text_of(node->span)), offset(node->span)));
                        break;
                    }
                    case ast::CONSTANT: {
                        const auto* node = static_cast<const ast::ASTConstant*>(statement);
                        const int id = add(SymbolKind::CONSTANT, node->name->name, node->name->token, trim(text_of(node->span)), offset(node->span));
                        index.symbols[id].type = node->type_name->name;
                        constants.try_emplace(node->name->name, id);
                        break;
                    }
                    default:
                        break;
                }
            }

            void declare_parameters(int owner, const std::vector<ast::ASTParameter*>& list) {
                for (const auto* parameter : list) {
                    const int id = add(SymbolKind::PARAMETER, parameter->name->name, parameter->name->token, parameter->type_name->name + " " + parameter->name->name, offset(parameter->name->token.value));
                    index.symbols[id].owner = owner;
                    index.symbols[id].type = parameter->type_name->name;
                    index.symbols[id].doc.clear();
                    members[owner].try_emplace(parameter->name->name, id);
                    parameters[owner].push_back(id);
                }
            }

            // 2. 이름을 쓰는 곳

            void resolve_file(const ast::ASTFile* file, const std::vector<NavSource>& sources) {
                for (const auto* statement : file->body) {
                    resolve_top(statement, sources);
                }
            }

            void resolve_top_all(const std::vector<ast::ASTNode*>& body, const std::vector<NavSource>& sources) {
                for (const auto* statement : body) {
                    resolve_top(statement, sources);
                }
            }

            void resolve_top(const ast::ASTNode* statement, const std::vector<NavSource>& sources) {
                switch (statement->type) {
                    case ast::INCLUDE:
                        include(static_cast<const ast::ASTInclude*>(statement), sources);
                        break;
                    case ast::IF: {
                        const auto* node = static_cast<const ast::ASTIf*>(statement);
                        expression(node->condition, Scope{}, "");
                        resolve_top_all(node->body, sources);
                        for (const ast::ASTNode* branch = node->branch; branch != nullptr;) {
                            if (branch->type == ast::ELSE_IF) {
                                const auto* next = static_cast<const ast::ASTElseIf*>(branch);
                                expression(next->condition, Scope{}, "");
                                resolve_top_all(next->body, sources);
                                branch = next->branch;
                            } else {
                                resolve_top_all(static_cast<const ast::ASTElse*>(branch)->body, sources);
                                branch = nullptr;
                            }
                        }
                        break;
                    }
                    case ast::STYLE: {
                        const auto* node = static_cast<const ast::ASTStyle*>(statement);
                        const int id = find_declaration(node->name_token);
                        Scope scope = parameter_scope(id, node->parameters);
                        for (const auto* inner : node->body) {
                            const auto* assign = static_cast<const ast::ASTStyleAssign*>(inner);
                            std::string type;
                            // color NAME VALUE;는 원문에 없는 color = 대입을 만든다
                            if (assign->identifier->token.value.data() != node->token.value.data()) {
                                builtin(assign->identifier->token, symbols.style_properties, "글자 style 속성", &type);
                            }
                            expression(assign->expression, scope, type);
                        }
                        break;
                    }
                    case ast::TEMPLATE: {
                        const auto* node = static_cast<const ast::ASTTemplate*>(statement);
                        const int id = find_declaration(node->name_token);
                        Scope scope;
                        for (const auto* inner : node->body) {
                            if (inner->type != ast::VAR) {
                                continue;
                            }
                            const auto* var = static_cast<const ast::ASTVar*>(inner);
                            var_declaration(var);
                            if (id >= 0) {
                                scope.vars.emplace_back(var->name->name, members[id].at(var->name->name));
                            }
                        }
                        block(node->body, scope);
                        break;
                    }
                    case ast::OBJECT:
                        for (const auto* inner : static_cast<const ast::ASTObject*>(statement)->body) {
                            if (inner->type == ast::VAR) {
                                var_declaration(static_cast<const ast::ASTVar*>(inner));
                            }
                        }
                        break;
                    case ast::MASTER: {
                        const auto* node = static_cast<const ast::ASTMaster*>(statement);
                        const int id = find_declaration(node->name_token);
                        const Scope scope = parameter_scope(id, node->parameters);
                        for (const auto* inner : node->properties) {
                            const auto* assign = static_cast<const ast::ASTAssign*>(inner);
                            if (assign->name->name == "theme" && assign->expression->type == ast::NAME) {
                                const auto* name = static_cast<const ast::ASTName*>(assign->expression);
                                use(name->token, lookup(themes, name->name));
                            } else {
                                expression(assign->expression, scope, "");
                            }
                        }
                        for (const auto* inner : node->body) {
                            block(static_cast<const ast::ASTMasterCase*>(inner)->body, scope);
                        }
                        break;
                    }
                    case ast::SLIDE:
                        block(static_cast<const ast::ASTSlide*>(statement)->body, Scope{});
                        break;
                    case ast::TARGET:
                        for (const auto* inner : static_cast<const ast::ASTTarget*>(statement)->body) {
                            expression(static_cast<const ast::ASTAssign*>(inner)->expression, Scope{}, "");
                        }
                        break;
                    case ast::THEME:
                        for (const auto* inner : static_cast<const ast::ASTTheme*>(statement)->body) {
                            expression(static_cast<const ast::ASTAssign*>(inner)->expression, Scope{}, "");
                        }
                        break;
                    case ast::SECTION:
                        expression(static_cast<const ast::ASTSection*>(statement)->name, Scope{}, "text");
                        break;
                    case ast::CONSTANT: {
                        const auto* node = static_cast<const ast::ASTConstant*>(statement);
                        expression(node->expression, Scope{}, node->type_name->name);
                        break;
                    }
                    default:
                        break;
                }
            }

            // 선언의 이름 토큰으로 1단계에서 만든 기호를 찾는다
            int find_declaration(const Token& token) const {
                const auto it = declared.find({source->path, offset(token.value)});
                return it == declared.end() ? -1 : it->second;
            }

            static int lookup(const std::map<std::string, int>& table, const std::string& name) {
                const auto it = table.find(name);
                return it == table.end() ? -1 : it->second;
            }

            Scope parameter_scope(int owner, const std::vector<ast::ASTParameter*>& list) {
                Scope scope;
                for (const auto* parameter : list) {
                    use(parameter->type_name->token, lookup(enums, parameter->type_name->name));
                    if (owner >= 0) {
                        scope.vars.emplace_back(parameter->name->name, members[owner].at(parameter->name->name));
                    }
                }
                return scope;
            }

            void var_declaration(const ast::ASTVar* var) {
                use(var->type_name->token, lookup(enums, var->type_name->name));
                if (var->default_value != nullptr) {
                    expression(var->default_value, Scope{}, var->type_name->name);
                }
            }

            void include(const ast::ASTInclude* node, const std::vector<NavSource>& sources) {
                const std::string file = node->name + ".tlide";
                const std::filesystem::path here(std::u8string(source->path.begin(), source->path.end()));
                const std::filesystem::path base = node->relative ? here.parent_path() : packages_dir;
                const std::filesystem::path target = (base / std::filesystem::path(std::u8string(file.begin(), file.end()))).lexically_normal();
                std::string path;
                bool package = !node->relative;
                for (const auto& each : sources) {
                    if (std::filesystem::path(std::u8string(each.path.begin(), each.path.end())).lexically_normal() == target) {
                        path = each.path;
                        package = each.package;
                    }
                }
                if (path.empty()) {
                    const auto text = target.u8string();
                    path = std::string(text.begin(), text.end());
                }
                int id = lookup(files, path);
                if (id < 0) {
                    NavSymbol symbol;
                    symbol.kind = SymbolKind::FILE;
                    symbol.name = node->name;
                    symbol.path = path;
                    symbol.line = 1;
                    symbol.file = display_file(path, package);
                    symbol.code = node->relative ? "#include \"" + node->name + "\"" : "#include <" + node->name + ">";
                    symbol.package = true;
                    index.symbols.push_back(std::move(symbol));
                    id = static_cast<int>(index.symbols.size()) - 1;
                    files.emplace(path, id);
                }
                // '#include' 뒤의 <...>나 "..." 부분
                const std::string span(node->span);
                const auto open = span.find_first_of("<\"", 1);
                const std::size_t begin = offset(node->span) + (open == std::string::npos ? 0 : open);
                index.occurrences.push_back({source->path, begin, offset(node->span) + span.size(), id, false, ""});
            }

            // slide, template, case처럼 put이 들어가는 블록. 그 안의 as 이름을 먼저 모은다
            void block(const std::vector<ast::ASTNode*>& body, Scope scope) {
                std::map<std::string, int> aliases;
                const int id = blocks++;
                collect_aliases(body, id, aliases);
                scope.aliases = &aliases;
                scope.block = id;
                statements(body, scope);
            }

            void collect_aliases(const std::vector<ast::ASTNode*>& body, int id, std::map<std::string, int>& aliases) {
                for (const auto* statement : body) {
                    switch (statement->type) {
                        case ast::PUT: {
                            const auto* node = static_cast<const ast::ASTPut*>(statement);
                            if (node->alias != nullptr) {
                                const int alias = add(SymbolKind::ALIAS, node->alias->name, node->alias->token, "put " + node->name + " as " + node->alias->name, offset(node->span));
                                index.symbols[alias].type = node->name;
                                index.symbols[alias].block = id;
                                aliases.try_emplace(node->alias->name, alias);
                            }
                            break;
                        }
                        case ast::GROUP: {
                            const auto* node = static_cast<const ast::ASTGroup*>(statement);
                            if (node->alias != nullptr) {
                                const int alias = add(SymbolKind::ALIAS, node->alias->name, node->alias->token, "group as " + node->alias->name, offset(node->span));
                                index.symbols[alias].type = "group";
                                index.symbols[alias].block = id;
                                aliases.try_emplace(node->alias->name, alias);
                            }
                            collect_aliases(node->body, id, aliases);
                            break;
                        }
                        case ast::IF: {
                            const auto* node = static_cast<const ast::ASTIf*>(statement);
                            collect_aliases(node->body, id, aliases);
                            for (const ast::ASTNode* branch = node->branch; branch != nullptr;) {
                                if (branch->type == ast::ELSE_IF) {
                                    collect_aliases(static_cast<const ast::ASTElseIf*>(branch)->body, id, aliases);
                                    branch = static_cast<const ast::ASTElseIf*>(branch)->branch;
                                } else {
                                    collect_aliases(static_cast<const ast::ASTElse*>(branch)->body, id, aliases);
                                    branch = nullptr;
                                }
                            }
                            break;
                        }
                        case ast::FOR:
                            collect_aliases(static_cast<const ast::ASTFor*>(statement)->body, id, aliases);
                            break;
                        default:
                            break;
                    }
                }
            }

            void statements(const std::vector<ast::ASTNode*>& body, const Scope& scope) {
                for (const auto* statement : body) {
                    switch (statement->type) {
                        case ast::PUT:
                            put(static_cast<const ast::ASTPut*>(statement), scope);
                            break;
                        case ast::GROUP: {
                            const auto* node = static_cast<const ast::ASTGroup*>(statement);
                            statements(node->body, scope);
                            for (const auto* animation : node->animations) {
                                animate(animation, scope);
                            }
                            break;
                        }
                        case ast::IF: {
                            const auto* node = static_cast<const ast::ASTIf*>(statement);
                            expression(node->condition, scope, "");
                            statements(node->body, scope);
                            for (const ast::ASTNode* branch = node->branch; branch != nullptr;) {
                                if (branch->type == ast::ELSE_IF) {
                                    const auto* next = static_cast<const ast::ASTElseIf*>(branch);
                                    expression(next->condition, scope, "");
                                    statements(next->body, scope);
                                    branch = next->branch;
                                } else {
                                    statements(static_cast<const ast::ASTElse*>(branch)->body, scope);
                                    branch = nullptr;
                                }
                            }
                            break;
                        }
                        case ast::FOR: {
                            const auto* node = static_cast<const ast::ASTFor*>(statement);
                            use(node->type_name->token, lookup(enums, node->type_name->name));
                            if (node->range_start != nullptr) {
                                expression(node->range_start, scope, "int");
                                expression(node->range_end, scope, "int");
                            }
                            for (const auto* item : node->items) {
                                expression(item, scope, node->type_name->name);
                            }
                            const int loop = add(SymbolKind::LOOP, node->name->name, node->name->token, node->type_name->name + " " + node->name->name, offset(node->span));
                            index.symbols[loop].type = node->type_name->name;
                            index.symbols[loop].block = scope.block;
                            index.symbols[loop].doc.clear();
                            Scope inner = scope;
                            inner.vars.emplace_back(node->name->name, loop);
                            statements(node->body, inner);
                            break;
                        }
                        case ast::ANIMATE:
                            animate(static_cast<const ast::ASTAnimate*>(statement), scope);
                            break;
                        case ast::COMMENT:
                            expression(static_cast<const ast::ASTComment*>(statement)->expression, scope, "text");
                            break;
                        case ast::TRANSITION: {
                            const auto* node = static_cast<const ast::ASTTransition*>(statement);
                            if (node->duration != nullptr) {
                                expression(node->duration, scope, "float");
                            }
                            break;
                        }
                        case ast::REVIEW:
                            for (const auto* inner : static_cast<const ast::ASTReview*>(statement)->body) {
                                expression(static_cast<const ast::ASTAssign*>(inner)->expression, scope, "");
                            }
                            break;
                        case ast::ASSIGN: {
                            // slide와 case의 속성 (layout, background 등)
                            const auto* assign = static_cast<const ast::ASTAssign*>(statement);
                            std::string type;
                            builtin(assign->name->token, symbols.slide_properties, "slide 속성", &type);
                            expression(assign->expression, scope, type);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            void put(const ast::ASTPut* node, const Scope& scope) {
                const int owner = lookup(puts, node->name);
                use(node->name_token, owner);
                for (const auto* inner : node->body) {
                    const auto* assign = static_cast<const ast::ASTAssign*>(inner);
                    std::string type;
                    const int member = owner >= 0 && members[owner].contains(assign->name->name) ? members[owner].at(assign->name->name) : -1;
                    if (member >= 0) {
                        use(assign->name->token, member);
                        type = index.symbols[member].type;
                    } else if (!builtin(assign->name->token, symbols.common_properties, "모든 개체에 쓸 수 있는 속성", &type)) {
                        builtin(assign->name->token, symbols.text_properties, "글이 있는 개체에 쓸 수 있는 속성", &type);
                    }
                    expression(assign->expression, scope, type);
                }
                for (const auto* animation : node->animations) {
                    animate(animation, scope);
                }
            }

            void animate(const ast::ASTAnimate* node, const Scope& scope) {
                if (node->target != nullptr && scope.aliases != nullptr) {
                    use(node->target->token, lookup(*scope.aliases, node->target->name));
                }
                for (const auto* value : {node->path, node->duration, node->delay, node->order}) {
                    if (value != nullptr) {
                        expression(value, scope, "");
                    }
                }
            }

            // master(값).case 또는 master.case
            bool layout(const ast::ASTMember* member, const Scope& scope) {
                const ast::ASTNode* object = member->object;
                if (object->type != ast::NAME && object->type != ast::CALL) {
                    return false;
                }
                const bool called = object->type == ast::CALL;
                const auto* name = called ? static_cast<const ast::ASTCall*>(object)->name : static_cast<const ast::ASTName*>(object);
                const int master = lookup(masters, name->name);
                if (master < 0) {
                    return false;
                }
                use(name->token, master);
                if (called) {
                    const auto& arguments = static_cast<const ast::ASTCall*>(object)->arguments;
                    const auto& list = parameters[master];
                    for (std::size_t i = 0; i < arguments.size(); ++i) {
                        expression(arguments[i], scope, i < list.size() ? index.symbols[list[i]].type : "");
                    }
                }
                const auto& cases = members[master];
                if (const auto it = cases.find(member->member->name); it != cases.end()) {
                    use(member->member->token, it->second);
                }
                return true;
            }

            void style_call(const ast::ASTCall* call, const Scope& scope) {
                const int style = lookup(styles, call->name->name);
                use(call->name->token, style);
                const auto& list = style >= 0 ? parameters[style] : std::vector<int>{};
                for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                    expression(call->arguments[i], scope, i < list.size() ? index.symbols[list[i]].type : "");
                }
            }

            // 이름 하나. expected는 그 자리에 오는 값의 타입 이름(모르면 빈 문자열)이다
            void name(const ast::ASTName* node, const Scope& scope, const std::string& expected) {
                const std::string& text = node->name;
                if (const int var = scope.find(text); var >= 0) {
                    use(node->token, var);
                    return;
                }
                if (const int expected_enum = lookup(enums, expected); expected_enum >= 0) {
                    if (const auto it = members[expected_enum].find(text); it != members[expected_enum].end()) {
                        use(node->token, it->second);
                        return;
                    }
                }
                if (scope.aliases != nullptr) {
                    if (const int alias = lookup(*scope.aliases, text); alias >= 0) {
                        use(node->token, alias);
                        return;
                    }
                }
                if (const int style = lookup(styles, text); style >= 0) {
                    use(node->token, style);
                    return;
                }
                if (const int constant = lookup(constants, text); constant >= 0) {
                    use(node->token, constant);
                    return;
                }
                for (const auto& [enum_name, id] : enums) {
                    if (const auto it = members[id].find(text); it != members[id].end()) {
                        use(node->token, it->second);
                        return;
                    }
                }
            }

            void expression(const ast::ASTNode* node, const Scope& scope, const std::string& expected) {
                if (node == nullptr) {
                    return;
                }
                switch (node->type) {
                    case ast::NAME:
                        name(static_cast<const ast::ASTName*>(node), scope, expected);
                        break;
                    case ast::TEXT:
                        for (const auto* part : static_cast<const ast::ASTText*>(node)->parts) {
                            expression(part, scope, "text");
                        }
                        break;
                    case ast::LIST:
                        for (const auto* item : static_cast<const ast::ASTList*>(node)->items) {
                            expression(item, scope, "text");
                        }
                        break;
                    case ast::FSTRING:
                        for (const auto* part : static_cast<const ast::ASTFString*>(node)->parts) {
                            expression(part, scope, "");
                        }
                        break;
                    case ast::CALL: {
                        const auto* call = static_cast<const ast::ASTCall*>(node);
                        if (styles.contains(call->name->name)) {
                            style_call(call, scope);
                        } else {
                            for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                                expression(call->arguments[i], scope, call->name->name == "pattern" && i == 0 ? "pattern_kind" : "");
                            }
                        }
                        break;
                    }
                    case ast::MEMBER: {
                        const auto* member = static_cast<const ast::ASTMember*>(node);
                        if (member->object->type == ast::NAME && static_cast<const ast::ASTName*>(member->object)->name == "theme") {
                            break;
                        }
                        if (!layout(member, scope) && member->object->type != ast::CONTEXT) {
                            expression(member->object, scope, "");
                        }
                        break;
                    }
                    case ast::INLINE_STYLE:
                        for (const auto* assign : static_cast<const ast::ASTInlineStyle*>(node)->properties) {
                            std::string type;
                            builtin(assign->identifier->token, symbols.style_properties, "글자 style 속성", &type);
                            expression(assign->expression, scope, type);
                        }
                        break;
                    case ast::ADD: {
                        const auto* binary = static_cast<const ast::ASTAdd*>(node);
                        expression(binary->left, scope, "");
                        expression(binary->right, scope, "");
                        break;
                    }
                    case ast::MINUS: {
                        const auto* binary = static_cast<const ast::ASTMinus*>(node);
                        expression(binary->left, scope, "");
                        expression(binary->right, scope, "");
                        break;
                    }
                    case ast::MULTIPLY: {
                        const auto* binary = static_cast<const ast::ASTMultiply*>(node);
                        expression(binary->left, scope, "");
                        expression(binary->right, scope, "");
                        break;
                    }
                    case ast::EQUAL: {
                        const auto* binary = static_cast<const ast::ASTEqual*>(node);
                        expression(binary->left, scope, "");
                        // 오른쪽이 enum 값이면 왼쪽 변수의 타입으로 찾는다
                        std::string type;
                        if (binary->left->type == ast::NAME) {
                            if (const int var = scope.find(static_cast<const ast::ASTName*>(binary->left)->name); var >= 0) {
                                type = index.symbols[var].type;
                            }
                        }
                        expression(binary->right, scope, type);
                        break;
                    }
                    case ast::COLOR_RGB: {
                        const auto* color = static_cast<const ast::ASTColorRGB*>(node);
                        for (const auto* component : {color->r, color->g, color->b}) {
                            expression(component, scope, "int");
                        }
                        break;
                    }
                    case ast::COLOR_RGBA: {
                        const auto* color = static_cast<const ast::ASTColorRGBA*>(node);
                        for (const auto* component : {color->r, color->g, color->b, color->a}) {
                            expression(component, scope, "");
                        }
                        break;
                    }
                    case ast::COLOR_SPACE:
                        for (const auto* component : static_cast<const ast::ASTColorSpace*>(node)->arguments) {
                            expression(component, scope, "");
                        }
                        break;
                    case ast::DIMENSION:
                        expression(static_cast<const ast::ASTDimension*>(node)->value, scope, "");
                        break;
                    default:
                        break;
                }
            }

            // 개요

            OutlineItem item(const std::string& name, const std::string& detail, int kind, const ast::ASTNode* node, std::string_view name_view) const {
                OutlineItem result;
                result.name = name;
                result.detail = detail;
                result.kind = kind;
                result.begin = offset(node->span);
                result.end = result.begin + node->span.size();
                result.name_begin = offset(name_view);
                result.name_end = result.name_begin + name_view.size();
                return result;
            }

            void outline(const std::vector<ast::ASTNode*>& body, std::vector<OutlineItem>& out, int& page) const {
                // LSP SymbolKind
                enum { FILE_KIND = 1, MODULE = 2, NAMESPACE = 3, PACKAGE = 4, CLASS = 5, PROPERTY = 7, ENUM_KIND = 10, INTERFACE = 11, FUNCTION = 12, CONSTANT = 14, STRING = 15, OBJECT_KIND = 19, ENUM_MEMBER = 22, STRUCT = 23, EVENT = 24 };
                for (const auto* statement : body) {
                    switch (statement->type) {
                        case ast::INCLUDE: {
                            const auto* node = static_cast<const ast::ASTInclude*>(statement);
                            out.push_back(item(node->relative ? "\"" + node->name + "\"" : "<" + node->name + ">", "#include", FILE_KIND, node, node->span));
                            break;
                        }
                        case ast::ENUM: {
                            const auto* node = static_cast<const ast::ASTEnum*>(statement);
                            OutlineItem entry = item(node->name, "enum", ENUM_KIND, node, node->name_token.value);
                            for (const auto* member : node->members) {
                                OutlineItem value;
                                value.name = member->name;
                                value.kind = ENUM_MEMBER;
                                value.begin = value.name_begin = offset(member->token.value);
                                value.end = value.name_end = value.begin + member->token.value.size();
                                entry.children.push_back(value);
                            }
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::STYLE: {
                            const auto* node = static_cast<const ast::ASTStyle*>(statement);
                            const bool color = node->token.value == "color";
                            out.push_back(item(node->name, color ? "color" : "style", color ? CONSTANT : FUNCTION, node, node->name_token.value));
                            break;
                        }
                        case ast::TEMPLATE:
                        case ast::OBJECT: {
                            const bool is_template = statement->type == ast::TEMPLATE;
                            const auto& name = is_template ? static_cast<const ast::ASTTemplate*>(statement)->name : static_cast<const ast::ASTObject*>(statement)->name;
                            const Token& token = is_template ? static_cast<const ast::ASTTemplate*>(statement)->name_token : static_cast<const ast::ASTObject*>(statement)->name_token;
                            const auto& body_of = is_template ? static_cast<const ast::ASTTemplate*>(statement)->body : static_cast<const ast::ASTObject*>(statement)->body;
                            OutlineItem entry = item(name, is_template ? "template" : "object", is_template ? CLASS : STRUCT, statement, token.value);
                            for (const auto* inner : body_of) {
                                if (inner->type == ast::VAR) {
                                    const auto* var = static_cast<const ast::ASTVar*>(inner);
                                    entry.children.push_back(item(var->name->name, var->type_name->name, PROPERTY, var, var->name->token.value));
                                }
                            }
                            if (is_template) {
                                int ignored = 0;
                                outline(body_of, entry.children, ignored);
                            }
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::MASTER: {
                            const auto* node = static_cast<const ast::ASTMaster*>(statement);
                            OutlineItem entry = item(node->name, "master", INTERFACE, node, node->name_token.value);
                            for (const auto* inner : node->body) {
                                const auto* layout = static_cast<const ast::ASTMasterCase*>(inner);
                                OutlineItem child = item(layout->name, "case", NAMESPACE, layout, layout->name_token.value);
                                int ignored = 0;
                                outline(layout->body, child.children, ignored);
                                entry.children.push_back(std::move(child));
                            }
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::THEME: {
                            const auto* node = static_cast<const ast::ASTTheme*>(statement);
                            out.push_back(item(node->name, "theme", OBJECT_KIND, node, node->name_token.value));
                            break;
                        }
                        case ast::TARGET: {
                            const auto* node = static_cast<const ast::ASTTarget*>(statement);
                            out.push_back(item(node->name, "target", PACKAGE, node, node->name_token.value));
                            break;
                        }
                        case ast::CONSTANT: {
                            const auto* node = static_cast<const ast::ASTConstant*>(statement);
                            out.push_back(item(node->name->name, node->type_name->name, CONSTANT, node, node->name->token.value));
                            break;
                        }
                        case ast::ASSET: {
                            const auto* node = static_cast<const ast::ASTAsset*>(statement);
                            out.push_back(item("\"" + node->path + "\"", node->is_default ? "asset (default)" : "asset", FILE_KIND, node, node->path_token.value));
                            break;
                        }
                        case ast::SECTION: {
                            const auto* node = static_cast<const ast::ASTSection*>(statement);
                            std::string label = trim(std::string(node->name->span));
                            if (node->name->type == ast::STRING) {
                                label = static_cast<const ast::ASTString*>(node->name)->value;
                            }
                            out.push_back(item(label, "section", STRING, node, node->name->span));
                            break;
                        }
                        case ast::SLIDE: {
                            ++page;
                            OutlineItem entry = item("슬라이드 " + std::to_string(page), "slide", MODULE, statement, statement->token.value);
                            int ignored = 0;
                            outline(static_cast<const ast::ASTSlide*>(statement)->body, entry.children, ignored);
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::PUT: {
                            const auto* node = static_cast<const ast::ASTPut*>(statement);
                            OutlineItem entry = item(node->alias != nullptr ? node->alias->name : node->name, node->alias != nullptr ? "put " + node->name : "put", OBJECT_KIND, node,
                                                     node->alias != nullptr ? node->alias->token.value : node->name_token.value);
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::GROUP: {
                            const auto* node = static_cast<const ast::ASTGroup*>(statement);
                            OutlineItem entry = item(node->alias != nullptr ? node->alias->name : "group", "group", OBJECT_KIND, node, node->alias != nullptr ? node->alias->token.value : node->token.value);
                            int ignored = 0;
                            outline(node->body, entry.children, ignored);
                            out.push_back(std::move(entry));
                            break;
                        }
                        case ast::FOR:
                            outline(static_cast<const ast::ASTFor*>(statement)->body, out, page);
                            break;
                        case ast::IF: {
                            const auto* node = static_cast<const ast::ASTIf*>(statement);
                            outline(node->body, out, page);
                            for (const ast::ASTNode* branch = node->branch; branch != nullptr;) {
                                if (branch->type == ast::ELSE_IF) {
                                    outline(static_cast<const ast::ASTElseIf*>(branch)->body, out, page);
                                    branch = static_cast<const ast::ASTElseIf*>(branch)->branch;
                                } else {
                                    outline(static_cast<const ast::ASTElse*>(branch)->body, out, page);
                                    branch = nullptr;
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        };

        std::string describe(const NavIndex& index, const NavSymbol& symbol) {
            const auto owner = [&]() { return symbol.owner >= 0 ? "`" + index.symbols[symbol.owner].name + "`" : std::string("?"); };
            switch (symbol.kind) {
                case SymbolKind::TEMPLATE: return "template · put으로 넣는 묶음";
                case SymbolKind::OBJECT: return "object · 기본 개체";
                case SymbolKind::STYLE: return "글자 style";
                case SymbolKind::COLOR: return "색 이름 · 글자 style로도 쓴다";
                case SymbolKind::ENUM: return "enum";
                case SymbolKind::ENUM_MEMBER: return "enum " + owner() + "의 값";
                case SymbolKind::MASTER: return "master";
                case SymbolKind::LAYOUT: return "master " + owner() + "의 레이아웃(case)";
                case SymbolKind::THEME: return "theme";
                case SymbolKind::TARGET: return "target · 만들 파일";
                case SymbolKind::VAR: {
                    const bool is_object = symbol.owner >= 0 && index.symbols[symbol.owner].kind == SymbolKind::OBJECT;
                    return std::string(is_object ? "object " : "template ") + owner() + "의 속성" + (symbol.required ? " · put할 때 꼭 넣어야 한다" : "");
                }
                case SymbolKind::PARAMETER: {
                    const bool is_master = symbol.owner >= 0 && index.symbols[symbol.owner].kind == SymbolKind::MASTER;
                    return std::string(is_master ? "master " : "style ") + owner() + "의 매개변수";
                }
                case SymbolKind::LOOP: return "for 반복 변수";
                case SymbolKind::ALIAS: return "개체 이름 (as)";
                case SymbolKind::FILE: return "include한 파일";
                case SymbolKind::CONSTANT: return symbol.type == "video" ? "비디오 파일 이름" : symbol.type == "audio" ? "오디오 파일 이름" : "그림 파일 이름";
            }
            return "";
        }
    }

    NavIndex build_index(const std::vector<NavSource>& sources, const middleend::Symbols& symbols, const std::filesystem::path& packages_dir) {
        NavIndex index;
        Builder builder{index, symbols, packages_dir};
        std::vector<std::pair<const ast::ASTFile*, const NavSource*>> files;
        for (const auto& source : sources) {
            lexor::Lexor lexer(*source.text);
            parser::Parser file_parser(source.path, lexer);
            const ast::ASTFile* file = file_parser.parse();
            if (file != nullptr) {
                files.emplace_back(file, &source);
            }
        }
        // include한 파일의 이름을 먼저 알아야 하므로 선언을 모두 모은 뒤에 쓰는 곳을 찾는다
        for (const auto& [file, source] : files) {
            builder.open(*source);
            builder.declare_file(file);
        }
        for (const auto& [file, source] : files) {
            builder.open(*source);
            builder.resolve_file(file, sources);
            int page = 0;
            builder.outline(file->body, index.outlines[source->path], page);
        }
        return index;
    }

    const Occurrence* occurrence_at(const NavIndex& index, const std::string& path, std::size_t offset) {
        const Occurrence* best = nullptr;
        for (const auto& occurrence : index.occurrences) {
            if (occurrence.path != path || offset < occurrence.begin || offset > occurrence.end) {
                continue;
            }
            // 이름 끝에 있는 커서는 다음 이름의 시작보다 이 이름에 가깝지 않으므로 안쪽에 있는 것을 고른다
            if (best == nullptr || (offset < occurrence.end && offset == best->end) || occurrence.end - occurrence.begin < best->end - best->begin) {
                best = &occurrence;
            }
        }
        return best;
    }

    std::string hover_text(const NavIndex& index, const Occurrence& occurrence) {
        if (occurrence.symbol < 0) {
            return occurrence.hover;
        }
        const NavSymbol& symbol = index.symbols[occurrence.symbol];
        std::string text = "```tlide\n" + symbol.code + "\n```\n" + describe(index, symbol);
        if (!symbol.doc.empty()) {
            text += "\n\n" + symbol.doc;
        }
        if (symbol.kind == SymbolKind::FILE) {
            text += "\n\n`" + symbol.file + "`";
        } else if (symbol.path != occurrence.path) {
            text += "\n\n`" + symbol.file + "` " + std::to_string(symbol.line) + "번째 줄";
        }
        return text;
    }

    std::optional<std::string> rename_problem(const NavIndex& index, int symbol, const std::string& main_path) {
        if (symbol < 0) {
            return "컴파일러에 내장된 이름이라 바꿀 수 없습니다";
        }
        const NavSymbol& target = index.symbols[symbol];
        if (target.kind == SymbolKind::FILE) {
            return "include한 파일 이름은 여기서 바꿀 수 없습니다";
        }
        if (target.package) {
            return "패키지(" + target.file + ")에 있는 이름이라 바꿀 수 없습니다";
        }
        for (const auto& occurrence : index.occurrences) {
            if (occurrence.symbol == symbol && occurrence.path != main_path) {
                return "다른 파일에서도 쓰는 이름이라 여기서 바꿀 수 없습니다. 그 파일을 열어 바꿔 주세요";
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> new_name_problem(const NavIndex& index, int symbol, const std::string& name) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_') ||
            !std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
            return "이름은 영문자나 _로 시작하고 영문자, 숫자, _만 쓸 수 있습니다";
        }
        if (keywords().contains(name)) {
            return "'" + name + "'은(는) 예약어라 이름으로 쓸 수 없습니다";
        }
        const NavSymbol& target = index.symbols[symbol];
        if (name == target.name) {
            return std::nullopt;
        }
        const std::string space = namespace_of(target);
        // 글 안에서는 변수가 같은 이름의 style을 가리므로 변수 이름은 style 이름과도 겹치면 안 된다
        const bool variable = target.kind == SymbolKind::VAR || target.kind == SymbolKind::PARAMETER || target.kind == SymbolKind::LOOP;
        for (const auto& other : index.symbols) {
            if (other.name == name && (namespace_of(other) == space || (variable && namespace_of(other) == "style"))) {
                return "'" + name + "'은(는) 이미 있는 이름입니다";
            }
        }
        return std::nullopt;
    }
}

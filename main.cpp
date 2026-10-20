#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#include "backend/backend.h"
#include "format/formatter.h"
#include "frontend/lexor.h"
#include "frontend/parser.h"
#include "middleend/accessibility.h"
#include "middleend/analyzer.h"
#include "middleend/library.h"
#include "middleend/report.h"
#include "middleend/source_text.h"
#include "middleend/tasset.h"
#include "server/server.h"

namespace {
    namespace ast = templide::parser::ast;
    namespace ir = templide::ir;

    void print(const ast::ASTNode* node, int depth);

    void print_line(int depth, const std::string& text) {
        std::cout << std::string(depth * 2, ' ') << text << '\n';
    }

    void print_all(const std::vector<ast::ASTNode*>& nodes, int depth) {
        for (const auto* node : nodes) {
            print(node, depth);
        }
    }

    template <typename T>
    void print_named_block(const std::string& label, const ast::ASTNode* node, int depth) {
        const auto* block = static_cast<const T*>(node);
        print_line(depth, label + " " + block->name);
        print_all(block->body, depth + 1);
    }

    template <typename T>
    void print_binary(const std::string& label, const ast::ASTNode* node, int depth) {
        const auto* binary = static_cast<const T*>(node);
        print_line(depth, label);
        print(binary->left, depth + 1);
        print(binary->right, depth + 1);
    }

    template <typename T>
    void print_conditional(const std::string& label, const ast::ASTNode* node, int depth) {
        const auto* conditional = static_cast<const T*>(node);
        print_line(depth, label);
        print_line(depth + 1, "condition:");
        print(conditional->condition, depth + 2);
        print_line(depth + 1, "body:");
        print_all(conditional->body, depth + 2);
        if (conditional->branch != nullptr) {
            print_line(depth + 1, "branch:");
            print(conditional->branch, depth + 2);
        }
    }

    std::string list_kind_name(ast::ListKind kind) {
        switch (kind) {
            case ast::ListKind::BULLETS: return "bullets";
            case ast::ListKind::NUMBERS: return "numbers";
            case ast::ListKind::DASHES: return "dashes";
            case ast::ListKind::PARAGRAPHS: return "paragraphs";
            case ast::ListKind::PLAIN: return "plain";
        }
        return "";
    }

    void print(const ast::ASTNode* node, int depth) {
        switch (node->type) {
            case ast::FILE: {
                const auto* file = static_cast<const ast::ASTFile*>(node);
                print_line(depth, "File " + file->filepath);
                print_all(file->body, depth + 1);
                break;
            }
            case ast::INCLUDE:
                print_line(depth, static_cast<const ast::ASTInclude*>(node)->relative ? "Include \"" + static_cast<const ast::ASTInclude*>(node)->name + "\""
                                                                                        : "Include <" + static_cast<const ast::ASTInclude*>(node)->name + ">");
                break;
            case ast::ASSET: {
                const auto* asset = static_cast<const ast::ASTAsset*>(node);
                print_line(depth, "Asset \"" + asset->path + "\"" + (asset->is_default ? " default" : "") + (asset->has_by ? " by" : ""));
                for (const auto& item : asset->items) {
                    print_line(depth + 1, (item.all ? "*" : "\"" + item.name + "\"") + " as " + item.alias->name);
                }
                break;
            }
            case ast::CONSTANT: {
                const auto* constant = static_cast<const ast::ASTConstant*>(node);
                print_line(depth, "Constant " + constant->type_name->name + " " + constant->name->name);
                print(constant->expression, depth + 1);
                break;
            }
            case ast::ENUM: {
                const auto* enumeration = static_cast<const ast::ASTEnum*>(node);
                std::string text = "Enum " + enumeration->name + " {";
                for (std::size_t i = 0; i < enumeration->members.size(); ++i) {
                    text += (i > 0 ? ", " : " ") + enumeration->members[i]->name;
                }
                print_line(depth, text + " }");
                break;
            }
            case ast::STYLE: {
                const auto* style = static_cast<const ast::ASTStyle*>(node);
                std::string text = "Style " + style->name;
                if (!style->parameters.empty()) {
                    text += "(";
                    for (std::size_t i = 0; i < style->parameters.size(); ++i) {
                        if (i > 0) {
                            text += ", ";
                        }
                        text += style->parameters[i]->type_name->name + " " + style->parameters[i]->name->name;
                    }
                    text += ")";
                }
                print_line(depth, text);
                print_all(style->body, depth + 1);
                break;
            }
            case ast::PARAMETER: {
                const auto* parameter = static_cast<const ast::ASTParameter*>(node);
                print_line(depth, "Parameter " + parameter->type_name->name + " " + parameter->name->name);
                break;
            }
            case ast::TEMPLATE:
                print_named_block<ast::ASTTemplate>("Template", node, depth);
                break;
            case ast::OBJECT:
                print_named_block<ast::ASTObject>("Object", node, depth);
                break;
            case ast::MASTER: {
                const auto* master = static_cast<const ast::ASTMaster*>(node);
                print_line(depth, "Master " + master->name);
                print_all(master->vars, depth + 1);
                print_all(master->properties, depth + 1);
                print_all(master->body, depth + 1);
                break;
            }
            case ast::MASTER_CASE:
                print_named_block<ast::ASTMasterCase>("Case", node, depth);
                break;
            case ast::SLIDE:
                print_line(depth, "Slide");
                print_all(static_cast<const ast::ASTSlide*>(node)->body, depth + 1);
                break;
            case ast::TARGET:
                print_named_block<ast::ASTTarget>("Target", node, depth);
                break;
            case ast::VAR: {
                const auto* var = static_cast<const ast::ASTVar*>(node);
                print_line(depth, "Var " + var->type_name->name + " " + var->name->name);
                if (var->default_value != nullptr) {
                    print(var->default_value, depth + 1);
                }
                break;
            }
            case ast::PUT: {
                const auto* put = static_cast<const ast::ASTPut*>(node);
                print_line(depth, "Put " + put->name + (put->alias != nullptr ? " as " + put->alias->name : ""));
                print_all(put->body, depth + 1);
                for (const auto* cell : put->cells) {
                    print(cell, depth + 1);
                }
                break;
            }
            case ast::CELL: {
                const auto* cell = static_cast<const ast::ASTCell*>(node);
                print_line(depth, "Cell");
                print(cell->row, depth + 1);
                print(cell->column, depth + 1);
                print_all(cell->body, depth + 1);
                break;
            }
            case ast::GROUP: {
                const auto* group = static_cast<const ast::ASTGroup*>(node);
                print_line(depth, "Group" + (group->alias != nullptr ? " as " + group->alias->name : ""));
                print_all(group->body, depth + 1);
                break;
            }
            case ast::THEME:
                print_named_block<ast::ASTTheme>("Theme", node, depth);
                break;
            case ast::SECTION:
                print_line(depth, "Section");
                print(static_cast<const ast::ASTSection*>(node)->name, depth + 1);
                break;
            case ast::ANIMATE: {
                const auto* animate = static_cast<const ast::ASTAnimate*>(node);
                std::string text = "Animate " + animate->target->name + " " + animate->category->name + " " + animate->effect->name;
                if (animate->option != nullptr) {
                    text += "." + animate->option->name;
                }
                if (animate->start != nullptr) {
                    text += " " + animate->start->name;
                }
                print_line(depth, text);
                if (animate->path != nullptr) {
                    print_line(depth + 1, "path:");
                    print(animate->path, depth + 2);
                }
                if (animate->duration != nullptr) {
                    print_line(depth + 1, "duration:");
                    print(animate->duration, depth + 2);
                }
                if (animate->delay != nullptr) {
                    print_line(depth + 1, "delay:");
                    print(animate->delay, depth + 2);
                }
                break;
            }
            case ast::REVIEW:
                print_line(depth, "Review");
                print_all(static_cast<const ast::ASTReview*>(node)->body, depth + 1);
                break;
            case ast::COMMENT:
                print_line(depth, "Comment");
                print(static_cast<const ast::ASTComment*>(node)->expression, depth + 1);
                break;
            case ast::TRANSITION: {
                const auto* transition = static_cast<const ast::ASTTransition*>(node);
                print_line(depth, "Transition " + transition->kind->name + (transition->option != nullptr ? "." + transition->option->name : ""));
                if (transition->duration != nullptr) {
                    print(transition->duration, depth + 1);
                }
                break;
            }
            case ast::IF:
                print_conditional<ast::ASTIf>("If", node, depth);
                break;
            case ast::ELSE_IF:
                print_conditional<ast::ASTElseIf>("ElseIf", node, depth);
                break;
            case ast::ELSE:
                print_line(depth, "Else");
                print_all(static_cast<const ast::ASTElse*>(node)->body, depth + 1);
                break;
            case ast::FOR: {
                const auto* loop = static_cast<const ast::ASTFor*>(node);
                print_line(depth, "For " + loop->type_name->name + " " + loop->name->name);
                if (loop->range_start != nullptr) {
                    print_line(depth + 1, "range:");
                    print(loop->range_start, depth + 2);
                    print(loop->range_end, depth + 2);
                } else {
                    print_line(depth + 1, "items:");
                    print_all(loop->items, depth + 2);
                }
                print_line(depth + 1, "body:");
                print_all(loop->body, depth + 2);
                break;
            }
            case ast::STYLE_ASSIGN: {
                const auto* assign = static_cast<const ast::ASTStyleAssign*>(node);
                print_line(depth, "StyleAssign " + assign->identifier->name);
                print(assign->expression, depth + 1);
                break;
            }
            case ast::INLINE_STYLE:
                print_line(depth, "InlineStyle");
                for (const auto* assign : static_cast<const ast::ASTInlineStyle*>(node)->properties) {
                    print(assign, depth + 1);
                }
                break;
            case ast::COLOR_RGB: {
                const auto* color = static_cast<const ast::ASTColorRGB*>(node);
                print_line(depth, "ColorRGB");
                print(color->r, depth + 1);
                print(color->g, depth + 1);
                print(color->b, depth + 1);
                break;
            }
            case ast::COLOR_RGBA: {
                const auto* color = static_cast<const ast::ASTColorRGBA*>(node);
                print_line(depth, "ColorRGBA");
                print(color->r, depth + 1);
                print(color->g, depth + 1);
                print(color->b, depth + 1);
                print(color->a, depth + 1);
                break;
            }
            case ast::COLOR_HEX:
                print_line(depth, "ColorHex " + static_cast<const ast::ASTString*>(static_cast<const ast::ASTColorHex*>(node)->hex)->value);
                break;
            case ast::COLOR_SPACE: {
                const auto* color = static_cast<const ast::ASTColorSpace*>(node);
                print_line(depth, "Color " + color->space);
                for (const auto* argument : color->arguments) {
                    print(argument, depth + 1);
                }
                break;
            }
            case ast::ASSIGN: {
                const auto* assign = static_cast<const ast::ASTAssign*>(node);
                print_line(depth, "Assign " + assign->name->name);
                print(assign->expression, depth + 1);
                break;
            }
            case ast::ADD:
                print_binary<ast::ASTAdd>("Add", node, depth);
                break;
            case ast::MINUS:
                print_binary<ast::ASTMinus>("Minus", node, depth);
                break;
            case ast::MULTIPLY:
                print_binary<ast::ASTMultiply>("Multiply", node, depth);
                break;
            case ast::EQUAL:
                print_binary<ast::ASTEqual>("Equal", node, depth);
                break;
            case ast::CALL: {
                const auto* call = static_cast<const ast::ASTCall*>(node);
                print_line(depth, "Call " + call->name->name);
                print_all(call->arguments, depth + 1);
                break;
            }
            case ast::MEMBER: {
                const auto* member = static_cast<const ast::ASTMember*>(node);
                print_line(depth, "Member ." + member->member->name);
                print(member->object, depth + 1);
                break;
            }
            case ast::CONTEXT:
                print_line(depth, "Context @" + static_cast<const ast::ASTContext*>(node)->name->name);
                break;
            case ast::LIST: {
                const auto* list = static_cast<const ast::ASTList*>(node);
                print_line(depth, "List " + list_kind_name(list->kind));
                print_all(list->items, depth + 1);
                break;
            }
            case ast::DIMENSION:
                print_line(depth, "Dimension " + std::string(node->token.value));
                break;
            case ast::NAME:
                print_line(depth, "Name " + static_cast<const ast::ASTName*>(node)->name);
                break;
            case ast::IDENTIFIER:
                print_line(depth, "Identifier " + static_cast<const ast::ASTIdentifier*>(node)->name);
                break;
            case ast::INT:
                print_line(depth, "Int " + std::to_string(static_cast<const ast::ASTInt*>(node)->value));
                break;
            case ast::FLOAT:
                print_line(depth, "Float " + std::string(node->token.value));
                break;
            case ast::STRING:
                print_line(depth, "String \"" + static_cast<const ast::ASTString*>(node)->value + "\"");
                break;
            case ast::FSTRING:
                print_line(depth, "FString");
                print_all(static_cast<const ast::ASTFString*>(node)->parts, depth + 1);
                break;
            case ast::TEXT:
                print_line(depth, "Text");
                print_all(static_cast<const ast::ASTText*>(node)->parts, depth + 1);
                break;
        }
    }

    // 명령줄 옵션 중 진단을 보여 주는 방식
    struct Options {
        templide::middleend::ReportFormat format = templide::middleend::ReportFormat::Text;
        bool strict = false;       // 경고도 실패로 센다
        bool check_format = false; // --check에서 #fmt가 있는 파일이 포맷되어 있는지도 본다
        bool check_access = false; // --check에서 접근성도 본다
    };

    // 진단을 형식에 맞게 보여 주고 세어 둔다. text는 stderr에 바로, github는 stdout에 바로 적고, json은 finish()에서 한 번에 적는다
    class Reporter {
    public:
        explicit Reporter(const Options& options) : options_(options) {}

        void add(templide::middleend::Diagnostic diagnostic) {
            using templide::middleend::Level;
            using templide::middleend::ReportFormat;
            switch (diagnostic.level) {
                case Level::Error: ++errors_; break;
                case Level::Warning: ++warnings_; break;
                case Level::Info: ++infos_; break;
            }
            switch (options_.format) {
                case ReportFormat::Text:
                    std::cerr << templide::middleend::format_text(diagnostic) << '\n';
                    break;
                case ReportFormat::Github:
                    std::cout << templide::middleend::format_github(diagnostic) << '\n';
                    break;
                case ReportFormat::Json:
                    collected_.push_back(std::move(diagnostic));
                    break;
            }
        }

        // 위치가 없는 오류. 메시지의 코드는 카탈로그에 있다
        void add_error(const char* code, const std::string& message) {
            add(templide::middleend::Diagnostic{"", 0, 0, message, 0, templide::middleend::Level::Error, code});
        }

        void finish() {
            if (options_.format == templide::middleend::ReportFormat::Json) {
                std::cout << templide::middleend::format_json(collected_) << '\n';
                collected_.clear();
            }
        }

        // 결과를 알리는 한 줄(성공 기록 등). json은 JSON만 stdout에 두려고 뺀다
        void note(const std::string& line) const {
            if (options_.format != templide::middleend::ReportFormat::Json) {
                std::cout << line << '\n';
            }
        }

        std::size_t errors() const { return errors_; }
        std::size_t warnings() const { return warnings_; }

        // 종료 코드: 0 성공, 1 검사 실패(오류, --strict면 경고도)
        int exit_code() const {
            return errors_ > 0 || (options_.strict && warnings_ > 0) ? 1 : 0;
        }

    private:
        Options options_;
        std::vector<templide::middleend::Diagnostic> collected_;
        std::size_t errors_ = 0;
        std::size_t warnings_ = 0;
        std::size_t infos_ = 0;
    };

    int dump_ast(const std::string& path, const Options& options) {
        std::ifstream file(templide::backend::utf8_path(path), std::ios::binary);
        if (!file) {
            Reporter(options).add_error("E2162", "Cannot open file");
            return 2;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string decode_error;
        const auto decoded = templide::middleend::decode_source(buffer.str(), decode_error);
        if (!decoded) {
            Reporter(options).add(templide::middleend::Diagnostic{path, 0, 0, path + " " + decode_error, 0, templide::middleend::Level::Error, "E2002"});
            return 2;
        }
        const std::string& source = *decoded;

        templide::lexor::Lexor lexer(source);
        templide::parser::Parser parser(path, lexer);
        const auto* file_node = parser.parse();
        if (file_node == nullptr) {
            const auto& error = parser.error;
            Reporter(options).add(templide::middleend::Diagnostic{path, error.line, error.column, error.message, 0, templide::middleend::Level::Error, error.code, error.help});
            return 1;
        }
        print(file_node, 0);
        return 0;
    }

    std::string quote(const std::string& text) {
        return "\"" + text + "\"";
    }

    std::string list_kind_name(ir::ListKind kind) {
        switch (kind) {
            case ir::ListKind::NONE: return "";
            case ir::ListKind::BULLETS: return "bullets";
            case ir::ListKind::NUMBERS: return "numbers";
            case ir::ListKind::DASHES: return "dashes";
        }
        return "";
    }

    std::string format_link(const ir::Link& link) {
        if (!link.url.empty()) {
            return quote(link.url);
        }
        return link.slide > 0 ? "slide(" + std::to_string(link.slide) + ")" : link.jump;
    }

    std::string format_style(const ir::TextStyle& style) {
        std::vector<std::string> items;
        if (style.color) {
            items.push_back("color: " + ir::format_color(*style.color));
        }
        if (style.font_weight) {
            items.push_back("font-weight: " + style.font_weight->member);
        }
        if (style.line_height) {
            items.push_back("line-height: " + ir::format_number(*style.line_height));
        }
        if (style.font_family) {
            items.push_back("font-family: " + quote(*style.font_family));
        }
        if (style.font_size) {
            items.push_back("font-size: " + ir::format_number(*style.font_size));
        }
        if (style.text_align) {
            items.push_back("text-align: " + style.text_align->member);
        }
        const auto add_enum = [&](const char* name, const std::optional<ir::EnumValue>& value) {
            if (value) {
                items.push_back(std::string(name) + ": " + value->member);
            }
        };
        const auto add_number = [&](const char* name, const std::optional<ir::Number>& value) {
            if (value) {
                items.push_back(std::string(name) + ": " + ir::format_number(*value));
            }
        };
        const auto add_color = [&](const char* name, const std::optional<ir::Color>& value) {
            if (value) {
                items.push_back(std::string(name) + ": " + ir::format_color(*value));
            }
        };
        add_enum("font-style", style.font_style);
        add_enum("text-decoration", style.text_decoration);
        add_enum("vertical-align", style.vertical_align);
        add_number("letter-spacing", style.letter_spacing);
        add_color("highlight", style.highlight);
        add_enum("text-transform", style.text_transform);
        add_number("space-before", style.space_before);
        add_number("space-after", style.space_after);
        add_number("margin-left", style.margin_left);
        add_number("text-indent", style.text_indent);
        if (style.list_marker) {
            items.push_back("list-marker: " + quote(*style.list_marker));
        }
        add_color("list-marker-color", style.list_marker_color);
        add_enum("list-style", style.list_style);
        add_number("list-start", style.list_start);
        if (style.link) {
            items.push_back("link: " + format_link(*style.link));
        }
        if (items.empty()) {
            return "";
        }
        std::string result = " [";
        for (std::size_t i = 0; i < items.size(); ++i) {
            result += (i > 0 ? ", " : "") + items[i];
        }
        return result + "]";
    }

    std::string format_inline(const ir::Value& value) {
        if (const auto* boolean = std::get_if<bool>(&value)) {
            return *boolean ? "true" : "false";
        }
        if (const auto* number = std::get_if<ir::Number>(&value)) {
            return ir::format_number(*number);
        }
        if (const auto* string = std::get_if<std::string>(&value)) {
            return quote(*string);
        }
        if (const auto* color = std::get_if<ir::Color>(&value)) {
            return ir::format_color(*color);
        }
        if (const auto* enum_value = std::get_if<ir::EnumValue>(&value)) {
            return enum_value->member;
        }
        if (const auto* gradient = std::get_if<ir::Gradient>(&value)) {
            return ir::format_gradient(*gradient);
        }
        if (const auto* image = std::get_if<ir::Image>(&value)) {
            return "image(" + quote(image->path) + ")";
        }
        if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
            return "pattern(" + pattern->kind + ", " + ir::format_color(pattern->foreground) + ", " + ir::format_color(pattern->background) + ")";
        }
        if (const auto* link = std::get_if<ir::Link>(&value)) {
            return format_link(*link);
        }
        return "";
    }

    void print_text(const ir::Text& text, int depth) {
        for (const auto& paragraph : text.paragraphs) {
            std::string header = "Paragraph";
            if (paragraph.list != ir::ListKind::NONE) {
                header += " " + list_kind_name(paragraph.list) + " level " + std::to_string(paragraph.level);
            }
            print_line(depth, header);
            for (const auto& run : paragraph.runs) {
                print_line(depth + 1, (run.field.empty() ? "Run " : "Field " + run.field + " ") + quote(run.text) + format_style(run.style));
            }
        }
    }

    void print_list(const ir::List& list, int depth) {
        for (const auto& item : list.items) {
            if (const auto* nested = std::get_if<ir::List>(&item)) {
                print_line(depth, "List");
                print_list(*nested, depth + 1);
            } else if (const auto* text = std::get_if<ir::Text>(&item)) {
                print_line(depth, "Text");
                print_text(*text, depth + 1);
            } else {
                print_line(depth, "Item " + format_inline(item));
            }
        }
    }

    void print_property(const ir::Property& property, int depth) {
        if (const auto* text = std::get_if<ir::Text>(&property.value)) {
            print_line(depth, property.name + ":");
            print_text(*text, depth + 1);
        } else if (const auto* list = std::get_if<ir::List>(&property.value)) {
            print_line(depth, property.name + ":");
            print_list(*list, depth + 1);
        } else {
            print_line(depth, property.name + ": " + format_inline(property.value));
        }
    }

    void print_elements(const std::vector<ir::Element>& elements, int depth) {
        for (const auto& element : elements) {
            print_line(depth, "Element " + element.object + (element.name.empty() ? "" : " as " + element.name));
            for (const auto& property : element.properties) {
                print_property(property, depth + 1);
            }
            for (const auto& cell : element.cell_styles) {
                print_line(depth + 1, "cell(" + std::to_string(cell.row) + ", " + std::to_string(cell.column) + ")");
                for (const auto& property : cell.properties) {
                    print_property(property, depth + 2);
                }
            }
            print_elements(element.children, depth + 1);
        }
    }

    void print_document(const ir::Document& document) {
        print_line(0, "Document");
        for (const auto& master : document.masters) {
            print_line(1, "Master " + master.name);
            if (master.theme) {
                std::string text = "theme " + master.theme->name + ":";
                for (const auto& [name, color] : master.theme->colors) {
                    text += " " + name + "=" + ir::format_color(color);
                }
                if (!master.theme->heading_font.empty()) {
                    text += " heading_font=" + quote(master.theme->heading_font);
                }
                if (!master.theme->body_font.empty()) {
                    text += " body_font=" + quote(master.theme->body_font);
                }
                print_line(2, text);
            }
            for (const auto& layout : master.layouts) {
                print_line(2, "Layout " + layout.name);
                if (layout.background) {
                    print_line(3, "background: " + format_inline(*layout.background));
                }
                print_elements(layout.elements, 3);
            }
        }
        for (const auto& slide : document.slides) {
            std::string header = "Slide " + std::to_string(slide.page);
            if (slide.layout) {
                const auto& master = document.masters[slide.layout->master];
                header += " layout " + master.name + "." + master.layouts[slide.layout->layout].name;
            }
            print_line(1, header);
            if (slide.background) {
                print_line(2, "background: " + format_inline(*slide.background));
            }
            if (slide.transition) {
                const auto& transition = *slide.transition;
                std::string text = "transition: " + transition.kind + (transition.option.empty() ? "" : "." + transition.option);
                if (transition.duration) {
                    text += " " + ir::format_number(*transition.duration);
                }
                print_line(2, text);
            }
            if (!slide.notes.paragraphs.empty()) {
                print_line(2, "notes:");
                print_text(slide.notes, 3);
            }
            if (slide.section) {
                print_line(2, "section: " + quote(*slide.section));
            }
            if (slide.hidden) {
                print_line(2, "hidden: true");
            }
            if (slide.advance_after) {
                print_line(2, "advance_after: " + ir::format_number(*slide.advance_after));
            }
            if (slide.transition_sound) {
                print_line(2, "transition_sound: " + quote(*slide.transition_sound));
            }
            for (const auto& [role, text] : slide.placeholders) {
                print_line(2, role + ":");
                print_text(text, 3);
            }
            print_elements(slide.elements, 2);
            for (const auto& animation : slide.animations) {
                std::string text = "animate " + animation.target + " " + animation.category + " " + animation.effect;
                if (!animation.path.empty()) {
                    text += " " + quote(animation.path);
                } else if (!animation.option.empty()) {
                    text += "." + animation.option;
                }
                if (animation.duration) {
                    text += " " + ir::format_number(*animation.duration);
                }
                text += " " + animation.start;
                if (animation.delay) {
                    text += " delay " + ir::format_number(*animation.delay);
                }
                print_line(2, text);
            }
            for (const auto& review : slide.reviews) {
                print_line(2, "review " + quote(review.text) + " by " + quote(review.author) + " at " + ir::format_number(review.x) + ", " + ir::format_number(review.y));
            }
        }
        for (const auto& target : document.targets) {
            print_line(1, "Target " + target.name);
            print_line(2, "path: " + quote(target.path));
            print_line(2, "type: " + target.type);
            for (const std::size_t master : target.masters) {
                print_line(2, "master: " + document.masters[master].name);
            }
            if (target.width && target.height) {
                print_line(2, "size: " + ir::format_number(*target.width) + " x " + ir::format_number(*target.height));
            }
            if (!target.title.empty()) {
                print_line(2, "title: " + quote(target.title));
            }
            if (!target.author.empty()) {
                print_line(2, "author: " + quote(target.author));
            }
            if (target.loop) {
                print_line(2, "loop: true");
            }
            if (target.angles != "powerpoint") {
                print_line(2, "angles: " + target.angles);
            }
            if (target.scale != 1) {
                print_line(2, "scale: " + ir::format_scalar(target.scale));
            }
            if (target.transparent) {
                print_line(2, "transparent: true");
            }
        }
    }

    // 파일의 바이트
    std::optional<std::string> read_bytes(const std::string& path) {
        std::ifstream file(templide::backend::utf8_path(path), std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // templide fmt [--check | --stdout] <file>...  종료 코드: 0 성공, 1 문법 오류가 있거나 --check에서 포맷이 필요하다, 2 파일을 읽거나 쓸 수 없음
    int fmt_command(const std::vector<std::string>& files, bool check, bool to_stdout, const Options& options) {
        using templide::middleend::Diagnostic;
        using templide::middleend::Level;
        if (files.empty() || (to_stdout && (files.size() != 1 || check))) {
            std::cerr << "usage: templide fmt [--check | --stdout] [--format text|json|github] <file>...\n";
            return 2;
        }
        Reporter reporter(options);
        bool io_error = false;
        for (const auto& path : files) {
            const auto bytes = read_bytes(path);
            if (!bytes) {
                reporter.add(Diagnostic{path, 0, 0, "Cannot open file", 0, Level::Error, "E2162"});
                io_error = true;
                continue;
            }
            std::string decode_error;
            const auto text = templide::middleend::decode_source(*bytes, decode_error);
            if (!text) {
                reporter.add(Diagnostic{path, 0, 0, path + " " + decode_error, 0, Level::Error, "E2002"});
                io_error = true;
                continue;
            }
            const auto result = templide::format::format_source(path, *text);
            for (const auto& diagnostic : result.diagnostics) {
                reporter.add(diagnostic);
            }
            if (!result.ok) {
                continue;
            }
            if (!result.has_settings) {
                reporter.note("skipped " + path + " (no #fmt)");
                continue;
            }
            if (to_stdout) {
                std::string shown;
                for (const char c : result.text) {
                    if (c != '\r') {
                        shown += c;
                    }
                }
                std::cout << shown;
                continue;
            }
            if (result.text == *text) {
                reporter.note("unchanged " + path);
            } else if (check) {
                reporter.add(Diagnostic{path, 0, 0, "File is not formatted; run 'templide fmt' to format it", 0, Level::Error, "E4003"});
            } else {
                std::ofstream out(templide::backend::utf8_path(path), std::ios::binary | std::ios::trunc);
                out << templide::middleend::encode_like(*bytes, result.text);
                if (!out) {
                    reporter.add(Diagnostic{path, 0, 0, "Cannot write file", 0, Level::Error, "E4004"});
                    io_error = true;
                    continue;
                }
                reporter.note("formatted " + path);
            }
        }
        reporter.finish();
        return io_error ? 2 : reporter.exit_code();
    }

    // 고른 target들(없으면 전부)의 파일을 쓴다. check면 파일을 쓰지 않고 에러와 경고만 알아본다
    void build(const ir::Document& document, const std::vector<std::string>& selected, const std::filesystem::path& base_dir, const std::filesystem::path& libs_dir, bool check,
               Reporter& reporter) {
        bool failed = false;
        for (const auto& name : selected) {
            const bool exists = std::any_of(document.targets.begin(), document.targets.end(), [&](const ir::Target& target) { return target.name == name; });
            if (!exists) {
                reporter.add_error("E3001", "unknown target: " + name);
                failed = true;
            }
        }
        if (failed) {
            return;
        }
        if (document.targets.empty()) {
            if (check) {
                reporter.note("no target; only the document was checked");
                return;
            }
            reporter.add_error("E3002", "no target to build");
            return;
        }
        for (const auto& target : document.targets) {
            if (!selected.empty() && std::find(selected.begin(), selected.end(), target.name) == selected.end()) {
                continue;
            }
            std::vector<templide::backend::Warning> warnings;
            const auto errors = templide::backend::write_target(document, target, base_dir, libs_dir, warnings, check);
            for (const auto& warning : warnings) {
                reporter.add(templide::backend::to_diagnostic(warning));
            }
            for (const auto& message : errors) {
                reporter.add_error("E3003", "target " + target.name + ": " + message);
            }
            if (errors.empty()) {
                // png는 슬라이드마다 파일 하나다 (slides.png -> slides-<n>.png)
                std::string written = target.path;
                if (target.type == "png") {
                    const std::size_t dot = written.rfind('.');
                    written = written.substr(0, dot) + "-<n>" + (dot == std::string::npos ? "" : written.substr(dot));
                }
                reporter.note(target.name + (check ? ": ok" : ": wrote " + written));
            }
        }
    }

    // 문서가 있는 폴더. target의 상대 경로는 여기 기준이다
    std::filesystem::path base_dir_of(const std::string& path) {
        std::error_code error;
        const auto document = templide::backend::utf8_path(path);
        const auto absolute = std::filesystem::absolute(document, error);
        return (error ? document : absolute).parent_path();
    }

    // 문서가 읽는 파일들. --watch가 바뀌었는지 보는 대상이다. .tasset 안의 파일은 그 묶음 파일로 본다
    struct Dependencies {
        std::filesystem::path base_dir;
        std::set<std::filesystem::path> files;

        void add(const std::filesystem::path& file) {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(file, error);
            files.insert((error ? file : absolute).lexically_normal());
        }

        // 문서에 적은 파일 경로. 상대 경로는 문서가 있는 폴더 기준이다
        void add_written(const std::string& written) {
            if (written.empty()) {
                return;
            }
            std::filesystem::path file = templide::backend::utf8_path(written);
            if (!file.is_absolute()) {
                file = base_dir / file;
            }
            std::error_code error;
            if (!std::filesystem::exists(file, error)) {
                if (const auto inside = templide::tasset::split(file)) {
                    file = inside->first;
                }
            }
            add(file);
        }

        void add_value(const ir::Value& value) {
            if (const auto* image = std::get_if<ir::Image>(&value)) {
                add_written(image->path);
            }
        }

        void add_elements(const std::vector<ir::Element>& elements) {
            for (const auto& element : elements) {
                for (const auto& property : element.properties) {
                    add_value(property.value);
                    if (property.name == "action_sound" || property.name == "hover_sound") {
                        if (const auto* sound = std::get_if<std::string>(&property.value)) {
                            add_written(*sound);
                        }
                    }
                }
                add_elements(element.children);
            }
        }

        void add_document(const ir::Document& document) {
            for (const auto& master : document.masters) {
                for (const auto& layout : master.layouts) {
                    if (layout.background) {
                        add_value(*layout.background);
                    }
                    add_elements(layout.elements);
                }
            }
            for (const auto& slide : document.slides) {
                if (slide.background) {
                    add_value(*slide.background);
                }
                if (slide.transition_sound) {
                    add_written(*slide.transition_sound);
                }
                add_elements(slide.elements);
            }
            for (const auto& target : document.targets) {
                add_written(target.script);
            }
        }
    };

    enum class Mode { BUILD, AST, IR };

    // #fmt가 있는 파일(표준 라이브러리는 뺀다)이 포맷되어 있는지 본다. 되어 있지 않으면 경고
    void check_formatting(const templide::middleend::Result& result, const char* program, Reporter& reporter) {
        const std::string packages = templide::middleend::default_packages_dir(program).lexically_normal().string();
        for (const auto& [file, text] : result.sources) {
            if (std::filesystem::path(file).lexically_normal().string().starts_with(packages)) {
                continue;
            }
            const auto formatted = templide::format::format_source(file, text);
            if (formatted.ok && formatted.has_settings && formatted.text != text) {
                reporter.add(templide::middleend::Diagnostic{file, 0, 0, "File is not formatted; run 'templide fmt' to format it", 0, templide::middleend::Level::Warning, "W4001"});
            }
        }
    }

    // templide tool access <file>: 문서의 접근성을 검사한다. 문서에 오류가 있으면 그 오류를 알리고 끝낸다
    int access_command(const std::string& path, const char* program, const Options& options) {
        const auto result = templide::middleend::analyze(templide::backend::utf8_path(path), templide::middleend::default_packages_dir(program));
        Reporter reporter(options);
        bool unreadable = false;
        for (const auto& diagnostic : result.diagnostics) {
            unreadable = unreadable || (diagnostic.line == 0 && (diagnostic.code == "E2162" || diagnostic.code == "E2002"));
            reporter.add(diagnostic);
        }
        if (!result.document) {
            reporter.finish();
            return unreadable ? 2 : 1;
        }
        for (const auto& diagnostic : templide::middleend::check_accessibility(*result.document, result.sources)) {
            reporter.add(diagnostic);
        }
        reporter.finish();
        return reporter.exit_code();
    }

    // 문서 하나를 분석해 target 파일을 만들거나(--check면 검사만 한다) AST, IR을 출력한다. 종료 코드: 0 성공, 1 검사 실패, 2 파일을 읽을 수 없음.
    // dependencies가 있으면 읽은 파일들을 거기에 모은다. 분석에 실패해도 읽은 .tlide 파일은 모은다
    int compile(const std::string& path, Mode mode, const std::vector<std::string>& selected, bool check, const char* program, Dependencies* dependencies, const Options& options) {
        if (mode == Mode::AST) {
            return dump_ast(path, options);
        }
        const auto result = templide::middleend::analyze(templide::backend::utf8_path(path), templide::middleend::default_packages_dir(program));
        if (dependencies != nullptr) {
            for (const auto& [file, text] : result.sources) {
                dependencies->add(templide::backend::utf8_path(file));
            }
            for (const auto& asset : result.symbols.assets) {
                dependencies->add(templide::backend::utf8_path(asset.bundle));
            }
            if (result.document) {
                dependencies->add_document(*result.document);
            }
        }
        // --ir은 stdout에 IR을 쓰므로 진단은 늘 text로 stderr에 쓴다
        Options shown = options;
        if (mode == Mode::IR) {
            shown.format = templide::middleend::ReportFormat::Text;
        }
        Reporter reporter(shown);
        // 문서 파일을 열거나 해독하지 못한 오류(위치 없음)는 검사 실패가 아니라 입출력 오류다
        bool unreadable = false;
        for (const auto& diagnostic : result.diagnostics) {
            unreadable = unreadable || (diagnostic.line == 0 && (diagnostic.code == "E2162" || diagnostic.code == "E2002"));
            reporter.add(diagnostic);
        }
        for (const auto& diagnostic : result.warnings) {
            reporter.add(diagnostic);
        }
        if (options.check_format) {
            check_formatting(result, program, reporter);
        }
        if (options.check_access && result.document) {
            for (const auto& diagnostic : templide::middleend::check_accessibility(*result.document, result.sources)) {
                reporter.add(diagnostic);
            }
        }
        if (!result.document) {
            reporter.finish();
            return unreadable ? 2 : 1;
        }
        if (mode == Mode::IR) {
            print_document(*result.document);
            return reporter.exit_code();
        }
        build(*result.document, selected, base_dir_of(path), templide::middleend::default_libs_dir(program), check, reporter);
        reporter.finish();
        return reporter.exit_code();
    }

    // 파일의 크기와 수정 시각. 없는 파일은 exists가 false다
    struct Stamp {
        bool exists = false;
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
        bool operator==(const Stamp&) const = default;
    };

    std::map<std::filesystem::path, Stamp> snapshot(const std::set<std::filesystem::path>& files) {
        std::map<std::filesystem::path, Stamp> result;
        for (const auto& file : files) {
            std::error_code error;
            Stamp stamp;
            stamp.time = std::filesystem::last_write_time(file, error);
            if (!error) {
                stamp.exists = true;
                stamp.size = std::filesystem::file_size(file, error);
            }
            result[file] = stamp;
        }
        return result;
    }

    // 문서와 그것이 읽는 파일이 바뀔 때마다 다시 만든다. Ctrl+C로 끝낸다. 저장이 끝날 때까지(수정이 멈출 때까지) 기다렸다가 만든다
    int watch(const std::string& path, const std::vector<std::string>& selected, bool check, const char* program, const Options& options) {
        std::set<std::filesystem::path> files;
        const auto cycle = [&]() {
            Dependencies dependencies{base_dir_of(path), {}};
            dependencies.add(templide::backend::utf8_path(path));
            compile(path, Mode::BUILD, selected, check, program, &dependencies, options);
            files = std::move(dependencies.files);
            std::cout << std::flush;
            std::cerr << std::flush;
        };
        std::cout << "watching " << path << " (Ctrl+C to stop)\n" << std::flush;
        cycle();
        auto stamps = snapshot(files);
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto now = snapshot(files);
            if (now == stamps) {
                continue;
            }
            std::vector<std::string> changed;
            for (const auto& [file, stamp] : now) {
                const auto before = stamps.find(file);
                if (before == stamps.end() || !(before->second == stamp)) {
                    changed.push_back(templide::backend::display(file.filename()));
                }
            }
            do {
                stamps = now;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                now = snapshot(files);
            } while (now != stamps);
            std::cout << "changed:";
            for (const auto& name : changed) {
                std::cout << ' ' << name;
            }
            std::cout << '\n';
            cycle();
            stamps = snapshot(files);
        }
    }
}

// templide [--ast | --ir | --check] [--watch] [--target <name>]... [--format text|json|github] [--strict] [--fmt] [file]
// templide fmt [--check | --stdout] [--format text|json|github] <file>...
// templide tool access [--format text|json|github] [--strict] <file>
// templide --serve
// 기본은 target 파일을 만든다. --ast는 parser의 AST를, --ir은 middle end의 IR을 출력한다.
// --check는 파일을 쓰지 않고 에러와 경고만 알아본다. --watch는 문서와 그것이 읽는 파일이 바뀔 때마다 다시 만든다.
// --format은 진단을 보여 주는 형식이다 (text는 stderr, json과 github는 stdout).
// --strict는 경고가 있어도 실패로 센다. --fmt는 --check에서 #fmt가 있는 파일이 포맷되어 있는지도 본다.
// tool access는 문서의 접근성(대체 텍스트, 색 대비, 글자 크기, 제목, 읽는 순서 등)을 검사한다. --access는 --check에서 같은 검사를 함께 한다.
// fmt는 .tlide 파일을 #fmt 지시문의 설정대로 다시 쓴다 (--check는 고치지 않고 알리기만, --stdout은 파일 대신 화면에 쓴다).
// 종료 코드: 0 성공, 1 검사 실패(오류가 있다), 2 사용법이나 파일 입출력 오류
// --serve는 편집기와 stdin/stdout으로 대화하는 언어 서버다 (server/server.h)
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--serve") {
        return templide::server::serve(templide::middleend::default_packages_dir(argv[0]), templide::middleend::default_libs_dir(argv[0]));
    }
    templide::middleend::use_utf8_console();
    Mode mode = Mode::BUILD;
    bool check = false;
    bool watching = false;
    bool to_stdout = false;
    Options options;
    std::vector<std::string> selected;
    std::vector<std::string> files;
    const std::vector<std::string> arguments = templide::middleend::command_line_arguments(argc, argv);
    const bool format_command = arguments.size() > 1 && arguments[1] == "fmt";
    const bool access_tool = arguments.size() > 2 && arguments[1] == "tool" && arguments[2] == "access";
    // "--format json"과 "--format=json" 둘 다 받는다. 값이 없으면 nullopt
    const auto value_of = [&](std::size_t& i, const std::string& argument, const std::string& name) -> std::optional<std::string> {
        if (argument == name) {
            return i + 1 < arguments.size() ? std::optional<std::string>(arguments[++i]) : std::nullopt;
        }
        return argument.substr(name.size() + 1);
    };
    for (std::size_t i = format_command ? 2 : access_tool ? 3 : 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        const auto is_option = [&](const std::string& name) { return argument == name || argument.starts_with(name + "="); };
        if (argument == "--ast") {
            mode = Mode::AST;
        } else if (argument == "--ir") {
            mode = Mode::IR;
        } else if (argument == "--check") {
            check = true;
        } else if (argument == "--watch") {
            watching = true;
        } else if (argument == "--strict") {
            options.strict = true;
        } else if (argument == "--fmt") {
            options.check_format = true;
        } else if (argument == "--access") {
            options.check_access = true;
        } else if (argument == "--stdout") {
            to_stdout = true;
        } else if (argument == "--target") {
            if (i + 1 >= arguments.size()) {
                std::cerr << "error: --target needs a target name\n";
                return 2;
            }
            selected.push_back(arguments[++i]);
        } else if (is_option("--format")) {
            const auto value = value_of(i, argument, "--format");
            const auto format = value ? templide::middleend::report_format_from_name(*value) : std::nullopt;
            if (!format) {
                std::cerr << "error: --format must be text, json or github\n";
                return 2;
            }
            options.format = *format;
        } else if (argument.starts_with("--")) {
            std::cerr << "error: unknown option: " << argument << '\n';
            return 2;
        } else {
            files.push_back(argument);
        }
    }
    if (format_command) {
        return fmt_command(files, check, to_stdout, options);
    }
    if (to_stdout) {
        std::cerr << "error: --stdout is only for 'templide fmt'\n";
        return 2;
    }
    if (access_tool) {
        if (files.size() != 1) {
            std::cerr << "usage: templide tool access [--format text|json|github] [--strict] <file>\n";
            return 2;
        }
        return access_command(files.front(), argv[0], options);
    }
    const std::string path = files.empty() ? "" : files.back();
    if (path.empty()) {
        std::cerr << "usage: templide [--ast | --ir | --check] [--watch] [--target <name>]... [--format text|json|github] [--strict] [--fmt] [--access] <file>\n"
                  << "       templide fmt [--check | --stdout] <file>...\n"
                  << "       templide tool access <file>\n"
                  << "       templide --serve\n";
        return 2;
    }
    if ((check || watching) && mode != Mode::BUILD) {
        std::cerr << "error: --check and --watch cannot be used with --ast or --ir\n";
        return 2;
    }
    if (watching) {
        return watch(path, selected, check, argv[0], options);
    }
    return compile(path, mode, selected, check, argv[0], nullptr, options);
}

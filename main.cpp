#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "backend/backend.h"
#include "frontend/lexor.h"
#include "frontend/parser.h"
#include "middleend/analyzer.h"
#include "middleend/library.h"
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
                std::string text = "Master " + master->name;
                if (!master->parameters.empty()) {
                    text += "(";
                    for (std::size_t i = 0; i < master->parameters.size(); ++i) {
                        text += (i > 0 ? ", " : "") + master->parameters[i]->type_name->name + " " + master->parameters[i]->name->name;
                    }
                    text += ")";
                }
                print_line(depth, text);
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

    int dump_ast(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "Cannot open file: " << path << std::endl;
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();

        templide::lexor::Lexor lexer(source);
        templide::parser::Parser parser(path, lexer);
        const auto* file_node = parser.parse();
        if (file_node == nullptr) {
            const auto& error = parser.error;
            std::cerr << path << ':' << error.line << ':' << error.column << ": error: " << error.message << std::endl;
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

    void print_elements(const std::vector<ir::Element>& elements, int depth) {
        for (const auto& element : elements) {
            print_line(depth, "Element " + element.object + (element.name.empty() ? "" : " as " + element.name));
            for (const auto& property : element.properties) {
                if (const auto* text = std::get_if<ir::Text>(&property.value)) {
                    print_line(depth + 1, property.name + ":");
                    print_text(*text, depth + 2);
                } else {
                    print_line(depth + 1, property.name + ": " + format_inline(property.value));
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
        }
    }

    // 고른 target들(없으면 전부)의 파일을 쓴다
    int build(const ir::Document& document, const std::vector<std::string>& selected, const std::filesystem::path& base_dir, const std::filesystem::path& libs_dir) {
        bool failed = false;
        for (const auto& name : selected) {
            const bool exists = std::any_of(document.targets.begin(), document.targets.end(), [&](const ir::Target& target) { return target.name == name; });
            if (!exists) {
                std::cerr << "error: unknown target: " << name << '\n';
                failed = true;
            }
        }
        if (failed) {
            return 1;
        }
        if (document.targets.empty()) {
            std::cerr << "error: no target to build\n";
            return 1;
        }
        for (const auto& target : document.targets) {
            if (!selected.empty() && std::find(selected.begin(), selected.end(), target.name) == selected.end()) {
                continue;
            }
            std::vector<std::string> warnings;
            const auto errors = templide::backend::write_target(document, target, base_dir, libs_dir, warnings);
            for (const auto& message : warnings) {
                std::cerr << message << '\n';
            }
            for (const auto& message : errors) {
                std::cerr << "error: target " << target.name << ": " << message << '\n';
            }
            if (errors.empty()) {
                std::cout << target.name << ": wrote " << target.path << '\n';
            } else {
                failed = true;
            }
        }
        return failed ? 1 : 0;
    }
}

// templide [--ast | --ir] [--target <name>]... [file]
// templide --serve
// 기본은 target 파일을 만든다. --ast는 parser의 AST를, --ir은 middle end의 IR을 출력한다.
// --serve는 편집기와 stdin/stdout으로 대화하는 언어 서버다 (server/server.h)
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--serve") {
        return templide::server::serve(templide::middleend::default_packages_dir(argv[0]), templide::middleend::default_libs_dir(argv[0]));
    }
    enum class Mode { BUILD, AST, IR } mode = Mode::BUILD;
    std::vector<std::string> selected;
    std::string path = "C:\\Users\\USER\\Desktop\\templide\\example.tlide";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ast") {
            mode = Mode::AST;
        } else if (argument == "--ir") {
            mode = Mode::IR;
        } else if (argument == "--target") {
            if (i + 1 >= argc) {
                std::cerr << "error: --target needs a target name\n";
                return 1;
            }
            selected.emplace_back(argv[++i]);
        } else {
            path = argument;
        }
    }
    if (mode == Mode::AST) {
        return dump_ast(path);
    }

    const auto result = templide::middleend::analyze(path, templide::middleend::default_packages_dir(argv[0]));
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.path;
        if (diagnostic.line > 0) {
            std::cerr << ':' << diagnostic.line << ':' << diagnostic.column;
        }
        std::cerr << ": error: " << diagnostic.message << '\n';
    }
    if (!result.document) {
        return 1;
    }
    if (mode == Mode::IR) {
        print_document(*result.document);
        return 0;
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return build(*result.document, selected, (error ? std::filesystem::path(path) : absolute).parent_path(), templide::middleend::default_libs_dir(argv[0]));
}

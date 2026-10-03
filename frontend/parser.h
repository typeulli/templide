#pragma once

#include "lexor.h"
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace templide::parser {
    using std::string;
    using std::vector;
    using std::string_view;
    using namespace templide::lexor;
    namespace ast {
        enum NodeType {
            FILE,
            INCLUDE,
            ENUM,

            STYLE,
            PARAMETER,
            TEMPLATE,
            OBJECT,
            MASTER,
            MASTER_CASE,
            SLIDE,
            TARGET,
            VAR,
            PUT,
            COMMENT,
            TRANSITION,
            GROUP,
            THEME,
            SECTION,
            ANIMATE,
            REVIEW,

            IF,
            ELSE_IF,
            ELSE,
            FOR,

            STYLE_ASSIGN,
            INLINE_STYLE,
            COLOR_RGB,
            COLOR_RGBA,
            COLOR_HEX,

            ASSIGN,

            ADD,
            MINUS,
            MULTIPLY,
            EQUAL,

            CALL,
            MEMBER,
            CONTEXT,
            LIST,
            DIMENSION,

            NAME,
            IDENTIFIER,
            INT,
            FLOAT,
            STRING,
            FSTRING,
            TEXT,
        };

        struct ASTNode {
            NodeType type;
            Token token;
            // 노드 전체의 원문. token처럼 원문 문자열을 가리키므로 원문의 시작과 비교해 위치를 얻는다.
            // 문장은 끝의 ';'나 '}'까지, 괄호로 감싼 식은 괄호까지 들어간다. parser가 채우기 전에는 token과 같다
            string_view span;
            ASTNode(NodeType type, Token token) : type(type), token(token), span(token.value) {}
        };



        struct ASTFString : ASTNode {
            std::vector<ASTNode*> parts;
            ASTFString(Token token) : ASTNode(FSTRING, token) {}
        };
        struct ASTName : ASTNode {
            std::string name;
            ASTName(Token token) : ASTNode(NAME, token), name(std::string(token.value)) {}
        };
        struct ASTIdentifier : ASTNode {
            std::string name;
            ASTIdentifier(Token token) : ASTNode(IDENTIFIER, token), name(std::string(token.value)) {}
        };
        struct ASTInt : ASTNode {
            long long value;
            ASTInt(Token token) : ASTNode(INT, token), value(std::stoll(std::string(token.value))) {}
        };
        struct ASTFloat : ASTNode {
            long double value;
            ASTFloat(Token token) : ASTNode(FLOAT, token), value(std::stold(std::string(token.value))) {}
        };
        struct ASTString : ASTNode {
            std::string value;
            // 문자열 리터럴 토큰이면 앞뒤 따옴표를 뺀 내용만 저장
            ASTString(Token token) : ASTNode(STRING, token), value(std::string(token.type == TokenType::STRING ? token.value.substr(1, token.value.size() - 2) : token.value)) {}
        };

        // style 참조와 문자열 등을 나열한 식. bold yellow "Hello" reset " world"
        struct ASTText : ASTNode {
            std::vector<ASTNode*> parts;
            ASTText(Token token) : ASTNode(TEXT, token) {}
        };
        enum class ListKind {
            BULLETS,
            NUMBERS,
            DASHES,
            PARAGRAPHS, // 기호 없이 문단만 나눈다
        };
        struct ASTList : ASTNode {
            ListKind kind;
            std::vector<ASTNode*> items;
            ASTList(Token token, ListKind kind) : ASTNode(LIST, token), kind(kind) {}
        };
        // 단위가 붙은 숫자. 200px, 50%
        struct ASTDimension : ASTNode {
            ASTNode* value;
            std::string unit;
            ASTDimension(Token token, ASTNode* value, std::string unit) : ASTNode(DIMENSION, token), value(value), unit(std::move(unit)) {}
        };
        struct ASTCall : ASTNode {
            ASTName* name;
            std::vector<ASTNode*> arguments;
            ASTCall(Token token, ASTName* name, std::vector<ASTNode*> arguments) : ASTNode(CALL, token), name(name), arguments(std::move(arguments)) {}
        };
        struct ASTMember : ASTNode {
            ASTNode* object;
            ASTName* member;
            ASTMember(Token token, ASTNode* object, ASTName* member) : ASTNode(MEMBER, token), object(object), member(member) {}
        };
        // @slide 처럼 '@'로 참조하는 이름
        struct ASTContext : ASTNode {
            ASTName* name;
            ASTContext(Token token, ASTName* name) : ASTNode(CONTEXT, token), name(name) {}
        };


        struct ASTAdd : ASTNode {
            ASTNode* left;
            ASTNode* right;
            ASTAdd(Token token, ASTNode* left, ASTNode* right) : ASTNode(ADD, token), left(left), right(right) {}
        };
        struct ASTMinus : ASTNode {
            ASTNode* left;
            ASTNode* right;
            ASTMinus(Token token, ASTNode* left, ASTNode* right) : ASTNode(MINUS, token), left(left), right(right) {}
        };
        struct ASTMultiply : ASTNode {
            ASTNode* left;
            ASTNode* right;
            ASTMultiply(Token token, ASTNode* left, ASTNode* right) : ASTNode(MULTIPLY, token), left(left), right(right) {}
        };
        struct ASTEqual : ASTNode {
            ASTNode* left;
            ASTNode* right;
            ASTEqual(Token token, ASTNode* left, ASTNode* right) : ASTNode(EQUAL, token), left(left), right(right) {}
        };
        struct ASTStyleAssign : ASTNode {
            ASTIdentifier* identifier;
            ASTNode* expression;
            ASTStyleAssign(Token token, ASTIdentifier* identifier, ASTNode* expression) : ASTNode(STYLE_ASSIGN, token), identifier(identifier), expression(expression) {}
        };
        // 이름 없이 text 안에 바로 쓰는 style. style(color = hex(F00), font-size = 20pt)
        struct ASTInlineStyle : ASTNode {
            std::vector<ASTStyleAssign*> properties;
            ASTInlineStyle(Token token) : ASTNode(INLINE_STYLE, token) {}
        };
        struct ASTColorRGB : ASTNode {
            ASTNode* r;
            ASTNode* g;
            ASTNode* b;
            ASTColorRGB(Token token, ASTNode* r, ASTNode* g, ASTNode* b) : ASTNode(COLOR_RGB, token), r(r), g(g), b(b) { type = COLOR_RGB; }
        };
        struct ASTColorRGBA : ASTNode {
            ASTNode* r;
            ASTNode* g;
            ASTNode* b;
            ASTNode* a;

            ASTColorRGBA(Token token, ASTNode* r, ASTNode* g, ASTNode* b, ASTNode* a) : ASTNode(COLOR_RGBA, token), r(r), g(g), b(b), a(a) { type = COLOR_RGBA; }
        };
        struct ASTColorHex :  ASTNode {
            ASTNode* hex;
            ASTColorHex(Token token, ASTNode* hex) : ASTNode(COLOR_HEX, token), hex(hex) { type = COLOR_HEX; }
        };

        struct ASTAssign : ASTNode {
            ASTName* name;
            ASTNode* expression;
            ASTAssign(Token token, ASTName* name, ASTNode* expression) : ASTNode(ASSIGN, token), name(name), expression(expression) { type = ASSIGN; }
        };

        struct ASTVar : ASTNode {
            ASTName* type_name;
            ASTName* name;
            ASTNode* default_value; // 기본값이 없으면 nullptr
            ASTVar(Token token, ASTName* type_name, ASTName* name, ASTNode* default_value) : ASTNode(VAR, token), type_name(type_name), name(name), default_value(default_value) {}
        };
        struct ASTParameter : ASTNode {
            ASTName* type_name;
            ASTName* name;
            ASTParameter(Token token, ASTName* type_name, ASTName* name) : ASTNode(PARAMETER, token), type_name(type_name), name(name) {}
        };
        // #include <path/file>은 packages 폴더에서, #include "path/file"은 이 파일의 폴더에서 찾는다
        struct ASTInclude : ASTNode {
            std::string name;
            bool relative;
            ASTInclude(Token token, std::string name, bool relative) : ASTNode(INCLUDE, token), name(std::move(name)), relative(relative) {}
        };
        struct ASTEnum : ASTNode {
            std::string name;
            Token name_token{}; // 이름 토큰. 편집기가 이름의 위치를 찾는 데 쓴다
            std::vector<ASTName*> members;
            ASTEnum(Token token) : ASTNode(ENUM, token) {}
        };
        // slide의 발표자 메모
        struct ASTComment : ASTNode {
            ASTNode* expression;
            ASTComment(Token token, ASTNode* expression) : ASTNode(COMMENT, token), expression(expression) {}
        };
        // transition wipe.up 0.5s;
        struct ASTTransition : ASTNode {
            ASTName* kind;
            ASTName* option;    // 없으면 nullptr
            ASTNode* duration;  // 없으면 nullptr
            ASTTransition(Token token, ASTName* kind, ASTName* option, ASTNode* duration) : ASTNode(TRANSITION, token), kind(kind), option(option), duration(duration) {}
        };
        // group [as NAME] { ... }
        struct ASTAnimate;
        struct ASTGroup : ASTNode {
            ASTName* alias = nullptr; // 없으면 nullptr
            std::vector<ASTNode*> body;
            std::vector<ASTAnimate*> animations; // 블록 안의 animate 문장
            ASTGroup(Token token) : ASTNode(GROUP, token) {}
        };
        // section "이름"; 뒤에 오는 slide들이 이 구역에 들어간다
        struct ASTSection : ASTNode {
            ASTNode* name;
            ASTSection(Token token, ASTNode* name) : ASTNode(SECTION, token), name(name) {}
        };
        // animate TARGET CATEGORY EFFECT[.OPTION] [path "..."] [DURATION] [with_previous | after_previous | on_click] [delay DURATION] [order N];
        // put, group 블록 안에서는 TARGET 없이 그 개체에 건다
        struct ASTAnimate : ASTNode {
            ASTName* target = nullptr;   // put, group 블록 안이면 nullptr
            ASTName* category = nullptr;
            ASTName* effect = nullptr;
            ASTName* option = nullptr;   // 없으면 nullptr
            ASTNode* path = nullptr;     // move path "..."일 때만
            ASTNode* duration = nullptr; // 없으면 nullptr
            ASTName* start = nullptr;    // 없으면 nullptr (클릭할 때)
            ASTNode* delay = nullptr;    // 없으면 nullptr
            ASTNode* order = nullptr;    // 없으면 nullptr. 슬라이드 안에서 재생하는 차례
            ASTAnimate(Token token) : ASTNode(ANIMATE, token) {}
        };
        // review { text = ...; author = ...; x = ...; y = ...; }
        struct ASTReview : ASTNode {
            std::vector<ASTNode*> body;
            ASTReview(Token token) : ASTNode(REVIEW, token) {}
        };

        struct ASTIf : ASTNode {
            std::vector<ASTNode*> body;
            ASTNode* condition;
            ASTNode* branch;
            ASTIf(Token token, ASTNode* condition, ASTNode* branch) : ASTNode(IF, token), condition(condition), branch(branch) { type = IF; }
        };
        struct ASTElseIf : ASTNode {
            std::vector<ASTNode*> body;
            ASTNode* condition;
            ASTNode* branch;
            ASTElseIf(Token token, ASTNode* condition, ASTNode* branch) : ASTNode(ELSE_IF, token), condition(condition), branch(branch) { type = ELSE_IF; }
        };
        struct ASTElse : ASTNode {
            std::vector<ASTNode*> body;
            ASTElse(Token token) : ASTNode(ELSE, token) { type = ELSE; }
        };
        // for (int i in 1..n) 또는 for (string s in ["a", "b"])
        struct ASTFor : ASTNode {
            ASTName* type_name;
            ASTName* name;
            ASTNode* range_start;        // 목록이면 nullptr
            ASTNode* range_end;
            std::vector<ASTNode*> items; // 범위면 비어 있다
            std::vector<ASTNode*> body;
            ASTFor(Token token, ASTName* type_name, ASTName* name) : ASTNode(FOR, token), type_name(type_name), name(name), range_start(nullptr), range_end(nullptr) {}
        };


        #define TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(class_name, node_type) \
            struct class_name : ASTNode { \
                std::string name; \
                Token name_token{}; /* 이름 토큰. 편집기가 이름의 위치를 찾는 데 쓴다 */ \
                std::vector<ASTNode*> body; \
                class_name(Token token) : ASTNode(node_type, token) {} \
            };

        TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(ASTTemplate, TEMPLATE)
        TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(ASTObject, OBJECT)
        TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(ASTMasterCase, MASTER_CASE)
        TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(ASTTarget, TARGET)
        TEMPLIDE_AST_DEF_NAMED_NODE_BLOCK(ASTTheme, THEME)

        // put NAME [as ALIAS] { ... }
        struct ASTPut : ASTNode {
            std::string name;
            Token name_token{}; // 이름 토큰. 편집기가 이름의 위치를 찾는 데 쓴다
            ASTName* alias = nullptr; // 없으면 nullptr
            std::vector<ASTNode*> body; // NAME = VALUE; 대입만 들어 있다
            std::vector<ASTAnimate*> animations; // 블록 안의 animate 문장
            ASTPut(Token token) : ASTNode(PUT, token) {}
        };

        struct ASTStyle : ASTNode {
            std::string name;
            Token name_token{}; // 이름 토큰. 편집기가 이름의 위치를 찾는 데 쓴다
            std::vector<ASTParameter*> parameters;
            std::vector<ASTNode*> body;
            ASTStyle(Token token) : ASTNode(STYLE, token) {}
        };
        struct ASTSlide : ASTNode {
            std::vector<ASTNode*> body;
            ASTSlide(Token token) : ASTNode(SLIDE, token) {}
        };
        // master NAME(매개변수) { theme = ...; case ... }
        struct ASTMaster : ASTNode {
            std::string name;
            Token name_token{}; // 이름 토큰. 편집기가 이름의 위치를 찾는 데 쓴다
            std::vector<ASTParameter*> parameters;
            std::vector<ASTNode*> body;       // case만 들어 있다
            std::vector<ASTNode*> properties; // theme = ...; 같은 대입
            ASTMaster(Token token) : ASTNode(MASTER, token) {}
        };


        struct ASTFile : ASTNode {
            std::vector<ASTNode*> body;
            std::string filepath;
            ASTFile(Token token) : ASTNode(FILE, token) {}
        };

    }

    struct ParseError {
        std::string message;
        std::size_t line;
        std::size_t column;
        std::size_t length;
        TokenType type; // 오류를 낸 토큰의 종류. 복구할 때 어디까지 건너뛸지 정한다
        void setToken(Token token) {
            line = token.line;
            column = token.column;
            length = token.length();
            type = token.type;
        }
    };
    struct Parser {
        // 블록 안의 문장 하나를 파싱하는 함수. 블록 종류마다 허용하는 문장이 다르다
        using StatementParser = ast::ASTNode* (Parser::*)(Token);

        string path;
        Lexor& lexer;
        ParseError error;
        vector<ParseError> errors; // 문장 단위로 복구하며 찾은 오류 모두. error는 그 첫 번째
        std::optional<Token> lookahead;
        const char* consumed_end = nullptr; // 마지막으로 읽은 토큰의 끝
        explicit Parser(string path, Lexor& lexer) : path(std::move(path)), lexer(lexer), error() {}

        Token next() {
            Token token = lookahead.has_value() ? *lookahead : lexer.next();
            lookahead.reset();
            consumed_end = token.value.data() + token.value.size();
            return token;
        }

        // begin부터 마지막으로 읽은 토큰의 끝까지를 node의 span으로 정한다
        template <typename T>
        T* spanned(T* node, const char* begin) const {
            node->span = string_view(begin, static_cast<std::size_t>(consumed_end - begin));
            return node;
        }

        Token peek() {
            if (!lookahead.has_value()) {
                lookahead = lexer.next();
            }
            return *lookahead;
        }

        bool accept(TokenType type) {
            if (peek().type != type) {
                return false;
            }
            next();
            return true;
        }

        Token expect(TokenType type, const string& message) {
            Token token = next();
            if (token.type != type) {
                fail(token, "Expected " + message + ", but got " + describe(token));
            }
            return token;
        }

        [[noreturn]] static void fail(const Token& token, const string& message) {
            ParseError parse_error{};
            parse_error.message = message;
            parse_error.setToken(token);
            throw parse_error;
        }

        [[noreturn]] static void fail_unexpected(const Token& token) {
            if (is_keyword(token, "else")) {
                fail(token, "'else' without preceding 'if'");
            }
            if (token.type == TokenType::IDENTIFIER) {
                fail(token, "Unexpected identifier: " + string(token.value));
            }
            fail(token, "Unexpected token: " + describe(token));
        }

        static string describe(const Token& token) {
            if (token.type == TokenType::END_OF_FILE) {
                return "end of file";
            }
            if (token.value.starts_with("/*")) {
                return "unterminated comment";
            }
            return "'" + string(token.value) + "'";
        }

        static bool is_keyword(const Token& token, string_view keyword) {
            return token.type == TokenType::IDENTIFIER && token.value == keyword;
        }

        static std::optional<ast::ListKind> list_kind(const Token& token) {
            if (is_keyword(token, "bullets")) { return ast::ListKind::BULLETS; }
            if (is_keyword(token, "numbers")) { return ast::ListKind::NUMBERS; }
            if (is_keyword(token, "dashes")) { return ast::ListKind::DASHES; }
            if (is_keyword(token, "paragraphs")) { return ast::ListKind::PARAGRAPHS; }
            return std::nullopt;
        }

        // 문자열 안의 \n, \t, \", \\, \{, \}를 푼다. \n은 같은 문단 안의 줄바꿈이다
        static string unescape(const Token& token, string_view text) {
            string result;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '\\') {
                    result += text[i];
                    continue;
                }
                const char c = i + 1 < text.size() ? text[++i] : '\0';
                switch (c) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case '"': case '\\': case '{': case '}': result += c; break;
                    default: fail(token, "Unknown escape '\\" + string(1, c) + "' in string (\\n, \\t, \\\", \\\\, \\{, \\})");
                }
            }
            return result;
        }

        // 두 토큰 사이에 공백이 없는지
        static bool adjacent(const Token& left, const Token& right) {
            return left.value.data() + left.value.size() == right.value.data();
        }

        // left부터 right까지의 원문을 하나의 토큰으로 합친다
        static Token merge(const Token& left, const Token& right) {
            const auto length = static_cast<std::size_t>(right.value.data() + right.value.size() - left.value.data());
            return {left.type, string_view(left.value.data(), length), left.line, left.column};
        }

        // TEXT 식에서 다음 항을 시작할 수 있는 토큰인지
        static bool starts_term(TokenType type) {
            switch (type) {
                case TokenType::IDENTIFIER:
                case TokenType::NUMBER:
                case TokenType::STRING:
                case TokenType::FSTRING_BEGIN:
                case TokenType::LBRACKET:
                case TokenType::LPAREN:
                case TokenType::AT:
                    return true;
                default:
                    return false;
            }
        }

        // expression := sequence ('==' sequence)*
        // sequence   := additive additive*        (항이 둘 이상이면 TEXT)
        // additive   := multiplicative (('+' | '-') multiplicative)*
        // multiplicative := postfix ('*' postfix)*
        // postfix    := primary ('.' IDENTIFIER)*
        ast::ASTNode* parse_expression() {
            auto *left = parse_sequence();
            while (peek().type == TokenType::EQ) {
                Token op = next();
                auto *right = parse_sequence();
                left = spanned(new ast::ASTEqual(op, left, right), left->span.data());
            }
            return left;
        }

        ast::ASTNode* parse_sequence() {
            Token start = peek();
            auto *first = parse_additive();
            if (!starts_term(peek().type)) {
                return first;
            }
            auto *node = new ast::ASTText(start);
            node->parts.push_back(first);
            while (starts_term(peek().type)) {
                node->parts.push_back(parse_additive());
            }
            return spanned(node, first->span.data());
        }

        ast::ASTNode* parse_additive() {
            auto *left = parse_multiplicative();
            while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
                Token op = next();
                auto *right = parse_multiplicative();
                if (op.type == TokenType::PLUS) {
                    left = spanned(new ast::ASTAdd(op, left, right), left->span.data());
                } else {
                    left = spanned(new ast::ASTMinus(op, left, right), left->span.data());
                }
            }
            return left;
        }

        ast::ASTNode* parse_multiplicative() {
            auto *left = parse_postfix();
            while (peek().type == TokenType::STAR) {
                Token op = next();
                auto *right = parse_postfix();
                left = spanned(new ast::ASTMultiply(op, left, right), left->span.data());
            }
            return left;
        }

        ast::ASTNode* parse_postfix() {
            auto *node = parse_primary();
            while (peek().type == TokenType::DOT) {
                Token dot = next();
                Token member = expect(TokenType::IDENTIFIER, "member name after '.'");
                node = spanned(new ast::ASTMember(dot, node, new ast::ASTName(member)), node->span.data());
            }
            return node;
        }

        ast::ASTNode* parse_primary() {
            const char* begin = peek().value.data();
            return spanned(parse_atom(), begin);
        }

        ast::ASTNode* parse_atom() {
            Token token = next();
            switch (token.type) {
                case TokenType::IDENTIFIER: {
                    if (const auto kind = list_kind(token)) {
                        expect(TokenType::LBRACKET, "'[' after '" + string(token.value) + "'");
                        return parse_list(token, *kind);
                    }
                    if (peek().type != TokenType::LPAREN) {
                        return new ast::ASTName(token);
                    }
                    if (token.value == "rgb") {
                        auto arguments = parse_arguments(token, 3);
                        return new ast::ASTColorRGB(token, arguments[0], arguments[1], arguments[2]);
                    }
                    if (token.value == "rgba") {
                        auto arguments = parse_arguments(token, 4);
                        return new ast::ASTColorRGBA(token, arguments[0], arguments[1], arguments[2], arguments[3]);
                    }
                    if (token.value == "hex") {
                        return parse_hex(token);
                    }
                    if (token.value == "style") {
                        return parse_inline_style(token);
                    }
                    return new ast::ASTCall(token, new ast::ASTName(token), parse_arguments());
                }
                case TokenType::NUMBER:
                    return parse_number(token);
                case TokenType::STRING: {
                    auto *node = new ast::ASTString(token);
                    node->value = unescape(token, node->value);
                    return node;
                }
                case TokenType::FSTRING_BEGIN:
                    return parse_fstring(token);
                case TokenType::LBRACKET:
                    fail(token, "A list must follow 'bullets', 'numbers' or 'dashes'");
                case TokenType::LPAREN: {
                    auto *node = parse_expression();
                    expect(TokenType::RPAREN, "')' after expression");
                    return node;
                }
                case TokenType::MINUS: {
                    // 음수 리터럴. -10px처럼 '-' 바로 뒤에 숫자가 붙어 있어야 한다
                    Token digits = next();
                    if (digits.type != TokenType::NUMBER || !adjacent(token, digits)) {
                        fail(digits, "Expected a number right after '-'");
                    }
                    Token number = merge(token, digits);
                    number.type = TokenType::NUMBER;
                    return parse_number(number);
                }
                case TokenType::AT: {
                    Token name = expect(TokenType::IDENTIFIER, "name after '@'");
                    return new ast::ASTContext(token, new ast::ASTName(name));
                }
                default:
                    fail(token, "Expected expression, but got " + describe(token));
            }
        }

        // 숫자 바로 뒤에 붙은 식별자나 '%'는 단위로 본다. 200px, 1.5px, 50%
        ast::ASTNode* parse_number(Token token) {
            ast::ASTNode* value;
            try {
                if (token.value.find('.') != string_view::npos) {
                    value = new ast::ASTFloat(token);
                } else {
                    value = new ast::ASTInt(token);
                }
            } catch (const std::out_of_range&) {
                fail(token, "Number literal out of range: " + string(token.value));
            }
            Token unit = peek();
            if ((unit.type == TokenType::IDENTIFIER || unit.type == TokenType::PERCENT) && adjacent(token, unit)) {
                next();
                return new ast::ASTDimension(merge(token, unit), value, string(unit.value));
            }
            return value;
        }

        ast::ASTFString* parse_fstring(Token token) {
            auto *node = new ast::ASTFString(token);
            while (true) {
                Token part = next();
                switch (part.type) {
                    case TokenType::FSTRING_TEXT: {
                        auto *text = new ast::ASTString(part);
                        text->value = unescape(part, text->value);
                        node->parts.push_back(text);
                        break;
                    }
                    case TokenType::LBRACE:
                        node->parts.push_back(parse_expression());
                        expect(TokenType::RBRACE, "'}' after f-string expression");
                        break;
                    case TokenType::FSTRING_END:
                        return node;
                    default:
                        fail(part, "Unterminated f-string");
                }
            }
        }

        ast::ASTList* parse_list(Token token, ast::ListKind kind) {
            auto *node = new ast::ASTList(token, kind);
            if (accept(TokenType::RBRACKET)) {
                return node;
            }
            do {
                node->items.push_back(parse_expression());
            } while (accept(TokenType::COMMA));
            expect(TokenType::RBRACKET, "']' after list items");
            return node;
        }

        vector<ast::ASTNode*> parse_arguments() {
            expect(TokenType::LPAREN, "'('");
            vector<ast::ASTNode*> arguments;
            if (accept(TokenType::RPAREN)) {
                return arguments;
            }
            do {
                arguments.push_back(parse_expression());
            } while (accept(TokenType::COMMA));
            expect(TokenType::RPAREN, "')' after arguments");
            return arguments;
        }

        // style(이름 = 식, ...). 속성 이름은 style 선언처럼 font-size 같은 '-' 이름을 쓴다
        ast::ASTInlineStyle* parse_inline_style(Token token) {
            auto *node = new ast::ASTInlineStyle(token);
            expect(TokenType::LPAREN, "'(' after 'style'");
            if (accept(TokenType::RPAREN)) {
                return node;
            }
            do {
                Token name = parse_hyphenated_name(expect(TokenType::IDENTIFIER, "style property name"));
                Token assign = expect(TokenType::ASSIGN, "'=' after style property name");
                node->properties.push_back(spanned(new ast::ASTStyleAssign(assign, new ast::ASTIdentifier(name), parse_expression()), name.value.data()));
            } while (accept(TokenType::COMMA));
            expect(TokenType::RPAREN, "')' after style properties");
            return node;
        }

        vector<ast::ASTNode*> parse_arguments(const Token& callee, std::size_t count) {
            auto arguments = parse_arguments();
            if (arguments.size() != count) {
                fail(callee, string(callee.value) + "() expects " + std::to_string(count) + " arguments, but got " + std::to_string(arguments.size()));
            }
            return arguments;
        }

        // lexer는 00ff00을 NUMBER(00), IDENTIFIER(ff00)로 나누므로 붙어 있는 토큰을 다시 합친다
        ast::ASTColorHex* parse_hex(Token token) {
            expect(TokenType::LPAREN, "'(' after 'hex'");
            Token digits = next();
            if (digits.type != TokenType::NUMBER && digits.type != TokenType::IDENTIFIER) {
                fail(digits, "Expected hex digits, but got " + describe(digits));
            }
            while ((peek().type == TokenType::NUMBER || peek().type == TokenType::IDENTIFIER) && adjacent(digits, peek())) {
                digits = merge(digits, next());
            }
            for (const char c : digits.value) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    fail(digits, "Invalid hex color: " + string(digits.value));
                }
            }
            expect(TokenType::RPAREN, "')' after hex digits");
            return new ast::ASTColorHex(token, new ast::ASTString(digits));
        }

        // 오류를 남기고 다음 문장 앞까지 건너뛴다. 바깥 블록이 같은 오류(파일 끝)를 다시 만나면 한 번만 남긴다
        // 오류를 낸 토큰이 지금 블록을 닫는 '}'였으면 true
        bool recover(const ParseError& parse_error) {
            const ParseError* last = errors.empty() ? nullptr : &errors.back();
            if (last == nullptr || last->line != parse_error.line || last->column != parse_error.column || last->message != parse_error.message) {
                errors.push_back(parse_error);
            }
            std::size_t depth = 0;
            // 오류를 낸 토큰을 이미 읽었으면 그 토큰부터 따진다. 아직 lookahead에 있으면 아래에서 읽는다
            const bool consumed = !lookahead.has_value() || lookahead->line != parse_error.line || lookahead->column != parse_error.column;
            if (consumed) {
                switch (parse_error.type) {
                    case TokenType::RBRACE:
                        return true;
                    case TokenType::SEMICOLON:
                    case TokenType::END_OF_FILE:
                        return false;
                    case TokenType::LBRACE:
                        depth = 1;
                        break;
                    default:
                        break;
                }
            }
            // ';'까지, 또는 짝이 맞는 '{ ... }'까지 건너뛴다. 지금 블록의 '}'는 블록이 읽도록 남긴다
            while (true) {
                const Token token = peek();
                switch (token.type) {
                    case TokenType::END_OF_FILE:
                        return false;
                    case TokenType::LBRACE:
                        ++depth;
                        break;
                    case TokenType::RBRACE:
                        if (depth == 0) {
                            return false;
                        }
                        if (--depth == 0) {
                            next();
                            return false;
                        }
                        break;
                    case TokenType::SEMICOLON:
                        if (depth == 0) {
                            next();
                            return false;
                        }
                        break;
                    default:
                        break;
                }
                next();
            }
        }

        // '{' 부터 '}' 까지. 블록 안의 빈 ';'는 건너뛴다. 문장에 오류가 있으면 남기고 다음 문장부터 다시 읽는다
        void parse_block(vector<ast::ASTNode*>& body, StatementParser parse_statement) {
            expect(TokenType::LBRACE, "'{'");
            while (true) {
                Token token = next();
                switch (token.type) {
                    case TokenType::RBRACE:
                        return;
                    case TokenType::SEMICOLON:
                        break;
                    case TokenType::END_OF_FILE:
                        fail(token, "Unexpected end of file in block");
                    default:
                        try {
                            body.push_back(spanned((this->*parse_statement)(token), token.value.data()));
                        } catch (const ParseError& parse_error) {
                            if (recover(parse_error)) {
                                return;
                            }
                        }
                        break;
                }
            }
        }

        template <typename T>
        T* parse_named_block(Token token, StatementParser parse_statement) {
            Token name = expect(TokenType::IDENTIFIER, "name after '" + string(token.value) + "'");
            auto *node = new T(token);
            node->name = string(name.value);
            node->name_token = name;
            parse_block(node->body, parse_statement);
            return node;
        }

        // put NAME [as ALIAS] { ... }
        ast::ASTPut* parse_put(Token token) {
            Token name = expect(TokenType::IDENTIFIER, "name after 'put'");
            auto *node = new ast::ASTPut(token);
            node->name = string(name.value);
            node->name_token = name;
            node->alias = parse_alias();
            parse_block(node->body, &Parser::parse_put_statement);
            split_animations(node->body, node->animations);
            return node;
        }

        // put 블록 안의 NAME = VALUE; 또는 animate ...;
        ast::ASTNode* parse_put_statement(Token token) {
            if (is_keyword(token, "animate") && peek().type != TokenType::ASSIGN) { return parse_animate(token, false); }
            return parse_property_statement(token);
        }

        // 블록의 animate 문장을 body에서 animations로 옮긴다
        static void split_animations(vector<ast::ASTNode*>& body, vector<ast::ASTAnimate*>& animations) {
            vector<ast::ASTNode*> rest;
            for (auto *statement : body) {
                if (statement->type == ast::ANIMATE) {
                    animations.push_back(static_cast<ast::ASTAnimate*>(statement));
                } else {
                    rest.push_back(statement);
                }
            }
            body = std::move(rest);
        }

        // 'as NAME'이 있으면 그 이름
        ast::ASTName* parse_alias() {
            if (!is_keyword(peek(), "as")) {
                return nullptr;
            }
            next();
            return new ast::ASTName(expect(TokenType::IDENTIFIER, "name after 'as'"));
        }

        // group [as NAME] { put, group, if, for }
        ast::ASTGroup* parse_group(Token token) {
            auto *node = new ast::ASTGroup(token);
            node->alias = parse_alias();
            parse_block(node->body, &Parser::parse_group_statement);
            split_animations(node->body, node->animations);
            return node;
        }

        ast::ASTNode* parse_group_statement(Token token) {
            if (is_keyword(token, "put")) { return parse_put(token); }
            if (is_keyword(token, "group")) { return parse_group(token); }
            if (is_keyword(token, "if")) { return parse_if(token, &Parser::parse_group_statement); }
            if (is_keyword(token, "for")) { return parse_for(token, &Parser::parse_group_statement); }
            if (is_keyword(token, "animate")) { return parse_animate(token, false); }
            fail_unexpected(token);
        }

        ast::ASTVar* parse_var(Token token) {
            Token type_name = expect(TokenType::IDENTIFIER, "type after 'var'");
            Token name = expect(TokenType::IDENTIFIER, "variable name after type");
            ast::ASTNode *default_value = nullptr;
            if (accept(TokenType::ASSIGN)) {
                default_value = parse_expression();
            }
            expect(TokenType::SEMICOLON, "';' after variable declaration");
            return new ast::ASTVar(token, new ast::ASTName(type_name), new ast::ASTName(name), default_value);
        }

        ast::ASTEnum* parse_enum(Token token) {
            Token name = expect(TokenType::IDENTIFIER, "name after 'enum'");
            auto *node = new ast::ASTEnum(token);
            node->name = string(name.value);
            node->name_token = name;
            expect(TokenType::LBRACE, "'{' after enum name");
            do {
                node->members.push_back(new ast::ASTName(expect(TokenType::IDENTIFIER, "enum member name")));
            } while (accept(TokenType::COMMA));
            expect(TokenType::RBRACE, "'}' after enum members");
            return node;
        }

        ast::ASTAssign* parse_assign(Token name) {
            Token assign = expect(TokenType::ASSIGN, "'=' after property name");
            auto *expression = parse_expression();
            expect(TokenType::SEMICOLON, "';' after expression");
            return new ast::ASTAssign(assign, new ast::ASTName(name), expression);
        }

        // font-weight 처럼 '-'로 이어진 속성 이름을 하나의 토큰으로 합친다
        Token parse_hyphenated_name(Token name) {
            while (peek().type == TokenType::MINUS && adjacent(name, peek())) {
                Token minus = next();
                Token part = next();
                if (part.type != TokenType::IDENTIFIER || !adjacent(minus, part)) {
                    fail(part, "Expected identifier after '-' in property name, but got " + describe(part));
                }
                name = merge(name, part);
            }
            return name;
        }

        ast::ASTStyleAssign* parse_style_assign(Token token) {
            Token name = parse_hyphenated_name(token);
            Token assign = expect(TokenType::ASSIGN, "'=' after style property name");
            auto *expression = parse_expression();
            expect(TokenType::SEMICOLON, "';' after expression");
            return new ast::ASTStyleAssign(assign, new ast::ASTIdentifier(name), expression);
        }

        ast::ASTNode* parse_condition() {
            expect(TokenType::LPAREN, "'(' before condition");
            auto *condition = parse_expression();
            expect(TokenType::RPAREN, "')' after condition");
            return condition;
        }

        // else if, else는 앞 if의 branch로 연결한다. 블록 안의 문장은 if를 감싼 블록의 규칙을 따른다
        ast::ASTIf* parse_if(Token token, StatementParser parse_statement) {
            auto *node = new ast::ASTIf(token, parse_condition(), nullptr);
            parse_block(node->body, parse_statement);
            node->branch = parse_branch(parse_statement);
            return node;
        }

        ast::ASTNode* parse_branch(StatementParser parse_statement) {
            Token token = peek();
            if (!is_keyword(token, "else")) {
                return nullptr;
            }
            next();
            if (is_keyword(peek(), "if")) {
                next();
                auto *node = new ast::ASTElseIf(token, parse_condition(), nullptr);
                parse_block(node->body, parse_statement);
                node->branch = parse_branch(parse_statement);
                return node;
            }
            auto *node = new ast::ASTElse(token);
            parse_block(node->body, parse_statement);
            return node;
        }

        // for (TYPE NAME in START..END) 또는 for (TYPE NAME in [ITEM, ...])
        ast::ASTFor* parse_for(Token token, StatementParser parse_statement) {
            expect(TokenType::LPAREN, "'(' after 'for'");
            Token type_name = expect(TokenType::IDENTIFIER, "type in for");
            Token name = expect(TokenType::IDENTIFIER, "variable name after type");
            Token in = next();
            if (!is_keyword(in, "in")) {
                fail(in, "Expected 'in', but got " + describe(in));
            }
            auto *node = new ast::ASTFor(token, new ast::ASTName(type_name), new ast::ASTName(name));
            if (accept(TokenType::LBRACKET)) {
                if (!accept(TokenType::RBRACKET)) {
                    do {
                        node->items.push_back(parse_expression());
                    } while (accept(TokenType::COMMA));
                    expect(TokenType::RBRACKET, "']' after items");
                }
            } else {
                node->range_start = parse_expression();
                expect(TokenType::RANGE, "'..' in range");
                node->range_end = parse_expression();
            }
            expect(TokenType::RPAREN, "')' after for");
            parse_block(node->body, parse_statement);
            return node;
        }

        // color NAME VALUE; 는 style NAME { color = VALUE; } 로 풀어서 만든다
        ast::ASTStyle* parse_color(Token token) {
            Token name = expect(TokenType::IDENTIFIER, "name after 'color'");
            auto *value = parse_expression();
            expect(TokenType::SEMICOLON, "';' after color value");
            auto *node = new ast::ASTStyle(token);
            node->name = string(name.value);
            node->name_token = name;
            // 원문에 없는 color = 값; 대입이라 선언 전체를 span으로 쓴다
            node->body.push_back(spanned(new ast::ASTStyleAssign(token, new ast::ASTIdentifier(token), value), token.value.data()));
            return node;
        }

        // (TYPE NAME, ...). 괄호가 없으면 매개변수도 없다
        void parse_parameters(vector<ast::ASTParameter*>& parameters) {
            if (!accept(TokenType::LPAREN) || accept(TokenType::RPAREN)) {
                return;
            }
            do {
                Token type_name = expect(TokenType::IDENTIFIER, "parameter type");
                Token parameter_name = expect(TokenType::IDENTIFIER, "parameter name after type");
                parameters.push_back(new ast::ASTParameter(type_name, new ast::ASTName(type_name), new ast::ASTName(parameter_name)));
            } while (accept(TokenType::COMMA));
            expect(TokenType::RPAREN, "')' after parameters");
        }

        ast::ASTStyle* parse_style(Token token) {
            Token name = expect(TokenType::IDENTIFIER, "name after 'style'");
            auto *node = new ast::ASTStyle(token);
            node->name = string(name.value);
            node->name_token = name;
            parse_parameters(node->parameters);
            parse_block(node->body, &Parser::parse_style_statement);
            return node;
        }

        ast::ASTMaster* parse_master(Token token) {
            Token name = expect(TokenType::IDENTIFIER, "name after 'master'");
            auto *node = new ast::ASTMaster(token);
            node->name = string(name.value);
            node->name_token = name;
            parse_parameters(node->parameters);
            vector<ast::ASTNode*> body;
            parse_block(body, &Parser::parse_master_statement);
            for (auto *statement : body) {
                (statement->type == ast::ASSIGN ? node->properties : node->body).push_back(statement);
            }
            return node;
        }

        // section "이름";
        ast::ASTSection* parse_section(Token token) {
            auto *name = parse_expression();
            expect(TokenType::SEMICOLON, "';' after section name");
            return new ast::ASTSection(token, name);
        }

        ast::ASTAnimate* parse_animate(Token token, bool with_target) {
            auto *node = new ast::ASTAnimate(token);
            if (with_target) {
                node->target = new ast::ASTName(expect(TokenType::IDENTIFIER, "object name after 'animate'"));
            }
            node->category = new ast::ASTName(expect(TokenType::IDENTIFIER, "enter, emphasis, exit, move or media"));
            node->effect = new ast::ASTName(expect(TokenType::IDENTIFIER, "animation effect"));
            if (node->effect->name == "path" && peek().type != TokenType::DOT) {
                node->path = parse_primary();
            } else if (accept(TokenType::DOT)) {
                node->option = new ast::ASTName(expect(TokenType::IDENTIFIER, "animation option after '.'"));
            }
            while (!accept(TokenType::SEMICOLON)) {
                Token upcoming = peek();
                if (upcoming.type == TokenType::NUMBER || upcoming.type == TokenType::LPAREN) {
                    if (node->duration != nullptr) {
                        fail(upcoming, "Duplicate animation duration");
                    }
                    node->duration = parse_additive();
                } else if (is_keyword(upcoming, "delay")) {
                    next();
                    if (node->delay != nullptr) {
                        fail(upcoming, "Duplicate animation delay");
                    }
                    node->delay = parse_additive();
                } else if (is_keyword(upcoming, "order")) {
                    next();
                    if (node->order != nullptr) {
                        fail(upcoming, "Duplicate animation order");
                    }
                    node->order = parse_additive();
                } else if (is_keyword(upcoming, "on_click") || is_keyword(upcoming, "with_previous") || is_keyword(upcoming, "after_previous")) {
                    next();
                    if (node->start != nullptr) {
                        fail(upcoming, "Duplicate animation start");
                    }
                    node->start = new ast::ASTName(upcoming);
                } else {
                    fail(upcoming, "Expected duration, on_click, with_previous, after_previous, delay, order or ';', but got " + describe(upcoming));
                }
            }
            return node;
        }

        // review { text = ...; ... }
        ast::ASTReview* parse_review(Token token) {
            auto *node = new ast::ASTReview(token);
            parse_block(node->body, &Parser::parse_property_statement);
            return node;
        }

        ast::ASTSlide* parse_slide(Token token) {
            auto *node = new ast::ASTSlide(token);
            parse_block(node->body, &Parser::parse_slide_statement);
            return node;
        }

        ast::ASTInclude* parse_include(Token token) {
            Token directive = expect(TokenType::IDENTIFIER, "directive after '#'");
            if (directive.value != "include") {
                fail(directive, "Unknown directive: #" + string(directive.value));
            }
            if (peek().type == TokenType::STRING) {
                Token path = next();
                return new ast::ASTInclude(token, unescape(path, path.value.substr(1, path.value.size() - 2)), true);
            }
            expect(TokenType::LT, "'<' or a quoted path after '#include'");
            // <std/layout/simple1>처럼 '/'로 하위 폴더를 쓸 수 있다
            string name(expect(TokenType::IDENTIFIER, "library name").value);
            while (accept(TokenType::SLASH)) {
                name += "/" + string(expect(TokenType::IDENTIFIER, "library name after '/'").value);
            }
            expect(TokenType::GT, "'>' after library name");
            return new ast::ASTInclude(token, name, false);
        }

        ast::ASTNode* parse_file_statement(Token token) {
            if (token.type == TokenType::HASH) { return parse_include(token); }
            if (is_keyword(token, "enum")) { return parse_enum(token); }
            if (is_keyword(token, "color")) { return parse_color(token); }
            if (is_keyword(token, "style")) { return parse_style(token); }
            if (is_keyword(token, "template")) { return parse_named_block<ast::ASTTemplate>(token, &Parser::parse_template_statement); }
            if (is_keyword(token, "object")) { return parse_named_block<ast::ASTObject>(token, &Parser::parse_object_statement); }
            if (is_keyword(token, "master")) { return parse_master(token); }
            if (is_keyword(token, "slide")) { return parse_slide(token); }
            if (is_keyword(token, "target")) { return parse_named_block<ast::ASTTarget>(token, &Parser::parse_property_statement); }
            if (is_keyword(token, "theme")) { return parse_named_block<ast::ASTTheme>(token, &Parser::parse_property_statement); }
            if (is_keyword(token, "section")) { return parse_section(token); }
            if (is_keyword(token, "if")) { return parse_if(token, &Parser::parse_file_statement); }
            fail_unexpected(token);
        }

        ast::ASTNode* parse_template_statement(Token token) {
            if (is_keyword(token, "var")) { return parse_var(token); }
            if (is_keyword(token, "put")) { return parse_put(token); }
            if (is_keyword(token, "group")) { return parse_group(token); }
            if (is_keyword(token, "if")) { return parse_if(token, &Parser::parse_template_statement); }
            if (is_keyword(token, "for")) { return parse_for(token, &Parser::parse_template_statement); }
            fail_unexpected(token);
        }

        ast::ASTNode* parse_object_statement(Token token) {
            if (is_keyword(token, "var")) { return parse_var(token); }
            fail_unexpected(token);
        }

        ast::ASTNode* parse_master_statement(Token token) {
            if (is_keyword(token, "case")) { return parse_named_block<ast::ASTMasterCase>(token, &Parser::parse_case_statement); }
            if (token.type == TokenType::IDENTIFIER) { return parse_assign(token); }
            fail_unexpected(token);
        }

        ast::ASTNode* parse_case_statement(Token token) {
            if (is_keyword(token, "put")) { return parse_put(token); }
            if (is_keyword(token, "group")) { return parse_group(token); }
            if (is_keyword(token, "if")) { return parse_if(token, &Parser::parse_case_statement); }
            if (is_keyword(token, "for")) { return parse_for(token, &Parser::parse_case_statement); }
            if (token.type == TokenType::IDENTIFIER) { return parse_assign(token); }
            fail_unexpected(token);
        }

        ast::ASTComment* parse_comment(Token token) {
            auto *expression = parse_expression();
            expect(TokenType::SEMICOLON, "';' after comment");
            return new ast::ASTComment(token, expression);
        }

        // transition KIND[.OPTION] [DURATION];
        ast::ASTTransition* parse_transition(Token token) {
            auto *kind = new ast::ASTName(expect(TokenType::IDENTIFIER, "transition kind"));
            ast::ASTName *option = nullptr;
            if (accept(TokenType::DOT)) {
                option = new ast::ASTName(expect(TokenType::IDENTIFIER, "transition option after '.'"));
            }
            ast::ASTNode *duration = nullptr;
            if (peek().type != TokenType::SEMICOLON) {
                duration = parse_expression();
            }
            expect(TokenType::SEMICOLON, "';' after transition");
            return new ast::ASTTransition(token, kind, option, duration);
        }

        ast::ASTNode* parse_slide_statement(Token token) {
            if (is_keyword(token, "put")) { return parse_put(token); }
            if (is_keyword(token, "comment")) { return parse_comment(token); }
            if (is_keyword(token, "transition")) { return parse_transition(token); }
            if (is_keyword(token, "group")) { return parse_group(token); }
            if (is_keyword(token, "animate")) { return parse_animate(token, true); }
            if (is_keyword(token, "review")) { return parse_review(token); }
            if (token.type == TokenType::IDENTIFIER) { return parse_assign(token); }
            fail_unexpected(token);
        }

        // put, target 블록 안의 NAME = VALUE;
        ast::ASTNode* parse_property_statement(Token token) {
            if (token.type == TokenType::IDENTIFIER) { return parse_assign(token); }
            fail_unexpected(token);
        }

        ast::ASTNode* parse_style_statement(Token token) {
            if (token.type == TokenType::IDENTIFIER) { return parse_style_assign(token); }
            fail_unexpected(token);
        }

        // 실패하면 nullptr을 반환하고 errors에 찾은 오류 모두를, error에 그 첫 번째를 남긴다
        ast::ASTFile *parse() {
            auto *file_node = new ast::ASTFile(Token{TokenType::START_OF_FILE, "", 0, 0});
            file_node->filepath = std::string(path);
            while (true) {
                Token token = next();
                if (token.type == TokenType::END_OF_FILE) {
                    file_node->token = token;
                    break;
                }
                if (token.type == TokenType::SEMICOLON) {
                    continue;
                }
                try {
                    file_node->body.push_back(spanned(parse_file_statement(token), token.value.data()));
                } catch (const ParseError& parse_error) {
                    recover(parse_error); // 파일 맨 위에는 닫을 블록이 없다
                }
            }
            if (!errors.empty()) {
                error = errors.front();
                return nullptr;
            }
            return file_node;
        }
    };
}

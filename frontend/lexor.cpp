#include "lexor.h"

#include <cctype>

namespace templide::lexor {
    namespace {
        bool is_space(char c) {
            return std::isspace(static_cast<unsigned char>(c));
        }

        bool is_digit(char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        }

        bool is_ident_start(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        bool is_ident_char(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }
    }

    Lexor::Lexor(std::string_view source) : source_(source) {}

    Token Lexor::next() {
        if (!fstring_depths_.empty() && fstring_depths_.back() == 0) {
            return lex_fstring_text();
        }

        if (!skip_trivia()) {
            return make_token(TokenType::UNKNOWN);
        }

        begin_token();
        if (at_end()) {
            return make_token(TokenType::END_OF_FILE);
        }

        const char c = peek();

        if (c == 'f' && peek(1) == '"') {
            advance();
            advance();
            fstring_depths_.push_back(0);
            return make_token(TokenType::FSTRING_BEGIN);
        }

        if (is_ident_start(c)) {
            while (!at_end() && is_ident_char(peek())) {
                advance();
            }
            return make_token(TokenType::IDENTIFIER);
        }

        if (is_digit(c)) {
            while (!at_end() && is_digit(peek())) {
                advance();
            }
            if (peek() == '.' && is_digit(peek(1))) {
                advance();
                while (!at_end() && is_digit(peek())) {
                    advance();
                }
            }
            return make_token(TokenType::NUMBER);
        }

        // '\'로 시작하는 이스케이프는 두 글자를 함께 건너뛴다. 뜻은 parser가 푼다
        if (c == '"') {
            advance();
            while (!at_end() && peek() != '"' && peek() != '\n') {
                skip_escape();
                advance();
            }
            if (at_end() || peek() == '\n') {
                return make_token(TokenType::UNKNOWN);
            }
            advance();
            return make_token(TokenType::STRING);
        }

        advance();
        switch (c) {
            case '{':
                if (!fstring_depths_.empty()) {
                    ++fstring_depths_.back();
                }
                return make_token(TokenType::LBRACE);
            case '}':
                if (!fstring_depths_.empty()) {
                    --fstring_depths_.back();
                }
                return make_token(TokenType::RBRACE);
            case '(': return make_token(TokenType::LPAREN);
            case ')': return make_token(TokenType::RPAREN);
            case '[': return make_token(TokenType::LBRACKET);
            case ']': return make_token(TokenType::RBRACKET);
            case ';': return make_token(TokenType::SEMICOLON);
            case ',': return make_token(TokenType::COMMA);
            case '.':
                if (peek() == '.') {
                    advance();
                    return make_token(TokenType::RANGE);
                }
                return make_token(TokenType::DOT);
            case '=':
                if (peek() == '=') {
                    advance();
                    return make_token(TokenType::EQ);
                }
                return make_token(TokenType::ASSIGN);
            case '+': return make_token(TokenType::PLUS);
            case '-': return make_token(TokenType::MINUS);
            case '*': return make_token(TokenType::STAR);
            case '%': return make_token(TokenType::PERCENT);
            case '/': return make_token(TokenType::SLASH);
            case '<': return make_token(TokenType::LT);
            case '>': return make_token(TokenType::GT);
            case '@': return make_token(TokenType::AT);
            case '#': return make_token(TokenType::HASH);
            default: return make_token(TokenType::UNKNOWN);
        }
    }

    bool Lexor::at_end() const {
        return pos_ >= source_.size();
    }

    char Lexor::peek(std::size_t offset) const {
        return pos_ + offset < source_.size() ? source_[pos_ + offset] : '\0';
    }

    void Lexor::advance() {
        if (source_[pos_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++pos_;
    }

    void Lexor::begin_token() {
        token_start_ = pos_;
        token_line_ = line_;
        token_column_ = column_;
    }

    Token Lexor::make_token(TokenType type) const {
        return {type, source_.substr(token_start_, pos_ - token_start_), token_line_, token_column_};
    }

    // 공백과 /* */ 주석을 건너뛴다. 닫히지 않은 주석이면 그 주석을 토큰 범위로 잡아두고 false를 반환
    bool Lexor::skip_trivia() {
        while (!at_end()) {
            if (is_space(peek())) {
                advance();
            } else if (peek() == '/' && peek(1) == '*') {
                begin_token();
                advance();
                advance();
                while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
                    advance();
                }
                if (at_end()) {
                    return false;
                }
                advance();
                advance();
            } else {
                break;
            }
        }
        return true;
    }

    // 문자열 안의 '\'이면 다음 글자와 함께 넘기도록 '\'만 먼저 넘긴다. 줄바꿈 앞의 '\'는 넘기지 않는다
    void Lexor::skip_escape() {
        if (peek() == '\\' && peek(1) != '\0' && peek(1) != '\n') {
            advance();
        }
    }

    Token Lexor::lex_fstring_text() {
        begin_token();
        while (!at_end() && peek() != '"' && peek() != '{' && peek() != '\n') {
            skip_escape();
            advance();
        }

        if (at_end() || peek() == '\n') {
            fstring_depths_.pop_back();
            return make_token(TokenType::UNKNOWN);
        }
        if (pos_ > token_start_) {
            return make_token(TokenType::FSTRING_TEXT);
        }

        const char c = peek();
        advance();
        if (c == '"') {
            fstring_depths_.pop_back();
            return make_token(TokenType::FSTRING_END);
        }
        fstring_depths_.back() = 1;
        return make_token(TokenType::LBRACE);
    }
}

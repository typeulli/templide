#ifndef TEMPLIDE_LEXOR_H
#define TEMPLIDE_LEXOR_H
#include <cstddef>
#include <string_view>
#include <vector>

namespace templide::lexor {
    enum class TokenType {
        IDENTIFIER,
        NUMBER,
        STRING,

        FSTRING_BEGIN, // f"
        FSTRING_TEXT,
        FSTRING_END,   // "

        LBRACE,        // {
        RBRACE,        // }
        LPAREN,        // (
        RPAREN,        // )
        LBRACKET,      // [
        RBRACKET,      // ]
        SEMICOLON,     // ;
        COMMA,         // ,
        DOT,           // .
        RANGE,         // ..
        ASSIGN,        // =
        EQ,            // ==
        PLUS,          // +
        MINUS,         // -
        STAR,          // *
        PERCENT,       // %
        SLASH,         // /
        LT,            // <
        GT,            // >
        AT,            // @
        HASH,          // #

        START_OF_FILE,
        END_OF_FILE,
        UNKNOWN,
    };
    struct Token {
        TokenType type;
        std::string_view value;
        std::size_t line;
        std::size_t column;
        [[nodiscard]] std::size_t length() const { return value.length(); }
    };

    class Lexor {
    public:
        explicit Lexor(std::string_view source);
        Token next();

    private:
        std::string_view source_;
        std::size_t pos_ = 0;
        std::size_t line_ = 1;
        std::size_t column_ = 1;

        std::size_t token_start_ = 0;
        std::size_t token_line_ = 1;
        std::size_t token_column_ = 1;

        // 열려 있는 f-string마다 보간식 안의 '{' 깊이. 맨 위 값이 0이면 문자열 조각을 읽는 중
        std::vector<std::size_t> fstring_depths_;

        bool at_end() const;
        char peek(std::size_t offset = 0) const;
        void advance();
        void begin_token();
        Token make_token(TokenType type) const;
        bool skip_trivia();
        void skip_escape();
        Token lex_fstring_text();
    };

}

#endif //TEMPLIDE_LEXOR_H

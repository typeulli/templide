#include "formatter.h"

#include "../frontend/lexor.h"
#include "../frontend/parser.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string_view>

namespace templide::format {
    namespace {
        using lexor::TokenType;

        struct Comment {
            std::string text;
            int newlines_before = 0; // 앞 요소(토큰이나 주석)와의 사이 줄바꿈 수
        };

        struct Tok {
            TokenType type = TokenType::END_OF_FILE;
            std::string text;           // 원문 (f-string은 전체, hex(...)는 괄호 안까지 하나로 묶은 글)
            std::string hex_inner;      // hex(...)일 때 괄호 안
            bool is_hex = false;
            std::size_t begin = 0;
            std::size_t end = 0;
            std::size_t line = 0;
            std::size_t column = 0;
            int newlines_before = 0;    // 마지막 앞 주석(없으면 앞 토큰)과 이 토큰 사이의 줄바꿈 수
            bool glued_before = false;  // 앞 토큰과 원문에서 붙어 있었다
            bool glued_after = false;
            std::vector<Comment> leading;  // 이 토큰 앞의, 자기 줄에 있는 주석
            std::vector<Comment> trailing; // 이 토큰 뒤에 같은 줄에 있는 주석
        };

        bool is_identifier(const Tok& tok, std::string_view text) {
            return tok.type == TokenType::IDENTIFIER && tok.text == text;
        }

        // ---------- 토큰화

        // from부터 to까지의 공백과 주석을 나눈다. 반환값은 마지막 요소 뒤의 줄바꿈 수
        int split_gap(const std::string& source, std::size_t from, std::size_t to, std::vector<Comment>& comments) {
            int newlines = 0;
            std::size_t i = from;
            while (i < to) {
                if (source[i] == '\n') {
                    ++newlines;
                    ++i;
                } else if (source[i] == '/' && i + 1 < to && source[i + 1] == '*') {
                    const std::size_t close = source.find("*/", i + 2);
                    const std::size_t end = close == std::string::npos ? to : std::min(close + 2, to);
                    comments.push_back({source.substr(i, end - i), newlines});
                    newlines = 0;
                    i = end;
                } else {
                    ++i;
                }
            }
            return newlines;
        }

        // 문법 오류가 없는 글을 토큰으로. f-string 전체와 hex(...)는 토큰 하나로 묶는다. 읽을 수 없으면 nullopt
        std::optional<std::vector<Tok>> tokenize(const std::string& source) {
            lexor::Lexor lexer(source);
            struct Raw {
                TokenType type;
                std::size_t begin;
                std::size_t end;
                std::size_t line;
                std::size_t column;
            };
            std::vector<Raw> raw;
            const auto offset = [&](const lexor::Token& token) { return static_cast<std::size_t>(token.value.data() - source.data()); };
            while (true) {
                const lexor::Token token = lexer.next();
                if (token.type == TokenType::UNKNOWN) {
                    return std::nullopt;
                }
                if (token.type == TokenType::FSTRING_BEGIN) {
                    const std::size_t begin = offset(token);
                    std::size_t depth = 1;
                    std::size_t end = begin + token.value.size();
                    while (depth > 0) {
                        const lexor::Token inner = lexer.next();
                        if (inner.type == TokenType::UNKNOWN || inner.type == TokenType::END_OF_FILE) {
                            return std::nullopt;
                        }
                        end = offset(inner) + inner.value.size();
                        if (inner.type == TokenType::FSTRING_BEGIN) {
                            ++depth;
                        } else if (inner.type == TokenType::FSTRING_END) {
                            --depth;
                        }
                    }
                    raw.push_back({TokenType::STRING, begin, end, token.line, token.column});
                    continue;
                }
                raw.push_back({token.type, offset(token), offset(token) + token.value.size(), token.line, token.column});
                if (token.type == TokenType::END_OF_FILE) {
                    break;
                }
            }
            std::vector<Tok> toks;
            std::size_t previous_end = 0;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                Raw current = raw[i];
                Tok tok;
                tok.type = current.type;
                // hex(...)는 안의 글자 사이를 건드리지 않도록 하나로 묶는다
                if (current.type == TokenType::IDENTIFIER && source.compare(current.begin, current.end - current.begin, "hex") == 0 && i + 1 < raw.size()
                    && raw[i + 1].type == TokenType::LPAREN && raw[i + 1].begin == current.end) {
                    std::size_t close = i + 2;
                    while (close < raw.size() && raw[close].type != TokenType::RPAREN && raw[close].type != TokenType::END_OF_FILE) {
                        ++close;
                    }
                    if (close < raw.size() && raw[close].type == TokenType::RPAREN) {
                        std::string inner = source.substr(raw[i + 1].end, raw[close].begin - raw[i + 1].end);
                        const std::size_t first = inner.find_first_not_of(" \t\r\n");
                        const std::size_t last = inner.find_last_not_of(" \t\r\n");
                        inner = first == std::string::npos ? "" : inner.substr(first, last - first + 1);
                        tok.is_hex = true;
                        tok.hex_inner = inner;
                        tok.type = TokenType::IDENTIFIER;
                        current.end = raw[close].end;
                        i = close;
                    }
                }
                tok.begin = current.begin;
                tok.end = current.end;
                tok.line = current.line;
                tok.column = current.column;
                tok.text = source.substr(tok.begin, tok.end - tok.begin);
                std::vector<Comment> gap;
                tok.newlines_before = split_gap(source, previous_end, tok.begin, gap);
                tok.glued_before = !toks.empty() && previous_end == tok.begin;
                if (!toks.empty()) {
                    toks.back().glued_after = tok.glued_before;
                    // 앞 토큰과 같은 줄에서 시작하는 주석은 앞 토큰의 줄 끝 주석이다
                    std::size_t own = 0;
                    while (own < gap.size() && gap[own].newlines_before == 0) {
                        toks.back().trailing.push_back(gap[own]);
                        ++own;
                    }
                    gap.erase(gap.begin(), gap.begin() + static_cast<std::ptrdiff_t>(own));
                }
                tok.leading = std::move(gap);
                previous_end = tok.end;
                toks.push_back(std::move(tok));
            }
            return toks;
        }

        // ---------- 문장과 블록

        struct Block;

        struct Item {
            std::size_t tok = 0;
            std::unique_ptr<Block> block;
        };

        struct Stmt {
            std::vector<Item> items;
            bool directive = false; // #include, #fmt (items[0]은 '#')
            std::size_t first = 0;  // 첫 토큰의 순서
        };

        struct Block {
            std::size_t open = 0;
            std::size_t close = 0;
            bool list = false;                 // enum { a, b }와 asset ... by { * as x }
            std::vector<Stmt> stmts;
            std::vector<std::size_t> list_toks; // list일 때 안쪽 토큰들
        };

        class TreeBuilder {
        public:
            explicit TreeBuilder(const std::vector<Tok>& toks) : t_(toks) {}

            bool build(std::vector<Stmt>& top) {
                top = statements(false);
                return ok_ && t_[i_].type == TokenType::END_OF_FILE;
            }

        private:
            const std::vector<Tok>& t_;
            std::size_t i_ = 0;
            bool ok_ = true;

            const Tok& cur() const { return t_[i_]; }

            std::vector<Stmt> statements(bool in_block) {
                std::vector<Stmt> result;
                while (ok_) {
                    const Tok& tok = cur();
                    if (tok.type == TokenType::END_OF_FILE) {
                        break;
                    }
                    if (tok.type == TokenType::RBRACE) {
                        if (!in_block) {
                            ok_ = false;
                        }
                        break;
                    }
                    result.push_back(statement());
                }
                return result;
            }

            Stmt statement() {
                Stmt st;
                st.first = i_;
                if (cur().type == TokenType::HASH) {
                    directive(st);
                    return st;
                }
                int depth = 0;
                while (ok_) {
                    const Tok& tok = cur();
                    if (tok.type == TokenType::END_OF_FILE) {
                        break;
                    }
                    if (tok.type == TokenType::RBRACE && depth <= 0) {
                        break;
                    }
                    if (tok.type == TokenType::LBRACE) {
                        const bool list = block_is_list(st);
                        // enum 이름 { ... }은 거기서 끝나지만 asset ... by { ... }는 ';'까지 이어진다
                        const bool ends = !list || is_enum(st);
                        Item item;
                        item.block = block(list);
                        st.items.push_back(std::move(item));
                        if (ends && depth <= 0) {
                            // else가 이어지면 문장은 계속된다
                            if (is_identifier(cur(), "else")) {
                                continue;
                            }
                            break;
                        }
                        continue;
                    }
                    Item item;
                    item.tok = i_;
                    st.items.push_back(std::move(item));
                    ++i_;
                    if (tok.type == TokenType::LPAREN || tok.type == TokenType::LBRACKET) {
                        ++depth;
                    } else if (tok.type == TokenType::RPAREN || tok.type == TokenType::RBRACKET) {
                        --depth;
                    }
                    if (tok.type == TokenType::SEMICOLON && depth <= 0) {
                        break;
                    }
                }
                return st;
            }

            void take(Stmt& st) {
                Item item;
                item.tok = i_;
                st.items.push_back(std::move(item));
                ++i_;
            }

            // #include "경로", #include <이름/이름>, #fmt 이름 값
            void directive(Stmt& st) {
                st.directive = true;
                take(st); // '#'
                if (cur().type != TokenType::IDENTIFIER) {
                    ok_ = false;
                    return;
                }
                const bool include = cur().text == "include";
                const bool fmt = cur().text == "fmt";
                take(st);
                if (include) {
                    if (cur().type == TokenType::STRING) {
                        take(st);
                    } else if (cur().type == TokenType::LT) {
                        take(st);
                        while (cur().type != TokenType::GT && cur().type != TokenType::END_OF_FILE) {
                            take(st);
                        }
                        if (cur().type == TokenType::GT) {
                            take(st);
                        } else {
                            ok_ = false;
                        }
                    } else {
                        ok_ = false;
                    }
                } else if (fmt) {
                    if (cur().type != TokenType::IDENTIFIER) {
                        ok_ = false;
                        return;
                    }
                    take(st);
                    if (cur().type != TokenType::IDENTIFIER && cur().type != TokenType::NUMBER) {
                        ok_ = false;
                        return;
                    }
                    take(st);
                } else {
                    ok_ = false;
                }
            }

            bool is_enum(const Stmt& st) const {
                return !st.items.empty() && !st.items.front().block && is_identifier(t_[st.items.front().tok], "enum");
            }

            // 'by {'(asset)나 'enum 이름 {'는 문장이 아니라 쉼표로 이은 목록이다
            bool block_is_list(const Stmt& st) const {
                if (st.items.empty()) {
                    return false;
                }
                const Item& last = st.items.back();
                if (!last.block && is_identifier(t_[last.tok], "by")) {
                    return true;
                }
                const Item& first = st.items.front();
                return !first.block && is_identifier(t_[first.tok], "enum");
            }

            std::unique_ptr<Block> block(bool list) {
                auto result = std::make_unique<Block>();
                result->open = i_;
                result->list = list;
                ++i_; // '{'
                if (list) {
                    while (cur().type != TokenType::RBRACE && cur().type != TokenType::END_OF_FILE) {
                        result->list_toks.push_back(i_);
                        ++i_;
                    }
                } else {
                    result->stmts = statements(true);
                }
                if (cur().type != TokenType::RBRACE) {
                    ok_ = false;
                    result->close = i_;
                    return result;
                }
                result->close = i_;
                ++i_;
                return result;
            }
        };

        // ---------- 인쇄

        bool value_end(const Tok& tok, bool percent_unit) {
            switch (tok.type) {
                case TokenType::IDENTIFIER:
                case TokenType::NUMBER:
                case TokenType::STRING:
                case TokenType::RPAREN:
                case TokenType::RBRACKET:
                    return true;
                case TokenType::PERCENT:
                    return percent_unit;
                default:
                    return false;
            }
        }

        // 한 문장을 훑으며 공백을 정하는 상태
        struct Flow {
            const Tok* prev = nullptr;
            int depth = 0;             // 열린 ( [ 수
            int generic = 0;           // 열린 list< 수
            bool prev_unary = false;   // 앞 토큰이 단항 '-'
            bool prev_hyphen = false;  // 앞 토큰이 foo-bar의 '-'
            bool prev_percent_unit = false;
            bool prev_generic_open = false;
            bool prev_generic_close = false;
        };

        class Printer {
        public:
            Printer(const std::vector<Tok>& toks, const Settings& settings) : t_(toks), s_(settings) {}

            std::string print(const std::vector<Stmt>& top) {
                std::vector<const Stmt*> list;
                for (const auto& st : top) {
                    list.push_back(&st);
                }
                sort_includes(list);
                statements(list, 0, true);
                dangling(t_.back().leading, 0);
                // 맨 끝의 빈 줄을 줄이고 마지막 줄바꿈을 정한다
                while (out_.size() >= 2 && out_[out_.size() - 1] == '\n' && out_[out_.size() - 2] == '\n') {
                    out_.pop_back();
                }
                if (out_.empty()) {
                    return out_;
                }
                if (!s_.final_newline) {
                    while (!out_.empty() && out_.back() == '\n') {
                        out_.pop_back();
                    }
                } else if (out_.back() != '\n') {
                    out_ += '\n';
                }
                return out_;
            }

        private:
            const std::vector<Tok>& t_;
            const Settings& s_;
            std::string out_;
            bool inline_mode_ = false;
            bool inline_failed_ = false;

            // ----- 글쓰기

            std::string indent(int level) const {
                return s_.use_tabs ? std::string(static_cast<std::size_t>(level), '\t') : std::string(static_cast<std::size_t>(level * s_.indent_size), ' ');
            }

            void write(const std::string& text) { out_ += text; }

            void newline() { out_ += '\n'; }

            // 지금 줄의 너비 (글자 수. 탭은 들여쓰기 한 칸 너비)
            std::size_t column() const {
                const std::size_t line_start = out_.rfind('\n');
                std::size_t width = 0;
                for (std::size_t i = line_start == std::string::npos ? 0 : line_start + 1; i < out_.size(); ++i) {
                    const auto byte = static_cast<unsigned char>(out_[i]);
                    if (byte == '\t') {
                        width += static_cast<std::size_t>(s_.indent_size);
                    } else if ((byte & 0xC0) != 0x80) {
                        ++width;
                    }
                }
                return width;
            }

            static std::string trim_comment(const std::string& text) {
                std::string result;
                std::size_t i = 0;
                while (i < text.size()) {
                    std::size_t end = text.find('\n', i);
                    const bool last = end == std::string::npos;
                    std::string line = text.substr(i, last ? std::string::npos : end - i);
                    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
                        line.pop_back();
                    }
                    result += line;
                    if (last) {
                        break;
                    }
                    result += '\n';
                    i = end + 1;
                }
                return result;
            }

            int capped(int blank) const {
                return std::clamp(blank, 0, s_.max_blank_lines);
            }

            // ----- 글자

            std::string token_text(const Tok& tok) const {
                if (!tok.is_hex) {
                    return tok.text;
                }
                std::string inner = tok.hex_inner;
                if (s_.hex_case != Settings::HexCase::Keep) {
                    for (char& c : inner) {
                        c = static_cast<char>(s_.hex_case == Settings::HexCase::Upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
                    }
                }
                return "hex(" + inner + ")";
            }

            // ----- 토큰 사이의 공백

            bool need_space(const Tok& cur, Flow& flow) const {
                const Tok& prev = *flow.prev;
                const bool minus_is_unary = cur.type == TokenType::MINUS && !value_end(prev, flow.prev_percent_unit);
                const bool percent_unit = cur.type == TokenType::PERCENT && cur.glued_before && prev.type == TokenType::NUMBER;
                const bool minus_hyphen = cur.type == TokenType::MINUS && cur.glued_before && cur.glued_after && prev.type == TokenType::IDENTIFIER;
                const bool generic_open = cur.type == TokenType::LT && prev.type == TokenType::IDENTIFIER && prev.text == "list" && cur.glued_before;
                const bool generic_close = cur.type == TokenType::GT && flow.generic > 0;
                const bool prev_binary = (prev.type == TokenType::EQ || prev.type == TokenType::PLUS || prev.type == TokenType::STAR || prev.type == TokenType::SLASH
                                          || (prev.type == TokenType::LT && !flow.prev_generic_open) || (prev.type == TokenType::GT && !flow.prev_generic_close)
                                          || (prev.type == TokenType::MINUS && !flow.prev_unary && !flow.prev_hyphen)
                                          || (prev.type == TokenType::PERCENT && !flow.prev_percent_unit));
                const bool cur_binary = cur.type == TokenType::EQ || cur.type == TokenType::PLUS || cur.type == TokenType::STAR || cur.type == TokenType::SLASH
                                        || (cur.type == TokenType::LT && !generic_open) || (cur.type == TokenType::GT && !generic_close)
                                        || (cur.type == TokenType::MINUS && !minus_is_unary && !minus_hyphen) || (cur.type == TokenType::PERCENT && !percent_unit);

                bool space = true;
                if (cur.type == TokenType::SEMICOLON || cur.type == TokenType::COMMA || cur.type == TokenType::DOT || cur.type == TokenType::RANGE
                    || cur.type == TokenType::RBRACKET) {
                    space = false;
                } else if (cur.type == TokenType::RPAREN) {
                    space = s_.space_inside_parens && prev.type != TokenType::LPAREN;
                } else if (percent_unit || (prev.type == TokenType::NUMBER && cur.type == TokenType::IDENTIFIER && cur.glued_before)) {
                    space = false;
                } else if (prev.type == TokenType::LPAREN) {
                    space = s_.space_inside_parens;
                } else if (prev.type == TokenType::LBRACKET || prev.type == TokenType::DOT || prev.type == TokenType::RANGE || prev.type == TokenType::AT) {
                    space = false;
                } else if (prev.type == TokenType::COMMA) {
                    space = s_.space_after_comma;
                } else if (flow.prev_unary || flow.prev_hyphen) {
                    space = false;
                } else if (generic_open || flow.prev_generic_open || generic_close) {
                    space = false;
                } else if (minus_hyphen) {
                    space = false;
                } else if (cur.type == TokenType::ASSIGN || prev.type == TokenType::ASSIGN) {
                    space = s_.space_around_assign;
                } else if (cur_binary || prev_binary) {
                    space = s_.space_around_operators;
                } else if ((cur.type == TokenType::LPAREN || cur.type == TokenType::LBRACKET)
                           && (prev.type == TokenType::IDENTIFIER || prev.type == TokenType::RPAREN || prev.type == TokenType::RBRACKET)) {
                    space = !cur.glued_before; // 부르는 식이나 인덱스는 쓴 대로 붙이거나 띄운다
                }

                flow.prev_unary = minus_is_unary;
                flow.prev_hyphen = minus_hyphen;
                flow.prev_percent_unit = percent_unit;
                flow.prev_generic_open = generic_open;
                flow.prev_generic_close = generic_close;
                if (generic_open) {
                    ++flow.generic;
                } else if (generic_close) {
                    --flow.generic;
                }
                return space;
            }

            // ----- 주석

            void comment_line(const Comment& comment, int level) {
                write(indent(level) + trim_comment(comment.text));
                newline();
            }

            // 문장이 없는 자리(블록이나 파일의 끝)에 남은 주석
            void dangling(const std::vector<Comment>& comments, int level) {
                for (std::size_t i = 0; i < comments.size(); ++i) {
                    if (i > 0 || !out_.empty()) {
                        for (int blank = capped(comments[i].newlines_before - 1); blank > 0; --blank) {
                            newline();
                        }
                    }
                    comment_line(comments[i], level);
                }
            }

            void trailing_comments(const Tok& tok, bool statement_end) {
                for (const auto& comment : tok.trailing) {
                    if (inline_mode_) {
                        inline_failed_ = true;
                        return;
                    }
                    write(std::string(static_cast<std::size_t>(statement_end ? s_.comment_gap : 1), ' ') + trim_comment(comment.text));
                }
            }

            // ----- 목록 정렬

            bool is_include(const Stmt& st) const {
                return st.directive && st.items.size() > 1 && !st.items[1].block && is_identifier(t_[st.items[1].tok], "include");
            }

            // 빈 줄과 주석 없이 이어진 #include 줄들을 이름순으로 (package 먼저)
            void sort_includes(std::vector<const Stmt*>& list) const {
                if (!s_.sort_includes) {
                    return;
                }
                std::size_t i = 0;
                while (i < list.size()) {
                    if (!is_include(*list[i])) {
                        ++i;
                        continue;
                    }
                    std::size_t j = i + 1;
                    while (j < list.size() && is_include(*list[j]) && t_[list[j]->first].newlines_before <= 1 && t_[list[j]->first].leading.empty()
                           && t_[list[j - 1]->items.back().tok].trailing.empty()) {
                        ++j;
                    }
                    const auto key = [&](const Stmt* st) {
                        std::string path;
                        for (std::size_t k = 2; k < st->items.size(); ++k) {
                            path += t_[st->items[k].tok].text;
                        }
                        const bool relative = !path.empty() && path.front() == '"';
                        return std::pair<bool, std::string>{relative, path};
                    };
                    std::stable_sort(list.begin() + static_cast<std::ptrdiff_t>(i), list.begin() + static_cast<std::ptrdiff_t>(j),
                                     [&](const Stmt* a, const Stmt* b) { return key(a) < key(b); });
                    i = j;
                }
            }

            // NAME = VALUE; 만 있는 블록은 이름순으로 바꿀 수 있다
            std::optional<std::string> assignment_name(const Stmt& st) const {
                if (st.directive || st.items.size() < 4) {
                    return std::nullopt;
                }
                std::string name;
                std::size_t k = 0;
                while (k < st.items.size() && !st.items[k].block && t_[st.items[k].tok].type != TokenType::ASSIGN) {
                    const TokenType type = t_[st.items[k].tok].type;
                    if (type != TokenType::IDENTIFIER && type != TokenType::MINUS) {
                        return std::nullopt;
                    }
                    name += t_[st.items[k].tok].text;
                    ++k;
                }
                if (k == 0 || k >= st.items.size() || st.items[k].block || t_[st.items[k].tok].type != TokenType::ASSIGN) {
                    return std::nullopt;
                }
                for (const auto& item : st.items) {
                    if (item.block) {
                        return std::nullopt;
                    }
                }
                if (t_[st.items.back().tok].type != TokenType::SEMICOLON) {
                    return std::nullopt;
                }
                return name;
            }

            void sort_properties(std::vector<const Stmt*>& list) const {
                if (!s_.sort_properties || list.size() < 2) {
                    return;
                }
                std::vector<std::string> names;
                for (const Stmt* st : list) {
                    const auto name = assignment_name(*st);
                    if (!name) {
                        return;
                    }
                    names.push_back(*name);
                }
                std::vector<std::size_t> order(list.size());
                for (std::size_t i = 0; i < order.size(); ++i) {
                    order[i] = i;
                }
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return names[a] < names[b]; });
                std::vector<const Stmt*> sorted;
                for (const std::size_t i : order) {
                    sorted.push_back(list[i]);
                }
                list = std::move(sorted);
            }

            // ----- 문장들

            void statements(std::vector<const Stmt*> list, int level, bool top) {
                if (!top) {
                    sort_properties(list);
                }
                for (std::size_t k = 0; k < list.size(); ++k) {
                    const Stmt& st = *list[k];
                    const Tok& first = t_[st.first];
                    if (k > 0) {
                        int blank = capped((first.leading.empty() ? first.newlines_before : first.leading.front().newlines_before) - 1);
                        if (top) {
                            const Stmt& before = *list[k - 1];
                            if (is_include(before) && !is_include(st) && s_.blank_lines_after_includes >= 0) {
                                blank = s_.blank_lines_after_includes;
                            } else if (is_identifier(first, "slide") && s_.blank_lines_before_slide >= 0) {
                                blank = s_.blank_lines_before_slide;
                            }
                        }
                        for (; blank > 0; --blank) {
                            newline();
                        }
                    }
                    for (std::size_t c = 0; c < first.leading.size(); ++c) {
                        if (c > 0) {
                            for (int blank = capped(first.leading[c].newlines_before - 1); blank > 0; --blank) {
                                newline();
                            }
                        }
                        comment_line(first.leading[c], level);
                    }
                    if (!first.leading.empty()) {
                        for (int blank = capped(first.newlines_before - 1); blank > 0; --blank) {
                            newline();
                        }
                    }
                    write(indent(level));
                    statement(st, level);
                    newline();
                }
            }

            // 블록이 둘 이상인 문장(if ... else ...)은 모두 한 줄에 들어가거나, 아니면 모두 펼친다
            void statement(const Stmt& st, int level) {
                std::size_t blocks = 0;
                bool eligible = true;
                for (const auto& item : st.items) {
                    if (item.block && !item.block->list) {
                        ++blocks;
                        eligible = eligible && block_inline_eligible(*item.block);
                    }
                }
                if (inline_mode_ || blocks < 2) {
                    statement_body(st, level, false);
                    return;
                }
                if (eligible) {
                    const std::size_t mark = out_.size();
                    inline_mode_ = true;
                    inline_failed_ = false;
                    statement_body(st, level, false);
                    inline_mode_ = false;
                    const bool too_long = s_.max_line_length > 0 && column() > static_cast<std::size_t>(s_.max_line_length);
                    if (!inline_failed_ && !too_long) {
                        return;
                    }
                    out_.resize(mark);
                    inline_failed_ = false;
                }
                statement_body(st, level, true);
            }

            void statement_body(const Stmt& st, int level, bool force_expand) {
                if (st.directive) {
                    directive(st);
                    return;
                }
                Flow flow;
                for (std::size_t n = 0; n < st.items.size(); ++n) {
                    const Item& item = st.items[n];
                    if (item.block) {
                        block(*item.block, level, n > 0, force_expand);
                        flow = Flow{};
                        if (inline_failed_) {
                            return;
                        }
                        continue;
                    }
                    const Tok& tok = t_[item.tok];
                    if (n > 0 && st.items[n - 1].block) {
                        // '} else': 같은 줄에 두거나 다음 줄로
                        if (s_.brace_next_line && !inline_mode_ && is_identifier(tok, "else")) {
                            newline();
                            write(indent(level));
                        } else {
                            write(" ");
                        }
                    } else if (flow.prev != nullptr) {
                        mid_statement_break(tok, flow, level);
                        if (inline_failed_) {
                            return;
                        }
                    }
                    write(token_text(tok));
                    trailing_comments(tok, tok.type == TokenType::SEMICOLON);
                    if (inline_failed_) {
                        return;
                    }
                    if (tok.type == TokenType::LPAREN || tok.type == TokenType::LBRACKET) {
                        ++flow.depth;
                    } else if ((tok.type == TokenType::RPAREN || tok.type == TokenType::RBRACKET) && flow.depth > 0) {
                        --flow.depth;
                    }
                    flow.prev = &tok;
                }
            }

            // 문장 중간의 토큰 앞: 괄호 안에서 줄을 바꿔 쓴 것은 그대로 두고, 앞에 주석이 있으면 줄을 바꾼다. 아니면 공백만 정한다
            void mid_statement_break(const Tok& tok, Flow& flow, int level) {
                const bool closing = tok.type == TokenType::RPAREN || tok.type == TokenType::RBRACKET;
                const int continuation = level + std::max(0, flow.depth - (closing ? 1 : 0)) + (flow.depth == 0 ? 1 : 0);
                if (!tok.leading.empty()) {
                    if (inline_mode_) {
                        inline_failed_ = true;
                        return;
                    }
                    for (const auto& comment : tok.leading) {
                        newline();
                        write(indent(continuation) + trim_comment(comment.text));
                    }
                    newline();
                    write(indent(continuation));
                    need_space(tok, flow); // 상태만 갱신한다
                    return;
                }
                if (flow.depth > 0 && tok.newlines_before > 0) {
                    if (inline_mode_) {
                        inline_failed_ = true;
                        return;
                    }
                    for (int blank = capped(tok.newlines_before - 1); blank > 0; --blank) {
                        newline();
                    }
                    newline();
                    write(indent(continuation));
                    need_space(tok, flow);
                    return;
                }
                if (need_space(tok, flow)) {
                    write(" ");
                }
            }

            void directive(const Stmt& st) {
                const Tok& hash = t_[st.items[0].tok];
                std::string text = hash.text;
                const std::string name = t_[st.items[1].tok].text;
                text += name;
                if (name == "include") {
                    if (st.items.size() > 2 && t_[st.items[2].tok].type == TokenType::STRING) {
                        text += " " + t_[st.items[2].tok].text;
                    } else {
                        text += " ";
                        for (std::size_t k = 2; k < st.items.size(); ++k) {
                            text += t_[st.items[k].tok].text;
                        }
                    }
                } else {
                    // #fmt 이름 값
                    text += " " + t_[st.items[2].tok].text + " " + t_[st.items[3].tok].text;
                }
                write(text);
                trailing_comments(t_[st.items.back().tok], true);
            }

            // ----- 블록

            bool block_has_comments(const Block& b) const {
                const Tok& open = t_[b.open];
                const Tok& close = t_[b.close];
                if (!open.trailing.empty() || !close.leading.empty()) {
                    return true;
                }
                for (std::size_t i = b.open + 1; i < b.close; ++i) {
                    if (!t_[i].leading.empty() || !t_[i].trailing.empty()) {
                        return true;
                    }
                }
                return !close.leading.empty();
            }

            std::size_t count_statements(const Block& b) const {
                return b.stmts.size();
            }

            // 한 줄로 쓴 짧은 블록을 한 줄로 둘 수 있는지 (설정과 원문만 본다. 길이는 쓴 뒤에 본다)
            bool block_inline_eligible(const Block& b) const {
                if (b.list) {
                    return true;
                }
                const bool comments = block_has_comments(b);
                if (b.stmts.empty() && !comments) {
                    return true;
                }
                return s_.keep_short_blocks && !comments && t_[b.open].line == t_[b.close].line
                       && (s_.inline_max_statements == 0 || b.stmts.size() <= static_cast<std::size_t>(s_.inline_max_statements));
            }

            void block(const Block& b, int level, bool has_prefix, bool force_expand = false) {
                const Tok& open = t_[b.open];
                const Tok& close = t_[b.close];
                const std::string gap = has_prefix ? " " : "";
                if (b.list) {
                    list_block(b, level, gap);
                    return;
                }
                const bool comments = block_has_comments(b);
                if (b.stmts.empty() && !comments) {
                    write(gap + (s_.empty_block_space ? "{ }" : "{}"));
                    trailing_comments(close, true);
                    return;
                }
                const bool single_line = open.line == close.line && !comments;
                if (inline_mode_) {
                    if (!single_line) {
                        inline_failed_ = true;
                        return;
                    }
                    render_inline(b, level, gap);
                    return;
                }
                if (!force_expand && block_inline_eligible(b)) {
                    const std::size_t mark = out_.size();
                    inline_mode_ = true;
                    inline_failed_ = false;
                    render_inline(b, level, gap);
                    inline_mode_ = false;
                    const bool too_long = s_.max_line_length > 0 && column() > static_cast<std::size_t>(s_.max_line_length);
                    if (!inline_failed_ && !too_long) {
                        trailing_comments(close, true); // 닫는 중괄호 뒤의 줄 끝 주석
                        return;
                    }
                    out_.resize(mark);
                    inline_failed_ = false;
                }
                expanded(b, level, gap);
            }

            void render_inline(const Block& b, int level, const std::string& gap) {
                const bool pad = s_.inline_brace_space;
                write(gap + (pad ? "{ " : "{"));
                std::vector<const Stmt*> list;
                for (const auto& st : b.stmts) {
                    list.push_back(&st);
                }
                sort_properties(list);
                for (std::size_t k = 0; k < list.size(); ++k) {
                    if (k > 0) {
                        write(" ");
                    }
                    statement(*list[k], level);
                    if (inline_failed_) {
                        return;
                    }
                }
                write(pad ? " }" : "}");
            }

            void expanded(const Block& b, int level, const std::string& gap) {
                const Tok& open = t_[b.open];
                const Tok& close = t_[b.close];
                if (s_.brace_next_line && !gap.empty()) {
                    newline();
                    write(indent(level) + "{");
                } else {
                    write(gap + "{");
                }
                for (const auto& comment : open.trailing) {
                    write(std::string(static_cast<std::size_t>(s_.comment_gap), ' ') + trim_comment(comment.text));
                }
                newline();
                std::vector<const Stmt*> list;
                for (const auto& st : b.stmts) {
                    list.push_back(&st);
                }
                statements(list, level + 1, false);
                if (!close.leading.empty()) {
                    if (!list.empty()) {
                        for (int blank = capped(close.leading.front().newlines_before - 1); blank > 0; --blank) {
                            newline();
                        }
                    }
                    for (std::size_t c = 0; c < close.leading.size(); ++c) {
                        if (c > 0) {
                            for (int blank = capped(close.leading[c].newlines_before - 1); blank > 0; --blank) {
                                newline();
                            }
                        }
                        comment_line(close.leading[c], level + 1);
                    }
                }
                write(indent(level) + "}");
                trailing_comments(close, true);
            }

            // enum 이름 { a, b }, asset "x" by { * as x, 이름 as 별명 }
            void list_block(const Block& b, int level, const std::string& gap) {
                const Tok& open = t_[b.open];
                const Tok& close = t_[b.close];
                // 쉼표(또는 세미콜론)로 나눈 항목들
                struct Entry {
                    std::vector<std::size_t> toks;
                    char separator = 0;
                };
                std::vector<Entry> entries;
                entries.emplace_back();
                for (const std::size_t i : b.list_toks) {
                    const TokenType type = t_[i].type;
                    if (type == TokenType::COMMA || type == TokenType::SEMICOLON) {
                        entries.back().separator = type == TokenType::COMMA ? ',' : ';';
                        entries.emplace_back();
                    } else {
                        entries.back().toks.push_back(i);
                    }
                }
                if (entries.back().toks.empty()) {
                    entries.pop_back();
                }
                const bool comments = block_has_comments(b);
                const bool multi_line = open.line != close.line || comments;
                if (entries.empty()) {
                    write(gap + (s_.empty_block_space ? "{ }" : "{}"));
                    return;
                }
                const auto entry_text = [&](const Entry& entry) {
                    std::string text;
                    for (std::size_t k = 0; k < entry.toks.size(); ++k) {
                        text += (k > 0 ? " " : "") + token_text(t_[entry.toks[k]]);
                    }
                    return text;
                };
                if (!multi_line || inline_mode_) {
                    if (comments && inline_mode_) {
                        inline_failed_ = true;
                        return;
                    }
                    write(gap + (s_.inline_brace_space ? "{ " : "{"));
                    for (std::size_t k = 0; k < entries.size(); ++k) {
                        write(entry_text(entries[k]));
                        if (k + 1 < entries.size()) {
                            write(std::string(1, entries[k].separator) + (s_.space_after_comma ? " " : ""));
                        } else if (entries[k].separator != 0) {
                            write(std::string(1, entries[k].separator));
                        }
                    }
                    write(s_.inline_brace_space ? " }" : "}");
                    return;
                }
                write(gap + "{");
                for (const auto& comment : open.trailing) {
                    write(std::string(static_cast<std::size_t>(s_.comment_gap), ' ') + trim_comment(comment.text));
                }
                newline();
                for (const auto& entry : entries) {
                    for (const auto& comment : t_[entry.toks.front()].leading) {
                        comment_line(comment, level + 1);
                    }
                    write(indent(level + 1) + entry_text(entry));
                    if (entry.separator != 0) {
                        write(std::string(1, entry.separator));
                    }
                    for (const std::size_t i : entry.toks) {
                        trailing_comments(t_[i], true);
                    }
                    newline();
                }
                for (const auto& comment : close.leading) {
                    comment_line(comment, level + 1);
                }
                write(indent(level) + "}");
            }
        };

        // ---------- 점검

        // 토큰의 글만 이어 둔 목록. 공백과 주석, 빈 문장(;), hex의 글자 대소문자는 뜻이 없다
        std::vector<std::string> token_texts(const std::string& source, bool& readable) {
            std::vector<std::string> texts;
            const auto toks = tokenize(source);
            readable = toks.has_value();
            if (!toks) {
                return texts;
            }
            for (const auto& tok : *toks) {
                if (tok.type == TokenType::END_OF_FILE) {
                    break;
                }
                std::string text = tok.text;
                if (tok.is_hex) {
                    text.clear();
                    for (const char c : "hex(" + tok.hex_inner + ")") {
                        text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                }
                texts.push_back(std::move(text));
            }
            return texts;
        }

        std::string normalize_newlines(const std::string& text) {
            std::string result;
            result.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                    continue;
                }
                result += text[i];
            }
            return result;
        }

        std::string with_crlf(const std::string& text) {
            std::string result;
            result.reserve(text.size() + text.size() / 20);
            for (const char c : text) {
                if (c == '\n') {
                    result += '\r';
                }
                result += c;
            }
            return result;
        }

    }

    std::vector<Directive> read_directives(const std::string& source) {
        std::vector<Directive> result;
        const std::string text = normalize_newlines(source);
        lexor::Lexor lexer(text);
        std::vector<lexor::Token> tokens;
        std::size_t fstring_depth = 0;
        while (true) {
            const lexor::Token token = lexer.next();
            if (token.type == TokenType::END_OF_FILE || token.type == TokenType::UNKNOWN) {
                break;
            }
            if (token.type == TokenType::FSTRING_BEGIN) {
                ++fstring_depth;
            } else if (token.type == TokenType::FSTRING_END && fstring_depth > 0) {
                --fstring_depth;
            }
            if (fstring_depth == 0 && token.type != TokenType::FSTRING_END) {
                tokens.push_back(token);
            }
        }
        for (std::size_t i = 0; i + 3 < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::HASH && tokens[i + 1].type == TokenType::IDENTIFIER && tokens[i + 1].value == "fmt" && tokens[i + 2].type == TokenType::IDENTIFIER
                && (tokens[i + 3].type == TokenType::IDENTIFIER || tokens[i + 3].type == TokenType::NUMBER) && tokens[i + 3].line == tokens[i + 2].line) {
                Directive directive;
                directive.key = std::string(tokens[i + 2].value);
                directive.value = std::string(tokens[i + 3].value);
                directive.line = tokens[i + 2].line;
                directive.column = tokens[i + 2].column;
                directive.key_length = tokens[i + 2].value.size();
                directive.value_line = tokens[i + 3].line;
                directive.value_column = tokens[i + 3].column;
                directive.value_length = tokens[i + 3].value.size();
                result.push_back(std::move(directive));
                i += 3;
            }
        }
        return result;
    }

    Result format_source(const std::string& path, const std::string& original) {
        Result result;
        const std::string source = normalize_newlines(original);
        // 문법 오류가 있으면 고치지 않는다
        {
            lexor::Lexor lexer(source);
            parser::Parser parser(path, lexer);
            if (parser.parse() == nullptr) {
                for (const auto& error : parser.errors) {
                    result.diagnostics.push_back({path, error.line, error.column, error.message, 0, middleend::Level::Error, error.code, error.help});
                }
                return result;
            }
        }
        const std::vector<Directive> directives = read_directives(source);
        std::vector<OptionProblem> problems;
        const Settings settings = read_settings(directives, &problems);
        for (const auto& problem : problems) {
            result.diagnostics.push_back(option_diagnostic(path, directives[problem.directive], problem));
        }
        result.has_settings = !directives.empty();
        if (directives.empty()) {
            result.ok = true;
            result.text = original;
            return result;
        }
        const auto toks = tokenize(source);
        std::vector<Stmt> top;
        if (!toks || !TreeBuilder(*toks).build(top)) {
            result.diagnostics.push_back({path, 1, 1, "Cannot format this file: its structure is not understood", 0, middleend::Level::Error, "E4001"});
            return result;
        }
        std::string text = Printer(*toks, settings).print(top);

        // 뜻이 달라지지 않았는지 점검한다: 다시 읽어 지고, 토큰이 같다 (순서를 바꾸는 옵션이 켜졌으면 순서만 다를 수 있다)
        bool before_readable = false;
        bool after_readable = false;
        std::vector<std::string> before = token_texts(source, before_readable);
        std::vector<std::string> after = token_texts(text, after_readable);
        before.erase(std::remove(before.begin(), before.end(), ";"), before.end());
        after.erase(std::remove(after.begin(), after.end(), ";"), after.end());
        bool same = before_readable && after_readable;
        if (same) {
            if (settings.sort_properties || settings.sort_includes) {
                std::sort(before.begin(), before.end());
                std::sort(after.begin(), after.end());
            }
            same = before == after;
        }
        if (same) {
            lexor::Lexor lexer(text);
            parser::Parser parser(path, lexer);
            same = parser.parse() != nullptr;
        }
        if (!same) {
            result.diagnostics.push_back({path, 1, 1, "Formatting would change the document, so the file was left alone", 0, middleend::Level::Error, "E4002"});
            return result;
        }

        bool crlf = settings.line_ending == Settings::LineEnding::Crlf;
        if (settings.line_ending == Settings::LineEnding::Keep) {
            std::size_t crlf_count = 0;
            std::size_t lf_count = 0;
            for (std::size_t i = 0; i < original.size(); ++i) {
                if (original[i] == '\n') {
                    (i > 0 && original[i - 1] == '\r' ? crlf_count : lf_count)++;
                }
            }
            crlf = crlf_count > lf_count;
        }
        result.ok = true;
        result.text = crlf ? with_crlf(text) : text;
        return result;
    }
}

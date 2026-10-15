#include "source_text.h"

#include <cstddef>
#include <string_view>

namespace templide::middleend {
    namespace {
        constexpr std::string_view fix = "; save it as UTF-8 (in the editor: Save with Encoding)";

        bool continuation(std::string_view text, std::size_t at, unsigned char low = 0x80, unsigned char high = 0xBF) {
            if (at >= text.size()) {
                return false;
            }
            const auto byte = static_cast<unsigned char>(text[at]);
            return byte >= low && byte <= high;
        }

        // 올바른 UTF-8이면 text.size(), 아니면 처음 잘못된 바이트의 위치. 겹쳐 쓴 부호, 서로게이트, U+10FFFF 너머는 잘못이다
        std::size_t valid_utf8_end(std::string_view text) {
            std::size_t at = 0;
            while (at < text.size()) {
                const auto lead = static_cast<unsigned char>(text[at]);
                std::size_t length = 0;
                if (lead < 0x80) {
                    length = 1;
                } else if (lead >= 0xC2 && lead <= 0xDF) {
                    length = continuation(text, at + 1) ? 2 : 0;
                } else if (lead >= 0xE0 && lead <= 0xEF) {
                    const unsigned char low = lead == 0xE0 ? 0xA0 : 0x80;
                    const unsigned char high = lead == 0xED ? 0x9F : 0xBF;
                    length = continuation(text, at + 1, low, high) && continuation(text, at + 2) ? 3 : 0;
                } else if (lead >= 0xF0 && lead <= 0xF4) {
                    const unsigned char low = lead == 0xF0 ? 0x90 : 0x80;
                    const unsigned char high = lead == 0xF4 ? 0x8F : 0xBF;
                    length = continuation(text, at + 1, low, high) && continuation(text, at + 2) && continuation(text, at + 3) ? 4 : 0;
                }
                if (length == 0) {
                    return at;
                }
                at += length;
            }
            return text.size();
        }

        std::size_t line_of(std::string_view text, std::size_t at) {
            std::size_t line = 1;
            for (std::size_t i = 0; i < at && i < text.size(); ++i) {
                line += text[i] == '\n';
            }
            return line;
        }

        void append_utf8(std::string& out, char32_t code) {
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

        // BOM 뒤의 UTF-16을 UTF-8로. 짝이 맞지 않는 서로게이트나 홀수 바이트는 잘못이다
        std::optional<std::string> from_utf16(std::string_view bytes, bool little_endian, std::string& error) {
            if (bytes.size() % 2 != 0) {
                error = std::string("is not valid UTF-16 (odd number of bytes)") + std::string(fix);
                return std::nullopt;
            }
            const auto unit = [&](std::size_t i) {
                const auto first = static_cast<unsigned char>(bytes[i]);
                const auto second = static_cast<unsigned char>(bytes[i + 1]);
                return static_cast<char32_t>(little_endian ? (second << 8) | first : (first << 8) | second);
            };
            std::string out;
            out.reserve(bytes.size());
            for (std::size_t i = 0; i < bytes.size(); i += 2) {
                char32_t code = unit(i);
                if (code >= 0xD800 && code <= 0xDBFF && i + 2 < bytes.size() && unit(i + 2) >= 0xDC00 && unit(i + 2) <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (unit(i + 2) - 0xDC00);
                    i += 2;
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    error = std::string("is not valid UTF-16 (unpaired surrogate)") + std::string(fix);
                    return std::nullopt;
                }
                append_utf8(out, code);
            }
            return out;
        }
    }

    std::optional<std::string> decode_source(const std::string& bytes, std::string& error) {
        const std::string_view text = bytes;
        if (text.substr(0, 4) == std::string_view("\xFF\xFE\x00\x00", 4) || text.substr(0, 4) == std::string_view("\x00\x00\xFE\xFF", 4)) {
            error = std::string("is UTF-32, which the compiler cannot read") + std::string(fix);
            return std::nullopt;
        }
        if (text.substr(0, 2) == "\xFF\xFE") {
            return from_utf16(text.substr(2), true, error);
        }
        if (text.substr(0, 2) == "\xFE\xFF") {
            return from_utf16(text.substr(2), false, error);
        }
        const std::size_t skip = text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
        const std::string_view body = text.substr(skip);
        if (const std::size_t bad = valid_utf8_end(body); bad != body.size()) {
            error = "is not UTF-8 (the first byte that is not valid UTF-8 is on line " + std::to_string(line_of(body, bad)) + ")" + std::string(fix);
            return std::nullopt;
        }
        return std::string(body);
    }
}

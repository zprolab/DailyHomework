#include "dhw/utf8.h"

namespace dhw::utf8 {
namespace {

bool continuation(char ch) {
    return (static_cast<unsigned char>(ch) & 0xC0u) == 0x80u;
}

}  // namespace

std::u32string decode(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t code_point = 0xFFFD;
        if (lead < 0x80u) {
            code_point = lead;
        } else if ((lead & 0xE0u) == 0xC0u) {
            length = 2;
            code_point = lead & 0x1Fu;
        } else if ((lead & 0xF0u) == 0xE0u) {
            length = 3;
            code_point = lead & 0x0Fu;
        } else if ((lead & 0xF8u) == 0xF0u) {
            length = 4;
            code_point = lead & 0x07u;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + length > text.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool complete = true;
        for (std::size_t k = 1; k < length; ++k) {
            if (!continuation(text[i + k])) {
                complete = false;
                break;
            }
            code_point = (code_point << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
        }
        if (!complete) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        const char32_t minimum =
            length == 1 ? 0x0 : (length == 2 ? 0x80 : (length == 3 ? 0x800 : 0x10000));
        if (code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            out.push_back(0xFFFD);
        } else {
            out.push_back(code_point);
        }
        i += length;
    }
    return out;
}

std::string encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t code_point : text) {
        if (code_point <= 0x7F) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }
    return out;
}

std::size_t count(std::string_view text) {
    std::size_t total = 0;
    for (char ch : text) {
        if (!continuation(ch)) {
            ++total;
        }
    }
    return total;
}

bool is_valid(std::string_view text) {
    return encode(decode(text)) == text;
}

bool is_wide(char32_t code_point) {
    return (code_point >= 0x1100 && code_point <= 0x115F) ||
           (code_point >= 0x2E80 && code_point <= 0xA4CF) ||
           (code_point >= 0xAC00 && code_point <= 0xD7A3) ||
           (code_point >= 0xF900 && code_point <= 0xFAFF) ||
           (code_point >= 0xFE30 && code_point <= 0xFE6F) ||
           (code_point >= 0xFF00 && code_point <= 0xFF60) ||
           (code_point >= 0xFFE0 && code_point <= 0xFFE6) ||
           (code_point >= 0x1F300 && code_point <= 0x1FAFF);
}

bool is_space(char32_t code_point) {
    return code_point == U' ' || code_point == U'\t' || code_point == U'\u3000';
}

}  // namespace dhw::utf8

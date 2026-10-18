#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace dhw::utf8 {

// Decodes UTF-8 into code points; invalid bytes are replaced by U+FFFD.
std::u32string decode(std::string_view text);
std::string encode(std::u32string_view text);

std::size_t count(std::string_view text);
bool is_valid(std::string_view text);

// Rough East-Asian-width classification used by the fallback metrics.
bool is_wide(char32_t code_point);
bool is_space(char32_t code_point);

}  // namespace dhw::utf8

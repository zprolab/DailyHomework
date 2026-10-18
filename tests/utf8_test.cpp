#include "dhw/utf8.h"
#include "dhw_test.h"

#include <string>

DHW_TEST(utf8_round_trips_text) {
    const std::string text = "语文 A1 数学 📚 é";
    DHW_CHECK_EQ(dhw::utf8::encode(dhw::utf8::decode(text)), text);
    DHW_CHECK(dhw::utf8::is_valid(text));
    DHW_CHECK_EQ(dhw::utf8::decode(text).size(), std::size_t{12});
    DHW_CHECK_EQ(dhw::utf8::count(text), std::size_t{12});
}

DHW_TEST(utf8_counts_code_points_not_bytes) {
    DHW_CHECK_EQ(dhw::utf8::count("abc"), std::size_t{3});
    DHW_CHECK_EQ(dhw::utf8::count("语文"), std::size_t{2});
    DHW_CHECK_EQ(dhw::utf8::count("😀"), std::size_t{1});
    DHW_CHECK_EQ(dhw::utf8::count(""), std::size_t{0});
}

DHW_TEST(utf8_replaces_invalid_sequences) {
    const std::string truncated = "a\xE8\xAF";
    DHW_CHECK_EQ(dhw::utf8::decode(truncated).back(), U'\uFFFD');
    DHW_CHECK(!dhw::utf8::is_valid(truncated));
    const std::string stray = "a\x80z";
    DHW_CHECK_EQ(dhw::utf8::decode(stray).size(), std::size_t{3});
    DHW_CHECK(!dhw::utf8::is_valid(stray));
    const std::string overlong = "\xC0\x80";
    DHW_CHECK(!dhw::utf8::is_valid(overlong));
    const std::string surrogate = "\xED\xA0\x80";
    DHW_CHECK(!dhw::utf8::is_valid(surrogate));
    const std::string too_large = "\xF4\x90\x80\x80";
    DHW_CHECK(!dhw::utf8::is_valid(too_large));
}

DHW_TEST(utf8_classifies_width) {
    DHW_CHECK(dhw::utf8::is_wide(U'语'));
    DHW_CHECK(dhw::utf8::is_wide(U'，'));
    DHW_CHECK(dhw::utf8::is_wide(U'😀'));
    DHW_CHECK(!dhw::utf8::is_wide(U'A'));
    DHW_CHECK(!dhw::utf8::is_wide(U'1'));
    DHW_CHECK(dhw::utf8::is_space(U' '));
    DHW_CHECK(dhw::utf8::is_space(U'\u3000'));
    DHW_CHECK(!dhw::utf8::is_space(U'A'));
}

DHW_TEST(utf8_encodes_astral_plane) {
    const std::u32string code_points = {0x1F4DA};
    DHW_CHECK_EQ(dhw::utf8::encode(code_points), std::string("📚"));
    DHW_CHECK_EQ(dhw::utf8::decode("📚")[0], U'\U0001F4DA');
}

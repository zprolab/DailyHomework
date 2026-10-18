#include "dhw/json.h"
#include "dhw_test.h"

#include <string>

DHW_TEST(json_parses_scalars) {
    DHW_CHECK(dhw::Json::parse("null").value.is_null());
    DHW_CHECK_EQ(dhw::Json::parse("true").value.as_bool(), true);
    DHW_CHECK_EQ(dhw::Json::parse("false").value.as_bool(), false);
    DHW_CHECK_EQ(dhw::Json::parse("42").value.as_int(), 42);
    DHW_CHECK_EQ(dhw::Json::parse("  -7  ").value.as_int(), -7);
    DHW_CHECK_EQ(dhw::Json::parse("1.5").value.as_number(), 1.5);
    DHW_CHECK_EQ(dhw::Json::parse("\"hi\"").value.as_string(), std::string("hi"));
    DHW_CHECK_EQ(dhw::Json::parse("[]").value.size(), std::size_t{0});
    DHW_CHECK_EQ(dhw::Json::parse("{}").value.size(), std::size_t{0});
}

DHW_TEST(json_parses_numbers) {
    DHW_CHECK_EQ(dhw::Json::parse("2e3").value.as_int(), 2000);
    DHW_CHECK_EQ(dhw::Json::parse("2E-2").value.as_number(), 0.02);
    DHW_CHECK_EQ(dhw::Json::parse("-0.5").value.as_number(), -0.5);
    DHW_CHECK_EQ(dhw::Json::parse("9007199254740993").value.as_number(), 9007199254740992.0);
    DHW_CHECK_EQ(dhw::Json::parse("1.0").value.as_number(), 1.0);
    DHW_CHECK_EQ(dhw::Json::parse("3.6").value.as_int(), 4);
}

DHW_TEST(json_parses_nested_structures) {
    const auto result = dhw::Json::parse(R"({"a":{"b":[1,2,{"c":null}]}})");
    DHW_CHECK(result.ok);
    const dhw::Json* b = result.value.find("a");
    DHW_CHECK(b != nullptr);
    const dhw::Json* list = b->find("b");
    DHW_CHECK(list != nullptr);
    DHW_CHECK_EQ(list->size(), std::size_t{3});
    DHW_CHECK_EQ(list->at(0).as_int(), 1);
    DHW_CHECK_EQ(list->at(2).find("c")->is_null(), true);
}

DHW_TEST(json_keeps_member_order) {
    const auto result = dhw::Json::parse(R"({"z":1,"a":2,"m":3})");
    DHW_CHECK(result.ok);
    DHW_CHECK_EQ(result.value.members().size(), std::size_t{3});
    DHW_CHECK_EQ(result.value.members()[0].first, std::string("z"));
    DHW_CHECK_EQ(result.value.members()[1].first, std::string("a"));
    DHW_CHECK_EQ(result.value.members()[2].first, std::string("m"));
}

DHW_TEST(json_duplicate_keys_keep_the_last_value) {
    const auto result = dhw::Json::parse(R"({"a":1,"b":9,"a":2})");
    DHW_CHECK(result.ok);
    DHW_CHECK_EQ(result.value.members().size(), std::size_t{2});
    DHW_CHECK_EQ(result.value.find("a")->as_int(), 2);
}

DHW_TEST(json_decodes_escapes_and_unicode) {
    DHW_CHECK_EQ(dhw::Json::parse(R"("a\n\t\"\\\/\b\f\r")").value.as_string(),
                 std::string("a\n\t\"\\/\b\f\r"));
    DHW_CHECK_EQ(dhw::Json::parse(R"("\u4e2d\u6587")").value.as_string(), std::string("中文"));
    DHW_CHECK_EQ(dhw::Json::parse(R"("\ud83d\ude00")").value.as_string(), std::string("😀"));
    DHW_CHECK_EQ(dhw::Json::parse(R"("\u00e9")").value.as_string(), std::string("é"));
}

DHW_TEST(json_keeps_utf8_text_verbatim) {
    const auto result = dhw::Json::parse(R"({"subject":"语文","students":"1号 张三"})");
    DHW_CHECK(result.ok);
    DHW_CHECK_EQ(result.value.find("subject")->as_string(), std::string("语文"));
    DHW_CHECK_EQ(result.value.dump(-1), std::string(R"({"subject":"语文","students":"1号 张三"})"));
}

DHW_TEST(json_accepts_utf8_bom) {
    const std::string text = "\xEF\xBB\xBF{\"a\":1}";
    const auto result = dhw::Json::parse(text);
    DHW_CHECK(result.ok);
    DHW_CHECK_EQ(result.value.find("a")->as_int(), 1);
}

DHW_TEST(json_reports_parse_errors) {
    const char* bad[] = {
        "",
        "   ",
        "{",
        "[1,",
        "{\"a\":}",
        "{\"a\" 1}",
        "{a:1}",
        "[1,2,]",
        "\"abc",
        R"("bad \q escape")",
        R"("\u12")",
        R"("\ud83d")",
        "01",
        "1.",
        "1e",
        "nul",
        "{} extra",
        "[1] [2]",
    };
    for (const char* text : bad) {
        const auto result = dhw::Json::parse(text);
        DHW_CHECK(!result.ok);
        DHW_CHECK(!result.error.empty());
    }
}

DHW_TEST(json_rejects_raw_control_characters) {
    const auto result = dhw::Json::parse("\"a\nb\"");
    DHW_CHECK(!result.ok);
}

DHW_TEST(json_dump_pretty_matches_serde_layout) {
    const auto result = dhw::Json::parse(R"({"a":1,"b":[1,2],"c":{"d":"x"},"e":[],"f":{}})");
    DHW_CHECK(result.ok);
    const std::string expected =
        "{\n"
        "  \"a\": 1,\n"
        "  \"b\": [\n"
        "    1,\n"
        "    2\n"
        "  ],\n"
        "  \"c\": {\n"
        "    \"d\": \"x\"\n"
        "  },\n"
        "  \"e\": [],\n"
        "  \"f\": {}\n"
        "}";
    DHW_CHECK_EQ(result.value.dump(2), expected);
}

DHW_TEST(json_dump_compact_and_round_trip) {
    const std::string source =
        R"({"version":1,"days":{"2026-10-01":{"homework":[{"subject":"语文","text":"a","submit_next":false}]}}})";
    const auto first = dhw::Json::parse(source);
    DHW_CHECK(first.ok);
    const std::string compact = first.value.dump(-1);
    DHW_CHECK_EQ(compact, source);
    const auto second = dhw::Json::parse(compact);
    DHW_CHECK(second.ok);
    DHW_CHECK_EQ(second.value.dump(2), first.value.dump(2));
}

DHW_TEST(json_formats_numbers_like_serde) {
    DHW_CHECK_EQ(dhw::Json(1.0).dump(-1), std::string("1"));
    DHW_CHECK_EQ(dhw::Json(0.5).dump(-1), std::string("0.5"));
    DHW_CHECK_EQ(dhw::Json(0.0).dump(-1), std::string("0"));
    DHW_CHECK_EQ(dhw::Json(-0.0).dump(-1), std::string("-0"));
    DHW_CHECK_EQ(dhw::Json(2000).dump(-1), std::string("2000"));
    DHW_CHECK_EQ(dhw::Json(-3).dump(-1), std::string("-3"));
    DHW_CHECK_EQ(dhw::Json(1.0 / 3.0).dump(-1), std::string("0.3333333333333333"));
    DHW_CHECK_EQ(dhw::Json(1e21).dump(-1), std::string("1e+21"));
}

DHW_TEST(json_escapes_on_dump) {
    std::string text;
    text += "line1\nline2\t\"quoted\"\\slash";
    text.push_back('\x01');
    DHW_CHECK_EQ(dhw::Json(text).dump(-1), std::string("\"line1\\nline2\\t\\\"quoted\\\"\\\\slash\\u0001\""));
}

DHW_TEST(json_accessors_and_mutation) {
    dhw::Json value = dhw::Json::object();
    value.set("a", dhw::Json(1));
    value.set("b", dhw::Json::array());
    value.find("b") == nullptr ? void() : void();
    DHW_CHECK_EQ(value.find("a")->as_int(), 1);
    DHW_CHECK(value.find("missing") == nullptr);
    value.set("a", dhw::Json(2));
    DHW_CHECK_EQ(value.size(), std::size_t{2});
    DHW_CHECK_EQ(value.find("a")->as_int(), 2);
    DHW_CHECK(value.erase("a"));
    DHW_CHECK(!value.erase("a"));
    DHW_CHECK_EQ(value.size(), std::size_t{1});

    DHW_CHECK_EQ(dhw::Json().as_int(5), 5);
    DHW_CHECK_EQ(dhw::Json(1).as_string(), std::string(""));
    DHW_CHECK_EQ(dhw::Json(1).string_or("x"), std::string("x"));
    DHW_CHECK_EQ(dhw::Json::array().at(0).is_null(), true);

    dhw::Json list = dhw::Json::array();
    list.push_back(dhw::Json(1));
    list.push_back(dhw::Json(2));
    DHW_CHECK_EQ(list.size(), std::size_t{2});
    DHW_CHECK_EQ(list.at(1).as_int(), 2);
    list.push_back(dhw::Json(3));
    DHW_CHECK_EQ(list.at(2).as_int(), 3);
}

DHW_TEST(json_rejects_excessive_nesting) {
    std::string text;
    for (int i = 0; i < 200; ++i) {
        text.push_back('[');
    }
    for (int i = 0; i < 200; ++i) {
        text.push_back(']');
    }
    const auto result = dhw::Json::parse(text);
    DHW_CHECK(!result.ok);
    DHW_CHECK(result.error.find("nesting") != std::string::npos);
}

DHW_TEST(json_parse_error_reports_position) {
    const auto result = dhw::Json::parse("{\n  \"a\": 1,\n  \"b\": oops\n}");
    DHW_CHECK(!result.ok);
    DHW_CHECK(result.error.find("line 3") != std::string::npos);
}

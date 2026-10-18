#include "dhw/fs.h"
#include "dhw/style.h"
#include "dhw_test.h"

#include <string>
#include <vector>

namespace {

struct ScopedDir {
    explicit ScopedDir(const std::string& label) {
        static int counter = 0;
        path = dhw::fs::join(dhw::fs::temp_dir(),
                             "dhw_style_test_" + label + "_" + std::to_string(++counter));
        dhw::fs::remove_all(path);
        dhw::fs::create_directories(path);
    }
    ~ScopedDir() { dhw::fs::remove_all(path); }
    std::string file(const std::string& name) const { return dhw::fs::join(path, name); }
    std::string path;
};

const std::vector<std::string> kFieldOrder = {
    "headline_font",       "headline_sub_font",    "panel_title_font",
    "panel_radius",        "panel_padding",        "announcement_font_max",
    "announcement_font_min", "homework_font_max",  "homework_font_min",
    "homework_label_max_chars", "badge_size",      "badge_radius",
    "badge_font",          "schedule_pitch_max",   "schedule_font_max",
    "schedule_font_min",   "schedule_row_ratio",   "schedule_chip_max",
    "speech_font_max",     "speech_font_min",      "empty_state_font",
    "warning_font",        "mode_pill_font",
};

dhw::LayoutStyle parse(const std::string& text) {
    const dhw::JsonParse parsed = dhw::Json::parse(text);
    DHW_CHECK(parsed.ok);
    return dhw::style_from_json(parsed.value);
}

bool same_style(const dhw::LayoutStyle& left, const dhw::LayoutStyle& right) {
    return dhw::style_to_json(left).dump() == dhw::style_to_json(right).dump();
}

}  // namespace

DHW_TEST(style_defaults_are_the_documented_table) {
    const dhw::LayoutStyle style;
    DHW_CHECK_EQ(style.headline_font, 48.0);
    DHW_CHECK_EQ(style.headline_sub_font, 26.0);
    DHW_CHECK_EQ(style.panel_title_font, 42.0);
    DHW_CHECK_EQ(style.panel_radius, 28.0);
    DHW_CHECK_EQ(style.panel_padding, 26.0);
    DHW_CHECK_EQ(style.announcement_font_max, 40.0);
    DHW_CHECK_EQ(style.announcement_font_min, 26.0);
    DHW_CHECK_EQ(style.homework_font_max, 36.0);
    DHW_CHECK_EQ(style.homework_font_min, 26.0);
    DHW_CHECK_EQ(style.homework_label_max_chars, 5);
    DHW_CHECK_EQ(style.badge_size, 44.0);
    DHW_CHECK_EQ(style.badge_radius, 15.0);
    DHW_CHECK_EQ(style.badge_font, 26.0);
    DHW_CHECK_EQ(style.schedule_pitch_max, 52.0);
    DHW_CHECK_EQ(style.schedule_font_max, 32.0);
    DHW_CHECK_EQ(style.schedule_font_min, 22.0);
    DHW_CHECK_EQ(style.schedule_row_ratio, 0.62);
    DHW_CHECK_EQ(style.schedule_chip_max, 30.0);
    DHW_CHECK_EQ(style.speech_font_max, 32.0);
    DHW_CHECK_EQ(style.speech_font_min, 24.0);
    DHW_CHECK_EQ(style.empty_state_font, 30.0);
    DHW_CHECK_EQ(style.warning_font, 26.0);
    DHW_CHECK_EQ(style.mode_pill_font, 26.0);
}

DHW_TEST(style_json_lists_every_field_in_declaration_order) {
    const dhw::Json root = dhw::style_to_json(dhw::LayoutStyle{});
    DHW_CHECK(root.is_object());
    DHW_CHECK_EQ(root.members().size(), kFieldOrder.size());
    for (std::size_t index = 0; index < kFieldOrder.size(); ++index) {
        DHW_CHECK_EQ(root.members()[index].first, kFieldOrder[index]);
    }
    DHW_CHECK_EQ(std::string(dhw::kStyleFileName), std::string("style.json"));
}

DHW_TEST(style_from_json_overrides_single_fields) {
    const dhw::LayoutStyle changed = parse("{\"badge_size\": 30, \"homework_label_max_chars\": 3}");
    dhw::LayoutStyle expected;
    expected.badge_size = 30.0;
    expected.homework_label_max_chars = 3;
    DHW_CHECK(same_style(changed, expected));
}

DHW_TEST(style_from_json_ignores_unknown_keys) {
    const dhw::LayoutStyle changed =
        parse("{\"nope\": 1, \"headline_font\": 56, \"nested\": {\"badge_size\": 12}}");
    dhw::LayoutStyle expected;
    expected.headline_font = 56.0;
    DHW_CHECK(same_style(changed, expected));
}

DHW_TEST(style_from_json_falls_back_on_wrong_types) {
    const dhw::LayoutStyle style = parse(
        "{\"headline_font\": \"48\", \"badge_size\": null, \"badge_radius\": [], "
        "\"schedule_row_ratio\": {}, \"homework_label_max_chars\": \"9\", "
        "\"panel_title_font\": true}");
    DHW_CHECK(same_style(style, dhw::LayoutStyle{}));
}

DHW_TEST(style_from_json_accepts_an_empty_object) {
    DHW_CHECK(same_style(parse("{}"), dhw::LayoutStyle{}));
    DHW_CHECK(same_style(dhw::style_from_json(dhw::Json()), dhw::LayoutStyle{}));
}

DHW_TEST(style_missing_file_is_not_an_error) {
    ScopedDir dir("missing");
    const dhw::StyleLoad loaded = dhw::load_style_file(dir.file("style.json"));
    DHW_CHECK(!loaded.loaded);
    DHW_CHECK(loaded.error.empty());
    DHW_CHECK(same_style(loaded.style, dhw::LayoutStyle{}));
}

DHW_TEST(style_broken_file_falls_back_and_explains) {
    ScopedDir dir("broken");
    const std::string path = dir.file("style.json");
    DHW_CHECK(dhw::fs::write_text(path, "{ this is not json"));
    const dhw::StyleLoad loaded = dhw::load_style_file(path);
    DHW_CHECK(!loaded.loaded);
    DHW_CHECK(!loaded.error.empty());
    DHW_CHECK(same_style(loaded.style, dhw::LayoutStyle{}));

    DHW_CHECK(dhw::fs::write_text(path, "[1, 2, 3]"));
    const dhw::StyleLoad array = dhw::load_style_file(path);
    DHW_CHECK(!array.loaded);
    DHW_CHECK(!array.error.empty());
    DHW_CHECK(same_style(array.style, dhw::LayoutStyle{}));
}

DHW_TEST(style_file_round_trips) {
    ScopedDir dir("round_trip");
    const std::string path = dir.file("nested/style.json");
    dhw::LayoutStyle style;
    style.headline_font = 60.0;
    style.badge_size = 30.5;
    style.badge_radius = 9.0;
    style.badge_font = 19.0;
    style.announcement_font_max = 33.0;
    style.homework_label_max_chars = 3;
    style.schedule_row_ratio = 0.7;
    style.mode_pill_font = 21.0;

    DHW_CHECK(dhw::save_style_file(path, style));
    DHW_CHECK(dhw::fs::is_regular_file(path));
    const dhw::StyleLoad loaded = dhw::load_style_file(path);
    DHW_CHECK(loaded.loaded);
    DHW_CHECK(loaded.error.empty());
    DHW_CHECK(same_style(loaded.style, style));

    DHW_CHECK(dhw::save_style_file(path, dhw::LayoutStyle{}));
    const dhw::StyleLoad again = dhw::load_style_file(path);
    DHW_CHECK(again.loaded);
    DHW_CHECK(same_style(again.style, dhw::LayoutStyle{}));
}

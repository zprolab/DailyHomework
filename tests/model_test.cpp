#include "dhw/fs.h"
#include "dhw/model.h"
#include "dhw_test.h"

#include <string>
#include <vector>

namespace {

dhw::AppData rich_sample() {
    dhw::AppData data = dhw::AppData::defaults();
    data.version = 1;
    data.settings.image_interval_secs = 120;
    data.settings.morning_cutoff = "07:30";
    data.settings.show_badge = false;
    data.settings.english_split = false;
    data.settings.ui_scale = 1.25;

    dhw::Day monday;
    monday.homework.push_back(dhw::Homework{"语文", "抄写第 3 课", true});
    monday.homework.push_back(dhw::Homework{"语文", "背诵古诗", false});
    monday.homework.push_back(dhw::Homework{"数学", "练习册 P12", true});
    monday.speeches.push_back(dhw::Speech{"英语", "1号 张三；2号 李四"});
    monday.schedule_override[0] = std::string("体育");
    monday.copied_from_weekday = 2;
    data.days["2026-10-01"] = monday;

    dhw::Day tuesday;
    tuesday.speeches.push_back(dhw::Speech{"语文", "3号 王五"});
    data.days["2026-10-02"] = tuesday;

    data.hidden_dates = {"2026-10-03", "2026-10-04"};
    data.pinned_dates = {"2026-10-08"};
    data.announcements.push_back(dhw::Announcement{1, "明天带手工材料", false});
    data.announcements.push_back(dhw::Announcement{4, "周五穿校服", true});
    return data;
}

}  // namespace

DHW_TEST(model_defaults_shape) {
    const dhw::AppData data = dhw::AppData::defaults();
    DHW_CHECK_EQ(data.version, 1);
    DHW_CHECK_EQ(data.weekly_schedule.size(), std::size_t{7});
    for (const std::vector<std::string>& row : data.weekly_schedule) {
        DHW_CHECK_EQ(row.size(), std::size_t{9});
    }
    DHW_CHECK_EQ(data.weekly_schedule[0][0], std::string("语文"));
    DHW_CHECK_EQ(data.weekly_schedule[0][8], std::string("班会"));
    DHW_CHECK_EQ(data.weekly_schedule[5][0], std::string(""));
    DHW_CHECK_EQ(data.weekly_schedule[6][0], std::string(""));
    DHW_CHECK(data.days.empty());
    DHW_CHECK(data.hidden_dates.empty());
    DHW_CHECK(data.pinned_dates.empty());
    DHW_CHECK(data.announcements.empty());
    DHW_CHECK_EQ(data.settings.image_interval_secs, 60);
    DHW_CHECK_EQ(data.settings.morning_cutoff, std::string("08:00"));
    DHW_CHECK_EQ(data.settings.show_badge, true);
    DHW_CHECK_EQ(data.settings.english_split, true);
    DHW_CHECK_EQ(data.settings.ui_scale, 1.0);
}

DHW_TEST(model_day_defaults_and_lookup) {
    const dhw::Day day;
    DHW_CHECK_EQ(day.schedule_override.size(), std::size_t{9});
    for (const std::optional<std::string>& slot : day.schedule_override) {
        DHW_CHECK(!slot.has_value());
    }
    DHW_CHECK(day.homework.empty());
    DHW_CHECK(!day.copied_from_weekday.has_value());

    dhw::AppData data = dhw::AppData::defaults();
    DHW_CHECK(data.find_day("2026-10-01") == nullptr);
    data.day("2026-10-01").homework.push_back(dhw::Homework{"数学", "x", false});
    DHW_CHECK(data.find_day("2026-10-01") != nullptr);
    DHW_CHECK_EQ(data.days.size(), std::size_t{1});
    DHW_CHECK_EQ(data.next_announcement_id(), 1);
    data.announcements.push_back(dhw::Announcement{7, "a", false});
    DHW_CHECK_EQ(data.next_announcement_id(), 8);
}

DHW_TEST(model_json_member_order_matches_serde) {
    const dhw::Json root = dhw::to_json(rich_sample());
    const std::vector<std::string> expected_keys = {"version",        "weekly_schedule",
                                                    "days",           "hidden_dates",
                                                    "pinned_dates",   "announcements",
                                                    "settings"};
    DHW_CHECK_EQ(root.members().size(), expected_keys.size());
    for (std::size_t i = 0; i < expected_keys.size(); ++i) {
        DHW_CHECK_EQ(root.members()[i].first, expected_keys[i]);
    }

    const dhw::Json day = *root.find("days")->find("2026-10-01");
    DHW_CHECK_EQ(day.members().size(), std::size_t{4});
    DHW_CHECK_EQ(day.members()[0].first, std::string("schedule_override"));
    DHW_CHECK_EQ(day.members()[1].first, std::string("homework"));
    DHW_CHECK_EQ(day.members()[2].first, std::string("speeches"));
    DHW_CHECK_EQ(day.members()[3].first, std::string("copied_from_weekday"));
    DHW_CHECK_EQ(day.find("schedule_override")->size(), std::size_t{9});
    DHW_CHECK(day.find("schedule_override")->at(0).as_string() == std::string("体育"));
    DHW_CHECK(day.find("schedule_override")->at(1).is_null());
    DHW_CHECK_EQ(day.find("copied_from_weekday")->as_int(), 2);

    const dhw::Json homework = day.find("homework")->at(0);
    DHW_CHECK_EQ(homework.members().size(), std::size_t{3});
    DHW_CHECK_EQ(homework.members()[0].first, std::string("subject"));
    DHW_CHECK_EQ(homework.members()[1].first, std::string("text"));
    DHW_CHECK_EQ(homework.members()[2].first, std::string("submit_next"));
    DHW_CHECK_EQ(homework.find("submit_next")->as_bool(), true);

    const dhw::Json settings = *root.find("settings");
    DHW_CHECK_EQ(settings.members().size(), std::size_t{5});
    DHW_CHECK_EQ(settings.members()[0].first, std::string("image_interval_secs"));
    DHW_CHECK_EQ(settings.members()[1].first, std::string("morning_cutoff"));
    DHW_CHECK_EQ(settings.members()[2].first, std::string("show_badge"));
    DHW_CHECK_EQ(settings.members()[3].first, std::string("english_split"));
    DHW_CHECK_EQ(settings.members()[4].first, std::string("ui_scale"));
    DHW_CHECK_EQ(settings.find("ui_scale")->as_number(), 1.25);

    const dhw::Json announcement = root.find("announcements")->at(1);
    DHW_CHECK_EQ(announcement.find("id")->as_int(), 4);
    DHW_CHECK_EQ(announcement.find("done")->as_bool(), true);
}

DHW_TEST(model_round_trips_through_json) {
    const dhw::AppData original = rich_sample();
    const std::string text = dhw::to_json(original).dump(2);
    const dhw::JsonParse parsed = dhw::Json::parse(text);
    DHW_CHECK(parsed.ok);
    const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
    DHW_CHECK(loaded.ok);
    DHW_CHECK(loaded.data == original);
    DHW_CHECK_EQ(dhw::to_json(loaded.data).dump(2), text);
}

DHW_TEST(model_loads_minimal_file_with_defaults) {
    const dhw::JsonParse parsed = dhw::Json::parse(R"({"version":1})");
    DHW_CHECK(parsed.ok);
    const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
    DHW_CHECK(loaded.ok);
    DHW_CHECK(loaded.data == dhw::AppData::defaults());
}

DHW_TEST(model_ignores_legacy_and_unknown_fields) {
    const dhw::JsonParse parsed = dhw::Json::parse(R"({
        "version": 1,
        "unknown_top_level": {"a": 1},
        "announcements": [
            {"id": 2, "text": "带跳绳", "done": true,
             "created_date": "2026-09-01", "done_date": "2026-09-02", "last_seen_date": "2026-09-03"}
        ]
    })");
    DHW_CHECK(parsed.ok);
    const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
    DHW_CHECK(loaded.ok);
    DHW_CHECK_EQ(loaded.data.announcements.size(), std::size_t{1});
    DHW_CHECK_EQ(loaded.data.announcements[0].id, 2);
    DHW_CHECK_EQ(loaded.data.announcements[0].text, std::string("带跳绳"));
    DHW_CHECK_EQ(loaded.data.announcements[0].done, true);
    DHW_CHECK_EQ(loaded.data.days.size(), std::size_t{0});
}

DHW_TEST(model_reports_structural_corruption) {
    const char* broken[] = {
        R"([])",
        R"("text")",
        R"({"days": "oops"})",
        R"({"days": {"2026-10-01": 5}})",
        R"({"days": {"2026-10-01": {"homework": "oops"}}})",
        R"({"days": {"2026-10-01": {"speeches": {}}}})",
        R"({"days": {"2026-10-01": {"schedule_override": {"a": 1}}}})",
        R"({"days": {"2026-10-01": {"schedule_override": [1]}}})",
        R"({"weekly_schedule": 5})",
        R"({"weekly_schedule": ["周一"]})",
        R"({"announcements": {}})",
        R"({"hidden_dates": "2026-10-01"})",
        R"({"settings": []})",
    };
    for (const char* text : broken) {
        const dhw::JsonParse parsed = dhw::Json::parse(text);
        DHW_CHECK(parsed.ok);
        const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
        DHW_CHECK(!loaded.ok);
        DHW_CHECK(!loaded.error.empty());
    }
}

DHW_TEST(model_tolerates_legacy_scalar_shapes) {
    const dhw::JsonParse parsed = dhw::Json::parse(R"({
        "days": {"2026-10-01": {
            "homework": [{"subject": "语文", "text": "x", "submit_next": 1}],
            "speeches": [{"subject": "英语", "students": ["1号 张三", "2号 李四"]}],
            "schedule_override": ["语文", null, "数学"],
            "copied_from_weekday": 9
        }}
    })");
    DHW_CHECK(parsed.ok);
    const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
    DHW_CHECK(loaded.ok);
    const dhw::Day* day = loaded.data.find_day("2026-10-01");
    DHW_CHECK(day != nullptr);
    DHW_CHECK_EQ(day->homework.size(), std::size_t{1});
    DHW_CHECK_EQ(day->homework[0].submit_next, true);
    DHW_CHECK_EQ(day->speeches[0].students, std::string("1号 张三, 2号 李四"));
    DHW_CHECK_EQ(day->schedule_override.size(), std::size_t{9});
    DHW_CHECK(day->schedule_override[0].value() == std::string("语文"));
    DHW_CHECK(!day->schedule_override[1].has_value());
    DHW_CHECK(!day->schedule_override[3].has_value());
    DHW_CHECK(!day->copied_from_weekday.has_value());
}

DHW_TEST(model_normalize_cleans_entries_and_lengths) {
    dhw::AppData data = dhw::AppData::defaults();
    data.weekly_schedule.resize(3);
    data.weekly_schedule[0] = {"语文"};
    dhw::Day day;
    day.schedule_override.resize(12, std::nullopt);
    day.schedule_override[11] = std::string("地理");
    day.homework.push_back(dhw::Homework{"语文", "   ", false});
    day.homework.push_back(dhw::Homework{"数学", "有效", false});
    day.speeches.push_back(dhw::Speech{"英语", "  "});
    day.speeches.push_back(dhw::Speech{"语文", "1号"});
    data.days["2026-10-05"] = day;

    data.normalize(dhw::Date{2026, 10, 5});

    DHW_CHECK_EQ(data.weekly_schedule.size(), std::size_t{7});
    DHW_CHECK_EQ(data.weekly_schedule[0].size(), std::size_t{9});
    DHW_CHECK_EQ(data.weekly_schedule[2].size(), std::size_t{9});
    const dhw::Day& cleaned = data.days["2026-10-05"];
    DHW_CHECK_EQ(cleaned.schedule_override.size(), std::size_t{9});
    DHW_CHECK_EQ(cleaned.homework.size(), std::size_t{1});
    DHW_CHECK_EQ(cleaned.homework[0].text, std::string("有效"));
    DHW_CHECK_EQ(cleaned.speeches.size(), std::size_t{1});
    DHW_CHECK_EQ(cleaned.speeches[0].subject, std::string("语文"));
}

DHW_TEST(model_normalize_prunes_date_lists) {
    dhw::AppData data = dhw::AppData::defaults();
    data.hidden_dates = {"2026-10-04", "2026-09-20", "2026-10-04"};
    data.pinned_dates = {"2026-09-01", "2026-10-08", "2026-10-08", "2026-10-04"};
    data.normalize(dhw::Date{2026, 10, 5});

    DHW_CHECK_EQ(data.hidden_dates.size(), std::size_t{2});
    DHW_CHECK_EQ(data.hidden_dates[0], std::string("2026-09-20"));
    DHW_CHECK_EQ(data.hidden_dates[1], std::string("2026-10-04"));
    DHW_CHECK_EQ(data.pinned_dates.size(), std::size_t{1});
    DHW_CHECK_EQ(data.pinned_dates[0], std::string("2026-10-08"));
}

DHW_TEST(model_reads_every_bundled_fixture) {
    const std::string root = DHW_FIXTURE_DIR;
    const std::vector<std::string> files = {
        "/data.sample.json",
        "/data.canonical.json",
        "/demo/data.json",
        "/demo-stress/data.json",
    };
    for (const std::string& relative : files) {
        const std::string path = root + relative;
        const std::optional<std::string> text = dhw::fs::read_text(path);
        DHW_CHECK(text.has_value());
        const dhw::JsonParse parsed = dhw::Json::parse(*text);
        DHW_CHECK(parsed.ok);
        const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
        DHW_CHECK(loaded.ok);
        DHW_CHECK_EQ(loaded.data.version, 1);
        DHW_CHECK_EQ(loaded.data.weekly_schedule.size(), std::size_t{7});
        DHW_CHECK(!loaded.data.days.empty());
        const std::string again = dhw::to_json(loaded.data).dump(2);
        const dhw::JsonParse reparsed = dhw::Json::parse(again);
        DHW_CHECK(reparsed.ok);
        DHW_CHECK(dhw::parse_app_data(reparsed.value).data == loaded.data);
    }
}

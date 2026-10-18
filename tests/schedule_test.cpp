#include "dhw/schedule.h"

#include "dhw_test.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

dhw::AppData sample() {
    // The schedule rules are tested against an explicit timetable: AppData::defaults()
    // itself must be empty now (see model_defaults_shape for that contract).
    dhw::AppData data = dhw::AppData::defaults();
    data.weekly_schedule = dhw::normalize_weekly({
        {"语文", "数学", "历史", "物理", "地理", "体育", "美术", "英语", "班会"},
        {"数学", "物理", "英语", "英语", "语文", "自习", "信息", "政治", "体育"},
        {"数学", "语文", "劳技", "英语", "英语", "心理", "数学", "历史", "生物"},
        {"政治", "生物", "数学", "语文", "语文", "英语", "英语", "物理", "体育"},
        {"语文", "英语", "英语", "数学", "体育", "信息", "物理", "地理", "音乐"},
        {},
        {},
    });
    return data;
}

dhw::Time at(int hour, int minute) {
    return dhw::Time{hour, minute};
}

dhw::Day day_with_override(int index, const std::string& name) {
    dhw::Day day;
    day.schedule_override[static_cast<std::size_t>(index)] = name;
    return day;
}

bool contains(const std::vector<std::string>& items, const std::string& value) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

int count_starting_with(const std::vector<std::string>& items, const std::string& prefix) {
    int count = 0;
    for (const std::string& item : items) {
        if (item.rfind(prefix, 0) == 0) {
            ++count;
        }
    }
    return count;
}

}  // namespace

DHW_TEST(default_weekly_is_7x9) {
    const dhw::AppData data = dhw::AppData::defaults();
    DHW_CHECK_EQ(data.weekly_schedule.size(), std::size_t{7});
    for (const std::vector<std::string>& row : data.weekly_schedule) {
        DHW_CHECK_EQ(row.size(), std::size_t{9});
        for (const std::string& cell : row) {
            DHW_CHECK_EQ(cell, std::string(""));
        }
    }
}

DHW_TEST(weekday_helpers) {
    DHW_CHECK_EQ(dhw::weekday_index("2026-10-01"), 3);
    DHW_CHECK_EQ(dhw::weekday_cn("2026-10-01"), std::string("星期四"));
    DHW_CHECK_EQ(dhw::add_days_text("2026-10-01", 1), std::string("2026-10-02"));
    DHW_CHECK_EQ(dhw::add_days_text("2026-10-01", -3), std::string("2026-09-28"));

    DHW_CHECK(!dhw::weekday_index("坏日期").has_value());
    DHW_CHECK(!dhw::weekday_index("2026-02-30").has_value());
    DHW_CHECK_EQ(dhw::weekday_cn("坏日期"), std::string(""));
    DHW_CHECK_EQ(dhw::add_days_text("坏日期", 5), std::string("坏日期"));
}

DHW_TEST(effective_uses_template) {
    const dhw::AppData data = sample();
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-01");
    DHW_CHECK_EQ(slots.size(), std::size_t{9});
    DHW_CHECK_EQ(slots[0].name, std::string("政治"));
    DHW_CHECK(!slots[0].changed);
}

DHW_TEST(override_marks_changed) {
    dhw::AppData data = sample();
    data.days["2026-10-01"] = day_with_override(0, "数学");
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-01");
    DHW_CHECK_EQ(slots[0].name, std::string("数学"));
    DHW_CHECK(slots[0].changed);
    DHW_CHECK(!slots[1].changed);
}

DHW_TEST(override_same_as_base_not_changed) {
    dhw::AppData data = sample();
    data.days["2026-10-01"] = day_with_override(0, "政治");
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-01");
    DHW_CHECK_EQ(slots[0].name, std::string("政治"));
    DHW_CHECK(!slots[0].changed);
}

DHW_TEST(copied_from_weekday) {
    dhw::AppData data = sample();
    dhw::Day day;
    day.copied_from_weekday = 0;
    data.days["2026-10-03"] = day;
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-03");
    DHW_CHECK_EQ(slots[0].name, std::string("语文"));
    DHW_CHECK(!slots[0].changed);
}

DHW_TEST(effective_out_of_range_template_index) {
    dhw::AppData data = sample();
    dhw::Day day;
    day.copied_from_weekday = 9;
    data.days["2026-10-03"] = day;
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-03");
    DHW_CHECK_EQ(slots.size(), std::size_t{9});
    for (const dhw::Slot& slot : slots) {
        DHW_CHECK_EQ(slot.name, std::string(""));
        DHW_CHECK(!slot.changed);
    }
    DHW_CHECK(dhw::is_school_day(data, "2026-10-03"));

    data.days["2026-10-03"].schedule_override[3] = std::string("体育");
    const dhw::Slot overridden = dhw::effective_schedule(data, "2026-10-03")[3];
    DHW_CHECK_EQ(overridden.name, std::string("体育"));
    DHW_CHECK(overridden.changed);

    dhw::Day negative;
    negative.copied_from_weekday = -1;
    data.days["2026-10-04"] = negative;
    for (const dhw::Slot& slot : dhw::effective_schedule(data, "2026-10-04")) {
        DHW_CHECK_EQ(slot.name, std::string(""));
        DHW_CHECK(!slot.changed);
    }
}

DHW_TEST(override_empty_string_marks_changed) {
    dhw::AppData data = sample();
    dhw::Day day;
    day.schedule_override[0] = std::string("");
    day.schedule_override[5] = std::string("英语");
    data.days["2026-10-01"] = day;
    const std::vector<dhw::Slot> slots = dhw::effective_schedule(data, "2026-10-01");
    DHW_CHECK_EQ(slots[0].name, std::string(""));
    DHW_CHECK(slots[0].changed);
    DHW_CHECK_EQ(slots[5].name, std::string("英语"));
    DHW_CHECK(!slots[5].changed);

    dhw::AppData weekend = sample();
    weekend.days["2026-10-03"] = day_with_override(0, "");
    const dhw::Slot weekend_slot = dhw::effective_schedule(weekend, "2026-10-03")[0];
    DHW_CHECK_EQ(weekend_slot.name, std::string(""));
    DHW_CHECK(!weekend_slot.changed);
}

DHW_TEST(suggestions_dedup) {
    const dhw::AppData data = sample();
    const std::vector<std::string> suggestions =
        dhw::homework_suggestions(dhw::effective_schedule(data, "2026-10-01"), true);
    const std::vector<std::string> expected = {"政治", "生物", "数学",   "语文",
                                               "英语A", "英语B", "物理", "体育"};
    DHW_CHECK_EQ(suggestions, expected);
}

DHW_TEST(suggestions_english_split_once) {
    const dhw::AppData data = sample();
    const std::vector<std::string> suggestions =
        dhw::homework_suggestions(dhw::effective_schedule(data, "2026-09-30"), true);
    DHW_CHECK_EQ(count_starting_with(suggestions, "英语"), 2);
    DHW_CHECK(contains(suggestions, "英语A"));
    DHW_CHECK(contains(suggestions, "英语B"));
}

DHW_TEST(english_not_split_when_disabled) {
    dhw::AppData data = sample();
    data.settings.english_split = false;
    const std::vector<std::string> suggestions =
        dhw::homework_suggestions(dhw::effective_schedule(data, "2026-09-30"), false);
    DHW_CHECK(contains(suggestions, "英语"));
    DHW_CHECK(!contains(suggestions, "英语A"));
    DHW_CHECK(!contains(suggestions, "英语B"));

    const std::vector<std::string> speech = dhw::speech_suggestions(data, "2026-09-30");
    DHW_CHECK(contains(speech, "英语"));
    DHW_CHECK_EQ(count_starting_with(speech, "英语"), 1);
}

DHW_TEST(speech_uses_tomorrow) {
    const dhw::AppData data = sample();
    const std::vector<std::string> today =
        dhw::homework_suggestions(dhw::effective_schedule(data, "2026-10-01"), true);
    const std::vector<std::string> speech = dhw::speech_suggestions(data, "2026-10-01");
    const std::vector<std::string> friday =
        dhw::homework_suggestions(dhw::effective_schedule(data, "2026-10-02"), true);
    DHW_CHECK_EQ(speech, friday);
    DHW_CHECK(speech != today);
}

DHW_TEST(prev_school_day_skips_weekend) {
    const dhw::AppData data = sample();
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-10-05"), std::string("2026-10-02"));
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-10-02"), std::string("2026-10-01"));
}

DHW_TEST(next_school_day_skips_weekend) {
    const dhw::AppData data = sample();
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-02"), std::string("2026-10-05"));
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-01"), std::string("2026-10-02"));
}

DHW_TEST(next_school_day_respects_makeup) {
    dhw::AppData data = sample();
    data.days["2026-10-04"] = dhw::Day();
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-02"), std::string("2026-10-04"));
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-04"), std::string("2026-10-05"));
}

DHW_TEST(prev_school_day_uses_makeup) {
    dhw::AppData data = sample();
    data.days["2026-10-04"] = dhw::Day();
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-10-05"), std::string("2026-10-04"));
}

DHW_TEST(step_school_day_stops_at_limit) {
    dhw::AppData data = sample();
    for (int i = 0; i <= 30; ++i) {
        data.hidden_dates.push_back(dhw::add_days_text("2026-10-01", i));
    }
    DHW_CHECK_EQ(dhw::kSchoolDaySearchLimit, 14);
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-01"), std::string("2026-10-15"));
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-10-31"), std::string("2026-10-17"));
}

DHW_TEST(step_school_day_unparseable_from_uses_system_today) {
    const dhw::AppData data = sample();
    const std::string today = dhw::to_string(dhw::today_local());
    DHW_CHECK_EQ(dhw::next_school_day(data, "不是日期"), dhw::next_school_day(data, today));
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-13-40"), dhw::prev_school_day(data, today));
}

DHW_TEST(launch_before_cutoff) {
    const dhw::AppData data = sample();
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-02", at(7, 30));
    DHW_CHECK(view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-10-01"));
}

DHW_TEST(launch_after_cutoff) {
    const dhw::AppData data = sample();
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-02", at(8, 0));
    DHW_CHECK(!view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-10-02"));
}

DHW_TEST(launch_cutoff_parsing_and_fallback) {
    dhw::AppData data = sample();
    data.settings.morning_cutoff = "7:30";
    DHW_CHECK_EQ(dhw::launch_view(data, "2026-10-02", at(7, 29)), (dhw::LaunchView{"2026-10-01", true}));
    DHW_CHECK_EQ(dhw::launch_view(data, "2026-10-02", at(7, 30)), (dhw::LaunchView{"2026-10-02", false}));

    data.settings.morning_cutoff = "八点";
    DHW_CHECK_EQ(dhw::launch_view(data, "2026-10-02", at(7, 59)), (dhw::LaunchView{"2026-10-01", true}));
    DHW_CHECK_EQ(dhw::launch_view(data, "2026-10-02", at(8, 0)), (dhw::LaunchView{"2026-10-02", false}));
}

DHW_TEST(launch_on_rest_day_uses_prev_school_day) {
    const dhw::AppData data = sample();
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-03", at(9, 0));
    DHW_CHECK(!view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-10-02"));
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-03"), std::string("2026-10-02"));
}

DHW_TEST(launch_on_makeup_weekend_is_school_day) {
    dhw::AppData data = sample();
    data.days["2026-10-04"] = dhw::Day();
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-04", at(9, 0));
    DHW_CHECK(!view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-10-04"));
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-04"), std::string("2026-10-04"));
}

DHW_TEST(removed_today_falls_back_to_prev_school_day) {
    dhw::AppData data = sample();
    data.hidden_dates.push_back("2026-10-01");
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-01"), std::string("2026-09-30"));
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-01", at(9, 0));
    DHW_CHECK(!view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-09-30"));
    data.days["2026-10-01"] = dhw::Day();
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-01"), std::string("2026-10-01"));
}

DHW_TEST(before_cutoff_skips_removed_prev_day) {
    dhw::AppData data = sample();
    data.hidden_dates.push_back("2026-10-01");
    const dhw::LaunchView view = dhw::launch_view(data, "2026-10-02", at(7, 30));
    DHW_CHECK(view.submit_only);
    DHW_CHECK_EQ(view.date, std::string("2026-09-30"));
}

DHW_TEST(removed_dates_count_as_holidays) {
    dhw::AppData data = sample();
    for (const char* date : {"2026-10-02", "2026-10-05", "2026-10-06", "2026-10-07"}) {
        data.hidden_dates.push_back(date);
    }
    DHW_CHECK(!dhw::is_school_day(data, "2026-10-02"));
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-02"), std::string("2026-10-01"));
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-02"), std::string("2026-10-08"));
    data.days["2026-10-06"] = dhw::Day();
    DHW_CHECK(dhw::is_school_day(data, "2026-10-06"));
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-02"), std::string("2026-10-06"));
}

DHW_TEST(pinned_dates_do_not_affect_rules) {
    dhw::AppData data = sample();
    data.pinned_dates.push_back("2026-10-03");
    data.pinned_dates.push_back("2026-10-07");
    DHW_CHECK(!dhw::is_school_day(data, "2026-10-03"));
    DHW_CHECK(dhw::is_school_day(data, "2026-10-07"));
    DHW_CHECK_EQ(dhw::effective_today(data, "2026-10-03"), std::string("2026-10-02"));
    DHW_CHECK_EQ(dhw::next_school_day(data, "2026-10-01"), std::string("2026-10-02"));
    DHW_CHECK_EQ(dhw::prev_school_day(data, "2026-10-08"), std::string("2026-10-07"));
}

DHW_TEST(is_school_day_unparseable_date_with_entry) {
    dhw::AppData data = sample();
    DHW_CHECK(!dhw::is_school_day(data, "不是日期"));
    DHW_CHECK(!dhw::is_school_day(data, ""));
    DHW_CHECK(!dhw::is_school_day(data, "2026-10-03"));
    data.days["不是日期"] = dhw::Day();
    DHW_CHECK(dhw::is_school_day(data, "不是日期"));
}

DHW_TEST(announcement_visibility_is_global) {
    const dhw::Announcement open{1, "x", false};
    DHW_CHECK(dhw::announcement_visible(open));
    DHW_CHECK(!dhw::announcement_visible(dhw::Announcement{1, "x", true}));
}

DHW_TEST(speeches_empty_check) {
    dhw::Day day;
    DHW_CHECK(dhw::speeches_empty(&day));
    DHW_CHECK(dhw::speeches_empty(nullptr));
    day.speeches.push_back(dhw::Speech{"语文", ""});
    DHW_CHECK(dhw::speeches_empty(&day));
    day.speeches.push_back(dhw::Speech{"数学", "   "});
    DHW_CHECK(dhw::speeches_empty(&day));
    day.speeches.push_back(dhw::Speech{"英语", "24"});
    DHW_CHECK(!dhw::speeches_empty(&day));
}

DHW_TEST(present_homework_filters) {
    dhw::AppData data = sample();
    dhw::Day day;
    day.homework.push_back(dhw::Homework{"语文", "背书", true});
    day.homework.push_back(dhw::Homework{"数学", "练习册", false});
    day.homework.push_back(dhw::Homework{"物理", "  ", true});
    data.days["2026-10-01"] = day;

    const std::vector<dhw::Homework> all = dhw::present_homework(data, "2026-10-01", false);
    DHW_CHECK_EQ(all.size(), std::size_t{2});
    DHW_CHECK_EQ(all[0].subject, std::string("语文"));
    DHW_CHECK_EQ(all[1].subject, std::string("数学"));

    const std::vector<dhw::Homework> submit = dhw::present_homework(data, "2026-10-01", true);
    DHW_CHECK_EQ(submit.size(), std::size_t{1});
    DHW_CHECK_EQ(submit[0].subject, std::string("语文"));

    DHW_CHECK(dhw::present_homework(data, "2026-10-09", false).empty());
}

DHW_TEST(present_homework_keeps_order_and_duplicates) {
    dhw::AppData data = sample();
    dhw::Day day;
    day.homework.push_back(dhw::Homework{"语文", "a", true});
    day.homework.push_back(dhw::Homework{"语文", "b", false});
    day.homework.push_back(dhw::Homework{"语文", "c", true});
    data.days["2026-10-01"] = day;

    const std::vector<dhw::Homework> all = dhw::present_homework(data, "2026-10-01", false);
    DHW_CHECK_EQ(all.size(), std::size_t{3});
    DHW_CHECK_EQ(all[0].text, std::string("a"));
    DHW_CHECK_EQ(all[1].text, std::string("b"));
    DHW_CHECK_EQ(all[2].text, std::string("c"));

    const std::vector<dhw::Homework> submit = dhw::present_homework(data, "2026-10-01", true);
    DHW_CHECK_EQ(submit.size(), std::size_t{2});
    DHW_CHECK_EQ(submit[0].text, std::string("a"));
    DHW_CHECK_EQ(submit[1].text, std::string("c"));
}

DHW_TEST(day_has_content_rules) {
    DHW_CHECK(!dhw::day_has_content(nullptr));
    dhw::Day empty;
    DHW_CHECK(!dhw::day_has_content(&empty));

    dhw::Day blank_homework;
    blank_homework.homework.push_back(dhw::Homework{"语文", "   ", false});
    DHW_CHECK(!dhw::day_has_content(&blank_homework));
    blank_homework.homework.push_back(dhw::Homework{"数学", "练习册", false});
    DHW_CHECK(dhw::day_has_content(&blank_homework));

    dhw::Day speeches;
    speeches.speeches.push_back(dhw::Speech{"语文", "1号"});
    DHW_CHECK(dhw::day_has_content(&speeches));

    dhw::Day blank_speech;
    blank_speech.speeches.push_back(dhw::Speech{"语文", "  "});
    DHW_CHECK(!dhw::day_has_content(&blank_speech));
}

DHW_TEST(build_view_model_submit_only_keeps_page_date) {
    dhw::AppData data = sample();
    dhw::Day friday;
    friday.homework.push_back(dhw::Homework{"语文", "背书", true});
    friday.homework.push_back(dhw::Homework{"数学", "练习册", false});
    friday.speeches.push_back(dhw::Speech{"英语", "1号"});
    data.days["2026-10-02"] = friday;
    data.announcements.push_back(dhw::Announcement{1, "留着", false});
    data.announcements.push_back(dhw::Announcement{2, "已完成", true});

    const dhw::ViewModel full = dhw::build_view_model(data, "2026-10-02", false);
    DHW_CHECK_EQ(full.date, std::string("2026-10-02"));
    DHW_CHECK_EQ(full.display_date, std::string("2026-10-02"));
    DHW_CHECK_EQ(full.weekday, std::string("星期五"));
    DHW_CHECK_EQ(full.periods[0].name, std::string("语文"));
    DHW_CHECK_EQ(full.announcements.size(), std::size_t{1});
    DHW_CHECK_EQ(full.announcements[0].id, 1LL);
    DHW_CHECK_EQ(full.homework.size(), std::size_t{2});
    DHW_CHECK_EQ(full.homework_date, std::string("2026-10-02"));
    DHW_CHECK_EQ(full.speeches.size(), std::size_t{1});
    DHW_CHECK(!full.speech_warn);
    DHW_CHECK(!full.submit_only);
    DHW_CHECK_EQ(full.homework_suggestions,
                 dhw::homework_suggestions(dhw::effective_schedule(data, "2026-10-02"), true));
    DHW_CHECK_EQ(full.speech_suggestions,
                 dhw::homework_suggestions(dhw::effective_schedule(data, "2026-10-05"), true));

    const dhw::ViewModel only = dhw::build_view_model(data, "2026-10-02", true);
    DHW_CHECK_EQ(only.date, std::string("2026-10-02"));
    DHW_CHECK_EQ(only.display_date, std::string("2026-10-05"));
    DHW_CHECK_EQ(only.weekday, std::string("星期一"));
    DHW_CHECK_EQ(only.periods[0].name, std::string("语文"));
    DHW_CHECK_EQ(only.homework.size(), std::size_t{1});
    DHW_CHECK_EQ(only.homework[0].subject, std::string("语文"));
    DHW_CHECK_EQ(only.homework_date, std::string("2026-10-02"));
    DHW_CHECK(only.submit_only);
}

DHW_TEST(build_view_model_missing_day_warns) {
    const dhw::AppData data = sample();
    const dhw::ViewModel model = dhw::build_view_model(data, "2026-10-09", false);
    DHW_CHECK(model.speeches.empty());
    DHW_CHECK(model.speech_warn);
    DHW_CHECK(model.homework.empty());
    DHW_CHECK(model.announcements.empty());
    DHW_CHECK_EQ(model.weekday, std::string("星期五"));
}

DHW_TEST(build_week_rows_follow_monday) {
    const dhw::AppData data = sample();
    const std::vector<std::vector<dhw::Slot>> week = dhw::build_week(data, "2026-09-28");
    DHW_CHECK_EQ(week.size(), std::size_t{7});
    for (int i = 0; i < dhw::kWeekdays; ++i) {
        DHW_CHECK_EQ(week[static_cast<std::size_t>(i)],
                     dhw::effective_schedule(data, dhw::add_days_text("2026-09-28", i)));
    }
    DHW_CHECK_EQ(week[0][0].name, std::string("语文"));
    DHW_CHECK_EQ(week[3][0].name, std::string("政治"));
    DHW_CHECK_EQ(dhw::add_days_text("2026-09-28", 3), std::string("2026-10-01"));

    const std::vector<std::vector<dhw::Slot>> bad = dhw::build_week(data, "坏日期");
    DHW_CHECK_EQ(bad.size(), std::size_t{7});
    for (const std::vector<dhw::Slot>& row : bad) {
        DHW_CHECK_EQ(row, dhw::effective_schedule(data, "坏日期"));
    }
}

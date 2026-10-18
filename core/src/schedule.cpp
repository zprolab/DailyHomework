#include "dhw/schedule.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace dhw {
namespace {

constexpr const char* kWhitespace = " \t\r\n";

bool is_blank(const std::string& text) {
    return text.find_first_not_of(kWhitespace) == std::string::npos;
}

bool parse_unsigned(const std::string& text, int& out) {
    if (text.empty() || text.size() > 2) {
        return false;
    }
    int value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

// "HH:MM" plus the single-digit hour form ("7:30") that chrono accepts.
std::optional<Time> cutoff_time(const std::string& text) {
    if (const std::optional<Time> parsed = parse_time(text)) {
        return parsed;
    }
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || text.find(':', colon + 1) != std::string::npos) {
        return std::nullopt;
    }
    int hour = 0;
    int minute = 0;
    if (!parse_unsigned(text.substr(0, colon), hour) ||
        !parse_unsigned(text.substr(colon + 1), minute)) {
        return std::nullopt;
    }
    const Time time{hour, minute};
    if (!is_valid(time)) {
        return std::nullopt;
    }
    return time;
}

bool contains(const std::vector<std::string>& items, const std::string& value) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

}  // namespace

std::optional<int> weekday_index(const std::string& date) {
    const std::optional<Date> parsed = parse_date(date);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return weekday(*parsed);
}

std::string weekday_cn(const std::string& date) {
    static const char* const kNames[] = {"星期一", "星期二", "星期三", "星期四",
                                         "星期五", "星期六", "星期日"};
    const std::optional<int> index = weekday_index(date);
    if (!index.has_value()) {
        return {};
    }
    return kNames[*index];
}

std::string add_days_text(const std::string& date, int days) {
    const std::optional<Date> parsed = parse_date(date);
    if (!parsed.has_value()) {
        return date;
    }
    return to_string(add_days(*parsed, days));
}

std::vector<Slot> effective_schedule(const AppData& data, const std::string& date) {
    const Day* day = data.find_day(date);
    int base_idx = 0;
    if (day != nullptr && day->copied_from_weekday.has_value()) {
        base_idx = *day->copied_from_weekday;
    } else if (const std::optional<int> index = weekday_index(date); index.has_value()) {
        base_idx = *index;
    }

    const std::vector<std::string> no_template;
    const bool in_range =
        base_idx >= 0 && static_cast<std::size_t>(base_idx) < data.weekly_schedule.size();
    const std::vector<std::string>& base =
        in_range ? data.weekly_schedule[static_cast<std::size_t>(base_idx)] : no_template;

    std::vector<Slot> slots;
    slots.reserve(static_cast<std::size_t>(kPeriods));
    for (int i = 0; i < kPeriods; ++i) {
        std::string base_name;
        if (static_cast<std::size_t>(i) < base.size()) {
            base_name = base[static_cast<std::size_t>(i)];
        }
        std::optional<std::string> override_name;
        if (day != nullptr && static_cast<std::size_t>(i) < day->schedule_override.size()) {
            override_name = day->schedule_override[static_cast<std::size_t>(i)];
        }
        if (override_name.has_value() && *override_name != base_name) {
            slots.push_back(Slot{*override_name, true});
        } else {
            slots.push_back(Slot{base_name, false});
        }
    }
    return slots;
}

std::vector<std::string> homework_suggestions(const std::vector<Slot>& slots, bool english_split) {
    std::vector<std::string> out;
    for (const Slot& slot : slots) {
        if (slot.name.empty()) {
            continue;
        }
        if (slot.name == "英语" && english_split) {
            for (const char* name : {"英语A", "英语B"}) {
                if (!contains(out, name)) {
                    out.emplace_back(name);
                }
            }
        } else if (!contains(out, slot.name)) {
            out.push_back(slot.name);
        }
    }
    return out;
}

std::vector<std::string> speech_suggestions(const AppData& data, const std::string& date) {
    const std::string next = next_school_day(data, date);
    return homework_suggestions(effective_schedule(data, next), data.settings.english_split);
}

bool is_school_day(const AppData& data, const std::string& date) {
    const bool listed = data.days.find(date) != data.days.end();
    if (!listed && std::find(data.hidden_dates.begin(), data.hidden_dates.end(), date) !=
                       data.hidden_dates.end()) {
        return false;
    }
    if (const std::optional<int> index = weekday_index(date); index.has_value() && *index < 5) {
        return true;
    }
    return listed;
}

std::string step_school_day(const AppData& data, const std::string& from, int step) {
    Date cursor = parse_date(from).value_or(today_local());
    std::string text = to_string(cursor);
    for (int i = 0; i < kSchoolDaySearchLimit; ++i) {
        cursor = add_days(cursor, step);
        text = to_string(cursor);
        if (is_school_day(data, text)) {
            return text;
        }
    }
    return text;
}

std::string prev_school_day(const AppData& data, const std::string& from) {
    return step_school_day(data, from, -1);
}

std::string next_school_day(const AppData& data, const std::string& from) {
    return step_school_day(data, from, 1);
}

std::string effective_today(const AppData& data, const std::string& real_today) {
    if (is_school_day(data, real_today)) {
        return real_today;
    }
    return prev_school_day(data, real_today);
}

LaunchView launch_view(const AppData& data, const std::string& real_today, const Time& now) {
    const std::string today = effective_today(data, real_today);
    const Time cutoff = cutoff_time(data.settings.morning_cutoff).value_or(Time{8, 0});
    if (today == real_today && now < cutoff) {
        return LaunchView{prev_school_day(data, real_today), true};
    }
    return LaunchView{today, false};
}

bool announcement_visible(const Announcement& item) {
    return !item.done;
}

bool speeches_empty(const Day* day) {
    if (day == nullptr) {
        return true;
    }
    return std::all_of(day->speeches.begin(), day->speeches.end(),
                       [](const Speech& item) { return is_blank(item.students); });
}

std::vector<Homework> present_homework(const AppData& data, const std::string& date,
                                       bool submit_only) {
    std::vector<Homework> out;
    const Day* day = data.find_day(date);
    if (day == nullptr) {
        return out;
    }
    for (const Homework& item : day->homework) {
        if (is_blank(item.text)) {
            continue;
        }
        if (submit_only && !item.submit_next) {
            continue;
        }
        out.push_back(item);
    }
    return out;
}

bool day_has_content(const Day* day) {
    if (day == nullptr) {
        return false;
    }
    for (const Homework& item : day->homework) {
        if (!is_blank(item.text)) {
            return true;
        }
    }
    for (const Speech& item : day->speeches) {
        if (!is_blank(item.students)) {
            return true;
        }
    }
    return false;
}

ViewModel build_view_model(const AppData& data, const std::string& date, bool submit_only) {
    ViewModel model;
    model.date = date;
    model.display_date = submit_only ? next_school_day(data, date) : date;
    model.weekday = weekday_cn(model.display_date);
    model.periods = effective_schedule(data, model.display_date);
    for (const Announcement& item : data.announcements) {
        if (announcement_visible(item)) {
            model.announcements.push_back(item);
        }
    }
    model.homework = present_homework(data, date, submit_only);
    model.homework_date = date;
    const Day* day = data.find_day(date);
    if (day != nullptr) {
        model.speeches = day->speeches;
    }
    model.speech_warn = day == nullptr || speeches_empty(day);
    model.homework_suggestions =
        homework_suggestions(effective_schedule(data, date), data.settings.english_split);
    model.speech_suggestions = speech_suggestions(data, date);
    model.submit_only = submit_only;
    return model;
}

std::vector<std::vector<Slot>> build_week(const AppData& data, const std::string& monday) {
    std::vector<std::vector<Slot>> rows;
    rows.reserve(static_cast<std::size_t>(kWeekdays));
    for (int i = 0; i < kWeekdays; ++i) {
        rows.push_back(effective_schedule(data, add_days_text(monday, i)));
    }
    return rows;
}

}  // namespace dhw

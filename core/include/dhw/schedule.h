#pragma once

#include <optional>
#include <string>
#include <vector>

#include "dhw/datetime.h"
#include "dhw/model.h"

namespace dhw {

// A period of the effective schedule of one date. `changed` marks a swapped
// (调课) period: an override that differs from the weekly template.
struct Slot {
    std::string name;
    bool changed = false;

    bool operator==(const Slot&) const = default;
};

inline constexpr int kSchoolDaySearchLimit = 14;

// 0 = Monday .. 6 = Sunday, nullopt when the text is not a parseable date.
std::optional<int> weekday_index(const std::string& date);
// "星期一" .. "星期日"; empty string for an unparseable date.
std::string weekday_cn(const std::string& date);
// Shifts a date text; an unparseable input is returned unchanged.
std::string add_days_text(const std::string& date, int days);

// Always returns exactly kPeriods entries.
std::vector<Slot> effective_schedule(const AppData& data, const std::string& date);

// First-appearance dedup of the period names; "英语" expands to 英语A/英语B when
// english_split is on.
std::vector<std::string> homework_suggestions(const std::vector<Slot>& slots, bool english_split);
std::vector<std::string> speech_suggestions(const AppData& data, const std::string& date);

bool is_school_day(const AppData& data, const std::string& date);
// Walks up to kSchoolDaySearchLimit days in `step` direction and returns the first
// school day, or the last visited date when nothing matched.
std::string step_school_day(const AppData& data, const std::string& from, int step);
std::string prev_school_day(const AppData& data, const std::string& from);
std::string next_school_day(const AppData& data, const std::string& from);
std::string effective_today(const AppData& data, const std::string& real_today);

struct LaunchView {
    std::string date;
    bool submit_only = false;

    bool operator==(const LaunchView&) const = default;
};

// Before the morning cutoff the page shows today but edits the previous school day
// in "submit only" mode.
LaunchView launch_view(const AppData& data, const std::string& real_today, const Time& now);

bool announcement_visible(const Announcement& item);
// True when the day is missing or every speech has a blank student list.
bool speeches_empty(const Day* day);
// Homework of one date, keeping order and duplicates; submit_only keeps only
// entries marked "要交". Blank texts are dropped.
std::vector<Homework> present_homework(const AppData& data, const std::string& date, bool submit_only);
// Homework or speeches with real content; drives the date bar.
bool day_has_content(const Day* day);

// Everything the renderer and the UI need for one (date, mode) pair.
struct ViewModel {
    std::string date;          // page day: the day the homework belongs to
    std::string display_date;  // the date shown on screen
    std::string weekday;       // weekday text of display_date
    std::vector<Slot> periods;               // schedule of display_date
    std::vector<Announcement> announcements; // visible (not done) announcements
    std::vector<Homework> homework;          // homework of `date`
    std::string homework_date;
    std::vector<Speech> speeches;            // speeches of `date`
    bool speech_warn = false;
    std::vector<std::string> homework_suggestions;  // subjects of `date`
    std::vector<std::string> speech_suggestions;    // subjects of the next school day
    bool submit_only = false;
};

ViewModel build_view_model(const AppData& data, const std::string& date, bool submit_only);

// Seven rows (Monday first) of the effective schedule for the week of `monday`.
std::vector<std::vector<Slot>> build_week(const AppData& data, const std::string& monday);

}  // namespace dhw

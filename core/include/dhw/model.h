#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dhw/datetime.h"
#include "dhw/json.h"

namespace dhw {

inline constexpr int kPeriods = 9;
inline constexpr int kWeekdays = 7;

struct Homework {
    std::string subject;
    std::string text;
    bool submit_next = false;

    bool operator==(const Homework&) const = default;
};

struct Speech {
    std::string subject;
    std::string students;

    bool operator==(const Speech&) const = default;
};

struct Day {
    // One entry per period; nullopt means "use the weekly template".
    std::vector<std::optional<std::string>> schedule_override;
    std::vector<Homework> homework;
    std::vector<Speech> speeches;
    // Source weekday of a swapped day (0 = Monday), nullopt when not swapped.
    std::optional<int> copied_from_weekday;

    Day();

    bool operator==(const Day&) const = default;
};

struct Announcement {
    long long id = 0;
    std::string text;
    bool done = false;

    bool operator==(const Announcement&) const = default;
};

struct Settings {
    int image_interval_secs = 60;
    std::string morning_cutoff = "08:00";
    bool show_badge = true;
    bool english_split = true;
    double ui_scale = 1.0;

    bool operator==(const Settings&) const = default;
};

struct AppData {
    int version = 1;
    std::vector<std::vector<std::string>> weekly_schedule;
    std::map<std::string, Day> days;
    std::vector<std::string> hidden_dates;
    std::vector<std::string> pinned_dates;
    std::vector<Announcement> announcements;
    Settings settings;

    bool operator==(const AppData&) const = default;

    static AppData defaults();

    const Day* find_day(const std::string& date) const;
    Day& day(const std::string& date);

    long long next_announcement_id() const;

    // Drops empty entries, clamps the weekly template and the day overrides and
    // prunes the date bookkeeping lists. `today` decides which pinned dates are stale.
    void normalize(const Date& today);
};

std::vector<std::vector<std::string>> default_weekly_schedule();
std::vector<std::vector<std::string>> normalize_weekly(std::vector<std::vector<std::string>> rows);

Json to_json(const AppData& data);

struct DataParse {
    bool ok = false;
    AppData data;
    std::string error;
};

// Lenient on unknown or legacy fields, strict on structural corruption so that a
// damaged data.json is reported instead of silently loading as empty.
DataParse parse_app_data(const Json& root);

}  // namespace dhw

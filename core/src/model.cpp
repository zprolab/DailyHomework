#include "dhw/model.h"

#include <algorithm>
#include <utility>

namespace dhw {
namespace {

std::string trimmed(const std::string& text) {
    const char* spaces = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(spaces);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(spaces);
    return text.substr(begin, end - begin + 1);
}

Json homework_to_json(const Homework& item) {
    Json json = Json::object();
    json.set("subject", Json(item.subject));
    json.set("text", Json(item.text));
    json.set("submit_next", Json(item.submit_next));
    return json;
}

Json speech_to_json(const Speech& item) {
    Json json = Json::object();
    json.set("subject", Json(item.subject));
    json.set("students", Json(item.students));
    return json;
}

Json day_to_json(const Day& day) {
    Json json = Json::object();

    Json override_list = Json::array();
    for (const std::optional<std::string>& slot : day.schedule_override) {
        override_list.push_back(slot.has_value() ? Json(*slot) : Json());
    }
    json.set("schedule_override", std::move(override_list));

    Json homework = Json::array();
    for (const Homework& item : day.homework) {
        homework.push_back(homework_to_json(item));
    }
    json.set("homework", std::move(homework));

    Json speeches = Json::array();
    for (const Speech& item : day.speeches) {
        speeches.push_back(speech_to_json(item));
    }
    json.set("speeches", std::move(speeches));

    json.set("copied_from_weekday",
             day.copied_from_weekday.has_value() ? Json(*day.copied_from_weekday) : Json());
    return json;
}

Json settings_to_json(const Settings& settings) {
    Json json = Json::object();
    json.set("image_interval_secs", Json(settings.image_interval_secs));
    json.set("morning_cutoff", Json(settings.morning_cutoff));
    json.set("show_badge", Json(settings.show_badge));
    json.set("english_split", Json(settings.english_split));
    json.set("ui_scale", Json(settings.ui_scale));
    return json;
}

// Legacy data files stored the students of a speech as a string; an array is
// tolerated and joined so that nothing is lost on load.
std::string read_students(const Json& value) {
    if (value.is_string()) {
        return value.as_string();
    }
    if (value.is_array()) {
        std::string joined;
        for (const Json& item : value.elements()) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += item.is_string() ? item.as_string() : item.dump(-1);
        }
        return joined;
    }
    return {};
}

bool read_override(const Json& value, std::vector<std::optional<std::string>>& out, std::string& error) {
    if (!value.is_array()) {
        error = "schedule_override must be an array";
        return false;
    }
    out.clear();
    out.reserve(static_cast<std::size_t>(kPeriods));
    for (const Json& slot : value.elements()) {
        if (slot.is_null()) {
            out.push_back(std::nullopt);
        } else if (slot.is_string()) {
            out.push_back(slot.as_string());
        } else {
            error = "schedule_override entries must be strings or null";
            return false;
        }
    }
    out.resize(static_cast<std::size_t>(kPeriods), std::nullopt);
    return true;
}

bool read_day(const Json& value, Day& out, std::string& error) {
    if (!value.is_object()) {
        error = "day entries must be objects";
        return false;
    }
    out = Day();
    if (const Json* override_value = value.find("schedule_override"); override_value != nullptr) {
        if (!read_override(*override_value, out.schedule_override, error)) {
            return false;
        }
    }
    if (const Json* homework = value.find("homework"); homework != nullptr) {
        if (!homework->is_array()) {
            error = "homework must be an array";
            return false;
        }
        for (const Json& item : homework->elements()) {
            Homework entry;
            if (const Json* subject = item.find("subject"); subject != nullptr) {
                entry.subject = subject->string_or("");
            }
            if (const Json* text = item.find("text"); text != nullptr) {
                entry.text = text->string_or("");
            }
            if (const Json* submit = item.find("submit_next"); submit != nullptr) {
                entry.submit_next = submit->is_bool() ? submit->as_bool() : submit->as_int() != 0;
            }
            out.homework.push_back(std::move(entry));
        }
    }
    if (const Json* speeches = value.find("speeches"); speeches != nullptr) {
        if (!speeches->is_array()) {
            error = "speeches must be an array";
            return false;
        }
        for (const Json& item : speeches->elements()) {
            Speech entry;
            if (const Json* subject = item.find("subject"); subject != nullptr) {
                entry.subject = subject->string_or("");
            }
            if (const Json* students = item.find("students"); students != nullptr) {
                entry.students = read_students(*students);
            }
            out.speeches.push_back(std::move(entry));
        }
    }
    if (const Json* copied = value.find("copied_from_weekday"); copied != nullptr && !copied->is_null()) {
        const int index = static_cast<int>(copied->as_int(-1));
        if (index >= 0 && index < kWeekdays) {
            out.copied_from_weekday = index;
        }
    }
    return true;
}

bool read_string_array(const Json& value, std::vector<std::string>& out, std::string& error,
                       const char* label) {
    if (!value.is_array()) {
        error = std::string(label) + " must be an array";
        return false;
    }
    out.clear();
    for (const Json& item : value.elements()) {
        if (!item.is_string()) {
            error = std::string(label) + " entries must be strings";
            return false;
        }
        out.push_back(item.as_string());
    }
    return true;
}

bool read_weekly(const Json& value, std::vector<std::vector<std::string>>& out, std::string& error) {
    if (!value.is_array()) {
        error = "weekly_schedule must be an array of rows";
        return false;
    }
    std::vector<std::vector<std::string>> rows;
    for (const Json& row : value.elements()) {
        if (!row.is_array()) {
            error = "weekly_schedule rows must be arrays";
            return false;
        }
        std::vector<std::string> periods;
        for (const Json& period : row.elements()) {
            periods.push_back(period.string_or(""));
        }
        rows.push_back(std::move(periods));
    }
    out = normalize_weekly(std::move(rows));
    return true;
}

}  // namespace

Day::Day() : schedule_override(static_cast<std::size_t>(kPeriods), std::nullopt) {}

AppData AppData::defaults() {
    AppData data;
    data.weekly_schedule = default_weekly_schedule();
    return data;
}

const Day* AppData::find_day(const std::string& date) const {
    const auto it = days.find(date);
    return it == days.end() ? nullptr : &it->second;
}

Day& AppData::day(const std::string& date) {
    return days[date];
}

long long AppData::next_announcement_id() const {
    long long highest = 0;
    for (const Announcement& item : announcements) {
        highest = std::max(highest, item.id);
    }
    return highest + 1;
}

void AppData::normalize(const Date& today) {
    weekly_schedule = normalize_weekly(std::move(weekly_schedule));
    for (auto& [date, day] : days) {
        (void)date;
        day.schedule_override.resize(static_cast<std::size_t>(kPeriods), std::nullopt);
        day.homework.erase(
            std::remove_if(day.homework.begin(), day.homework.end(),
                           [](const Homework& item) { return trimmed(item.text).empty(); }),
            day.homework.end());
        day.speeches.erase(
            std::remove_if(day.speeches.begin(), day.speeches.end(),
                           [](const Speech& item) { return trimmed(item.students).empty(); }),
            day.speeches.end());
    }
    // hidden_dates is domain data: school day detection and "today" fall back to it,
    // so past entries must survive.
    std::sort(hidden_dates.begin(), hidden_dates.end());
    hidden_dates.erase(std::unique(hidden_dates.begin(), hidden_dates.end()), hidden_dates.end());
    const std::string today_text = to_string(today);
    std::sort(pinned_dates.begin(), pinned_dates.end());
    pinned_dates.erase(std::unique(pinned_dates.begin(), pinned_dates.end()), pinned_dates.end());
    pinned_dates.erase(std::remove_if(pinned_dates.begin(), pinned_dates.end(),
                                      [&today_text](const std::string& date) {
                                          return date < today_text;
                                      }),
                       pinned_dates.end());
    pinned_dates.erase(
        std::remove_if(pinned_dates.begin(), pinned_dates.end(),
                       [this](const std::string& date) {
                           return std::find(hidden_dates.begin(), hidden_dates.end(), date) !=
                                  hidden_dates.end();
                       }),
        pinned_dates.end());
}

std::vector<std::vector<std::string>> default_weekly_schedule() {
    // A fresh installation starts with an empty timetable: no subject names are baked
    // into the binary. The grid keeps its 7 x 9 shape so the editor and the layout do
    // not need a second "no timetable" code path; only the content is empty.
    return normalize_weekly({});
}

std::vector<std::vector<std::string>> normalize_weekly(std::vector<std::vector<std::string>> rows) {
    rows.resize(static_cast<std::size_t>(kWeekdays));
    for (std::vector<std::string>& row : rows) {
        row.resize(static_cast<std::size_t>(kPeriods));
    }
    return rows;
}

Json to_json(const AppData& data) {
    Json root = Json::object();
    root.set("version", Json(data.version));

    Json weekly = Json::array();
    for (const std::vector<std::string>& row : data.weekly_schedule) {
        Json periods = Json::array();
        for (const std::string& period : row) {
            periods.push_back(Json(period));
        }
        weekly.push_back(std::move(periods));
    }
    root.set("weekly_schedule", std::move(weekly));

    Json days = Json::object();
    for (const auto& [date, day] : data.days) {
        days.set(date, day_to_json(day));
    }
    root.set("days", std::move(days));

    Json hidden = Json::array();
    for (const std::string& date : data.hidden_dates) {
        hidden.push_back(Json(date));
    }
    root.set("hidden_dates", std::move(hidden));

    Json pinned = Json::array();
    for (const std::string& date : data.pinned_dates) {
        pinned.push_back(Json(date));
    }
    root.set("pinned_dates", std::move(pinned));

    Json announcements = Json::array();
    for (const Announcement& item : data.announcements) {
        Json entry = Json::object();
        entry.set("id", Json(static_cast<long long>(item.id)));
        entry.set("text", Json(item.text));
        entry.set("done", Json(item.done));
        announcements.push_back(std::move(entry));
    }
    root.set("announcements", std::move(announcements));

    root.set("settings", settings_to_json(data.settings));
    return root;
}

DataParse parse_app_data(const Json& root) {
    DataParse result;
    if (!root.is_object()) {
        result.error = "the data file must contain a JSON object";
        return result;
    }

    AppData data;
    data.weekly_schedule = default_weekly_schedule();

    if (const Json* version = root.find("version"); version != nullptr && !version->is_null()) {
        data.version = static_cast<int>(version->as_int(1));
    }
    if (const Json* weekly = root.find("weekly_schedule"); weekly != nullptr && !weekly->is_null()) {
        if (!read_weekly(*weekly, data.weekly_schedule, result.error)) {
            return result;
        }
    }
    if (const Json* days = root.find("days"); days != nullptr && !days->is_null()) {
        if (!days->is_object()) {
            result.error = "days must be an object keyed by date";
            return result;
        }
        for (const Json::Member& member : days->members()) {
            Day day;
            if (!read_day(member.second, day, result.error)) {
                result.error = member.first + ": " + result.error;
                return result;
            }
            data.days.emplace(member.first, std::move(day));
        }
    }
    if (const Json* hidden = root.find("hidden_dates"); hidden != nullptr && !hidden->is_null()) {
        if (!read_string_array(*hidden, data.hidden_dates, result.error, "hidden_dates")) {
            return result;
        }
    }
    if (const Json* pinned = root.find("pinned_dates"); pinned != nullptr && !pinned->is_null()) {
        if (!read_string_array(*pinned, data.pinned_dates, result.error, "pinned_dates")) {
            return result;
        }
    }
    if (const Json* announcements = root.find("announcements"); announcements != nullptr &&
                                                                 !announcements->is_null()) {
        if (!announcements->is_array()) {
            result.error = "announcements must be an array";
            return result;
        }
        for (const Json& item : announcements->elements()) {
            Announcement entry;
            if (const Json* id = item.find("id"); id != nullptr && !id->is_null()) {
                entry.id = id->as_int(0);
            }
            if (const Json* text = item.find("text"); text != nullptr) {
                entry.text = text->string_or("");
            }
            if (const Json* done = item.find("done"); done != nullptr) {
                entry.done = done->as_bool(false);
            }
            data.announcements.push_back(std::move(entry));
        }
    }
    if (const Json* settings = root.find("settings"); settings != nullptr && !settings->is_null()) {
        if (!settings->is_object()) {
            result.error = "settings must be an object";
            return result;
        }
        if (const Json* value = settings->find("image_interval_secs");
            value != nullptr && !value->is_null()) {
            data.settings.image_interval_secs = static_cast<int>(value->as_int(60));
        }
        if (const Json* value = settings->find("morning_cutoff"); value != nullptr && !value->is_null()) {
            data.settings.morning_cutoff = value->string_or("08:00");
        }
        if (const Json* value = settings->find("show_badge"); value != nullptr && !value->is_null()) {
            data.settings.show_badge = value->as_bool(true);
        }
        if (const Json* value = settings->find("english_split"); value != nullptr && !value->is_null()) {
            data.settings.english_split = value->as_bool(true);
        }
        if (const Json* value = settings->find("ui_scale"); value != nullptr && !value->is_null()) {
            data.settings.ui_scale = value->as_number(1.0);
        }
    }

    result.ok = true;
    result.data = std::move(data);
    return result;
}

}  // namespace dhw

#include "dhw/datetime.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace dhw {
namespace {

// Howard Hinnant's civil calendar algorithms: no locale or time zone involved.
int days_from_civil(int year, unsigned int month, unsigned int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int year_of_era = static_cast<unsigned int>(year - era * 400);
    const unsigned int day_of_year =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned int day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<int>(day_of_era) - 719468;
}

void civil_from_days(int days, int& year, unsigned int& month, unsigned int& day) {
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned int day_of_era = static_cast<unsigned int>(days - era * 146097);
    const unsigned int year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int computed_year = static_cast<int>(year_of_era) + era * 400;
    const unsigned int day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned int mp = (5 * day_of_year + 2) / 153;
    day = day_of_year - (153 * mp + 2) / 5 + 1;
    month = mp + (mp < 10 ? 3 : -9);
    year = computed_year + (month <= 2 ? 1 : 0);
}

bool digits(std::string_view text, std::size_t offset, std::size_t count, int& out) {
    if (offset + count > text.size()) {
        return false;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char ch = text[offset + i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

}  // namespace

bool operator==(const Date& a, const Date& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

bool operator!=(const Date& a, const Date& b) {
    return !(a == b);
}

bool operator<(const Date& a, const Date& b) {
    if (a.year != b.year) {
        return a.year < b.year;
    }
    if (a.month != b.month) {
        return a.month < b.month;
    }
    return a.day < b.day;
}

bool operator>(const Date& a, const Date& b) {
    return b < a;
}

bool operator<=(const Date& a, const Date& b) {
    return !(b < a);
}

bool operator>=(const Date& a, const Date& b) {
    return !(a < b);
}

bool operator==(const Time& a, const Time& b) {
    return a.hour == b.hour && a.minute == b.minute;
}

bool operator!=(const Time& a, const Time& b) {
    return !(a == b);
}

bool operator<(const Time& a, const Time& b) {
    if (a.hour != b.hour) {
        return a.hour < b.hour;
    }
    return a.minute < b.minute;
}

bool is_valid(const Date& date) {
    if (date.month < 1 || date.month > 12) {
        return false;
    }
    if (date.day < 1) {
        return false;
    }
    const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int limit = month_days[date.month - 1];
    const bool leap = (date.year % 4 == 0 && date.year % 100 != 0) || date.year % 400 == 0;
    if (date.month == 2 && leap) {
        limit = 29;
    }
    return date.day <= limit;
}

bool is_valid(const Time& time) {
    return time.hour >= 0 && time.hour <= 23 && time.minute >= 0 && time.minute <= 59;
}

int to_days(const Date& date) {
    return days_from_civil(date.year, static_cast<unsigned int>(date.month),
                           static_cast<unsigned int>(date.day));
}

Date from_days(int days) {
    int year = 0;
    unsigned int month = 1;
    unsigned int day = 1;
    civil_from_days(days, year, month, day);
    return Date{year, static_cast<int>(month), static_cast<int>(day)};
}

Date add_days(const Date& date, int days) {
    return from_days(to_days(date) + days);
}

int weekday(const Date& date) {
    const int days = to_days(date);
    const int shifted = (days + 3) % 7;
    return shifted < 0 ? shifted + 7 : shifted;
}

bool is_weekend(const Date& date) {
    const int index = weekday(date);
    return index == 5 || index == 6;
}

std::string to_string(const Date& date) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", date.year, date.month, date.day);
    return buffer;
}

std::optional<Date> parse_date(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (!digits(text, 0, 4, year) || !digits(text, 5, 2, month) || !digits(text, 8, 2, day)) {
        return std::nullopt;
    }
    const Date date{year, month, day};
    if (!is_valid(date)) {
        return std::nullopt;
    }
    return date;
}

std::string to_string(const Time& time) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", time.hour, time.minute);
    return buffer;
}

std::optional<Time> parse_time(std::string_view text) {
    if (text.size() != 5 || text[2] != ':') {
        return std::nullopt;
    }
    int hour = 0;
    int minute = 0;
    if (!digits(text, 0, 2, hour) || !digits(text, 3, 2, minute)) {
        return std::nullopt;
    }
    const Time time{hour, minute};
    if (!is_valid(time)) {
        return std::nullopt;
    }
    return time;
}

Date today_local() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    return Date{parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday};
}

Time now_local() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    return Time{parts.tm_hour, parts.tm_min};
}

}  // namespace dhw

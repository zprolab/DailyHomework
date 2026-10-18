#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dhw {

struct Date {
    int year = 1970;
    int month = 1;
    int day = 1;
};

struct Time {
    int hour = 0;
    int minute = 0;
};

bool operator==(const Date& a, const Date& b);
bool operator!=(const Date& a, const Date& b);
bool operator<(const Date& a, const Date& b);
bool operator>(const Date& a, const Date& b);
bool operator<=(const Date& a, const Date& b);
bool operator>=(const Date& a, const Date& b);

bool operator==(const Time& a, const Time& b);
bool operator!=(const Time& a, const Time& b);
bool operator<(const Time& a, const Time& b);

bool is_valid(const Date& date);
bool is_valid(const Time& time);

// Days since 1970-01-01 (may be negative for earlier dates).
int to_days(const Date& date);
Date from_days(int days);
Date add_days(const Date& date, int days);

// 0 = Monday .. 6 = Sunday.
int weekday(const Date& date);
bool is_weekend(const Date& date);

std::string to_string(const Date& date);
std::optional<Date> parse_date(std::string_view text);

std::string to_string(const Time& time);
std::optional<Time> parse_time(std::string_view text);

Date today_local();
Time now_local();

}  // namespace dhw

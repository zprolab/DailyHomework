#include "dhw/datetime.h"
#include "dhw_test.h"

#include <string>

DHW_TEST(date_epoch_anchors) {
    DHW_CHECK_EQ(dhw::to_days(dhw::Date{1970, 1, 1}), 0);
    DHW_CHECK_EQ(dhw::to_days(dhw::Date{1970, 1, 2}), 1);
    DHW_CHECK_EQ(dhw::to_days(dhw::Date{1969, 12, 31}), -1);
    DHW_CHECK(dhw::from_days(0) == (dhw::Date{1970, 1, 1}));
    DHW_CHECK(dhw::from_days(-1) == (dhw::Date{1969, 12, 31}));
    DHW_CHECK(dhw::from_days(1) == (dhw::Date{1970, 1, 2}));
}

DHW_TEST(date_weekday_is_monday_first) {
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{1970, 1, 1}), 3);   // Thursday
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{2000, 1, 1}), 5);   // Saturday
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{2024, 1, 1}), 0);   // Monday
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{2026, 10, 12}), 0); // Monday
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{2026, 10, 17}), 5); // Saturday
    DHW_CHECK_EQ(dhw::weekday(dhw::Date{2026, 10, 18}), 6); // Sunday
    DHW_CHECK(dhw::is_weekend(dhw::Date{2026, 10, 17}));
    DHW_CHECK(dhw::is_weekend(dhw::Date{2026, 10, 18}));
    DHW_CHECK(!dhw::is_weekend(dhw::Date{2026, 10, 16}));
}

DHW_TEST(date_round_trips_and_steps) {
    for (int days = -20000; days <= 20000; days += 137) {
        const dhw::Date date = dhw::from_days(days);
        DHW_CHECK_EQ(dhw::to_days(date), days);
    }
    DHW_CHECK(dhw::add_days(dhw::Date{2026, 12, 31}, 1) == (dhw::Date{2027, 1, 1}));
    DHW_CHECK(dhw::add_days(dhw::Date{2026, 1, 1}, -1) == (dhw::Date{2025, 12, 31}));
    DHW_CHECK(dhw::add_days(dhw::Date{2024, 2, 28}, 1) == (dhw::Date{2024, 2, 29}));
    DHW_CHECK_EQ(dhw::weekday(dhw::add_days(dhw::Date{2026, 10, 12}, 7)), 0);
}

DHW_TEST(date_validity_and_leap_years) {
    DHW_CHECK(dhw::is_valid(dhw::Date{2024, 2, 29}));
    DHW_CHECK(dhw::is_valid(dhw::Date{2000, 2, 29}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{1900, 2, 29}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{2023, 2, 29}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{2026, 13, 1}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{2026, 0, 1}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{2026, 4, 31}));
    DHW_CHECK(!dhw::is_valid(dhw::Date{2026, 1, 0}));
    DHW_CHECK(dhw::is_valid(dhw::Date{2026, 4, 30}));
}

DHW_TEST(date_text_round_trip) {
    DHW_CHECK_EQ(dhw::to_string(dhw::Date{2026, 10, 1}), std::string("2026-10-01"));
    DHW_CHECK_EQ(dhw::to_string(dhw::Date{999, 1, 5}), std::string("0999-01-05"));
    DHW_CHECK(dhw::parse_date("2026-10-01").value() == (dhw::Date{2026, 10, 1}));
    DHW_CHECK(!dhw::parse_date("2026-2-01").has_value());
    DHW_CHECK(!dhw::parse_date("2026-13-01").has_value());
    DHW_CHECK(!dhw::parse_date("2026-02-30").has_value());
    DHW_CHECK(!dhw::parse_date("20261001").has_value());
    DHW_CHECK(!dhw::parse_date("").has_value());
    DHW_CHECK(!dhw::parse_date("2026-10-0a").has_value());
    const dhw::Date date{2026, 10, 12};
    DHW_CHECK(dhw::parse_date(dhw::to_string(date)).value() == date);
}

DHW_TEST(time_text_round_trip) {
    DHW_CHECK_EQ(dhw::to_string(dhw::Time{8, 5}), std::string("08:05"));
    DHW_CHECK_EQ(dhw::to_string(dhw::Time{0, 0}), std::string("00:00"));
    DHW_CHECK_EQ(dhw::to_string(dhw::Time{23, 59}), std::string("23:59"));
    DHW_CHECK(dhw::parse_time("08:00").value() == (dhw::Time{8, 0}));
    DHW_CHECK(dhw::parse_time("00:00").value() == (dhw::Time{0, 0}));
    DHW_CHECK(!dhw::parse_time("24:00").has_value());
    DHW_CHECK(!dhw::parse_time("12:60").has_value());
    DHW_CHECK(!dhw::parse_time("8:00").has_value());
    DHW_CHECK(!dhw::parse_time("08-00").has_value());
    const dhw::Time a{7, 30};
    const dhw::Time b{8, 0};
    const dhw::Time c{7, 31};
    DHW_CHECK(a < b);
    DHW_CHECK(a < c);
    DHW_CHECK(a != c);
}

DHW_TEST(date_comparisons) {
    const dhw::Date first{2026, 10, 1};
    const dhw::Date second{2026, 10, 2};
    const dhw::Date september{2026, 9, 30};
    const dhw::Date new_year{2026, 1, 1};
    const dhw::Date old_year{2025, 12, 31};
    DHW_CHECK(first < second);
    DHW_CHECK(september < first);
    DHW_CHECK(old_year < new_year);
    DHW_CHECK(first <= first);
    DHW_CHECK(second > first);
    DHW_CHECK(first >= first);
    DHW_CHECK(first != second);
    DHW_CHECK(first == first);
}

DHW_TEST(local_clock_is_plausible) {
    const dhw::Date today = dhw::today_local();
    DHW_CHECK(today.year >= 2024 && today.year <= 2100);
    DHW_CHECK(dhw::is_valid(today));
    DHW_CHECK_EQ(dhw::to_days(today), dhw::to_days(dhw::parse_date(dhw::to_string(today)).value()));
    const dhw::Time now = dhw::now_local();
    DHW_CHECK(dhw::is_valid(now));
}

#include "dhw/datetime.h"
#include "dhw/fs.h"
#include "dhw/json.h"
#include "dhw/model.h"
#include "dhw/store.h"
#include "dhw_test.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

long process_id() {
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

struct TempDir {
    explicit TempDir(const std::string& label) {
        static int counter = 0;
        path = dhw::fs::join(dhw::fs::temp_dir(), "dhw_store_" + label + "_" +
                                                       std::to_string(process_id()) + "_" +
                                                       std::to_string(++counter));
        dhw::fs::remove_all(path);
        dhw::fs::create_directories(path);
    }
    ~TempDir() { dhw::fs::remove_all(path); }
    std::string file(const std::string& name) const { return dhw::fs::join(path, name); }
    std::string path;
};

dhw::AppData sample() {
    dhw::AppData data = dhw::AppData::defaults();
    dhw::Day day;
    day.homework.push_back(dhw::Homework{"语文", "背书", true});
    data.days["2026-10-01"] = day;
    return data;
}

std::vector<std::uint8_t> png(std::initializer_list<int> values) {
    // Explicit narrowing keeps MSVC quiet (C4244 fires inside the STL otherwise).
    std::vector<std::uint8_t> bytes;
    bytes.reserve(values.size());
    for (const int value : values) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }
    return bytes;
}

std::vector<std::string> image_names(const std::string& dir) {
    std::vector<std::string> names;
    for (const std::string& name : dhw::fs::list_dir(dir)) {
        if (name.rfind("homework_", 0) == 0) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::size_t migrated_count(const std::string& dir) {
    std::size_t count = 0;
    for (const std::string& name : dhw::fs::list_dir(dir)) {
        if (name.rfind("data.json.migrated_", 0) == 0) {
            ++count;
        }
    }
    return count;
}

std::string compact_date(const dhw::Date& date) {
    std::string text = dhw::to_string(date);
    text.erase(std::remove(text.begin(), text.end(), '-'), text.end());
    return text;
}

std::string fixture_path(const std::string& name) {
    return std::string(DHW_FIXTURE_DIR) + "/" + name;
}

dhw::AppData decode_fixture() {
    const std::optional<std::string> text = dhw::fs::read_text(fixture_path("data.sample.json"));
    DHW_CHECK(text.has_value());
    const dhw::JsonParse parsed = dhw::Json::parse(*text);
    DHW_CHECK(parsed.ok);
    const dhw::DataParse decoded = dhw::parse_app_data(parsed.value);
    DHW_CHECK(decoded.ok);
    return decoded.data;
}

std::string canonical_text() {
    return dhw::to_json(decode_fixture()).dump(2);
}

}  // namespace

DHW_TEST(save_and_load_roundtrip) {
    TempDir dir("roundtrip");
    const dhw::AppData data = sample();
    DHW_CHECK(dhw::save(dir.path, data));
    const dhw::LoadResult loaded = dhw::load(dir.path);
    DHW_CHECK(loaded.ok);
    DHW_CHECK(loaded.data == data);
    DHW_CHECK(loaded.error.empty());
    DHW_CHECK(!loaded.recovered_from_backup);
    DHW_CHECK(loaded.backup_used.empty());
}

DHW_TEST(atomic_save_leaves_no_temp_file) {
    TempDir dir("atomic");
    dhw::AppData data = sample();
    DHW_CHECK(dhw::save(dir.path, data));
    data.announcements.push_back(dhw::Announcement{1, "明天带跳绳", false});
    DHW_CHECK(dhw::save(dir.path, data));
    DHW_CHECK(!dhw::fs::exists(dir.file("data.json.tmp")));
    DHW_CHECK_EQ(dhw::fs::read_text(dir.file("data.json")).value(), dhw::to_json(data).dump(2));
    DHW_CHECK_EQ(dhw::fs::list_dir(dir.path).size(), std::size_t{2});
}

DHW_TEST(backup_created_and_pruned_per_day) {
    TempDir dir("backup");
    dhw::AppData data = sample();
    DHW_CHECK(dhw::save(dir.path, data));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    data.announcements.push_back(dhw::Announcement{1, "v2", false});
    DHW_CHECK(dhw::save(dir.path, data));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    DHW_CHECK(dhw::save(dir.path, data));

    const std::vector<std::string> backups = dhw::list_backups(dir.path);
    DHW_CHECK_EQ(backups.size(), std::size_t{1});
    DHW_CHECK_EQ(dhw::fs::list_dir(dhw::backups_dir(dir.path)).size(), std::size_t{1});
    DHW_CHECK_EQ(dhw::fs::read_text(dhw::fs::join(dhw::backups_dir(dir.path), backups[0])).value(),
                 dhw::to_json(data).dump(2));
}

DHW_TEST(backup_pruning_keeps_thirty_days_and_newest_of_day) {
    TempDir dir("prune");
    DHW_CHECK(dhw::save(dir.path, sample()));
    const std::string folder = dhw::backups_dir(dir.path);
    for (int i = 0; i < 40; ++i) {
        const std::string day = compact_date(dhw::add_days(dhw::Date{2020, 1, 1}, i));
        DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_" + day + "_120000.json"), "{}"));
        if (i == 20) {
            DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_" + day + "_100000.json"), "{}"));
            DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_" + day + "_110000.json"), "{}"));
        }
    }
    DHW_CHECK(dhw::save(dir.path, sample()));

    const std::vector<std::string> backups = dhw::list_backups(dir.path);
    DHW_CHECK_EQ(backups.size(), std::size_t{30});
    DHW_CHECK_EQ(dhw::fs::list_dir(folder).size(), std::size_t{30});
    for (std::size_t i = 1; i < backups.size(); ++i) {
        DHW_CHECK(backups[i - 1] > backups[i]);
    }
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(folder, "data_20200112_120000.json")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(folder, "data_20200111_120000.json")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(folder, "data_20200101_120000.json")));
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(folder, "data_20200121_120000.json")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(folder, "data_20200121_100000.json")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(folder, "data_20200121_110000.json")));
    std::size_t crafted = 0;
    for (const std::string& name : dhw::fs::list_dir(folder)) {
        if (name.rfind("data_2020", 0) == 0) {
            ++crafted;
        }
    }
    DHW_CHECK_EQ(crafted, std::size_t{29});
}

DHW_TEST(recover_from_corrupt_main) {
    TempDir dir("recover");
    const dhw::AppData data = sample();
    DHW_CHECK(dhw::save(dir.path, data));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    DHW_CHECK(dhw::save(dir.path, data));
    DHW_CHECK(dhw::fs::write_text(dir.file("data.json"), "{ 损坏"));

    const dhw::LoadResult broken = dhw::load(dir.path);
    DHW_CHECK(!broken.ok);
    DHW_CHECK(!broken.error.empty());

    const dhw::LoadResult recovered = dhw::load_or_recover(dir.path);
    DHW_CHECK(recovered.ok);
    DHW_CHECK(recovered.data == data);
    DHW_CHECK(!recovered.error.empty());
    DHW_CHECK(recovered.recovered_from_backup);
    DHW_CHECK(!recovered.backup_used.empty());

    const dhw::LoadResult reloaded = dhw::load(dir.path);
    DHW_CHECK(reloaded.ok);
    DHW_CHECK(reloaded.data == data);
}

DHW_TEST(load_or_recover_uses_newest_backup_first) {
    TempDir dir("recover_newest");
    const std::string folder = dhw::backups_dir(dir.path);
    DHW_CHECK(dhw::fs::create_directories(folder));
    dhw::AppData older = sample();
    older.announcements.push_back(dhw::Announcement{1, "旧备份", false});
    dhw::AppData newer = sample();
    newer.announcements.push_back(dhw::Announcement{2, "新备份", false});
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_20200101_120000.json"),
                                  dhw::to_json(older).dump(2)));
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_20200102_120000.json"),
                                  dhw::to_json(newer).dump(2)));
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(folder, "data_20200103_120000.json"), "{ 坏备份"));
    DHW_CHECK(dhw::fs::write_text(dir.file("data.json"), "{ 损坏"));

    const dhw::LoadResult recovered = dhw::load_or_recover(dir.path);
    DHW_CHECK(recovered.ok);
    DHW_CHECK(recovered.data == newer);
    DHW_CHECK_EQ(recovered.backup_used, std::string("data_20200102_120000.json"));
    DHW_CHECK(dhw::load(dir.path).data == newer);
}

DHW_TEST(ignores_legacy_fields_on_load) {
    TempDir dir("legacy_fields");
    const std::string text = R"({
        "version": 1, "weekly_schedule": [], "days": {},
        "announcements": [{"id": 1, "text": "旧公告", "created_date": "2026-09-30", "done": true, "done_date": "2026-10-01"}],
        "settings": {"data_dir": null, "image_interval_secs": 60, "morning_cutoff": "08:00", "show_badge": true, "last_seen_date": "2026-10-01"}
    })";
    DHW_CHECK(dhw::fs::write_text(dir.file("data.json"), text));
    const dhw::LoadResult loaded = dhw::load(dir.path);
    DHW_CHECK(loaded.ok);
    DHW_CHECK_EQ(loaded.data.announcements.size(), std::size_t{1});
    DHW_CHECK_EQ(loaded.data.announcements[0].id, 1);
    DHW_CHECK_EQ(loaded.data.announcements[0].text, std::string("旧公告"));
    DHW_CHECK_EQ(loaded.data.announcements[0].done, true);
    DHW_CHECK_EQ(dhw::to_json(loaded.data).dump(2).find("created_date"), std::string::npos);
    DHW_CHECK_EQ(dhw::to_json(loaded.data).dump(2).find("data_dir"), std::string::npos);
}

DHW_TEST(empty_dir_is_a_clean_first_run) {
    TempDir dir("empty");
    const dhw::LoadResult result = dhw::load_or_recover(dir.path);
    DHW_CHECK(result.ok);
    DHW_CHECK(result.error.empty());
    DHW_CHECK(!result.recovered_from_backup);
    DHW_CHECK(result.data == dhw::AppData::defaults());
    DHW_CHECK(result.data.days.empty());
    DHW_CHECK(!dhw::fs::exists(dir.file("data.json")));
}

DHW_TEST(accepts_utf8_bom) {
    TempDir dir("bom");
    const dhw::AppData data = sample();
    DHW_CHECK(dhw::save(dir.path, data));
    const std::string text = dhw::fs::read_text(dir.file("data.json")).value();
    DHW_CHECK(dhw::fs::write_text(dir.file("data.json"), "\xEF\xBB\xBF\xEF\xBB\xBF" + text));

    const dhw::LoadResult loaded = dhw::load(dir.path);
    DHW_CHECK(loaded.ok);
    DHW_CHECK(loaded.data == data);

    const dhw::LoadResult recovered = dhw::load_or_recover(dir.path);
    DHW_CHECK(recovered.ok);
    DHW_CHECK(recovered.data == data);
    DHW_CHECK(!recovered.recovered_from_backup);
    DHW_CHECK(recovered.error.empty());
}

DHW_TEST(migrate_copies_and_verifies) {
    TempDir src("migrate_src");
    TempDir dst("migrate_dst");
    const dhw::AppData data = sample();
    DHW_CHECK(dhw::save(src.path, data));
    DHW_CHECK(dhw::fs::write_text(src.file("badge.png"), "png"));

    const dhw::MigrateResult result = dhw::migrate_data_dir(src.path, dst.path);
    DHW_CHECK(result.ok);
    DHW_CHECK(result.error.empty());
    DHW_CHECK_EQ(result.renamed_to.rfind("data.json.migrated_", 0), std::size_t{0});
    DHW_CHECK(dhw::load(dst.path).data == data);
    DHW_CHECK(dhw::fs::exists(dst.file("badge.png")));
    DHW_CHECK(!dhw::has_data(src.path));
    DHW_CHECK_EQ(migrated_count(src.path), std::size_t{1});
    DHW_CHECK_EQ(dhw::fs::read_text(dhw::fs::join(src.path, result.renamed_to)).value(),
                 dhw::to_json(data).dump(2));
}

DHW_TEST(has_data_only_when_data_file_exists) {
    TempDir gone("has_data_gone");
    const std::string absent = gone.path;
    dhw::fs::remove_all(absent);
    DHW_CHECK(!dhw::has_data(absent));
    DHW_CHECK_EQ(dhw::data_file(absent), dhw::fs::join(absent, "data.json"));
    DHW_CHECK_EQ(dhw::badge_file(absent), dhw::fs::join(absent, "badge.png"));
    DHW_CHECK_EQ(dhw::backups_dir(absent), dhw::fs::join(absent, "backups"));

    TempDir dir("has_data");
    DHW_CHECK(!dhw::has_data(dir.path));
    DHW_CHECK(dhw::save(dir.path, sample()));
    DHW_CHECK(dhw::has_data(dir.path));
}

DHW_TEST(migrate_rejects_same_dir) {
    TempDir src("migrate_same");
    DHW_CHECK(dhw::save(src.path, sample()));
    const dhw::MigrateResult result = dhw::migrate_data_dir(src.path, src.path);
    DHW_CHECK(!result.ok);
    DHW_CHECK(!result.error.empty());
    DHW_CHECK(dhw::has_data(src.path));
    DHW_CHECK(!dhw::migrate_data_dir(src.path, src.path + "/").ok);
    DHW_CHECK(dhw::has_data(src.path));
}

DHW_TEST(migrate_rejects_destination_inside_source) {
    TempDir src("migrate_inner");
    DHW_CHECK(dhw::save(src.path, sample()));
    const std::string inner = dhw::fs::join(src.path, "子目录");
    const dhw::MigrateResult result = dhw::migrate_data_dir(src.path, inner);
    DHW_CHECK(!result.ok);
    DHW_CHECK(!result.error.empty());
    DHW_CHECK(dhw::has_data(src.path));
    DHW_CHECK(!dhw::fs::exists(inner));
}

DHW_TEST(migrate_rejects_unloadable_target) {
    TempDir src("migrate_bad_src");
    TempDir dst("migrate_bad_dst");
    DHW_CHECK(dhw::fs::write_text(src.file("别的东西.txt"), "x"));
    const dhw::MigrateResult result = dhw::migrate_data_dir(src.path, dst.path);
    DHW_CHECK(!result.ok);
    DHW_CHECK(result.error.find("校验") != std::string::npos);
    DHW_CHECK(!dhw::has_data(src.path));
}

DHW_TEST(write_image_keeps_other_days) {
    TempDir dir("image_keep");
    DHW_CHECK_EQ(dhw::image_file_name("2026-09-30"), std::string("homework_2026-09-30.png"));
    DHW_CHECK(dhw::write_image(dir.path, "2026-09-30", png({1, 2, 3})));
    DHW_CHECK(dhw::fs::exists(dir.file("homework_2026-09-30.png")));
    DHW_CHECK(dhw::write_image(dir.path, "2026-10-01", png({4, 5, 6})));
    DHW_CHECK_EQ(image_names(dir.path),
                 (std::vector<std::string>{"homework_2026-09-30.png", "homework_2026-10-01.png"}));
    DHW_CHECK(dhw::fs::read_bytes(dir.file("homework_2026-10-01.png")).value() == png({4, 5, 6}));
    DHW_CHECK(!dhw::write_image(dir.path, "../坏日期", png({7})));
    DHW_CHECK(!dhw::write_image(dir.path, "2026-2-30", png({7})));
    DHW_CHECK(!dhw::write_image(dir.path, "", png({7})));
    DHW_CHECK_EQ(image_names(dir.path).size(), std::size_t{2});
}

DHW_TEST(stale_images_move_into_data_dir) {
    TempDir exe("image_exe");
    TempDir data("image_data");
    const std::string archive = dhw::fs::join(data.path, "images");
    DHW_CHECK(dhw::write_image(exe.path, "2026-09-30", png({1})));
    DHW_CHECK(dhw::write_image(exe.path, "2026-10-08", png({2})));
    DHW_CHECK(dhw::fs::write_text(exe.file("别的东西.txt"), "x"));

    const std::int64_t now = dhw::file_mtime(exe.file("homework_2026-10-08.png")).value();
    dhw::ArchiveReport report =
        dhw::archive_stale_images(exe.path, archive, "2026-09-30", "2026-09-30", now);
    DHW_CHECK_EQ(report.moved, 0);
    DHW_CHECK_EQ(report.kept, 2);
    DHW_CHECK(!dhw::fs::exists(archive));

    report =
        dhw::archive_stale_images(exe.path, archive, "2026-09-30", "2026-09-30", now + 25 * 3600);
    DHW_CHECK_EQ(report.moved, 1);
    DHW_CHECK_EQ(report.kept, 1);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, "homework_2026-10-08.png")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(archive, "homework_2026-09-30.png")));
    DHW_CHECK_EQ(image_names(exe.path), std::vector<std::string>{"homework_2026-09-30.png"});
    DHW_CHECK(dhw::fs::exists(exe.file("别的东西.txt")));

    report =
        dhw::archive_stale_images(exe.path, archive, "2026-10-01", "2026-10-01", now + 26 * 3600);
    DHW_CHECK_EQ(report.moved, 1);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, "homework_2026-09-30.png")));
    DHW_CHECK(!dhw::fs::exists(exe.file("homework_2026-09-30.png")));
}

DHW_TEST(todays_image_archives_next_day_within_24h) {
    TempDir exe("image_rollover");
    TempDir data("image_rollover_data");
    const std::string archive = dhw::fs::join(data.path, "images");
    const std::string today = dhw::to_string(dhw::today_local());
    const std::string tomorrow = dhw::to_string(dhw::add_days(dhw::today_local(), 1));
    DHW_CHECK(dhw::write_image(exe.path, today, png({1})));
    DHW_CHECK(dhw::write_image(exe.path, tomorrow, png({2})));

    const std::int64_t now = dhw::file_mtime(exe.file(dhw::image_file_name(today))).value();
    DHW_CHECK_EQ(dhw::archive_stale_images(exe.path, archive, today, tomorrow, now).moved, 0);
    DHW_CHECK_EQ(dhw::archive_stale_images(exe.path, archive, today, today, now).moved, 0);

    const dhw::ArchiveReport report =
        dhw::archive_stale_images(exe.path, archive, tomorrow, tomorrow, now + 3600);
    DHW_CHECK_EQ(report.moved, 1);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, dhw::image_file_name(today))));
    DHW_CHECK_EQ(image_names(exe.path), std::vector<std::string>{dhw::image_file_name(tomorrow)});
}

DHW_TEST(manual_other_day_image_waits_24h) {
    TempDir exe("image_manual");
    TempDir data("image_manual_data");
    const std::string archive = dhw::fs::join(data.path, "images");
    const std::string today = dhw::to_string(dhw::today_local());
    const std::string other = dhw::to_string(dhw::add_days(dhw::today_local(), -3));
    DHW_CHECK(dhw::write_image(exe.path, other, png({1})));

    const std::string file = exe.file(dhw::image_file_name(other));
    const std::int64_t base = dhw::file_mtime(file).value();
    DHW_CHECK_EQ(dhw::archive_stale_images(exe.path, archive, today, today, base).moved, 0);
    DHW_CHECK_EQ(dhw::archive_stale_images(exe.path, archive, today, today, base + 86399).moved, 0);
    DHW_CHECK_EQ(dhw::archive_stale_images(exe.path, archive, today, today, base + 86400).moved, 1);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, dhw::image_file_name(other))));
    DHW_CHECK(!dhw::fs::exists(file));
}

DHW_TEST(clear_images_moves_everything_but_kept) {
    TempDir exe("clear_exe");
    TempDir data("clear_data");
    const std::string archive = dhw::fs::join(data.path, "images");
    DHW_CHECK(dhw::write_image(exe.path, "2026-09-30", png({1})));
    DHW_CHECK(dhw::write_image(exe.path, "2026-10-08", png({2})));
    DHW_CHECK(dhw::write_image(exe.path, "2026-10-09", png({3})));
    DHW_CHECK(dhw::fs::write_text(exe.file("别的东西.txt"), "x"));

    const dhw::ArchiveReport report = dhw::clear_images(exe.path, archive, "2026-10-09", "2026-10-09");
    DHW_CHECK_EQ(report.moved, 2);
    DHW_CHECK_EQ(report.kept, 1);
    DHW_CHECK_EQ(image_names(exe.path), std::vector<std::string>{"homework_2026-10-09.png"});
    const dhw::ArchiveReport again = dhw::clear_images(exe.path, archive, "2026-10-09", "2026-10-01");
    DHW_CHECK_EQ(again.moved, 0);
    DHW_CHECK_EQ(again.kept, 1);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, "homework_2026-09-30.png")));
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive, "homework_2026-10-08.png")));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(archive, "homework_2026-10-09.png")));
    DHW_CHECK(dhw::fs::exists(exe.file("别的东西.txt")));
    DHW_CHECK_EQ(dhw::fs::read_bytes(dhw::fs::join(archive, "homework_2026-10-08.png")).value(),
                 png({2}));
}

DHW_TEST(keeps_past_hidden_dates_on_load) {
    TempDir dir("hidden_past");
    dhw::AppData data = sample();
    data.hidden_dates = {"2026-10-01", "2026-10-02", "2999-01-01"};
    data.pinned_dates = {"2020-01-01", "2999-02-01"};
    DHW_CHECK(dhw::save(dir.path, data));

    const dhw::LoadResult loaded = dhw::load(dir.path);
    DHW_CHECK(loaded.ok);
    DHW_CHECK_EQ(loaded.data.hidden_dates.size(), std::size_t{3});
    DHW_CHECK_EQ(loaded.data.hidden_dates[0], std::string("2026-10-01"));
    DHW_CHECK_EQ(loaded.data.hidden_dates[1], std::string("2026-10-02"));
    DHW_CHECK_EQ(loaded.data.hidden_dates[2], std::string("2999-01-01"));
    DHW_CHECK_EQ(loaded.data.pinned_dates.size(), std::size_t{1});
    DHW_CHECK_EQ(loaded.data.pinned_dates[0], std::string("2999-02-01"));
}

DHW_TEST(holiday_marks_keep_today_on_last_school_day) {
    TempDir dir("holiday_today");
    dhw::AppData data = sample();
    data.days.clear();
    data.hidden_dates = {"2026-10-01", "2026-10-02", "2026-10-05", "2026-10-06", "2026-10-07"};
    DHW_CHECK(dhw::save(dir.path, data));

    const dhw::LoadResult loaded = dhw::load(dir.path);
    DHW_CHECK(loaded.ok);
    DHW_CHECK(loaded.data.days.empty());
    const std::vector<std::string> expected = {"2026-10-01", "2026-10-02", "2026-10-05", "2026-10-06",
                                              "2026-10-07"};
    DHW_CHECK(loaded.data.hidden_dates == expected);
    DHW_CHECK(dhw::save(dir.path, loaded.data));
    DHW_CHECK(dhw::load(dir.path).data.hidden_dates == expected);
}

DHW_TEST(decodes_shared_fixture) {
    const dhw::AppData decoded = decode_fixture();
    DHW_CHECK_EQ(decoded.hidden_dates.size(), std::size_t{2});
    DHW_CHECK_EQ(decoded.hidden_dates[0], std::string("2026-10-02"));
    DHW_CHECK_EQ(decoded.hidden_dates[1], std::string("2026-10-05"));
    DHW_CHECK_EQ(decoded.pinned_dates.size(), std::size_t{1});
    DHW_CHECK_EQ(decoded.pinned_dates[0], std::string("2026-10-08"));
    DHW_CHECK_EQ(decoded.settings.english_split, false);
    DHW_CHECK_EQ(decoded.settings.ui_scale, 1.25);
    DHW_CHECK_EQ(decoded.settings.image_interval_secs, 90);
    DHW_CHECK_EQ(decoded.days.size(), std::size_t{2});

    const std::string out = canonical_text();
    DHW_CHECK_EQ(out.find("created_date"), std::string::npos);
    DHW_CHECK_EQ(out.find("data_dir"), std::string::npos);
    const dhw::JsonParse parsed = dhw::Json::parse(out);
    DHW_CHECK(parsed.ok);
    DHW_CHECK(dhw::parse_app_data(parsed.value).data == decoded);
}

DHW_TEST(serialization_matches_canonical_fixture) {
    const std::optional<std::string> want =
        dhw::fs::read_text(fixture_path("data.canonical.json"));
    DHW_CHECK(want.has_value());
    DHW_CHECK_EQ(canonical_text() + "\n", *want);
}

DHW_TEST(regenerate_canonical_fixture) {
    const std::optional<std::string> want =
        dhw::fs::read_text(fixture_path("data.canonical.json"));
    DHW_CHECK(want.has_value());
    DHW_CHECK_EQ(canonical_text() + "\n", *want);
}

DHW_TEST(local_settings_round_trip) {
    TempDir dir("local_round");
    DHW_CHECK_EQ(dhw::local_settings_file_name(), std::string("local_settings.json"));
    const dhw::LocalSettings defaults;
    DHW_CHECK_EQ(defaults.keep_awake, true);
    DHW_CHECK_EQ(defaults.lock_switching, true);
    DHW_CHECK_EQ(defaults.screen_device, std::string(""));
    DHW_CHECK_EQ(defaults.appearance, std::string("system"));
    DHW_CHECK_EQ(defaults.sidebar_collapsed, false);
    DHW_CHECK(dhw::load_local_settings(dir.path) == defaults);

    dhw::LocalSettings settings;
    settings.lock_switching = false;
    settings.screen_device = "DISPLAY2";
    settings.appearance = "dark";
    settings.sidebar_collapsed = true;
    DHW_CHECK(dhw::save_local_settings(dir.path, settings));
    const std::string expected =
        "{\n"
        "  \"keep_awake\": true,\n"
        "  \"lock_switching\": false,\n"
        "  \"screen_device\": \"DISPLAY2\",\n"
        "  \"appearance\": \"dark\",\n"
        "  \"sidebar_collapsed\": true\n"
        "}\n";
    DHW_CHECK_EQ(dhw::fs::read_text(dir.file(dhw::local_settings_file_name())).value(), expected);
    DHW_CHECK(!dhw::fs::exists(dir.file(dhw::local_settings_file_name() + ".tmp")));
    DHW_CHECK(dhw::load_local_settings(dir.path) == settings);
    DHW_CHECK(dhw::load_local_settings(dhw::fs::join(dir.path, "无此目录")) == defaults);
}

DHW_TEST(local_settings_fall_back_to_defaults_when_missing_or_corrupt) {
    TempDir dir("local_bad");
    DHW_CHECK(dhw::load_local_settings(dir.path) == dhw::LocalSettings{});
    const std::string file = dir.file(dhw::local_settings_file_name());
    DHW_CHECK(dhw::fs::write_text(file, "{ 损坏"));
    DHW_CHECK(dhw::load_local_settings(dir.path) == dhw::LocalSettings{});
    DHW_CHECK_EQ(dhw::fs::read_text(file).value(), std::string("{ 损坏"));
    DHW_CHECK(dhw::fs::write_text(file, R"({"keep_awake": "yes"})"));
    DHW_CHECK(dhw::load_local_settings(dir.path) == dhw::LocalSettings{});
    DHW_CHECK(dhw::fs::write_text(file, R"({"appearance": null})"));
    DHW_CHECK(dhw::load_local_settings(dir.path) == dhw::LocalSettings{});

    DHW_CHECK(dhw::fs::write_text(file, R"({"unknown": 1, "appearance": "light"})"));
    dhw::LocalSettings partial;
    partial.appearance = "light";
    DHW_CHECK(dhw::load_local_settings(dir.path) == partial);
}

DHW_TEST(local_settings_migrate_from_legacy_dir) {
    TempDir data("local_data");
    TempDir legacy("local_legacy");
    const std::string name = dhw::local_settings_file_name();
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(legacy.path, name),
                                  R"({"appearance":"dark","sidebar_collapsed":true})"));
    dhw::LocalSettings migrated;
    migrated.appearance = "dark";
    migrated.sidebar_collapsed = true;
    DHW_CHECK(dhw::load_local_settings(data.path, legacy.path) == migrated);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(data.path, name)));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(legacy.path, name)));

    dhw::LocalSettings stored;
    stored.appearance = "light";
    DHW_CHECK(dhw::save_local_settings(data.path, stored));
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(legacy.path, name),
                                  R"({"appearance":"dark","sidebar_collapsed":true})"));
    DHW_CHECK(dhw::load_local_settings(data.path, legacy.path) == stored);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(legacy.path, name)));

    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(data.path, name), "{ 损坏"));
    DHW_CHECK(dhw::load_local_settings(data.path, legacy.path) == migrated);
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(data.path, name)));
    DHW_CHECK(!dhw::fs::exists(dhw::fs::join(legacy.path, name)));
    DHW_CHECK(dhw::load_local_settings(data.path) == migrated);
}

DHW_TEST(remove_legacy_ini_cleanup) {
    TempDir dir("legacy_ini");
    DHW_CHECK(dhw::remove_legacy_ini(dir.path));
    DHW_CHECK(dhw::fs::write_text(dir.file("每日作业配置.ini"), "[data]\ndir=x\n"));
    DHW_CHECK(dhw::remove_legacy_ini(dir.path));
    DHW_CHECK(!dhw::fs::exists(dir.file("每日作业配置.ini")));
    DHW_CHECK(dhw::fs::write_text(dir.file("别的东西.txt"), "x"));
    DHW_CHECK(dhw::remove_legacy_ini(dir.path));
    DHW_CHECK(dhw::fs::exists(dir.file("别的东西.txt")));
}

DHW_TEST(badge_round_trip_and_empty_rejection) {
    TempDir dir("badge");
    DHW_CHECK(dhw::read_badge(dir.path).empty());
    const std::vector<std::uint8_t> image = png({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A});
    DHW_CHECK(dhw::write_badge(dir.path, image));
    DHW_CHECK(dhw::read_badge(dir.path) == image);
    DHW_CHECK(dhw::fs::exists(dir.file("badge.png")));
    DHW_CHECK(!dhw::write_badge(dir.path, {}));
    DHW_CHECK(dhw::read_badge(dir.path) == image);
    const std::string nested = dhw::fs::join(dir.path, "新目录");
    DHW_CHECK(dhw::write_badge(nested, image));
    DHW_CHECK(dhw::read_badge(nested) == image);
}

DHW_TEST(default_data_dir_uses_executable_dir) {
    const std::string dir = dhw::default_data_dir();
    DHW_CHECK(!dir.empty());
    DHW_CHECK_EQ(dir, dhw::fs::join(dhw::fs::executable_dir(), "data"));
    DHW_CHECK_EQ(dhw::fs::filename(dir), std::string("data"));
}

DHW_TEST(store_constants_match_spec) {
    DHW_CHECK_EQ(dhw::kMaxBackups, 30);
    DHW_CHECK_EQ(dhw::kImageMaxAgeSecs, std::int64_t{24 * 60 * 60});
    DHW_CHECK_EQ(dhw::kArchiveCheckSecs, 300);
}

DHW_TEST(legacy_image_prefix_is_still_archived) {
    TempDir exe("legacy_image");
    TempDir archive("legacy_image_archive");
    DHW_CHECK(dhw::fs::write_bytes(exe.file("家庭作业_2026-10-08.png"), "png", 3));
    const dhw::ArchiveReport report = dhw::archive_stale_images(
        exe.path, archive.path, "2026-10-12", "2026-10-12", 4100000000);
    DHW_CHECK_EQ(report.moved, 1);
    DHW_CHECK(!dhw::fs::exists(exe.file("家庭作业_2026-10-08.png")));
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(archive.path, "家庭作业_2026-10-08.png")));
}

DHW_TEST(legacy_local_settings_name_is_read_and_migrated) {
    TempDir dir("legacy_settings");
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(dir.path, "每日作业本机设置.json"),
                                  "{\"appearance\":\"dark\",\"sidebar_collapsed\":true}"));
    const dhw::LocalSettings settings = dhw::load_local_settings(dir.path);
    DHW_CHECK_EQ(settings.appearance, std::string("dark"));
    DHW_CHECK_EQ(settings.sidebar_collapsed, true);
    DHW_CHECK_EQ(dhw::local_settings_file_name(), std::string("local_settings.json"));
    DHW_CHECK(dhw::fs::exists(dhw::fs::join(dir.path, "local_settings.json")));
}

DHW_TEST(legacy_badge_name_is_read) {
    TempDir dir("legacy_badge");
    DHW_CHECK(dhw::fs::write_bytes(dhw::fs::join(dir.path, "班徽.png"), "png", 3));
    const std::vector<std::uint8_t> bytes = dhw::read_badge(dir.path);
    DHW_CHECK_EQ(bytes.size(), std::size_t{3});
    DHW_CHECK_EQ(dhw::badge_file(dir.path), dhw::fs::join(dir.path, "badge.png"));
}

DHW_TEST(image_and_settings_file_names_are_ascii) {
    for (const std::string& name : {dhw::image_file_name("2026-10-12"), dhw::local_settings_file_name()}) {
        for (const char ch : name) {
            DHW_CHECK(static_cast<unsigned char>(ch) < 0x80);
        }
    }
}

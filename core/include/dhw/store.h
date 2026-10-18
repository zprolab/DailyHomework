#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dhw/model.h"

namespace dhw {

inline constexpr int kMaxBackups = 30;
inline constexpr int kArchiveCheckSecs = 300;
inline constexpr std::int64_t kImageMaxAgeSecs = 24 * 60 * 60;

// Per machine settings, shared by both builds through the data directory.
struct LocalSettings {
    bool keep_awake = true;
    bool lock_switching = true;
    std::string screen_device;
    std::string appearance = "system";
    bool sidebar_collapsed = false;

    bool operator==(const LocalSettings&) const = default;
};

std::string local_settings_file_name();

// Reads <data_dir>/local_settings.json (falling back to the pre-rewrite file name in
// the same directory). A missing or damaged file falls back to `legacy_dir` (pass
// fs::executable_dir() to honor the old executable side location), which is then
// migrated into the data directory and removed; without a usable legacy copy the
// defaults are used.
LocalSettings load_local_settings(const std::string& data_dir,
                                  const std::string& legacy_dir = std::string());
bool save_local_settings(const std::string& data_dir, const LocalSettings& settings);

std::string default_data_dir();
// The pre-rewrite data directory (<exe>/每日作业数据). Read only.
std::string legacy_data_dir();
// default_data_dir(), unless that one has no data and the legacy directory does:
// an existing installation keeps using its data instead of silently starting empty.
std::string resolve_data_dir();
bool has_data(const std::string& dir);

std::string data_file(const std::string& dir);
std::string badge_file(const std::string& dir);
std::string backups_dir(const std::string& dir);

// Unix epoch seconds of a file's modification time, comparable with std::time(nullptr).
std::optional<std::int64_t> file_mtime(const std::string& path);

struct LoadResult {
    // False only when neither the data file nor any backup could be decoded; `data`
    // then holds an empty default dataset and `error` explains what went wrong.
    bool ok = false;
    AppData data;
    // Failure reason, or the recovery notice once a backup was used.
    std::string error;
    bool recovered_from_backup = false;
    std::string backup_used;
};

// Parses <dir>/data.json, strips every leading UTF-8 BOM and normalizes the result.
LoadResult load(const std::string& dir);

// `load`, then the newest decodable backup (by file name); a successful recovery is
// copied back over the main file.
LoadResult load_or_recover(const std::string& dir);

// Copies the current data file into backups/data_YYYYMMDD_HHMMSS.json, writes the new
// data atomically and prunes the backups.
bool save(const std::string& dir, const AppData& data);

// Backup file names (not paths), newest first.
std::vector<std::string> list_backups(const std::string& dir);

// Removes <dir>/每日作业配置.ini (pre-rewrite name); a missing file already counts as clean.
bool remove_legacy_ini(const std::string& dir);

struct MigrateResult {
    bool ok = false;
    std::string error;
    // Name the old data.json was archived under, empty when there was nothing to rename.
    std::string renamed_to;
};

// Copies the whole data tree, verifies that the copy loads and renames the old
// data.json to data.json.migrated_<stamp> so the old location stops looking like data.
MigrateResult migrate_data_dir(const std::string& from, const std::string& to);

std::string image_file_name(const std::string& date);
bool write_image(const std::string& image_dir, const std::string& page_date,
                 const std::vector<std::uint8_t>& png);

struct ArchiveReport {
    int moved = 0;
    int kept = 0;
};

// Moves non-kept homework_<date>.png files into `archive_dir` (the pre-rewrite
// 家庭作业_<date>.png prefix is recognized too). Images whose file name date equals the
// local date of their mtime are moved as soon as the day rolls over; the others only
// once they are kImageMaxAgeSecs old. Kept = matching images left in place.
ArchiveReport archive_stale_images(const std::string& image_dir, const std::string& archive_dir,
                                   const std::string& real_today, const std::string& effective_today,
                                   std::int64_t now_epoch_seconds);

// Moves every non-kept image into `archive_dir` regardless of age (never deletes).
ArchiveReport clear_images(const std::string& image_dir, const std::string& archive_dir,
                           const std::string& real_today, const std::string& effective_today);

std::vector<std::uint8_t> read_badge(const std::string& data_dir);
bool write_badge(const std::string& data_dir, const std::vector<std::uint8_t>& png);

}  // namespace dhw

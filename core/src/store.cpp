#include "dhw/store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <system_error>
#include <utility>

#include "dhw/datetime.h"
#include "dhw/fs.h"
#include "dhw/json.h"

namespace dhw {
namespace {

namespace stdfs = std::filesystem;

constexpr const char* kDataFileName = "data.json";
constexpr const char* kBackupsDirName = "backups";
constexpr const char* kBadgeFileName = "badge.png";
constexpr const char* kDefaultDataDirName = "data";
constexpr const char* kLocalSettingsName = "local_settings.json";
constexpr const char* kImagePrefix = "homework_";
constexpr const char* kImageSuffix = ".png";
constexpr const char* kMigratedInfix = ".migrated_";

// Names written by the pre-rewrite builds. They are only read (or removed) so an
// existing installation keeps working; nothing new is ever written under them.
constexpr const char* kLegacyBadgeFileName = "班徽.png";
constexpr const char* kLegacyDataDirName = "每日作业数据";
constexpr const char* kLegacyLocalSettingsName = "每日作业本机设置.json";
constexpr const char* kLegacyIniName = "每日作业配置.ini";
constexpr const char* kLegacyImagePrefix = "家庭作业_";

stdfs::path to_native(std::string_view utf8) {
#if defined(_WIN32)
    return stdfs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
    return stdfs::path(std::string(utf8));
#endif
}

std::string strip_bom(std::string text) {
    while (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
           static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::string local_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d_%02d%02d%02d", parts.tm_year + 1900,
                  parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
    return buffer;
}

std::string local_date_of_epoch(std::int64_t seconds) {
    const std::time_t stamp = static_cast<std::time_t>(seconds);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &stamp);
#else
    localtime_r(&stamp, &parts);
#endif
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", parts.tm_year + 1900, parts.tm_mon + 1,
                  parts.tm_mday);
    return buffer;
}

std::int64_t mtime_seconds(const std::string& path, bool& ok) {
    std::error_code error;
    const stdfs::file_time_type stamp = stdfs::last_write_time(to_native(path), error);
    if (error) {
        ok = false;
        return 0;
    }
    ok = true;
    std::int64_t seconds =
        std::chrono::duration_cast<std::chrono::seconds>(stamp.time_since_epoch()).count();
#if defined(_WIN32)
    constexpr std::int64_t kFileClockToUnix = 11644473600LL;
    seconds -= kFileClockToUnix;
#endif
    return seconds;
}

std::string without_trailing_separators(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    return path;
}

bool is_inside(const std::string& child, const std::string& parent) {
    if (parent.empty() || child.size() <= parent.size()) {
        return false;
    }
    if (child.compare(0, parent.size(), parent) != 0) {
        return false;
    }
    return child[parent.size()] == '/' || child[parent.size()] == '\\';
}

bool decode_data(const std::string& text, AppData& out, std::string& error) {
    const JsonParse parsed = Json::parse(text);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    const DataParse data = parse_app_data(parsed.value);
    if (!data.ok) {
        error = data.error;
        return false;
    }
    out = data.data;
    return true;
}

bool is_utf8_boundary(const std::string& text, std::size_t index) {
    if (index > text.size()) {
        return false;
    }
    if (index == text.size()) {
        return true;
    }
    return (static_cast<unsigned char>(text[index]) & 0xC0) != 0x80;
}

std::string backup_day_key(const std::string& name) {
    if (name.size() < 13 || !is_utf8_boundary(name, 5) || !is_utf8_boundary(name, 13)) {
        return {};
    }
    return name.substr(5, 8);
}

std::vector<std::string> backup_names(const std::string& dir) {
    const std::string folder = backups_dir(dir);
    std::vector<std::string> names;
    for (const std::string& name : fs::list_dir(folder)) {
        if (fs::extension(name) == ".json" && fs::is_regular_file(fs::join(folder, name))) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void prune_backups(const std::string& dir) {
    const std::string folder = backups_dir(dir);
    const std::vector<std::string> names = backup_names(dir);
    std::map<std::string, std::string> latest_per_day;
    for (const std::string& name : names) {
        latest_per_day[backup_day_key(name)] = name;
    }
    std::vector<std::string> survivors;
    survivors.reserve(latest_per_day.size());
    for (const auto& [day, name] : latest_per_day) {
        (void)day;
        survivors.push_back(name);
    }
    while (survivors.size() > static_cast<std::size_t>(kMaxBackups)) {
        survivors.erase(survivors.begin());
    }
    for (const std::string& name : names) {
        if (std::find(survivors.begin(), survivors.end(), name) == survivors.end()) {
            fs::remove_file(fs::join(folder, name));
        }
    }
}

bool copy_tree(const std::string& from, const std::string& to) {
    if (!fs::is_directory(from)) {
        return false;
    }
    if (!fs::create_directories(to)) {
        return false;
    }
    for (const std::string& name : fs::list_dir(from)) {
        const std::string source = fs::join(from, name);
        const std::string target = fs::join(to, name);
        if (fs::is_directory(source)) {
            if (!copy_tree(source, target)) {
                return false;
            }
        } else if (!fs::copy_file(source, target)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> image_date_of(const std::string& name) {
    const std::size_t suffix = std::string(kImageSuffix).size();
    std::size_t prefix = 0;
    if (name.rfind(kImagePrefix, 0) == 0) {
        prefix = std::string(kImagePrefix).size();
    } else if (name.rfind(kLegacyImagePrefix, 0) == 0) {
        prefix = std::string(kLegacyImagePrefix).size();
    } else {
        return std::nullopt;
    }
    if (name.size() <= prefix + suffix) {
        return std::nullopt;
    }
    if (name.compare(name.size() - suffix, suffix, kImageSuffix) != 0) {
        return std::nullopt;
    }
    const std::string date = name.substr(prefix, name.size() - prefix - suffix);
    if (!parse_date(date).has_value()) {
        return std::nullopt;
    }
    return date;
}

std::vector<std::string> keep_dates(const std::string& real_today, const std::string& effective_today) {
    std::vector<std::string> dates{real_today};
    if (effective_today != real_today) {
        dates.push_back(effective_today);
    }
    return dates;
}

ArchiveReport relocate_images(const std::string& image_dir, const std::string& archive_dir,
                              const std::vector<std::string>& keep, bool stale_only,
                              std::int64_t now_epoch_seconds) {
    ArchiveReport report;
    for (const std::string& name : fs::list_dir(image_dir)) {
        const std::optional<std::string> date = image_date_of(name);
        if (!date.has_value()) {
            continue;
        }
        if (std::find(keep.begin(), keep.end(), *date) != keep.end()) {
            ++report.kept;
            continue;
        }
        bool readable = false;
        const std::int64_t modified = mtime_seconds(fs::join(image_dir, name), readable);
        if (!readable) {
            ++report.kept;
            continue;
        }
        if (stale_only) {
            if (local_date_of_epoch(modified) != *date) {
                if (now_epoch_seconds < modified) {
                    ++report.kept;
                    continue;
                }
                if (now_epoch_seconds - modified < kImageMaxAgeSecs) {
                    ++report.kept;
                    continue;
                }
            }
        }
        if (!fs::create_directories(archive_dir)) {
            ++report.kept;
            continue;
        }
        const std::string source = fs::join(image_dir, name);
        const std::string target = fs::join(archive_dir, name);
        if (fs::exists(target)) {
            fs::remove_file(target);
        }
        if (fs::rename(source, target) ||
            (fs::copy_file(source, target) && fs::remove_file(source))) {
            ++report.moved;
        } else {
            ++report.kept;
        }
    }
    return report;
}

bool parse_local_settings(const std::string& text, LocalSettings& out) {
    const JsonParse parsed = Json::parse(text);
    if (!parsed.ok || !parsed.value.is_object()) {
        return false;
    }
    LocalSettings settings;
    if (const Json* value = parsed.value.find("keep_awake"); value != nullptr) {
        if (!value->is_bool()) {
            return false;
        }
        settings.keep_awake = value->as_bool();
    }
    if (const Json* value = parsed.value.find("lock_switching"); value != nullptr) {
        if (!value->is_bool()) {
            return false;
        }
        settings.lock_switching = value->as_bool();
    }
    if (const Json* value = parsed.value.find("screen_device"); value != nullptr) {
        if (!value->is_string()) {
            return false;
        }
        settings.screen_device = value->as_string();
    }
    if (const Json* value = parsed.value.find("appearance"); value != nullptr) {
        if (!value->is_string()) {
            return false;
        }
        settings.appearance = value->as_string();
    }
    if (const Json* value = parsed.value.find("sidebar_collapsed"); value != nullptr) {
        if (!value->is_bool()) {
            return false;
        }
        settings.sidebar_collapsed = value->as_bool();
    }
    out = settings;
    return true;
}

struct SettingsCandidate {
    std::string text;
    std::string path;
};

std::optional<SettingsCandidate> read_local_settings_file(const std::string& dir) {
    for (const char* name : {kLocalSettingsName, kLegacyLocalSettingsName}) {
        const std::string path = fs::join(dir, name);
        if (const std::optional<std::string> text = fs::read_text(path); text.has_value()) {
            return SettingsCandidate{*text, path};
        }
    }
    return std::nullopt;
}

}  // namespace

std::string local_settings_file_name() {
    return kLocalSettingsName;
}

LocalSettings load_local_settings(const std::string& data_dir, const std::string& legacy_dir) {
    const std::string canonical = fs::join(data_dir, kLocalSettingsName);
    for (const std::string& dir : {data_dir, legacy_dir}) {
        if (dir.empty()) {
            continue;
        }
        const std::optional<SettingsCandidate> found = read_local_settings_file(dir);
        if (!found.has_value()) {
            continue;
        }
        LocalSettings settings;
        if (!parse_local_settings(found->text, settings)) {
            continue;
        }
        if (found->path != canonical && save_local_settings(data_dir, settings)) {
            fs::remove_file(found->path);
        }
        return settings;
    }
    return LocalSettings{};
}

bool save_local_settings(const std::string& data_dir, const LocalSettings& settings) {
    if (!fs::create_directories(data_dir)) {
        return false;
    }
    Json root = Json::object();
    root.set("keep_awake", Json(settings.keep_awake));
    root.set("lock_switching", Json(settings.lock_switching));
    root.set("screen_device", Json(settings.screen_device));
    root.set("appearance", Json(settings.appearance));
    root.set("sidebar_collapsed", Json(settings.sidebar_collapsed));
    const std::string path = fs::join(data_dir, local_settings_file_name());
    if (!fs::write_text(path + ".tmp", root.dump(2) + "\n")) {
        return false;
    }
    return fs::rename(path + ".tmp", path);
}

std::string default_data_dir() {
    return fs::join(fs::executable_dir(), kDefaultDataDirName);
}

std::string legacy_data_dir() {
    return fs::join(fs::executable_dir(), kLegacyDataDirName);
}

std::string resolve_data_dir() {
    const std::string preferred = default_data_dir();
    if (has_data(preferred)) {
        return preferred;
    }
    const std::string legacy = legacy_data_dir();
    if (has_data(legacy)) {
        return legacy;
    }
    return preferred;
}

bool has_data(const std::string& dir) {
    return fs::exists(data_file(dir));
}

std::string data_file(const std::string& dir) {
    return fs::join(dir, kDataFileName);
}

std::string badge_file(const std::string& dir) {
    return fs::join(dir, kBadgeFileName);
}

std::string backups_dir(const std::string& dir) {
    return fs::join(dir, kBackupsDirName);
}

std::optional<std::int64_t> file_mtime(const std::string& path) {
    bool readable = false;
    const std::int64_t seconds = mtime_seconds(path, readable);
    if (!readable) {
        return std::nullopt;
    }
    return seconds;
}

LoadResult load(const std::string& dir) {
    LoadResult result;
    result.data = AppData::defaults();
    const std::string path = data_file(dir);
    const std::optional<std::string> text = fs::read_text(path);
    if (!text.has_value()) {
        result.error = "读取 " + path + " 失败";
        return result;
    }
    AppData data;
    std::string error;
    if (!decode_data(strip_bom(*text), data, error)) {
        result.error = "解析 " + path + " 失败：" + error;
        return result;
    }
    data.normalize(today_local());
    result.ok = true;
    result.data = std::move(data);
    return result;
}

LoadResult load_or_recover(const std::string& dir) {
    const bool had_data_file = fs::exists(data_file(dir));
    LoadResult result = load(dir);
    if (result.ok) {
        return result;
    }
    const std::string folder = backups_dir(dir);
    std::vector<std::string> names = backup_names(dir);
    std::sort(names.begin(), names.end(), std::greater<std::string>());
    for (const std::string& name : names) {
        const std::optional<std::string> text = fs::read_text(fs::join(folder, name));
        if (!text.has_value()) {
            continue;
        }
        AppData data;
        std::string error;
        if (!decode_data(*text, data, error)) {
            continue;
        }
        data.normalize(today_local());
        fs::copy_file(fs::join(folder, name), data_file(dir));
        result.ok = true;
        result.data = std::move(data);
        result.recovered_from_backup = true;
        result.backup_used = name;
        result.error = "主数据损坏（" + result.error + "），已从备份 " + name + " 恢复";
        return result;
    }
    result.data = AppData::defaults();
    if (!had_data_file) {
        // Fresh installation: the directory (or data.json) simply does not exist yet.
        // That is a healthy empty start, not a load failure.
        result.ok = true;
        result.error.clear();
        return result;
    }
    result.error = "数据文件不可用（" + result.error + "），已创建空数据";
    return result;
}

bool save(const std::string& dir, const AppData& data) {
    const std::string folder = backups_dir(dir);
    if (!fs::create_directories(folder)) {
        return false;
    }
    const std::string target = data_file(dir);
    if (fs::exists(target)) {
        const std::string copy = fs::join(folder, "data_" + local_timestamp() + ".json");
        if (!fs::copy_file(target, copy)) {
            return false;
        }
    }
    if (!fs::write_text_atomic(target, to_json(data).dump(2))) {
        return false;
    }
    prune_backups(dir);
    return true;
}

std::vector<std::string> list_backups(const std::string& dir) {
    std::vector<std::string> names = backup_names(dir);
    std::sort(names.begin(), names.end(), std::greater<std::string>());
    return names;
}

bool remove_legacy_ini(const std::string& dir) {
    const std::string path = fs::join(dir, kLegacyIniName);
    if (!fs::exists(path)) {
        return true;
    }
    return fs::remove_file(path);
}

MigrateResult migrate_data_dir(const std::string& from, const std::string& to) {
    MigrateResult result;
    const std::string source = without_trailing_separators(from);
    const std::string target = without_trailing_separators(to);
    if (source == target) {
        result.error = "新目录与当前目录相同";
        return result;
    }
    if (is_inside(target, source)) {
        result.error = "新目录不能位于当前数据目录内部";
        return result;
    }
    if (!copy_tree(source, target)) {
        result.error = "复制数据失败";
        return result;
    }
    const LoadResult verified = load(target);
    if (!verified.ok) {
        result.error = "新目录数据校验失败：" + verified.error;
        return result;
    }
    const std::string old = data_file(source);
    if (fs::exists(old)) {
        const std::string name = std::string(kDataFileName) + kMigratedInfix + local_timestamp();
        if (fs::rename(old, fs::join(source, name))) {
            result.renamed_to = name;
        }
    }
    result.ok = true;
    return result;
}

std::string image_file_name(const std::string& date) {
    return std::string(kImagePrefix) + date + kImageSuffix;
}

bool write_image(const std::string& image_dir, const std::string& page_date,
                 const std::vector<std::uint8_t>& png) {
    if (!parse_date(page_date).has_value()) {
        return false;
    }
    if (!fs::create_directories(image_dir)) {
        return false;
    }
    return fs::write_bytes(fs::join(image_dir, image_file_name(page_date)), png.data(), png.size());
}

ArchiveReport archive_stale_images(const std::string& image_dir, const std::string& archive_dir,
                                   const std::string& real_today, const std::string& effective_today,
                                   std::int64_t now_epoch_seconds) {
    return relocate_images(image_dir, archive_dir, keep_dates(real_today, effective_today), true,
                           now_epoch_seconds);
}

ArchiveReport clear_images(const std::string& image_dir, const std::string& archive_dir,
                           const std::string& real_today, const std::string& effective_today) {
    return relocate_images(image_dir, archive_dir, keep_dates(real_today, effective_today), false,
                           0);
}

std::vector<std::uint8_t> read_badge(const std::string& data_dir) {
    std::optional<std::vector<std::uint8_t>> bytes = fs::read_bytes(badge_file(data_dir));
    if (!bytes.has_value() || bytes->empty()) {
        bytes = fs::read_bytes(fs::join(data_dir, kLegacyBadgeFileName));
    }
    if (!bytes.has_value() || bytes->empty()) {
        return {};
    }
    return *bytes;
}

bool write_badge(const std::string& data_dir, const std::vector<std::uint8_t>& png) {
    if (png.empty()) {
        return false;
    }
    if (!fs::create_directories(data_dir)) {
        return false;
    }
    return fs::write_bytes(badge_file(data_dir), png.data(), png.size());
}

}  // namespace dhw

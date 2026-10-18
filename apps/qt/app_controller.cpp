#include "app_controller.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <algorithm>
#include <cstdio>

#include "dhw/fs.h"
#include "dhw/layout.h"
#include "dhw/qt/renderer.h"

namespace dhw::qt {
namespace {

constexpr int kSaveDebounceMs = 1000;
constexpr int kAutoImageIntervalMs = 60 * 1000;
// Upper bound of the in-memory undo history: older steps are dropped, newest kept.
constexpr std::size_t kMaxUndoSteps = 50;

std::string from_qstring(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

AppController::AppController(QString data_dir, QObject* parent)
    : QObject(parent), data_dir_(std::move(data_dir)) {
    const LoadResult loaded = dhw::load_or_recover(from_qstring(data_dir_));
    if (loaded.ok) {
        data_ = loaded.data;
        recovered_from_backup_ = loaded.recovered_from_backup;
    } else {
        data_ = AppData::defaults();
        save_failed_ = true;
        last_error_ = to_qstring(loaded.error);
    }
    local_settings_ =
        load_local_settings(from_qstring(data_dir_), fs::executable_dir());
    layout_style_ = load_style_file(fs::join(from_qstring(data_dir_), kStyleFileName)).style;
    apply_launch_view();
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(kSaveDebounceMs);
    connect(&save_timer_, &QTimer::timeout, this, &AppController::on_save_timer);
    image_timer_.setInterval(kAutoImageIntervalMs);
    connect(&image_timer_, &QTimer::timeout, this, &AppController::on_image_timer);
}

std::string AppController::today_text() const {
    return to_string(today_local());
}

void AppController::apply_launch_view() {
    const LaunchView launch = launch_view(data_, today_text(), now_local());
    current_date_ = to_qstring(launch.date);
    submit_only_ = launch.submit_only;
}

ViewModel AppController::view_model() const {
    return build_view_model(data_, from_qstring(current_date_), submit_only_);
}

std::vector<std::vector<Slot>> AppController::week(const QString& monday) const {
    return build_week(data_, from_qstring(monday));
}

std::string AppController::effective_today() const {
    return dhw::effective_today(data_, today_text());
}

std::string AppController::next_school_day() const {
    return dhw::next_school_day(data_, from_qstring(current_date_));
}

std::vector<std::string> AppController::navigation_dates() const {
    std::vector<std::string> dates;
    const std::string today = effective_today();
    const std::string next = dhw::next_school_day(data_, today);
    const auto remember = [&dates](const std::string& date) {
        if (!date.empty() && std::find(dates.begin(), dates.end(), date) == dates.end()) {
            dates.push_back(date);
        }
    };
    for (const auto& [date, day] : data_.days) {
        if (day_has_content(&day)) {
            remember(date);
        }
    }
    remember(today);
    remember(next);
    for (const std::string& date : data_.pinned_dates) {
        remember(date);
    }
    if (day_has_content(data_.find_day(from_qstring(current_date_)))) {
        remember(from_qstring(current_date_));
    }
    std::sort(dates.begin(), dates.end());
    return dates;
}

std::string AppController::launch_date() const {
    return launch_view(data_, today_text(), now_local()).date;
}

void AppController::set_current_date(const QString& date) {
    if (date.isEmpty() || date == current_date_) {
        return;
    }
    current_date_ = date;
    emit changed();
}

void AppController::set_submit_only(bool value) {
    if (value == submit_only_) {
        return;
    }
    submit_only_ = value;
    emit changed();
}

void AppController::update_day(const Day& day) {
    const std::string key = from_qstring(current_date_);
    Day next = day;
    if (next.schedule_override.size() < static_cast<std::size_t>(kPeriods)) {
        next.schedule_override.resize(static_cast<std::size_t>(kPeriods));
    }
    // The edit page re-commits its draft after every debounce, on every reload and after
    // an undo. An identical day is not an edit: it must not dirty the state, schedule a
    // save or push an undo step. A missing day with no content is equally meaningless.
    const Day* existing = data_.find_day(key);
    if (existing != nullptr) {
        if (*existing == next) {
            return;
        }
    } else if (!day_has_content(&next)) {
        return;
    }
    AppData before = data_;
    const QString before_date = current_date_;
    data_.days[key] = std::move(next);
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_announcements(std::vector<Announcement> items) {
    if (items == data_.announcements) {
        return;
    }
    AppData before = data_;
    const QString before_date = current_date_;
    data_.announcements = std::move(items);
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_settings(const Settings& settings) {
    if (settings == data_.settings) {
        return;
    }
    AppData before = data_;
    const QString before_date = current_date_;
    data_.settings = settings;
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_weekly_schedule(std::vector<std::vector<std::string>> rows) {
    rows = normalize_weekly(std::move(rows));
    if (rows == data_.weekly_schedule) {
        return;
    }
    AppData before = data_;
    const QString before_date = current_date_;
    data_.weekly_schedule = std::move(rows);
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_hidden_dates(std::vector<std::string> dates) {
    AppData before = data_;
    const QString before_date = current_date_;
    data_.hidden_dates = std::move(dates);
    std::sort(data_.hidden_dates.begin(), data_.hidden_dates.end());
    data_.hidden_dates.erase(std::unique(data_.hidden_dates.begin(), data_.hidden_dates.end()),
                             data_.hidden_dates.end());
    const std::vector<std::string> hidden = data_.hidden_dates;
    data_.pinned_dates.erase(
        std::remove_if(data_.pinned_dates.begin(), data_.pinned_dates.end(),
                       [&hidden](const std::string& date) {
                           return std::find(hidden.begin(), hidden.end(), date) != hidden.end();
                       }),
        data_.pinned_dates.end());
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_pinned_dates(std::vector<std::string> dates) {
    AppData before = data_;
    const QString before_date = current_date_;
    data_.pinned_dates = std::move(dates);
    std::sort(data_.pinned_dates.begin(), data_.pinned_dates.end());
    data_.pinned_dates.erase(std::unique(data_.pinned_dates.begin(), data_.pinned_dates.end()),
                             data_.pinned_dates.end());
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::delete_day(const QString& date) {
    const auto it = data_.days.find(from_qstring(date));
    if (it == data_.days.end()) {
        return;
    }
    AppData before = data_;
    const QString before_date = current_date_;
    data_.days.erase(it);
    if (current_date_ == date) {
        current_date_ = to_qstring(effective_today());
    }
    record_history(std::move(before), before_date);
    mark_dirty();
}

void AppController::update_local_settings(const LocalSettings& settings) {
    local_settings_ = settings;
    dhw::save_local_settings(from_qstring(data_dir_), local_settings_);
    emit appearance_changed();
}

void AppController::record_history(AppData before, const QString& before_date) {
    if (before == data_) {
        return;
    }
    // A fresh edit makes the redo branch unreachable, exactly like a text editor.
    redo_stack_.clear();
    undo_stack_.push_back(HistoryEntry{std::move(before), before_date});
    if (undo_stack_.size() > kMaxUndoSteps) {
        undo_stack_.erase(undo_stack_.begin());
    }
    emit_undo_state();
}

void AppController::restore_history(HistoryEntry entry) {
    data_ = std::move(entry.data);
    if (!entry.date.isEmpty()) {
        current_date_ = entry.date;
    }
    emit_undo_state();
    // Same tail as a normal edit: dirty flag, debounced save and a full view refresh.
    mark_dirty();
}

void AppController::emit_undo_state() {
    emit undo_state_changed(can_undo(), can_redo());
}

void AppController::undo() {
    if (undo_stack_.empty()) {
        return;
    }
    HistoryEntry previous = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    redo_stack_.push_back(HistoryEntry{data_, current_date_});
    restore_history(std::move(previous));
}

void AppController::redo() {
    if (redo_stack_.empty()) {
        return;
    }
    HistoryEntry next = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    undo_stack_.push_back(HistoryEntry{data_, current_date_});
    restore_history(std::move(next));
}

void AppController::mark_dirty() {
    dirty_ = true;
    save_timer_.start();
    emit changed();
}

void AppController::save_now() {
    save_timer_.stop();
    if (!fs::is_directory(from_qstring(data_dir_))) {
        fs::create_directories(from_qstring(data_dir_));
    }
    if (dhw::save(from_qstring(data_dir_), data_)) {
        dirty_ = false;
        save_failed_ = false;
        emit saved();
        emit changed();
        return;
    }
    save_failed_ = true;
    last_error_ = QStringLiteral("保存失败：%1").arg(data_dir_);
    emit save_failed_signal(last_error_);
    emit changed();
}

void AppController::on_save_timer() {
    if (dirty_) {
        save_now();
    }
}

void AppController::on_image_timer() {
    if (dirty_) {
        return;
    }
    export_today_png();
}

void AppController::start_timers() {
    image_timer_.start();
}

void AppController::stop_timers() {
    save_timer_.stop();
    image_timer_.stop();
}

QString AppController::executable_dir_path() const {
    return to_qstring(fs::executable_dir());
}

QString AppController::image_path_for(const QString& page_date) const {
    return QDir(executable_dir_path()).filePath(to_qstring(image_file_name(from_qstring(page_date))));
}

QImage AppController::render_current_png(double pixel_ratio) const {
    const QtTextMetrics metrics;
    const ViewModel view = view_model();
    std::vector<QImage> images;
    LayoutOptions options;
    options.style = layout_style_;
    const QImage badge = badge_image();
    if (!badge.isNull() && show_badge_effective()) {
        images.push_back(badge);
        options.badge_image_id = 0;
        options.show_badge = true;
    }
    const LayoutResult result = layout_presentation(view, metrics, options);
    return render_commands(result.commands, static_cast<int>(layout_geometry::kWidth),
                           static_cast<int>(layout_geometry::kHeight), images, metrics,
                           pixel_ratio);
}

QImage AppController::render_today_png(double pixel_ratio) const {
    const QtTextMetrics metrics;
    const ViewModel view = build_view_model(data_, effective_today(), false);
    std::vector<QImage> images;
    LayoutOptions options;
    options.style = layout_style_;
    const QImage badge = badge_image();
    if (!badge.isNull() && show_badge_effective()) {
        images.push_back(badge);
        options.badge_image_id = 0;
        options.show_badge = true;
    }
    const LayoutResult result = layout_presentation(view, metrics, options);
    return render_commands(result.commands, static_cast<int>(layout_geometry::kWidth),
                           static_cast<int>(layout_geometry::kHeight), images, metrics,
                           pixel_ratio);
}

QString AppController::export_current_png() {
    const QImage image = render_current_png();
    const QString path = image_path_for(current_date_);
    if (!image.save(path, "PNG")) {
        last_error_ = QStringLiteral("出图失败：%1").arg(path);
        emit save_failed_signal(last_error_);
        return QString();
    }
    return path;
}

QString AppController::export_today_png() {
    const QImage image = render_today_png();
    const QString path = image_path_for(to_qstring(effective_today()));
    if (!image.save(path, "PNG")) {
        return QString();
    }
    return path;
}

void AppController::archive_images() {
    const QString archive = QDir(data_dir_).filePath(QStringLiteral("images"));
    dhw::archive_stale_images(from_qstring(executable_dir_path()), from_qstring(archive),
                         today_text(), effective_today(),
                         static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch()));
    emit changed();
}

void AppController::clear_images() {
    const QString archive = QDir(data_dir_).filePath(QStringLiteral("images"));
    dhw::clear_images(from_qstring(executable_dir_path()), from_qstring(archive), today_text(),
                      effective_today());
    emit changed();
}

QString AppController::badge_path() const {
    return to_qstring(dhw::badge_file(from_qstring(data_dir_)));
}

bool AppController::reload_layout_style() {
    const StyleLoad loaded = load_style_file(fs::join(from_qstring(data_dir_), kStyleFileName));
    layout_style_ = loaded.style;
    emit changed();
    return loaded.loaded;
}

QImage AppController::badge_image() const {
    QImage image(badge_path());
    return image;
}

bool AppController::badge_available() const {
    const QImage image = badge_image();
    if (image.isNull()) {
        return false;
    }
    return !ink_bounds(image).isNull();
}

bool AppController::show_badge_effective() const {
    return data_.settings.show_badge && badge_available();
}

bool AppController::store_badge(const QImage& image) {
    if (image.isNull()) {
        return false;
    }
    if (!fs::create_directories(from_qstring(data_dir_))) {
        return false;
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return false;
    }
    const std::vector<std::uint8_t> png(bytes.begin(), bytes.end());
    return dhw::write_badge(from_qstring(data_dir_), png);
}

}  // namespace dhw::qt

#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>
#include <string>
#include <vector>

#include "dhw/model.h"
#include "dhw/schedule.h"
#include "dhw/store.h"
#include "dhw/style.h"

namespace dhw::qt {

// Single source of truth of the desktop shell: owns the data, the data directory,
// the page day and the edit mode, and persists with a debounce. Every view talks
// to this class only, so the domain stays in core and the widgets stay dumb.
class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QString data_dir, QObject* parent = nullptr);

    // --- state -----------------------------------------------------------
    const AppData& data() const { return data_; }
    const QString& data_dir() const { return data_dir_; }
    const QString& current_date() const { return current_date_; }
    bool submit_only() const { return submit_only_; }
    bool show_badge_effective() const;
    bool dirty() const { return dirty_; }
    LocalSettings local_settings() const { return local_settings_; }
    LayoutStyle layout_style() const { return layout_style_; }
    bool save_failed() const { return save_failed_; }
    QString last_error() const { return last_error_; }
    bool recovered_from_backup() const { return recovered_from_backup_; }

    // --- derived views ---------------------------------------------------
    ViewModel view_model() const;
    std::vector<std::vector<Slot>> week(const QString& monday) const;
    std::string effective_today() const;
    std::string next_school_day() const;
    std::vector<std::string> navigation_dates() const;
    std::string launch_date() const;

    // --- editing (each one marks the state dirty and schedules a save) ----
    void set_current_date(const QString& date);
    void set_submit_only(bool value);
    void update_day(const Day& day);
    void update_announcements(std::vector<Announcement> items);
    void update_settings(const Settings& settings);
    void update_weekly_schedule(std::vector<std::vector<std::string>> rows);
    void update_hidden_dates(std::vector<std::string> dates);
    void update_pinned_dates(std::vector<std::string> dates);
    void delete_day(const QString& date);
    void update_local_settings(const LocalSettings& settings);

    void mark_dirty();
    void save_now();

    // --- undo / redo ------------------------------------------------------
    // Every mutator above records the state it is about to replace, so one undo step
    // is one committed edit. The history holds whole-dataset snapshots, is bounded to
    // kMaxUndoSteps entries and is never written to disk: it dies with this object.
    // A no-op update records nothing, so the editor's 300 ms draft commit cannot flood
    // the history. undo()/redo() also restore the page day of the recorded edit.
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    int undo_depth() const { return static_cast<int>(undo_stack_.size()); }
    int redo_depth() const { return static_cast<int>(redo_stack_.size()); }
    void undo();
    void redo();

    // --- images ----------------------------------------------------------
    // 1920x1080 PNG of the page day in the current mode (manual export).
    QImage render_current_png() const;
    QImage render_today_png() const;
    QString image_path_for(const QString& page_date) const;
    // Writes the manual export next to the executable and returns its path.
    QString export_current_png();
    // Automatic export: today, all mode.
    QString export_today_png();
    // Moves stale exports into "<data dir>/images".
    void archive_images();
    void clear_images();

    // --- badge -----------------------------------------------------------
    QImage badge_image() const;
    bool badge_available() const;
    bool store_badge(const QImage& image);
    QString badge_path() const;

    // --- style -----------------------------------------------------------
    // Re-reads "<data dir>/style.json"; defaults apply when it is missing.
    bool reload_layout_style();

    // --- lifecycle -------------------------------------------------------
    void start_timers();
    void stop_timers();

signals:
    void changed();          // data or view state changed
    void saved();            // a save completed
    void save_failed_signal(const QString& message);
    void appearance_changed();
    // Both flags after every history mutation, so a toolbar can mirror the availability
    // of its undo / redo actions without polling.
    void undo_state_changed(bool can_undo, bool can_redo);

private slots:
    void on_save_timer();
    void on_image_timer();

private:
    // One undoable state: the whole dataset plus the page day the edit was made on, so
    // undoing a deletion brings the deleted day back into view.
    struct HistoryEntry {
        AppData data;
        QString date;
    };

    void apply_launch_view();
    QString executable_dir_path() const;
    std::string today_text() const;

    // Pushes `before` unless the mutation left the dataset untouched, drops the redo
    // branch and announces the new availability. Callers still mark the state dirty.
    void record_history(AppData before, const QString& before_date);
    void restore_history(HistoryEntry entry);
    void emit_undo_state();

    QString data_dir_;
    QString current_date_;
    AppData data_;
    LocalSettings local_settings_;
    LayoutStyle layout_style_;
    bool submit_only_ = false;
    bool dirty_ = false;
    bool save_failed_ = false;
    bool recovered_from_backup_ = false;
    QString last_error_;
    QTimer save_timer_;
    QTimer image_timer_;
    std::vector<HistoryEntry> undo_stack_;
    std::vector<HistoryEntry> redo_stack_;
};

}  // namespace dhw::qt

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

#include "app_controller.h"
#include "dhw/model.h"

class QCheckBox;
class QComboBox;
class QCompleter;
class QEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QShortcut;
class QSpacerItem;
class QTimer;
class QVBoxLayout;

namespace dhw::qt {

class EditorCard;
class PreviewLabel;

// The edit page: four cards (announcement / homework / schedule / speech) wired to the
// controller. Text edits update a local draft at once and reach the controller through a
// 300 ms debounce, so typing never rebuilds the row being edited.
//
// Rows follow the spec: a card renders only the entries that carry real content plus
// exactly one trailing ghost row, and blank entries never reach the controller.
class EditorPage : public QWidget {
    Q_OBJECT

public:
    explicit EditorPage(AppController* controller, QWidget* parent = nullptr);

    void reload_all();
    void refresh_cards();
    void commit_pending();
    QString current_date() const { return current_date_; }
    QLineEdit* announcement_new_input() const { return announcement_ghost_; }
    QLineEdit* first_homework_input() const;
    QLineEdit* first_speech_input() const;
    QLineEdit* first_schedule_input() const;
    // Headless helper for the smoke run: routes one new homework entry through the
    // same draft and commit path the widgets use, so both stay in sync.
    bool smoke_add_homework(const QString& subject, const QString& text);

protected:
    // Row editors claim Cmd+Z / Cmd+Shift+Z for their own text history, which would
    // rewrite the visible text without telling the draft. The edit page takes those keys
    // back and routes them to the controller's history.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The skin's own row emphasis ("已完成" strike through, the semantic red of a swapped
    // period) sits on the editor font and palette. A widget from the Fluent family
    // re-derives both from its design tokens, so a light/dark or display-size change has
    // to re-apply them from the draft.
    void changeEvent(QEvent* event) override;

private slots:
    void on_controller_changed();
    void on_debounce();
    void undo_edit();
    void redo_edit();

private:
    void build_cards();
    void rebuild_cards();
    void rebuild_announcement_rows();
    void rebuild_homework_rows();
    void rebuild_schedule_rows();
    void rebuild_speech_rows();
    void refresh_badges();
    void refresh_speech_warning();
    // Pins every editable row (announcement, homework, schedule, speech) to one height
    // measured from the tallest control the current skin and font produce, so the left
    // subject combo and the right text field are visually equal and the whole page keeps
    // one vertical rhythm.
    void sync_row_heights();
    // Re-applies the announcement checkbox gap after a display-size change; the spacer
    // items are tracked because the rows themselves are not rebuilt on a font change.
    void refresh_announcement_gaps();
    void commit(bool immediate);
    void flush_pending();

    // Undo / redo. The shortcuts belong to this page (WidgetWithChildrenShortcut), so
    // they only fire while the keyboard is inside the edit page.
    void install_history_shortcuts();
    bool page_has_focus() const;
    void restore_edit_focus();

    // Live preview of the whole 1920x1080 slide. Renders through the controller's
    // existing renderer and only after the edit debounce has settled.
    void schedule_preview_refresh();
    void refresh_preview();
    void export_preview();
    void open_preview_zoom();
    QString preview_file_for_drag();

    // Re-applies the row emphasis the skin may have reset (see changeEvent).
    void refresh_row_emphasis();

    void create_announcement_row(int index);
    void create_announcement_ghost();
    void create_homework_row(int draft_index, int display_index);
    void create_homework_ghost();
    void create_speech_row(int draft_index);
    void create_speech_ghost();
    void create_schedule_row(int period);
    void create_empty_state(QVBoxLayout* page, const QString& text);

    void add_announcement(const QString& text);
    void add_homework_entry(int subject_index, const QString& text, bool submit_next = false);
    void add_speech_entry(const QString& subject, const QString& students);
    void remove_homework(int draft_index);
    void remove_speech(int draft_index);
    void move_homework(int draft_index, int delta);
    void move_speech(int draft_index, int delta);
    void move_announcement(long long id, int delta);
    void focus_announcement_ghost();
    void submit_homework_ghost();
    void submit_speech_ghost();
    // Enter / plus in a real row hands the keyboard over to the single ghost row with
    // that subject preselected, because an empty row is no longer rendered.
    void focus_homework_ghost(const QString& subject);
    void focus_speech_ghost(const QString& subject);
    void open_schedule_dialog();

    QString subject_of(int subject_index) const;
    QStringList unused_homework_subjects() const;
    QStringList unused_speech_subjects() const;
    QString default_homework_ghost_subject() const;
    QString default_speech_ghost_subject() const;
    void refresh_completer_subjects();
    // Drops blank homework / speech entries from the copy handed to the controller.
    dhw::Day pruned_draft() const;

    AppController* controller_ = nullptr;
    QTimer* debounce_ = nullptr;
    // One entry per standard key binding of QKeySequence::Undo / Redo, plus the
    // Ctrl+Shift+Z alias Windows needs (Qt maps Redo to Ctrl+Y there).
    std::vector<QShortcut*> undo_shortcuts_;
    std::vector<QShortcut*> redo_shortcuts_;
    QString current_date_;
    dhw::Day draft_;
    bool draft_ready_ = false;
    bool applying_ = false;

    EditorCard* announcement_card_ = nullptr;
    EditorCard* homework_card_ = nullptr;
    EditorCard* schedule_card_ = nullptr;
    EditorCard* speech_card_ = nullptr;

    QVBoxLayout* announcement_page_ = nullptr;
    QVBoxLayout* homework_page_ = nullptr;
    QGridLayout* schedule_grid_ = nullptr;
    QVBoxLayout* speech_page_ = nullptr;

    QWidget* speech_warning_ = nullptr;

    // Right-hand live preview. The label is a QLabel subclass in editor_page.cpp that
    // scales the slide and starts a QDrag with a real PNG path.
    EditorCard* preview_card_ = nullptr;
    PreviewLabel* preview_ = nullptr;
    QLabel* preview_status_ = nullptr;
    QTimer* preview_timer_ = nullptr;

    QScrollArea* announcement_scroll_ = nullptr;
    QScrollArea* homework_scroll_ = nullptr;
    QScrollArea* schedule_scroll_ = nullptr;
    QScrollArea* speech_scroll_ = nullptr;

    QStringList homework_suggestions_;
    QStringList speech_suggestions_;
    // Model for the subject completer: current-data subjects plus the fixed school list.
    QStringList completer_subjects_;
    std::vector<QString> subjects_;
    // Rendered homework rows: the order list feeds first_homework_input(), the hash maps
    // a draft index to its widget so focus can follow a freshly added entry.
    std::vector<QLineEdit*> homework_input_order_;
    QHash<int, QLineEdit*> homework_inputs_;
    QLineEdit* homework_ghost_input_ = nullptr;
    QComboBox* homework_ghost_subject_ = nullptr;
    QCheckBox* homework_ghost_submit_ = nullptr;
    QString homework_ghost_subject_text_;
    bool homework_ghost_submit_state_ = false;
    // Announcement row editors in storage order, so a skin change can re-apply the
    // "done" strike through without rebuilding the rows (see refresh_row_emphasis).
    std::vector<QLineEdit*> announcement_inputs_;
    std::vector<QSpacerItem*> announcement_gaps_;
    std::vector<QLineEdit*> speech_inputs_;
    QHash<QString, QLineEdit*> speech_inputs_by_subject_;
    QLineEdit* speech_ghost_ = nullptr;
    QComboBox* speech_ghost_subject_ = nullptr;
    QString speech_ghost_subject_text_;
    QLineEdit* announcement_ghost_ = nullptr;
    std::vector<QLineEdit*> schedule_inputs_;
    QStringList template_subjects_;
};

}  // namespace dhw::qt

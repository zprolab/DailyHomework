#pragma once

#include <QString>
#include <QWidget>
#include <vector>

#include "app_controller.h"
#include "dhw/store.h"
#include "fluent_style.h"

class QComboBox;
class QFormLayout;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QSlider;
class QStandardItemModel;
class QTableWidget;
class QVBoxLayout;

#ifdef DHW_UI_FLUENT
namespace fluent::status_info {
class InfoBar;
}
#endif

namespace dhw::qt {

// The settings page: two form style columns that fold into one when the window is
// narrow, with the general and data groups on the left and the presentation and day
// management groups on the right; the shortcut list and the about box fill the last
// row. Every change is written through the controller at once, so the page has no
// save button. The day management group edits the hidden date list, the swapped day
// source and the pinned (quick show) dates; its list is a Fluent ListView of row
// widgets (a QTableWidget in the native build).
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(AppController* controller, QWidget* parent = nullptr);

    // Re-reads every value from the controller; used after an external change.
    void refresh();

    // Headless helper used by the startup page switch and the self check.
    bool set_morning_cutoff(const QString& text);
    bool set_ui_scale(int percent);
    QString morning_cutoff_text() const;
    int ui_scale_percent() const;

signals:
    // The 52 px page header owns the hamburger and the back button; the shell performs
    // the actual sidebar toggle and page switch.
    void sidebar_toggle_requested();
    void back_requested();
    void theme_preview_requested(double scale);
    void toast_requested(const QString& text);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void on_controller_changed();

private:
    void build_header(QVBoxLayout* root);
    void build_scroll_body(QVBoxLayout* root);
    SectionCard* build_general_card();
    SectionCard* build_data_card();
    SectionCard* build_present_card();
    SectionCard* build_days_card();
    SectionCard* build_shortcuts_card();
    SectionCard* build_about_card();

    // Adds an empty QFormLayout to a group box body and returns it. Every row is a
    // right aligned label plus a field whose control is followed by the help text.
    QFormLayout* make_form(SectionCard* card);
    void add_form_row(QFormLayout* form, const QString& label, QWidget* control,
                      const QString& help);

    // Places the six group boxes in two columns or one, depending on the page width.
    void apply_columns(bool two_columns);
    // Page width below which the two columns no longer fit. The display size scales it,
    // so a 150% page folds at a wider window instead of squeezing the form.
    int two_column_min_width() const;

    void refresh_general();
    void refresh_days();
    void rebuild_day_table();
    void update_day_table_height();
    void refresh_footer_bar();
    void update_badge_preview();
    int day_row_height() const;

    void save_settings(const dhw::Settings& settings, const QString& toast_text);
    void open_data_dir();
    void copy_data_dir();
    void export_image();
    void commit_morning_cutoff();
    void preview_scale(int percent);
    void commit_scale(int percent);
    void reset_scale();
    void clear_images();
    void upload_badge();
    void remove_badge();
    void confirm_remove_date(const QString& date);

    std::vector<QString> managed_dates() const;
    bool date_has_content(const QString& date) const;
    bool date_pinned(const QString& date) const;
    bool date_hidden(const QString& date) const;
    bool in_bar_by_default(const QString& date) const;
    void set_date_pinned(const QString& date, bool pinned);
    void remove_date(const QString& date);
    void add_date();
    void set_copied_weekday(const QString& date, int weekday);

    AppController* controller_ = nullptr;
    bool refreshing_ = false;
    bool two_columns_ = false;
    bool refresh_pending_ = false;

    ToggleControl* sidebar_default_ = nullptr;
    QLineEdit* data_dir_text_ = nullptr;
    NumberControl* interval_spin_ = nullptr;
    TimeControl* morning_cutoff_ = nullptr;
    ToggleControl* show_badge_check_ = nullptr;
    QLabel* show_badge_hint_ = nullptr;
    QLabel* badge_status_ = nullptr;
    QLabel* badge_preview_ = nullptr;
    ActionControl* badge_upload_ = nullptr;
    ActionControl* badge_delete_ = nullptr;
    ToggleControl* english_split_ = nullptr;
    QSlider* scale_slider_ = nullptr;
    QLabel* scale_title_ = nullptr;
    QPushButton* scale_reset_ = nullptr;

    DateControl* new_date_ = nullptr;
    QComboBox* new_source_ = nullptr;
    ActionControl* new_button_ = nullptr;
#ifdef DHW_UI_FLUENT
    fluent::collections::ListView* day_list_ = nullptr;
    QStandardItemModel* day_model_ = nullptr;
#else
    QTableWidget* day_table_ = nullptr;
#endif
    QLabel* hidden_count_ = nullptr;
    ActionControl* restore_button_ = nullptr;
    QString day_signature_;

    SectionCard* general_card_ = nullptr;
    SectionCard* data_card_ = nullptr;
    SectionCard* present_card_ = nullptr;
    SectionCard* days_card_ = nullptr;
    SectionCard* shortcuts_card_ = nullptr;
    SectionCard* about_card_ = nullptr;
    QGridLayout* grid_ = nullptr;
#ifdef DHW_UI_FLUENT
    // Page level Fluent InfoBar: an informational hint that turns into the badge warning.
    fluent::status_info::InfoBar* notice_ = nullptr;
#endif
    // One slot per group, in grid reading order. A slot fills its grid cell and keeps
    // the group box at its natural height, which a plain widget cell would stretch.
    QWidget* slots_[6] = {};
};

}  // namespace dhw::qt

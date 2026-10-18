#pragma once

#include <QMainWindow>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QToolButton>

#include "app_controller.h"
#include "fluent_style.h"
#include "shell_widgets.h"

class QAction;
class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QStandardItemModel;

namespace dhw::qt {

class EditorPage;
class CreditsPage;
class PresentWindow;
class SettingsPage;
class WeekPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);

    EditorPage* editor() const { return editor_; }
    SettingsPage* settings_page() const { return settings_; }
    WeekPage* week_page() const { return week_; }
    CreditsPage* credits_page() const { return credits_; }
    void set_current_date(const QString& date);
    void show_page(const QString& page);
    void toggle_sidebar();
    void enter_present();
    void exit_present();
    bool presenting() const { return presenting_; }
    void toast(const QString& message);
    void apply_theme();
    void navigate_day(int delta);
    void jump_to_today();
    void export_image();
    void save_now();
    void quit_app();
    // Deterministic edit used by the headless smoke run: adds one homework entry to the
    // current day (never twice) and writes it through the controller.
    bool smoke_edit(const QString& subject, const QString& text, bool submit_next);
    // Headless interaction walk-through: exercises the editor commit path, the date bar,
    // the page switching and the presentation toggle, printing one PASS/FAIL line each.
    int run_selfcheck();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void on_controller_changed();
    void on_save_failed(const QString& message);
    void on_toggle_sidebar();
    void on_page_clicked(int index);
    void on_date_clicked(const QString& date);
    void on_segment_clicked(int index);
    void on_previous_clicked();
    void on_next_clicked();
    void on_today_clicked();
    void on_present_clicked();
    void on_image_clicked();
    void on_quit_clicked();
    void on_appearance_changed();

private:
    void build_shell();
    QWidget* build_sidebar();
    QWidget* build_toolbar();
    void rebuild_date_bar();
    // Re-applies size, fonts and palette to the existing date chips: the page day can
    // move inside the same day list, and a display size or appearance change has to
    // reach the chips without rebuilding their buttons.
    void refresh_date_chips();
    // Re-tints the one destructive action from the live palette.
    void refresh_toolbar_actions();
    void refresh_state();
    void refresh_footer();
    void refresh_theme_dependent();
    void refresh_nav_icons();
    void set_present_action_enabled(bool enabled);
    void install_shortcuts();
    bool focus_accepts_text() const;
    void set_sidebar_collapsed(bool collapsed);
    // Compact rail vs expanded column, applied to the sidebar that already exists.
    void apply_sidebar_mode();
    // Re-derives the shell's fixed pixel metrics (pane widths, toolbar height, chip
    // padding, icon sides) from the live display size.
    void apply_shell_metrics();
    void sync_nav_selection();
    void update_date_strip_overflow();
    void scroll_date_strip(int direction);
    void ensure_current_chip_visible();
    // WinUI in-app notification tone. The native skin ignores it and keeps the status
    // bar message, so call sites stay free of the Fluent headers.
    enum class ToastTone { Info, Success, Error };
    void show_toast(const QString& message, ToastTone tone);
    QString folder_name() const;
    void toggled_present_placeholder();
    void focus_page_section(int section);

    AppController* controller_ = nullptr;
    EditorPage* editor_ = nullptr;
    SettingsPage* settings_ = nullptr;
    WeekPage* week_ = nullptr;
    CreditsPage* credits_ = nullptr;
    PresentWindow* present_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QWidget* page_root_ = nullptr;
    QScrollArea* date_scroll_ = nullptr;
    QHBoxLayout* date_layout_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* footer_text_ = nullptr;
#ifdef DHW_UI_FLUENT
    QWidget* content_ = nullptr;
    fluent::navigation::NavigationView* navigation_ = nullptr;
    fluent::collections::ListView* nav_list_ = nullptr;
    QStandardItemModel* nav_model_ = nullptr;
    fluent::status_info::InfoBadge* footer_badge_ = nullptr;
    fluent::menus_toolbars::CommandBar* command_bar_ = nullptr;
    fluent::menus_toolbars::CommandBar* quit_bar_ = nullptr;
    // Expanded column only.
    QLabel* brand_label_ = nullptr;
    QLabel* section_label_ = nullptr;
    QVBoxLayout* sidebar_layout_ = nullptr;
    QHBoxLayout* footer_layout_ = nullptr;
    // Fluent compact mode: the narrow icon rail shown while the pane is collapsed.
    QWidget* sidebar_rail_ = nullptr;
    QList<ActionControl*> nav_rail_buttons_;
#else
    QLabel* footer_dot_ = nullptr;
    QToolButton* nav_edit_ = nullptr;
    QToolButton* nav_week_ = nullptr;
    QToolButton* nav_settings_ = nullptr;
    QToolButton* nav_credits_ = nullptr;
#endif
    QHBoxLayout* toolbar_layout_ = nullptr;
    ActionControl* previous_ = nullptr;
    ActionControl* next_ = nullptr;
    ActionControl* hamburger_ = nullptr;
    // Date strip overflow arrows: hidden while every chip fits.
    ActionControl* strip_left_ = nullptr;
    ActionControl* strip_right_ = nullptr;
#ifdef DHW_UI_FLUENT
    QAction* today_action_ = nullptr;
    QAction* present_action_ = nullptr;
    QAction* image_action_ = nullptr;
    QAction* quit_action_ = nullptr;
#else
    ActionControl* today_button_ = nullptr;
    ActionControl* present_button_ = nullptr;
    ActionControl* image_button_ = nullptr;
    ActionControl* quit_button_ = nullptr;
#endif
    QComboBox* segments_ = nullptr;
    QStringList chip_dates_;
    QList<ActionControl*> date_chips_;
    // Last state the chips were styled for; every controller change calls the refresh,
    // and a keystroke commit must not restyle eight buttons. -1 forces a full restyle.
    QString chip_style_current_;
    QString chip_style_today_;
    QSize chip_style_size_;
    int chip_style_count_ = -1;
    QStringList nav_cache_;
    QString current_date_;
    QString appearance_;
    bool sidebar_collapsed_ = false;
    bool updating_segments_ = false;
    bool quitting_ = false;
    bool presenting_ = false;
    bool updating_nav_ = false;
};

}  // namespace dhw::qt

#pragma once

#include <QString>
#include <QWidget>

#include "app_controller.h"
#include "fluent_style.h"

class QComboBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QModelIndex;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QVBoxLayout;

namespace dhw::qt {

class WeekCard;

// The week overview page: one 7 x 9 table of this week's effective schedule. Today's
// column is tinted from the live palette, swapped periods are marked with the warning
// glyph, the semantic red and a tooltip that names the original lesson. The day selector
// in the toolbar picks which day a double click edits (that day's schedule override,
// written through AppController), and the template button switches the same table into
// weekly-template edit mode, whose save writes the whole grid through the controller.
//
// The view is the Fluent DataGrid (a QTableView with token-painted cells, header and
// border); the native build keeps a plain QTableView. Both read the same
// QStandardItemModel, so the cell roles, the edit path and the template draft are one
// implementation and only the chrome differs.
class WeekPage : public QWidget {
    Q_OBJECT

public:
    explicit WeekPage(AppController* controller, QWidget* parent = nullptr);

    // Re-reads the week and the template from the controller.
    void refresh();
    // Monday of the week that contains the page day.
    QString monday() const;
    bool editing_template() const { return editing_; }
    // Opens the template editor (the header button and the screenshot harness use it).
    void begin_template_edit();

signals:
    void sidebar_toggle_requested();
    void back_requested();
    void toast_requested(const QString& text);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void on_controller_changed();

private:
    void build_header(QVBoxLayout* root);
    void build_body(QVBoxLayout* root);
    void build_toolbar(QHBoxLayout* layout);
    void configure_table();
    // Sizes the grid to exactly its header plus its nine fixed rows, so the table never
    // stretches into empty cells below the last period and the card hugs its content.
    void fit_table_height();
    // Rebuilds the "today" header tint (the Fluent header paints its own background, so
    // the tint is an overlay it cannot paint over).
    void update_header_tint();
    void rebuild_overview();
    void rebuild_template();
    void rebuild_day_combo();
    void apply_mode_ui();
    void update_corner_label();
    void cancel_template_edit();
    void save_template();
    void jump_to_today();
    void on_day_selected(int index);
    void on_cell_double_clicked(const QModelIndex& index);
    void on_item_changed(QStandardItem* item);
    void on_current_changed(const QModelIndex& current, const QModelIndex& previous);
    void open_cell_editor(int row, int column);
    void apply_override(const QString& date, int period, const QString& text);
    void schedule_refresh();
    void focus_date_column(const QString& date, bool focus_table);
    void refresh_headers();
    QString week_date(int column) const;
    QString template_text_for(const QString& date, int period) const;

    AppController* controller_ = nullptr;
    WeekCard* card_ = nullptr;
    QTableView* table_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    // Overlay that tints today's header section; only the Fluent grid needs it (the
    // native header honours the model's BackgroundRole). Kept as QWidget* so the skin
    // specific class stays in the .cpp.
    QWidget* header_tint_ = nullptr;
    QComboBox* day_combo_ = nullptr;
    QLabel* week_hint_ = nullptr;
    QLabel* mode_hint_ = nullptr;
    QLabel* legend_ = nullptr;
    QLabel* corner_label_ = nullptr;
    ActionControl* edit_button_ = nullptr;
    ActionControl* today_button_ = nullptr;
    ActionControl* cancel_button_ = nullptr;
    ActionControl* save_button_ = nullptr;
    bool editing_ = false;
    bool rebuilding_ = false;
    bool suppress_refresh_ = false;
    bool refresh_scheduled_ = false;
    int last_row_ = -1;
    int last_column_ = -1;
};

}  // namespace dhw::qt

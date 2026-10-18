#include "week_page.h"

#include <QAbstractButton>
#include <QAbstractItemDelegate>
#include <QAbstractItemModel>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemEditorFactory>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include "dhw/datetime.h"
#include "dhw/model.h"
#include "dhw/schedule.h"
#include "shell_widgets.h"
#include "theme.h"

#ifdef DHW_UI_FLUENT
#include <FluentQt/BasicInput.h>
#include <FluentQt/Collections.h>
#include <FluentQt/Design.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#endif

namespace dhw::qt {

// ---------------------------------------------------------------------------
// Section card of the week page: body_layout() plus the two live texts the mode switch
// writes. The native build keeps the platform QGroupBox from shell_widgets.h, whose
// title and badge already work this way; the Fluent build paints a Fluent Card with the
// heading inside it, like fluent_style.h's SectionCard, but with a mutable title.
// ---------------------------------------------------------------------------
#ifdef DHW_UI_FLUENT
class WeekCard : public QWidget {
public:
    WeekCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
             QWidget* parent = nullptr);

    QVBoxLayout* body_layout() const { return body_layout_; }
    void set_title(const QString& title);
    void set_badge(const QString& text);

private:
    void refresh_title();

    QString title_text_;
    QString badge_text_;
    fluent::textfields::Label* title_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
};

WeekCard::WeekCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
                   QWidget* parent)
    : QWidget(parent), title_text_(title) {
    Q_UNUSED(icon);
    auto* host = new QVBoxLayout(this);
    host->setContentsMargins(0, 0, 0, 0);
    host->setSpacing(0);

    auto* surface = new fluent::layout::Card(this);
    surface->setAppearance(fluent::layout::Card::Layer);
    surface->setBorderVisible(true);
    host->addWidget(surface);

    auto* column = new QVBoxLayout(surface);
    column->setContentsMargins(16, 12, 16, 14);
    column->setSpacing(8);

    title_ = new fluent::textfields::Label(title, surface);
    title_->setFluentTypography(Typography::FontRole::BodyStrong);
    column->addWidget(title_);

    if (!hint.isEmpty()) {
        auto* hint_label = new InfoLabel(hint, surface);
        hint_label->setWordWrap(true);
        hint_label->setToolTip(hint);
        column->addWidget(hint_label);
    }

    auto* body = new QWidget(surface);
    body_layout_ = new QVBoxLayout(body);
    body_layout_->setContentsMargins(0, 0, 0, 0);
    body_layout_->setSpacing(6);
    column->addWidget(body, 1);
}

void WeekCard::set_title(const QString& title) {
    title_text_ = title;
    refresh_title();
}

void WeekCard::set_badge(const QString& text) {
    badge_text_ = text;
    refresh_title();
}

void WeekCard::refresh_title() {
    if (title_ == nullptr) {
        return;
    }
    const QString suffix =
        badge_text_.isEmpty() ? QString() : QStringLiteral(" · ") + badge_text_;
    title_->setText(title_text_ + suffix);
}

#else  // !DHW_UI_FLUENT

class WeekCard : public Card {
public:
    using Card::Card;
};

#endif  // DHW_UI_FLUENT

namespace {

constexpr int kPeriods = dhw::kPeriods;
constexpr int kWeekdays = dhw::kWeekdays;
constexpr int kRowHeight = 42;
constexpr int kRowHeaderWidth = 64;
constexpr int kColumnMinWidth = 72;
constexpr int kWarningIconSide = 16;

std::string to_std(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString from_std(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString placeholder_text() {
    return QStringLiteral("（空）");
}

QString weekday_name(int index) {
    static const QStringList names = {QStringLiteral("周一"), QStringLiteral("周二"),
                                      QStringLiteral("周三"), QStringLiteral("周四"),
                                      QStringLiteral("周五"), QStringLiteral("周六"),
                                      QStringLiteral("周日")};
    return names.at(std::clamp(index, 0, kWeekdays - 1));
}

// "MM-DD" of a date text; unparseable input yields an empty string.
QString short_date(const QString& date) {
    const std::optional<dhw::Date> parsed = dhw::parse_date(to_std(date));
    if (!parsed.has_value()) {
        return QString();
    }
    return QStringLiteral("%1-%2")
        .arg(parsed->month, 2, 10, QLatin1Char('0'))
        .arg(parsed->day, 2, 10, QLatin1Char('0'));
}

// Today's column: the platform highlight blended into the platform base colour. Both
// ends come from the live QPalette, so a light/dark switch follows without a stylesheet
// rule. The blend is stronger on a dark palette because a highlight over a dark base
// otherwise disappears.
QColor soft_highlight(const QPalette& palette) {
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor base = palette.color(QPalette::Base);
    if (!highlight.isValid()) {
        return base;
    }
    const double alpha = base.lightness() < 128 ? 0.30 : 0.15;
    return QColor::fromRgbF(highlight.redF() * alpha + base.redF() * (1.0 - alpha),
                            highlight.greenF() * alpha + base.greenF() * (1.0 - alpha),
                            highlight.blueF() * alpha + base.blueF() * (1.0 - alpha));
}

// Assigns text to a label of either skin. The Fluent Label keeps the string it was built
// with and re-renders from that cache on every resize, so a plain QLabel-typed setText is
// undone by the next layout pass; routing every run time text change through the concrete
// type keeps the label's cache and its painted text in step.
void set_label_text(QLabel* label, const QString& text) {
    if (label == nullptr) {
        return;
    }
#ifdef DHW_UI_FLUENT
    if (auto* fluent_label = qobject_cast<fluent::textfields::Label*>(label)) {
        fluent_label->setText(text);
        return;
    }
#endif
    label->setText(text);
}

// Today's column tint. The platform palette is the source for both skins: the Fluent
// grid carries its own token palette whose Highlight is an internal hover grey, so
// reading the tint back from the view would paint today's column invisible.
QColor today_tint() {
    return soft_highlight(QApplication::palette());
}

// The warning marker of a swapped period. The Fluent build paints the library's own
// icon glyph, the native build asks the platform style for its standard warning icon;
// either way the mark is a real icon in the cell, not a hand drawn shape.
QPixmap warning_pixmap(int side) {
#ifdef DHW_UI_FLUENT
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // paintGlyph draws with the painter's own pen, so the semantic colour rides on it.
    painter.setPen(danger_color());
    Typography::Icons::paintGlyph(painter, QRectF(0, 0, side, side),
                                  Typography::Icons::Warning, side);
    painter.end();
    return pixmap;
#else
    const QStyle* widget_style = QApplication::style();
    if (widget_style == nullptr) {
        return QPixmap();
    }
    return widget_style->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(side, side);
#endif
}

// Does the active platform style draw the content of an item view cell? It is asked once,
// because it decides how the table below paints: the Fluent DataGrid paints its surface,
// its hover row, its dividers and its current-cell ring itself, but the cell *content* is
// drawn by QStyle::CE_ItemViewItem. QMacStyle off a native Cocoa screen (any offscreen or
// non-Cocoa run) paints nothing there, the same reason every style painted control of the
// old native page came out blank. On a healthy style the overlay below only adds the quiet
// placeholder of an empty cell; on a style that cannot draw item content it draws the
// content too, so the grid never depends on the host style for what it displays.
bool style_paints_item_text(QWidget* widget) {
    static const bool painted = [](QWidget* host) {
        if (host == nullptr) {
            return true;
        }
        QImage canvas(96, 28, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);
        QStyleOptionViewItem option;
        option.rect = canvas.rect().adjusted(4, 4, -4, -4);
        option.font = QApplication::font();
        option.state = QStyle::State_Enabled;
        option.text = QStringLiteral("Ww");
        option.displayAlignment = Qt::AlignCenter;
        option.palette.setColor(QPalette::Base, Qt::white);
        option.palette.setColor(QPalette::Text, Qt::black);
        option.palette.setColor(QPalette::WindowText, Qt::black);
        option.palette.setColor(QPalette::HighlightedText, Qt::black);
        option.widget = host;
        host->style()->drawControl(QStyle::CE_ItemViewItem, &option, &painter, host);
        painter.end();
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                if (qGray(canvas.pixel(x, y)) < 160) {
                    return true;
                }
            }
        }
        return false;
    }(widget);
    return painted;
}

// Draws one cell's icon and text the way the item style lays a centred group out, so the
// fallback above and a healthy platform look alike.
void paint_cell_content(QTableView* view, QPainter& painter, const QModelIndex& index,
                        const QRect& rect) {
    QAbstractItemModel* model = view->model();
    const QString text = model->data(index, Qt::DisplayRole).toString();
    const bool empty = text.isEmpty();

    QColor colour = view->palette().color(QPalette::Text);
    const QVariant foreground = model->data(index, Qt::ForegroundRole);
    if (foreground.canConvert<QBrush>()) {
        const QBrush brush = qvariant_cast<QBrush>(foreground);
        if (brush.style() != Qt::NoBrush) {
            colour = brush.color();
        }
    }
    QFont font = view->font();
    const QVariant font_role = model->data(index, Qt::FontRole);
    if (font_role.canConvert<QFont>()) {
        font = qvariant_cast<QFont>(font_role);
    }
    painter.setFont(font);
    painter.setPen(empty ? view->palette().color(QPalette::Disabled, QPalette::Text) : colour);

    QIcon icon;
    const QVariant decoration = model->data(index, Qt::DecorationRole);
    if (decoration.canConvert<QIcon>()) {
        icon = qvariant_cast<QIcon>(decoration);
    }
    const QRect content = rect.adjusted(6, 0, -6, 0);
    const QString shown = empty ? placeholder_text() : text;
    const QFontMetrics metrics(font);
    const int icon_side = icon.isNull() ? 0 : kWarningIconSide;
    const int gap = icon.isNull() ? 0 : 6;
    const int available = std::max(0, content.width() - icon_side - gap);
    const int wanted = metrics.horizontalAdvance(shown);
    const bool fits = wanted <= available;
    const int text_width = fits ? wanted : available;
    const int total = icon_side + gap + text_width;
    int x = content.center().x() - total / 2;
    if (!icon.isNull()) {
        icon.paint(&painter, x, content.center().y() - icon_side / 2, icon_side, icon_side);
        x += icon_side + gap;
    }
    const QRect text_rect(x, content.y(), text_width, content.height());
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     fits ? shown : metrics.elidedText(shown, Qt::ElideRight, text_width));
}

// The cell content pass the view below runs after the skin has painted its cells. It is
// not an item delegate on purpose: the Fluent DataGrid owns the cell painting (surface,
// hover row, dividers, current-cell ring, selection tint) in a delegate of its own that a
// page must not replace, so the content that delegate leaves to the platform style is
// drawn here instead.
void paint_cell_contents(QTableView* view) {
    if (view == nullptr || view->model() == nullptr) {
        return;
    }
    QAbstractItemModel* model = view->model();
    const bool draw_content = !style_paints_item_text(view);
    QPainter painter(view->viewport());
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    for (int row = 0; row < model->rowCount(); ++row) {
        for (int column = 0; column < model->columnCount(); ++column) {
            const QModelIndex index = model->index(row, column);
            if (!index.isValid()) {
                continue;
            }
            const bool empty = model->data(index, Qt::DisplayRole).toString().isEmpty();
            if (!empty && !draw_content) {
                continue;
            }
            const QRect rect = view->visualRect(index);
            if (rect.isEmpty()) {
                continue;
            }
            paint_cell_content(view, painter, index, rect);
        }
    }
}

// A soft wash of the live highlight over the row under the pointer. The Fluent DataGrid
// already tints its hovered row from its own grey token, and the native table does not
// tint at all; the page adds the palette highlight itself so both skins read the same.
// The wash is translucent rather than opaque so the cell text, the warning glyph and the
// today tint underneath all stay readable.
void paint_hover_row(QTableView* view, int row) {
    if (view == nullptr || view->model() == nullptr || row < 0 ||
        row >= view->model()->rowCount()) {
        return;
    }
    // The platform palette, not the view's: the Fluent grid overwrites its own
    // QPalette::Highlight with an internal hover grey, which would turn this wash grey.
    const QPalette palette = QApplication::palette();
    QColor wash = palette.color(QPalette::Highlight);
    if (!wash.isValid()) {
        return;
    }
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    wash.setAlpha(dark ? 42 : 26);
    QPainter painter(view->viewport());
    for (int column = 0; column < view->model()->columnCount(); ++column) {
        const QRect rect = view->visualRect(view->model()->index(row, column));
        if (!rect.isEmpty()) {
            painter.fillRect(rect, wash);
        }
    }
}

// ---------------------------------------------------------------------------
// The grid's own delegate paints every cell (the Fluent DataGrid keeps that painting for
// itself), but the editor a cell opens still comes from the stock QStyledItemDelegate
// factory, which can only build a platform QLineEdit. The item editor factory is the one
// seam QStyledItemDelegate exposes for the editor alone, so it is replaced with one that
// builds the same Fluent LineEdit the edit page uses and leaves every other type to the
// default factory.
// ---------------------------------------------------------------------------
#ifdef DHW_UI_FLUENT
class FluentCellEditorFactory : public QItemEditorFactory {
public:
    QWidget* createEditor(int user_type, QWidget* parent) const override {
        if (user_type == QMetaType::QString) {
            QLineEdit* line = make_fluent_line_edit(QString(), parent);
            if (auto* fluent_line = qobject_cast<fluent::textfields::LineEdit*>(line)) {
                // A table cell is far narrower than a form field, and the clear
                // affordance would cover the subject text at the 72 px minimum width.
                fluent_line->setClearButtonEnabled(false);
                const QMargins margins = fluent_line->contentMargins();
                fluent_line->setContentMargins(
                    QMargins(8, margins.top(), 8, margins.bottom()));
            }
            return line;
        }
        return QItemEditorFactory::defaultFactory()->createEditor(user_type, parent);
    }
};

// The delegate keeps a raw pointer and QItemEditorFactory is not a QObject, so the
// factory outlives every grid as a single stateless instance.
QItemEditorFactory* fluent_cell_editor_factory() {
    static FluentCellEditorFactory factory;
    return &factory;
}
#endif

// The schedule table. The Fluent build is the library's DataGrid (a QTableView whose
// cells, header and frame are token painted); the native build is a plain QTableView so
// the platform style keeps drawing it. Both add the same hover wash, the same
// editor-lifetime helpers and the same placeholder pass, so the page code above them
// never branches.
#ifdef DHW_UI_FLUENT
class WeekTable : public fluent::collections::DataGrid {
public:
    explicit WeekTable(QWidget* parent) : fluent::collections::DataGrid(parent) {}

    // Editor lifetime. The table keeps NoEditTriggers and the page opens the editor
    // itself, so double clicking a column only starts an edit after the page day (and
    // therefore the write target of AppController::update_day) has been switched to that
    // column. A click on a toolbar button has to settle the open editor first, in either
    // direction.
    void commit_open_editor() {
        QWidget* editor = QApplication::focusWidget();
        if (editor == nullptr || !viewport()->isAncestorOf(editor)) {
            return;
        }
        const QModelIndex index = currentIndex();
        if (!index.isValid()) {
            return;
        }
        if (QAbstractItemDelegate* delegate = itemDelegateForIndex(index)) {
            delegate->setModelData(editor, model(), index);
        }
        closeEditor(editor, QAbstractItemDelegate::NoHint);
    }

    void discard_open_editor() {
        QWidget* editor = QApplication::focusWidget();
        if (editor == nullptr || !viewport()->isAncestorOf(editor)) {
            return;
        }
        closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
    }

    // The row under the pointer. The Fluent grid tints its hovered row from a grey token
    // of its own; the page adds the live highlight on top, so both skins read the same.
    void set_hover_row(int row) {
        if (hover_row_ == row) {
            return;
        }
        hover_row_ = row;
        viewport()->update();
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        // Deliberately bypass the grid's own hover state: it tints the row with its
        // neutral hover token, which would grey out the palette highlight this page adds
        // and make today's column unreadable. The table's selection handling is unchanged.
        QTableView::mouseMoveEvent(event);
        set_hover_row(indexAt(event->position().toPoint()).row());
    }

    void leaveEvent(QEvent* event) override {
        set_hover_row(-1);
        fluent::collections::DataGrid::leaveEvent(event);
    }

    bool viewportEvent(QEvent* event) override {
        if (event != nullptr && event->type() == QEvent::Leave) {
            set_hover_row(-1);
        }
        return fluent::collections::DataGrid::viewportEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        fluent::collections::DataGrid::paintEvent(event);
        paint_hover_row(this, hover_row_);
        paint_cell_contents(this);
    }

private:
    int hover_row_ = -1;
};
#else
class WeekTable : public QTableView {
public:
    explicit WeekTable(QWidget* parent) : QTableView(parent) {}

    void commit_open_editor() {
        QWidget* editor = QApplication::focusWidget();
        if (editor == nullptr || !viewport()->isAncestorOf(editor)) {
            return;
        }
        const QModelIndex index = currentIndex();
        if (!index.isValid()) {
            return;
        }
        if (QAbstractItemDelegate* delegate = itemDelegateForIndex(index)) {
            delegate->setModelData(editor, model(), index);
        }
        closeEditor(editor, QAbstractItemDelegate::NoHint);
    }

    void discard_open_editor() {
        QWidget* editor = QApplication::focusWidget();
        if (editor == nullptr || !viewport()->isAncestorOf(editor)) {
            return;
        }
        closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
    }

    void set_hover_row(int row) {
        if (hover_row_ == row) {
            return;
        }
        hover_row_ = row;
        viewport()->update();
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        QTableView::mouseMoveEvent(event);
        set_hover_row(indexAt(event->position().toPoint()).row());
    }

    void leaveEvent(QEvent* event) override {
        set_hover_row(-1);
        QTableView::leaveEvent(event);
    }

    bool viewportEvent(QEvent* event) override {
        if (event != nullptr && event->type() == QEvent::Leave) {
            set_hover_row(-1);
        }
        return QTableView::viewportEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QTableView::paintEvent(event);
        paint_hover_row(this, hover_row_);
        paint_cell_contents(this);
    }

private:
    int hover_row_ = -1;
};
#endif

#ifdef DHW_UI_FLUENT
// Today's header section needs one more layer than the Fluent grid offers: its header
// delegate paints every section from a header role and ignores the model's
// BackgroundRole, so the tint of today's column never reaches the header. This overlay is
// positioned over that single section and paints it from the live palette - the same
// blend the cells below use - plus the header's own font, foreground and dividers, so the
// header keeps the grid's anatomy instead of a hard coded colour.
class HeaderTintOverlay : public QWidget {
public:
    explicit HeaderTintOverlay(WeekTable* table)
        : QWidget(table->horizontalHeader()->viewport()), table_(table) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
    }

    void set_section(int column) {
        if (column_ == column) {
            return;
        }
        column_ = column;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (table_ == nullptr || column_ < 0 || table_->model() == nullptr) {
            return;
        }
        QHeaderView* header = table_->horizontalHeader();
        const auto& colors = table_->themeColorsRef();
        const QRect section = rect();
        QPainter painter(this);
        painter.fillRect(section, today_tint());
        painter.setPen(QPen(colors.strokeDivider, 1.0));
        painter.drawLine(section.bottomLeft(), section.bottomRight());
        const int visual = header->visualIndex(column_);
        if (visual >= 0 && visual != header->count() - 1) {
            painter.drawLine(section.topRight(), section.bottomRight());
        }

        const QAbstractItemModel* model = table_->model();
        QFont font = header->font();
        const QVariant font_role = model->headerData(column_, Qt::Horizontal, Qt::FontRole);
        if (font_role.canConvert<QFont>()) {
            font = qvariant_cast<QFont>(font_role);
        }
        QColor foreground = colors.textPrimary;
        const QVariant foreground_role =
            model->headerData(column_, Qt::Horizontal, Qt::ForegroundRole);
        if (foreground_role.canConvert<QBrush>()) {
            const QBrush brush = qvariant_cast<QBrush>(foreground_role);
            if (brush.style() != Qt::NoBrush) {
                foreground = brush.color();
            }
        }
        Qt::Alignment alignment = Qt::AlignCenter;
        const QVariant alignment_role =
            model->headerData(column_, Qt::Horizontal, Qt::TextAlignmentRole);
        if (alignment_role.isValid()) {
            alignment = static_cast<Qt::Alignment>(alignment_role.toInt());
        }
        const QString text =
            model->headerData(column_, Qt::Horizontal, Qt::DisplayRole).toString();
        const QRect text_rect = section.adjusted(12, 0, -12, 0);
        painter.setFont(font);
        painter.setPen(foreground);
        painter.drawText(text_rect, alignment,
                         QFontMetrics(font).elidedText(text, Qt::ElideRight, text_rect.width()));
    }

private:
    WeekTable* table_ = nullptr;
    int column_ = -1;
};
#endif

// The page only ever holds a WeekTable; this keeps the skin specific type out of the
// header while the editor helpers stay private to the table.
WeekTable* week_table(QTableView* view) {
    return static_cast<WeekTable*>(view);
}

}  // namespace

WeekPage::WeekPage(AppController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    // Idempotent: main.cpp initialises the Fluent resources right after QApplication,
    // but the page also constructs correctly when built on its own in a harness.
    ensure_fluent_ready();
    setObjectName(QStringLiteral("weekPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    build_header(root);
    build_body(root);
    refresh();
    apply_mode_ui();
    connect(controller_, &AppController::changed, this, &WeekPage::on_controller_changed);
}

void WeekPage::build_header(QVBoxLayout* root) {
    auto* head = new QWidget(this);
    head->setFixedHeight(52);
    auto* layout = new QHBoxLayout(head);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* hamburger = make_fluent_icon_button(QStringLiteral("hamburger"), 18, head);
    hamburger->setToolTip(QStringLiteral("收起 / 展开侧边栏（Ctrl+B）"));
    set_fluent_icon_raised(hamburger, false);
    connect(hamburger, &QAbstractButton::clicked, this, &WeekPage::sidebar_toggle_requested);
    layout->addWidget(hamburger);

    auto* back = make_fluent_icon_button(QStringLiteral("previous"), 16, head);
    back->setToolTip(QStringLiteral("返回编辑页（Esc）"));
    set_fluent_icon_raised(back, false);
    connect(back, &QAbstractButton::clicked, this, &WeekPage::back_requested);
    layout->addWidget(back);

    auto* title = make_fluent_title(QStringLiteral("课表总览"), head);
    layout->addWidget(title);
    layout->addStretch(1);

    week_hint_ = make_fluent_label(QString(), true, head);
    layout->addWidget(week_hint_);
    root->addWidget(head);
    root->addWidget(make_fluent_divider(Qt::Horizontal, this));
}

void WeekPage::build_body(QVBoxLayout* root) {
    auto* wrap = new QWidget();
    auto* column = new QVBoxLayout(wrap);
    column->setContentsMargins(16, 12, 16, 16);
    column->setSpacing(10);

    // One toolbar row above the table: the template mode switch, the jump to today, and
    // the day selector that decides which column a double click edits.
    auto* toolbar = new QWidget(wrap);
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_layout->setSpacing(8);
    build_toolbar(toolbar_layout);
    column->addWidget(toolbar);

    card_ = new WeekCard(dhw::icons::Icon::Calendar, QStringLiteral("本周课表"), QString(), this);
    table_ = new WeekTable(card_);
    model_ = new QStandardItemModel(table_);
    table_->setModel(model_);
    configure_table();
    card_->body_layout()->addWidget(table_, 0);

    mode_hint_ = make_fluent_label(QString(), true, card_);
    mode_hint_->setWordWrap(true);
    card_->body_layout()->addWidget(mode_hint_);

    legend_ = make_fluent_label(
        QStringLiteral("● 红色 = 该日单独调课（双击单元格即可修改；也可在编辑页或设置 → "
                       "天数管理中修改）"),
        true, card_);
    legend_->setWordWrap(true);
    card_->body_layout()->addWidget(legend_);
    // Keep the hints directly under the grid: the leftover page height belongs to the
    // card, not to the space between the table and its legend.
    card_->body_layout()->addStretch(1);

    // The card hugs the grid and its hints; the leftover page height stays page
    // background instead of becoming a second block of empty card below the last row.
    column->addWidget(card_, 0);
    column->addStretch(1);

    auto* scroll = make_scroll(wrap);
    scroll->setObjectName(QStringLiteral("weekScroll"));
    root->addWidget(scroll, 1);
}

void WeekPage::build_toolbar(QHBoxLayout* layout) {
    QWidget* bar = layout->parentWidget();

    edit_button_ = make_fluent_action_button(QStringLiteral("改课表模板"), QStringLiteral("week"),
                                             false, bar);
    edit_button_->setToolTip(QStringLiteral("修改周课表模版：编辑每周一到周日的课表模版"));
    connect(edit_button_, &QAbstractButton::clicked, this, &WeekPage::begin_template_edit);
    layout->addWidget(edit_button_);

    today_button_ = make_fluent_action_button(QStringLiteral("跳到今天"),
                                              QStringLiteral("today"), false, bar);
    today_button_->setToolTip(QStringLiteral("把页面日和课表切回今天（Ctrl+T）"));
    connect(today_button_, &QAbstractButton::clicked, this, &WeekPage::jump_to_today);
    layout->addWidget(today_button_);

    auto* day_label = make_fluent_label(QStringLiteral("编辑哪一天："), false, bar);
    layout->addWidget(day_label);
    day_combo_ = make_fluent_combo(bar);
    day_combo_->setObjectName(QStringLiteral("weekDayCombo"));
    day_combo_->setAccessibleName(QStringLiteral("编辑哪一天"));
    day_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    day_label->setBuddy(day_combo_);
    connect(day_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { on_day_selected(index); });
    layout->addWidget(day_combo_);

    layout->addStretch(1);

    cancel_button_ = make_fluent_action_button(QStringLiteral("取消"), QStringLiteral("remove"),
                                               false, bar);
    connect(cancel_button_, &QAbstractButton::clicked, this, &WeekPage::cancel_template_edit);
    layout->addWidget(cancel_button_);

    save_button_ = make_fluent_action_button(QStringLiteral("保存模版"), QString(), true, bar);
    save_button_->setToolTip(QStringLiteral("保存每周一到周日的课表模版"));
    connect(save_button_, &QAbstractButton::clicked, this, &WeekPage::save_template);
    layout->addWidget(save_button_);
}

void WeekPage::configure_table() {
    table_->setObjectName(QStringLiteral("weekTable"));
    table_->setAccessibleName(QStringLiteral("本周课表"));
    table_->setCornerButtonEnabled(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Editing is opened by the double click handler below, never by the view itself: the
    // write target must follow the selected day first.
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setAlternatingRowColors(false);
    // The grid is exactly as tall as its header plus its nine fixed rows (see
    // fit_table_height); the card must not stretch it into a block of empty cells below
    // the last period.
    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    table_->setMouseTracking(true);
    table_->viewport()->setMouseTracking(true);

    table_->verticalHeader()->setDefaultSectionSize(kRowHeight);
    table_->verticalHeader()->setFixedWidth(kRowHeaderWidth);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setMinimumSectionSize(kColumnMinWidth);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table_->horizontalHeader()->setHighlightSections(false);
#ifdef DHW_UI_FLUENT
    // The Fluent grid has no cell grid lines: the header dividers and the row separators
    // its delegate paints are the only rules, which is the WinUI table anatomy.
    table_->setShowGrid(false);
    // DataGrid hides its row header by default (it is designed for model columns); this
    // table is read row by row, so the period labels stay visible.
    table_->verticalHeader()->show();
#else
    table_->setShowGrid(true);
#endif

#ifdef DHW_UI_FLUENT
    // The grid's painting delegate stays in charge of the cells; only the editor widget it
    // creates is swapped for the Fluent text field (see FluentCellEditorFactory).
    if (auto* styled = dynamic_cast<QStyledItemDelegate*>(table_->itemDelegate())) {
        styled->setItemEditorFactory(fluent_cell_editor_factory());
    }
    // Today's header tint is an overlay because the Fluent header paints its sections
    // itself and ignores the model's BackgroundRole.
    header_tint_ = new HeaderTintOverlay(week_table(table_));
    header_tint_->hide();
    connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int, int, int) { update_header_tint(); });
    connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this,
            [this](int, int, int) { update_header_tint(); });
    connect(table_->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { update_header_tint(); });
#endif

    // QTableView cannot label its corner; a plain label over the style painted corner
    // fills that gap and is kept in sync with the two header geometries.
    corner_label_ = make_fluent_label(QStringLiteral("节次/星期"), true, table_);
    corner_label_->setObjectName(QStringLiteral("weekCornerLabel"));
    corner_label_->setAlignment(Qt::AlignCenter);
    corner_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    corner_label_->raise();

    connect(table_->horizontalHeader(), &QHeaderView::geometriesChanged, this,
            &WeekPage::update_corner_label);
    connect(table_->verticalHeader(), &QHeaderView::geometriesChanged, this,
            &WeekPage::update_corner_label);
    connect(table_, &QTableView::doubleClicked, this, &WeekPage::on_cell_double_clicked);
    connect(model_, &QStandardItemModel::itemChanged, this, &WeekPage::on_item_changed);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &WeekPage::on_current_changed);
    QTimer::singleShot(0, this, &WeekPage::update_corner_label);
}

QString WeekPage::monday() const {
    const QString date = controller_->current_date();
    const std::optional<int> weekday = dhw::weekday_index(to_std(date));
    if (!weekday.has_value()) {
        return date;
    }
    return from_std(dhw::add_days_text(to_std(date), -*weekday));
}

QString WeekPage::week_date(int column) const {
    return from_std(dhw::add_days_text(to_std(monday()), column));
}

QString WeekPage::template_text_for(const QString& date, int period) const {
    if (period < 0 || period >= kPeriods) {
        return QString();
    }
    const dhw::AppData& data = controller_->data();
    const std::string date_text = to_std(date);
    int base = 0;
    if (const std::optional<int> weekday = dhw::weekday_index(date_text); weekday.has_value()) {
        base = *weekday;
    }
    if (const dhw::Day* day = data.find_day(date_text); day != nullptr &&
        day->copied_from_weekday.has_value()) {
        base = *day->copied_from_weekday;
    }
    if (base < 0 || static_cast<std::size_t>(base) >= data.weekly_schedule.size()) {
        return QString();
    }
    const std::vector<std::string>& row = data.weekly_schedule[static_cast<std::size_t>(base)];
    if (static_cast<std::size_t>(period) >= row.size()) {
        return QString();
    }
    return from_std(row[static_cast<std::size_t>(period)]);
}

void WeekPage::refresh() {
    if (table_ == nullptr) {
        return;
    }
    const QString start = monday();
    set_label_text(week_hint_, QStringLiteral("本周（%1 起）；调课显示为红色").arg(start));
    rebuild_day_combo();
    if (!editing_) {
        rebuild_overview();
    }
}

void WeekPage::refresh_headers() {
    const QString today = from_std(controller_->effective_today());
    QStringList periods;
    for (int period = 0; period < kPeriods; ++period) {
        periods << QStringLiteral("第%1节").arg(period + 1);
    }
    model_->setVerticalHeaderLabels(periods);

    for (int column = 0; column < kWeekdays; ++column) {
        const QString date = week_date(column);
        const bool is_today = date == today;
        auto* header = new QStandardItem(weekday_name(column));
        header->setTextAlignment(Qt::AlignCenter);
        if (is_today) {
            // Only today's header carries a font, so every other column keeps the skin's
            // own header typography instead of a copy of the table font. The background
            // is the same palette blend as today's cells: the native header honours the
            // model role, and the Fluent header gets it from HeaderTintOverlay.
            QFont font = table_->font();
            font.setBold(true);
            header->setFont(font);
            header->setBackground(QBrush(today_tint()));
        }
        header->setToolTip(is_today ? QStringLiteral("%1（今天）").arg(date) : date);
        model_->setHorizontalHeaderItem(column, header);
    }
    if (corner_label_ != nullptr) {
        // Keep the corner label one step below the live app font, so a display-size change
        // cannot leave it at the startup size.
        QFont corner_font = table_->font();
        if (corner_font.pointSizeF() > 0.0) {
            corner_font.setPointSizeF(std::max(7.5, corner_font.pointSizeF() - 1.5));
        }
        corner_label_->setFont(corner_font);
    }
}

void WeekPage::rebuild_day_combo() {
    if (day_combo_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(day_combo_);
    const QString current = controller_->current_date();
    const QString today = from_std(controller_->effective_today());
    day_combo_->clear();
    for (int column = 0; column < kWeekdays; ++column) {
        const QString date = week_date(column);
        QString text = QStringLiteral("%1 %2").arg(weekday_name(column), short_date(date));
        if (date == today) {
            text += QStringLiteral("（今天）");
        }
        day_combo_->addItem(text, date);
    }
    int index = day_combo_->findData(current);
    if (index < 0) {
        index = 0;
    }
    day_combo_->setCurrentIndex(index);
    day_combo_->setEnabled(!editing_);
}

void WeekPage::rebuild_overview() {
    if (table_ == nullptr || model_ == nullptr) {
        return;
    }
    rebuilding_ = true;
    model_->clear();
    refresh_headers();

    const std::string start = to_std(monday());
    const std::vector<std::vector<dhw::Slot>> rows = controller_->week(from_std(start));
    const QString today = from_std(controller_->effective_today());
    const QColor tint = today_tint();
    const QIcon warning(warning_pixmap(kWarningIconSide));
    const std::vector<dhw::Slot> no_slots;

    for (int column = 0; column < kWeekdays; ++column) {
        const QString date = from_std(dhw::add_days_text(start, column));
        const bool is_today = date == today;
        const std::vector<dhw::Slot>& column_slots =
            static_cast<std::size_t>(column) < rows.size()
                ? rows[static_cast<std::size_t>(column)]
                : no_slots;
        for (int period = 0; period < kPeriods; ++period) {
            const dhw::Slot slot =
                static_cast<std::size_t>(period) < column_slots.size()
                    ? column_slots[static_cast<std::size_t>(period)]
                    : dhw::Slot{};
            auto* item = new QStandardItem(from_std(slot.name));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
            if (is_today) {
                item->setBackground(QBrush(tint));
            }
            if (slot.changed) {
                // The warning marker names the swapped lesson, the bold face and the
                // semantic red keep the legend below the table truthful.
                QFont font = table_->font();
                font.setBold(true);
                item->setFont(font);
                item->setForeground(QBrush(danger_color()));
                item->setIcon(warning);
                const QString original = template_text_for(date, period);
                item->setToolTip(QStringLiteral("调课：原为%1")
                                     .arg(original.isEmpty() ? placeholder_text() : original));
            } else if (!slot.name.empty()) {
                item->setToolTip(from_std(slot.name));
            }
            model_->setItem(period, column, item);
        }
    }

    rebuilding_ = false;
    update_corner_label();
    fit_table_height();
    if (last_row_ >= 0 && last_row_ < kPeriods && last_column_ >= 0 &&
        last_column_ < kWeekdays) {
        table_->setCurrentIndex(model_->index(last_row_, last_column_));
    } else {
        // No user selection yet: a current cell would paint a strong selection that can
        // be mistaken for the today marker.
        table_->setCurrentIndex(QModelIndex());
        table_->clearSelection();
    }
}

void WeekPage::rebuild_template() {
    if (table_ == nullptr || model_ == nullptr) {
        return;
    }
    rebuilding_ = true;
    model_->clear();
    refresh_headers();

    const dhw::AppData& data = controller_->data();
    const QString today = from_std(controller_->effective_today());
    const QColor tint = today_tint();
    for (int column = 0; column < kWeekdays; ++column) {
        const QString date = week_date(column);
        const bool is_today = date == today;
        const std::vector<std::string> no_row;
        const std::vector<std::string>& row =
            static_cast<std::size_t>(column) < data.weekly_schedule.size()
                ? data.weekly_schedule[static_cast<std::size_t>(column)]
                : no_row;
        for (int period = 0; period < kPeriods; ++period) {
            QString value;
            if (static_cast<std::size_t>(period) < row.size()) {
                value = from_std(row[static_cast<std::size_t>(period)]);
            }
            auto* item = new QStandardItem(value);
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
            if (is_today) {
                item->setBackground(QBrush(tint));
            }
            model_->setItem(period, column, item);
        }
    }

    rebuilding_ = false;
    update_corner_label();
    fit_table_height();
    if (last_row_ >= 0 && last_row_ < kPeriods && last_column_ >= 0 &&
        last_column_ < kWeekdays) {
        table_->setCurrentIndex(model_->index(last_row_, last_column_));
    } else {
        table_->setCurrentIndex(QModelIndex());
        table_->clearSelection();
    }
}

void WeekPage::update_corner_label() {
    if (corner_label_ == nullptr || table_ == nullptr) {
        return;
    }
    const QRect rows = table_->verticalHeader()->geometry();
    const QRect columns = table_->horizontalHeader()->geometry();
    corner_label_->setGeometry(rows.x(), columns.y(), rows.width(), columns.height());
    corner_label_->raise();
    update_header_tint();
}

void WeekPage::fit_table_height() {
    if (table_ == nullptr || model_ == nullptr) {
        return;
    }
    // Exactly the header plus the fixed nine rows: no stretch, so the card's leftover
    // height never turns into empty table cells below the last period.
    int height = table_->horizontalHeader()->sizeHint().height();
    for (int row = 0; row < model_->rowCount(); ++row) {
        height += table_->rowHeight(row);
    }
    height += 2 * table_->frameWidth();
    if (height <= 0) {
        return;
    }
    table_->setFixedHeight(height);
}

void WeekPage::update_header_tint() {
#ifdef DHW_UI_FLUENT
    if (header_tint_ == nullptr || table_ == nullptr || model_ == nullptr) {
        return;
    }
    const QString today = from_std(controller_->effective_today());
    int column = -1;
    for (int index = 0; index < kWeekdays; ++index) {
        if (week_date(index) == today) {
            column = index;
            break;
        }
    }
    if (column < 0 || column >= model_->columnCount()) {
        header_tint_->hide();
        return;
    }
    auto* overlay = static_cast<HeaderTintOverlay*>(header_tint_);
    overlay->set_section(column);
    QHeaderView* header = table_->horizontalHeader();
    header_tint_->setGeometry(header->sectionViewportPosition(column), 0,
                              header->sectionSize(column), header->viewport()->height());
    header_tint_->raise();
    header_tint_->show();
    // The tint and the divider colour come from the live palette/tokens, so a theme
    // change that rebuilds the header has to repaint the overlay too.
    header_tint_->update();
#endif
}

void WeekPage::apply_mode_ui() {
    if (edit_button_ == nullptr) {
        return;
    }
    edit_button_->setVisible(!editing_);
    cancel_button_->setVisible(editing_);
    save_button_->setVisible(editing_);
    day_combo_->setEnabled(!editing_);
    today_button_->setEnabled(!editing_);
    card_->set_title(editing_ ? QStringLiteral("周课表模版") : QStringLiteral("本周课表"));
    card_->set_badge(editing_ ? QStringLiteral("编辑中") : QString());
    legend_->setVisible(!editing_);
    set_label_text(mode_hint_,
                   editing_ ? QStringLiteral("正在编辑周课表模版：双击单元格修改，Enter 提交、"
                                             "Esc 取消；改完后点“保存模版”。")
                            : QStringLiteral("双击单元格修改该日课表；“改课表模板”编辑每周模版。"));
}

void WeekPage::begin_template_edit() {
    if (editing_ || table_ == nullptr) {
        return;
    }
    week_table(table_)->commit_open_editor();
    editing_ = true;
    apply_mode_ui();
    rebuild_template();
    focus_date_column(controller_->current_date(), true);
}

void WeekPage::cancel_template_edit() {
    if (!editing_) {
        return;
    }
    week_table(table_)->discard_open_editor();
    editing_ = false;
    apply_mode_ui();
    refresh();
}

void WeekPage::save_template() {
    if (!editing_ || table_ == nullptr) {
        return;
    }
    // Commit an editor that the click on the button left open, then read every cell.
    week_table(table_)->commit_open_editor();
    std::vector<std::vector<std::string>> rows(
        static_cast<std::size_t>(kWeekdays),
        std::vector<std::string>(static_cast<std::size_t>(kPeriods)));
    for (int column = 0; column < kWeekdays; ++column) {
        for (int period = 0; period < kPeriods; ++period) {
            const QStandardItem* item = model_ != nullptr ? model_->item(period, column) : nullptr;
            const QString text = item != nullptr ? item->text().trimmed() : QString();
            rows[static_cast<std::size_t>(column)][static_cast<std::size_t>(period)] =
                to_std(text);
        }
    }
    editing_ = false;
    apply_mode_ui();
    // The controller broadcasts changed synchronously; refresh once explicitly after
    // muting that first rebuild so the table repaints a single time.
    suppress_refresh_ = true;
    controller_->update_weekly_schedule(std::move(rows));
    suppress_refresh_ = false;
    refresh();
    emit toast_requested(QStringLiteral("周课表模版已更新"));
}

void WeekPage::jump_to_today() {
    if (editing_ || table_ == nullptr) {
        return;
    }
    const QString today = from_std(controller_->effective_today());
    if (today.isEmpty()) {
        return;
    }
    week_table(table_)->commit_open_editor();
    if (today != controller_->current_date()) {
        controller_->set_current_date(today);
    }
    focus_date_column(today, true);
}

void WeekPage::on_day_selected(int index) {
    if (editing_ || table_ == nullptr || index < 0) {
        return;
    }
    const QString date = day_combo_->itemData(index).toString();
    if (date.isEmpty()) {
        return;
    }
    // The previous day's open editor still belongs to the previous write target.
    week_table(table_)->commit_open_editor();
    if (date != controller_->current_date()) {
        controller_->set_current_date(date);
    }
    focus_date_column(date, true);
}

void WeekPage::focus_date_column(const QString& date, bool focus_table) {
    if (table_ == nullptr || model_ == nullptr) {
        return;
    }
    const std::optional<int> weekday = dhw::weekday_index(to_std(date));
    if (!weekday.has_value()) {
        return;
    }
    const int column = std::clamp(*weekday, 0, kWeekdays - 1);
    const int row = std::clamp(last_row_ >= 0 ? last_row_ : 0, 0, kPeriods - 1);
    const QModelIndex index = model_->index(row, column);
    if (index.isValid()) {
        table_->setCurrentIndex(index);
        table_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
    if (focus_table) {
        table_->setFocus(Qt::OtherFocusReason);
    }
}

void WeekPage::on_current_changed(const QModelIndex& current, const QModelIndex& previous) {
    Q_UNUSED(previous);
    if (current.isValid()) {
        last_row_ = current.row();
        last_column_ = current.column();
    }
}

void WeekPage::on_cell_double_clicked(const QModelIndex& index) {
    if (table_ == nullptr || !index.isValid()) {
        return;
    }
    const int row = index.row();
    const int column = index.column();
    if (row < 0 || row >= kPeriods || column < 0 || column >= kWeekdays) {
        return;
    }
    if (!editing_) {
        const QString date = week_date(column);
        if (date != controller_->current_date()) {
            // The write target of AppController::update_day is the page day, so switch
            // it first, but do not rebuild the table inside this mouse event: refresh and
            // the editor opening are both deferred to the next event-loop pass.
            suppress_refresh_ = true;
            controller_->set_current_date(date);
            suppress_refresh_ = false;
            schedule_refresh();
            QTimer::singleShot(0, this, [this, row, column]() {
                open_cell_editor(row, column);
            });
            return;
        }
    }
    open_cell_editor(row, column);
}

void WeekPage::open_cell_editor(int row, int column) {
    if (table_ == nullptr || model_ == nullptr || row < 0 || row >= kPeriods || column < 0 ||
        column >= kWeekdays) {
        return;
    }
    const QModelIndex index = model_->index(row, column);
    if (!index.isValid()) {
        return;
    }
    last_row_ = row;
    last_column_ = column;
    table_->setCurrentIndex(index);
    table_->edit(index);
    // The cell editor the delegates create is a plain line edit, so the quiet placeholder
    // of an empty cell is not part of it; hand it one on the next pass.
    QTimer::singleShot(0, this, [this]() {
        if (table_ == nullptr) {
            return;
        }
        QWidget* editor = QApplication::focusWidget();
        if (editor == nullptr || !table_->viewport()->isAncestorOf(editor)) {
            return;
        }
        if (auto* line = qobject_cast<QLineEdit*>(editor)) {
            line->setPlaceholderText(placeholder_text());
        }
    });
}

void WeekPage::on_item_changed(QStandardItem* item) {
    if (rebuilding_ || editing_ || item == nullptr || model_ == nullptr) {
        return;
    }
    const int row = item->row();
    const int column = item->column();
    if (row < 0 || row >= kPeriods || column < 0 || column >= kWeekdays) {
        return;
    }
    const QString date = week_date(column);
    if (date != controller_->current_date()) {
        // The page day moved between opening the editor and committing it; do not write
        // the text to a different date.
        return;
    }
    apply_override(date, row, item->text());
}

void WeekPage::apply_override(const QString& date, int period, const QString& text) {
    if (period < 0 || period >= kPeriods) {
        return;
    }
    const QString trimmed = text.trimmed();
    const QString original = template_text_for(date, period);
    std::optional<std::string> next;
    if (!trimmed.isEmpty() && trimmed != original) {
        next = to_std(trimmed);
    }

    const std::string date_text = to_std(date);
    const dhw::Day* stored = controller_->data().find_day(date_text);
    dhw::Day day = stored != nullptr ? *stored : dhw::Day();
    if (day.schedule_override.size() < static_cast<std::size_t>(kPeriods)) {
        day.schedule_override.resize(static_cast<std::size_t>(kPeriods));
    }
    const std::optional<std::string> current =
        day.schedule_override[static_cast<std::size_t>(period)];
    if (current == next) {
        // No model change, but the item may hold untrimmed text; normalise the view.
        schedule_refresh();
        return;
    }
    if (stored == nullptr && !next.has_value()) {
        // Clearing an already empty day must not create an entry: on a weekend an entry
        // in `days` would make the day a school day.
        return;
    }
    day.schedule_override[static_cast<std::size_t>(period)] = next;

    // update_day broadcasts changed synchronously, while the delegate may still be
    // finishing its commit. Mute that rebuild and schedule one for the next pass.
    suppress_refresh_ = true;
    controller_->update_day(day);
    suppress_refresh_ = false;
    schedule_refresh();
}

void WeekPage::schedule_refresh() {
    if (refresh_scheduled_) {
        return;
    }
    refresh_scheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        refresh_scheduled_ = false;
        refresh();
    });
}

void WeekPage::on_controller_changed() {
    if (suppress_refresh_) {
        return;
    }
    refresh();
}

void WeekPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange &&
        event->type() != QEvent::FontChange) {
        return;
    }
    if (table_ == nullptr || editing_) {
        return;
    }
    // The item colours and fonts above are resolved from the live palette and the app
    // font, so a system light/dark switch or a display-size change has to rebuild them.
    // Deferred: a rebuild inside changeEvent reenters the palette/font machinery.
    schedule_refresh();
}

}  // namespace dhw::qt

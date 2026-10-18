#include "main_window.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyle>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyleHints>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>
#include <cstdio>
#include <cmath>

#include "dhw/schedule.h"
#include "credits_page.h"
#include "editor_page.h"
#include "present_window.h"
#include "settings_page.h"
#include "theme.h"
#include "week_page.h"

namespace dhw::qt {
namespace {

constexpr int kSidebarWidth = 190;
#ifdef DHW_UI_FLUENT
// Fluent compact mode keeps a narrow icon rail when the pane is collapsed, like
// WinUI's NavigationView: 52 px of chrome, a 40 px item centred with 6 px margins.
constexpr int kCompactSidebarWidth = 52;
constexpr int kCompactRailSide = 40;
constexpr int kCompactRailIcon = 20;
#endif
// Minimum toolbar height; the two line date chips grow it when the display size does.
constexpr int kToolbarHeight = 52;
// How long a status bar message stays up (the native skin's notification).
constexpr int kToastMs = 2600;
// Date chip metrics. The width is shared by every chip so the strip is a regular grid.
constexpr int kChipPadX = 8;
constexpr int kChipPadY = 3;
constexpr int kChipMinWidth = 46;
constexpr int kChipScrollPad = 6;
constexpr int kToolbarPad = 20;
constexpr int kStripArrowSide = 12;

std::string to_std(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString from_std(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

// Two digit month.day for the chip line: every chip is then five monospaced glyphs
// wide, so the decimal points and the two lines line up across the whole strip.
QString chip_day_month(const QString& date) {
    const QStringList parts = date.split(QLatin1Char('-'));
    if (parts.size() != 3) {
        return date;
    }
    return QStringLiteral("%1.%2")
        .arg(parts.at(1).toInt(), 2, 10, QLatin1Char('0'))
        .arg(parts.at(2).toInt(), 2, 10, QLatin1Char('0'));
}

// "星期四" -> "周四" for the second chip line. The weekday names stay owned by core,
// so only the leading character is replaced here.
QString short_weekday(const QString& weekday) {
    if (weekday.size() < 2) {
        return weekday;
    }
    return QStringLiteral("周") + weekday.right(1);
}

QString chip_tooltip(const QString& date) {
    const QString weekday = short_weekday(from_std(dhw::weekday_cn(to_std(date))));
    return weekday.isEmpty() ? date : QStringLiteral("%1 %2").arg(date, weekday);
}

// The number line uses the platform's own monospaced font, at the size of the live UI
// font so a display size change still applies. Monospaced digits keep the days of the
// month aligned with each other across the strip.
QFont chip_number_font(bool bold) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont ui = QApplication::font();
    if (ui.pointSizeF() > 0.0) {
        font.setPointSizeF(ui.pointSizeF());
    } else if (ui.pixelSize() > 0) {
        font.setPixelSize(ui.pixelSize());
    }
    font.setBold(bold);
    return font;
}

QFont chip_weekday_font(bool bold) {
    QFont font = QApplication::font();
    font.setBold(bold);
    return font;
}

int date_chip_height() {
    const QFontMetrics number(chip_number_font(false));
    const QFontMetrics weekday(chip_weekday_font(false));
    return 2 * std::max(number.height(), weekday.height()) + 2 * ui_px(kChipPadY);
}

// One width for every chip: the widest month.day text at the live font size. The chips
// keep that fixed width, so the strip keeps a regular rhythm.
int date_chip_width() {
    const QFontMetrics number(chip_number_font(false));
    const QFontMetrics weekday(chip_weekday_font(false));
    const int text = std::max({number.horizontalAdvance(QStringLiteral("88.88")),
                               weekday.horizontalAdvance(QStringLiteral("周一")),
                               weekday.horizontalAdvance(QStringLiteral("周日"))});
    return std::max(ui_px(kChipMinWidth), text + 2 * ui_px(kChipPadX));
}

// Everything the two states of a chip contribute, all through the live palette:
//   today    — bold font plus the highlight colour as the label colour;
//   selected — the platform style's own "on" panel, plus a soft accent tint in the
//              Button role. The labels keep the ordinary text colour, so a chip stays
//              readable whether or not a style honours the tint.
// The two are independent, so today's chip can be the selected one at the same time.
// The label colours are a foreground *role* on the chip's palette rather than a palette
// of their own: the two lines are plain labels inside the button, and a role keeps them
// following the palette the button itself was given.
void style_date_chip(ActionControl* chip, bool selected, bool today) {
    if (chip == nullptr) {
        return;
    }
#ifdef DHW_UI_FLUENT
    // Selected: filled Standard chip (token accent, on-accent text). Today: Subtle but
    // checked, which is the build's accent-text treatment. Rest: plain Subtle.
    chip->setChecked(selected || today);
    chip->setFluentStyle(selected ? fluent::basicinput::Button::Standard
                                  : fluent::basicinput::Button::Subtle);
#else
    chip->setChecked(selected);
    auto* number = chip->findChild<QLabel*>(QStringLiteral("chipDate"));
    auto* weekday = chip->findChild<QLabel*>(QStringLiteral("chipWeekday"));
    if (number == nullptr || weekday == nullptr) {
        return;
    }
    number->setFont(chip_number_font(today));
    weekday->setFont(chip_weekday_font(today));

    QPalette chip_palette = QApplication::palette();
    // QPalette::Highlight is meant as a selection background, so the theme helper lifts it
    // to a readable foreground when the raw role would be illegible; with the common
    // light palette the value is the Highlight colour itself.
    chip_palette.setColor(QPalette::Highlight, accent_color());
    if (selected) {
        const QColor tint = soft_accent_color();
        chip_palette.setColor(QPalette::Button, tint);
        chip_palette.setColor(QPalette::Window, tint);
    }
    chip->setPalette(chip_palette);

    const QPalette::ColorRole role = today ? QPalette::Highlight : QPalette::WindowText;
    number->setForegroundRole(role);
    weekday->setForegroundRole(role);
#endif
}

QShortcut* add_shortcut(QWidget* owner, const QKeySequence& keys, const std::function<void()>& action) {
    auto* shortcut = new QShortcut(keys, owner);
    shortcut->setContext(Qt::WindowShortcut);
    QObject::connect(shortcut, &QShortcut::activated, owner, action);
    return shortcut;
}

#ifdef DHW_UI_FLUENT
// One item of the compact rail. It is an ordinary Fluent Subtle button in IconOnly
// mode, so hover and the checked tint come from the library; the only addition is the
// 3 px accent bar WinUI's NavigationView shows on the selected destination.
class NavRailButton : public fluent::basicinput::Button {
public:
    NavRailButton(const QString& role, const QString& tip, int side, QWidget* parent)
        : Button(parent) {
        setFluentStyle(fluent::basicinput::Button::Subtle);
        setFluentSize(fluent::basicinput::Button::StandardSize);
        setFluentLayout(fluent::basicinput::Button::IconOnly);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(side, side);
        setToolTip(tip);
        setAccessibleName(tip);
        const int icon_side = ui_px(kCompactRailIcon);
        const QPixmap pixmap = theme_icon(role, icon_side, accent_color());
        if (!pixmap.isNull()) {
            setIcon(QIcon(pixmap));
            setIconSize(QSize(icon_side, icon_side));
        }
        setProperty("dhwRole", role);
        setProperty("dhwIconSide", icon_side);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Button::paintEvent(event);
        if (!isChecked()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent_color());
        const qreal bar = std::max(2.0, static_cast<qreal>(ui_px(3)));
        const qreal inset = static_cast<qreal>(ui_px(2));
        const qreal length = height() - 2.0 * ui_px(9);
        painter.drawRoundedRect(QRectF(inset, (height() - length) / 2.0, bar, length), bar / 2.0,
                                bar / 2.0);
    }
};
#endif

}  // namespace

MainWindow::MainWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    setWindowTitle(QStringLiteral("每日作业"));
    build_shell();
    appearance_ = from_std(controller_->local_settings().appearance);
    sidebar_collapsed_ = controller_->local_settings().sidebar_collapsed;
    set_sidebar_collapsed(sidebar_collapsed_);
    install_shortcuts();
    apply_theme();
    refresh_state();
    connect(controller_, &AppController::changed, this, &MainWindow::on_controller_changed);
    connect(controller_, &AppController::save_failed_signal, this, &MainWindow::on_save_failed);
    connect(controller_, &AppController::saved, this, [this]() { refresh_footer(); });
    connect(controller_, &AppController::appearance_changed, this, &MainWindow::on_appearance_changed);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // A system light/dark switch hands the app a new platform palette. Qt repaints the
    // controls; the pages that colour a cell from the palette have to be rebuilt.
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() { apply_theme(); });
    }
#endif
}

void MainWindow::build_shell() {
    QWidget* pages_parent = nullptr;
#ifdef DHW_UI_FLUENT
    QVBoxLayout* content_layout = nullptr;
    // WinUI shell: NavigationView owns the pane geometry (Ctrl+B = paneOpen) and
    // hosts one content page. The toolbar lives inside that page, so the pane still
    // runs the full window height like the native shell did.
    navigation_ = new fluent::navigation::NavigationView(this);
    navigation_->setDisplayMode(
        fluent::navigation::NavigationView::DisplayMode::Left);
    // Expanded: the original 190 px column with text. Collapsed (Ctrl+B): WinUI's
    // compact mode, a narrow rail that keeps the vector icons instead of freeing the
    // whole width. Both widths go through ui_px so they follow the display size.
    navigation_->setExpandedPaneWidth(ui_px(kSidebarWidth));
    navigation_->setCompactPaneWidth(ui_px(kCompactSidebarWidth));
    navigation_->setAnimationEnabled(true);

    sidebar_ = build_sidebar();
    navigation_->setMainChromeWidget(sidebar_);

    content_ = new QWidget();
    content_layout = new QVBoxLayout(content_);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);
    toolbar_ = build_toolbar();
    content_layout->addWidget(toolbar_);
    content_layout->addWidget(make_fluent_divider(Qt::Horizontal, content_));
    pages_parent = content_;
#else
    auto* root = new QWidget(this);
    auto* root_layout = new QHBoxLayout(root);
    root_layout->setSizeConstraint(QLayout::SetNoConstraint);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    sidebar_ = build_sidebar();
    root_layout->addWidget(sidebar_);
    // The sidebar used to be separated from the page by a stylesheet border; a native
    // QFrame draws the same hairline through the platform style.
    root_layout->addWidget(make_separator(QFrame::VLine, root));

    auto* main = new QWidget(root);
    auto* main_layout = new QVBoxLayout(main);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    toolbar_ = build_toolbar();
    main_layout->addWidget(toolbar_);
    main_layout->addWidget(make_separator(QFrame::HLine, main));
    pages_parent = main;
#endif

    pages_ = new QStackedWidget(pages_parent);
    editor_ = new EditorPage(controller_, pages_);
    week_ = new WeekPage(controller_, pages_);
    settings_ = new SettingsPage(controller_, pages_);
    credits_ = new CreditsPage(pages_);
    pages_->addWidget(editor_);
    pages_->addWidget(week_);
    pages_->addWidget(settings_);
    pages_->addWidget(credits_);
    connect(week_, &WeekPage::sidebar_toggle_requested, this, &MainWindow::on_toggle_sidebar);
    connect(week_, &WeekPage::back_requested, this, [this]() { show_page(QStringLiteral("edit")); });
    connect(week_, &WeekPage::toast_requested, this, &MainWindow::toast);
    connect(settings_, &SettingsPage::sidebar_toggle_requested, this,
            &MainWindow::on_toggle_sidebar);
    connect(settings_, &SettingsPage::back_requested, this,
            [this]() { show_page(QStringLiteral("edit")); });
    connect(settings_, &SettingsPage::toast_requested, this, &MainWindow::toast);
    connect(credits_, &CreditsPage::sidebar_toggle_requested, this,
            &MainWindow::on_toggle_sidebar);
    connect(credits_, &CreditsPage::back_requested, this,
            [this]() { show_page(QStringLiteral("edit")); });
    connect(settings_, &SettingsPage::theme_preview_requested, this,
            [this](double scale) {
                dhw::qt::apply_theme(appearance_, scale);
                // The shell's own pixel metrics follow the same factor as the font, so
                // the live preview has to re-apply them too.
                apply_shell_metrics();
            });

#ifdef DHW_UI_FLUENT
    content_layout->addWidget(pages_, 1);
    navigation_->contentHost()->insertPage(0, content_);
    navigation_->contentHost()->setCurrentIndex(0);
    setCentralWidget(navigation_);
#else
    main_layout->addWidget(pages_, 1);
    root_layout->addWidget(main, 1);
    setCentralWidget(root);
#endif

    present_ = new PresentWindow(controller_);
    connect(present_, &PresentWindow::closed, this, &MainWindow::exit_present);
    connect(controller_, &AppController::appearance_changed, this, [this]() { present_->update(); });
}

QWidget* MainWindow::build_sidebar() {
    auto* sidebar = new QWidget(this);
#ifdef DHW_UI_FLUENT
    // The pane is positioned by NavigationView, so it must be allowed to shrink to
    // the compact rail width; the expanded column and the rail swap inside this widget.
    sidebar->setMinimumWidth(0);
    sidebar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(ui_px(8), ui_px(14), ui_px(8), ui_px(12));
    layout->setSpacing(ui_px(8));
    sidebar_layout_ = layout;

    const struct {
        const char* text;
        const char* role;
        const char* tip;
        const char* compact_tip;
    } items[] = {
        {"编辑", "edit", "编辑（Alt+1~4 在条目间跳）", "编辑"},
        {"课表总览", "week", "课表总览", "课表总览"},
        {"设置", "settings", "设置", "设置"},
        {"致谢", "credits", "致谢（Credits）", "致谢"},
    };

    brand_label_ = make_fluent_title(QStringLiteral("每日作业"), sidebar);
    layout->addWidget(brand_label_);

    section_label_ = make_fluent_label(QStringLiteral("页面"), true, sidebar);
    layout->addWidget(section_label_);

    nav_model_ = new QStandardItemModel(this);
    for (const auto& entry : items) {
        auto* item = new QStandardItem(QString::fromUtf8(entry.text));
        item->setEditable(false);
        item->setSizeHint(QSize(0, ui_px(40)));
        item->setToolTip(QString::fromUtf8(entry.tip));
        item->setData(QString::fromLatin1(entry.role), Qt::UserRole);
        nav_model_->appendRow(item);
    }
    nav_list_ = new fluent::collections::ListView(sidebar);
    nav_list_->setModel(nav_model_);
    nav_list_->setSelectionMode(fluent::collections::SelectionMode::Single);
    nav_list_->setSelectionIndicatorVisible(true);
    nav_list_->setSelectedIndicatorAnimationEnabled(false);
    nav_list_->setBorderVisible(false);
    nav_list_->setBackgroundVisible(false);
    nav_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    nav_list_->setIconSize(QSize(ui_px(18), ui_px(18)));
    nav_list_->setUniformItemSizes(true);
    nav_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_list_->setFixedHeight(nav_model_->rowCount() * ui_px(40) + ui_px(8));
    nav_list_->setSelectedIndex(0);
    connect(nav_list_, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || updating_nav_) {
            return;
        }
        on_page_clicked(index.row());
    });
    layout->addWidget(nav_list_);
    refresh_nav_icons();

    // WinUI compact mode: one 40 px icon-only button per destination, with the same
    // vector icon and the simplified tooltip wording of the compact pane. The checked
    // state supplies the highlight, NavRailButton adds the accent indicator bar.
    sidebar_rail_ = new QWidget(sidebar);
    auto* rail_layout = new QVBoxLayout(sidebar_rail_);
    rail_layout->setContentsMargins(0, 0, 0, 0);
    rail_layout->setSpacing(ui_px(4));
    const int nav_count = static_cast<int>(sizeof(items) / sizeof(items[0]));
    for (int row = 0; row < nav_count; ++row) {
        const auto& entry = items[row];
        auto* button = new NavRailButton(QString::fromLatin1(entry.role),
                                         QString::fromUtf8(entry.compact_tip),
                                         ui_px(kCompactRailSide), sidebar_rail_);
        connect(button, &QAbstractButton::clicked, this,
                [this, row]() { on_page_clicked(row); });
        rail_layout->addWidget(button, 0, Qt::AlignHCenter);
        nav_rail_buttons_.append(button);
    }
    sidebar_rail_->hide();
    layout->addWidget(sidebar_rail_);

    layout->addStretch(1);

    auto* footer = new QWidget(sidebar);
    footer_layout_ = new QHBoxLayout(footer);
    footer_layout_->setContentsMargins(0, 0, 0, 0);
    footer_layout_->setSpacing(ui_px(7));
    footer_badge_ = new fluent::status_info::InfoBadge(footer);
    footer_badge_->setDisplayMode(
        fluent::status_info::InfoBadge::InfoBadgeDisplayMode::Dot);
    footer_badge_->setStatus(fluent::status_info::InfoBadge::InfoBadgeStatus::Success);
    footer_badge_->setBeaconDiameter(10);
    footer_layout_->addWidget(footer_badge_);
    footer_text_ = make_fluent_label(QString(), true, footer);
    footer_layout_->addWidget(footer_text_, 1);
    layout->addWidget(footer);
    return sidebar;
#else
    sidebar->setFixedWidth(ui_px(kSidebarWidth));
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(ui_px(12), ui_px(14), ui_px(12), ui_px(12));
    layout->setSpacing(ui_px(8));

    auto* brand = make_title_label(QStringLiteral("每日作业"), sidebar);
    layout->addWidget(brand);

    auto* section = new QLabel(QStringLiteral("页面"), sidebar);
    layout->addWidget(section);

    const auto make_nav = [sidebar](const QString& text, const QString& role,
                                          const QString& tip) {
        QToolButton* button = make_action_button(text, role, sidebar, 18);
        button->setCheckable(true);
        button->setToolTip(tip);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMinimumHeight(ui_px(34));
        return button;
    };
    nav_edit_ = make_nav(QStringLiteral("编辑"), QStringLiteral("edit"),
                         QStringLiteral("编辑（Alt+1~4 在条目间跳）"));
    nav_week_ = make_nav(QStringLiteral("课表总览"), QStringLiteral("week"),
                         QStringLiteral("课表总览"));
    nav_settings_ = make_nav(QStringLiteral("设置"), QStringLiteral("settings"),
                             QStringLiteral("设置"));
    nav_credits_ = make_nav(QStringLiteral("致谢"), QStringLiteral("credits"),
                            QStringLiteral("致谢（Credits）"));
    nav_edit_->setChecked(true);
    layout->addWidget(nav_edit_);
    layout->addWidget(nav_week_);
    layout->addWidget(nav_settings_);
    layout->addWidget(nav_credits_);
    connect(nav_edit_, &QToolButton::clicked, this, [this]() { on_page_clicked(0); });
    connect(nav_week_, &QToolButton::clicked, this, [this]() { on_page_clicked(1); });
    connect(nav_settings_, &QToolButton::clicked, this, [this]() { on_page_clicked(2); });
    connect(nav_credits_, &QToolButton::clicked, this, [this]() { on_page_clicked(3); });

    layout->addStretch(1);

    auto* footer = new QWidget(sidebar);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(ui_px(7));
    footer_dot_ = new QLabel(QStringLiteral("●"), footer);
    footer_text_ = new QLabel(footer);
    footer_layout->addWidget(footer_dot_);
    footer_layout->addWidget(footer_text_, 1);
    layout->addWidget(footer);
    return sidebar;
#endif
}

QWidget* MainWindow::build_toolbar() {
    auto* toolbar = new QWidget(this);
    auto* layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(ui_px(12), ui_px(5), ui_px(12), ui_px(5));
    layout->setSpacing(ui_px(8));
    toolbar_layout_ = layout;

    // --- group 1: sidebar toggle and title -------------------------------------
    hamburger_ = make_fluent_icon_button(QStringLiteral("hamburger"), 18, toolbar);
    hamburger_->setToolTip(QStringLiteral("收起 / 展开侧边栏（Ctrl+B）"));
    set_fluent_icon_raised(hamburger_, false);
    connect(hamburger_, &QAbstractButton::clicked, this, &MainWindow::on_toggle_sidebar);
    layout->addWidget(hamburger_);

    auto* brand = make_fluent_subtitle(QStringLiteral("每日作业"), toolbar);
    layout->addWidget(brand);

    layout->addWidget(make_fluent_divider(Qt::Vertical, toolbar));

    // --- group 2: previous arrow, date strip, next arrow, filter ---------------
    previous_ = make_fluent_icon_button(QStringLiteral("previous"), 16, toolbar);
    previous_->setToolTip(QStringLiteral("上一天（←）"));
    set_fluent_icon_raised(previous_, true);
    connect(previous_, &QAbstractButton::clicked, this, &MainWindow::on_previous_clicked);
    layout->addWidget(previous_);

    // Overflow arrow for the date strip: hidden while every chip fits, shown on the
    // side that can still scroll when the window is too narrow. It moves the strip by
    // whole chips; it is not the one-day previous/next step.
    strip_left_ = make_fluent_icon_button(QStringLiteral("strip-left"), kStripArrowSide, toolbar);
    strip_left_->setToolTip(QStringLiteral("向左滚动日期栏"));
    set_fluent_icon_raised(strip_left_, false);
    connect(strip_left_, &QAbstractButton::clicked, this, [this]() { scroll_date_strip(-1); });
    strip_left_->hide();
    layout->addWidget(strip_left_);

    auto* dates = new QWidget(toolbar);
    date_layout_ = new QHBoxLayout(dates);
    date_layout_->setContentsMargins(0, 0, 0, 0);
    date_layout_->setSpacing(ui_px(4));
    date_scroll_ = new QScrollArea(toolbar);
    date_scroll_->setWidget(dates);
    date_scroll_->setWidgetResizable(true);
    date_scroll_->setFrameShape(QFrame::NoFrame);
    date_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    date_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    date_scroll_->setFixedHeight(date_chip_height() + ui_px(kChipScrollPad));
    // The strip owns the slack between the arrows and the filter, so as many days as
    // fit stay visible; when the window is narrower than the strip it shrinks and the
    // overflow arrows (or the wheel) move it. The current day is always scrolled back
    // into view.
    date_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    date_scroll_->setMinimumWidth(ui_px(120));
    // The viewport is painted by the style rather than by the palette it inherits, so it
    // is pinned to the live palette explicitly; otherwise it stays light in dark mode.
    date_scroll_->viewport()->setAutoFillBackground(true);
    date_scroll_->viewport()->setBackgroundRole(QPalette::Window);
    dates->setAutoFillBackground(true);
    dates->setBackgroundRole(QPalette::Window);
    date_scroll_->viewport()->installEventFilter(this);
    date_scroll_->installEventFilter(this);
    connect(date_scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { update_date_strip_overflow(); });
    date_scroll_->setToolTip(QStringLiteral("滚轮可左右滚动日期栏"));
    layout->addWidget(date_scroll_, 1);

    strip_right_ =
        make_fluent_icon_button(QStringLiteral("strip-right"), kStripArrowSide, toolbar);
    strip_right_->setToolTip(QStringLiteral("向右滚动日期栏"));
    set_fluent_icon_raised(strip_right_, false);
    connect(strip_right_, &QAbstractButton::clicked, this, [this]() { scroll_date_strip(1); });
    strip_right_->hide();
    layout->addWidget(strip_right_);

    next_ = make_fluent_icon_button(QStringLiteral("next"), 16, toolbar);
    next_->setToolTip(QStringLiteral("下一天（→）"));
    set_fluent_icon_raised(next_, true);
    connect(next_, &QAbstractButton::clicked, this, &MainWindow::on_next_clicked);
    layout->addWidget(next_);

    // All / submit-only filter. The entries spell out what they keep in their tooltip.
    segments_ = make_fluent_combo(toolbar);
    segments_->addItems({QStringLiteral("全部"), QStringLiteral("仅要交")});
    segments_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    segments_->setToolTip(QStringLiteral(
        "筛选作业：「全部」显示所有条目；「仅要交」只保留勾选「要交」的条目，"
        "放映与手动出图同样生效"));
    segments_->setItemData(0, QStringLiteral("显示这一天的全部作业条目"), Qt::ToolTipRole);
    segments_->setItemData(1,
                           QStringLiteral("只显示勾选「要交」的条目；放映与手动出图同样生效"),
                           Qt::ToolTipRole);
    connect(segments_, &QComboBox::activated, this, &MainWindow::on_segment_clicked);
    layout->addWidget(segments_);

    // --- group 3: today, present, image, quit ---------------------------------
    layout->addWidget(make_fluent_divider(Qt::Vertical, toolbar));

#ifdef DHW_UI_FLUENT
    // WinUI command bar: the four actions are borrowed QActions split into the primary
    // (today/present/image) and secondary (quit) sections, so the bar draws the group
    // separator and moves members into its overflow when the window gets narrow.
    command_bar_ = new fluent::menus_toolbars::CommandBar(toolbar);
    command_bar_->setLabelPosition(fluent::menus_toolbars::CommandBar::LabelPosition::Right);
    command_bar_->setDynamicOverflowEnabled(false);
    command_bar_->setBackgroundVisible(false);

    const auto make_command = [this](const QString& text, const QString& role,
                                     const QString& tip) {
        auto* action =
            new QAction(QIcon(theme_icon(role, ui_px(16), accent_color())), text, this);
        action->setToolTip(tip);
        action->setProperty("dhwRole", role);
        return action;
    };
    today_action_ =
        make_command(QStringLiteral("今天"), QStringLiteral("today"),
                     QStringLiteral("回到今天（Ctrl+T）"));
    present_action_ =
        make_command(QStringLiteral("放映"), QStringLiteral("play"),
                     QStringLiteral("全屏放映（F5）"));
    image_action_ =
        make_command(QStringLiteral("出图"), QStringLiteral("image"),
                     QStringLiteral("生成当前这一天的图片（Ctrl+D）"));
    quit_action_ =
        make_command(QStringLiteral("退出"), QStringLiteral("quit"),
                     QStringLiteral("完全退出（Ctrl+Q）：先生成今天的作业图片，然后退出程序"));
    connect(today_action_, &QAction::triggered, this, &MainWindow::on_today_clicked);
    connect(present_action_, &QAction::triggered, this, &MainWindow::on_present_clicked);
    connect(image_action_, &QAction::triggered, this, &MainWindow::on_image_clicked);
    connect(quit_action_, &QAction::triggered, this, &MainWindow::on_quit_clicked);
    command_bar_->addPrimaryAction(today_action_);
    command_bar_->addPrimaryAction(present_action_);
    command_bar_->addPrimaryAction(image_action_);
    layout->addWidget(command_bar_);
    // CommandBar puts secondary commands into its overflow by design, which would hide
    // the only way out behind an ellipsis. A second one command bar keeps 退出 visible;
    // the divider between the two is the command group boundary.
    layout->addWidget(make_fluent_divider(Qt::Vertical, toolbar));
    quit_bar_ = new fluent::menus_toolbars::CommandBar(toolbar);
    quit_bar_->setLabelPosition(fluent::menus_toolbars::CommandBar::LabelPosition::Right);
    quit_bar_->setDynamicOverflowEnabled(false);
    quit_bar_->setBackgroundVisible(false);
    quit_bar_->addPrimaryAction(quit_action_);
    layout->addWidget(quit_bar_);
#else
    today_button_ =
        make_fluent_action_button(QStringLiteral("今天"), QStringLiteral("today"), false,
                                  toolbar);
    today_button_->setToolTip(QStringLiteral("回到今天（Ctrl+T）"));
    connect(today_button_, &QAbstractButton::clicked, this, &MainWindow::on_today_clicked);
    layout->addWidget(today_button_);

    present_button_ =
        make_fluent_action_button(QStringLiteral("放映"), QStringLiteral("play"), false,
                                  toolbar);
    present_button_->setToolTip(QStringLiteral("全屏放映（F5）"));
    connect(present_button_, &QAbstractButton::clicked, this, &MainWindow::on_present_clicked);
    layout->addWidget(present_button_);

    image_button_ =
        make_fluent_action_button(QStringLiteral("出图"), QStringLiteral("image"), false,
                                  toolbar);
    image_button_->setToolTip(QStringLiteral("生成当前这一天的图片（Ctrl+D）"));
    connect(image_button_, &QAbstractButton::clicked, this, &MainWindow::on_image_clicked);
    layout->addWidget(image_button_);

    quit_button_ =
        make_fluent_action_button(QStringLiteral("退出"), QStringLiteral("quit"), false,
                                  toolbar);
    quit_button_->setToolTip(
        QStringLiteral("完全退出（Ctrl+Q）：先生成今天的作业图片，然后退出程序"));
    connect(quit_button_, &QAbstractButton::clicked, this, &MainWindow::on_quit_clicked);
    layout->addWidget(quit_button_);
#endif

    refresh_toolbar_actions();
    toolbar->setFixedHeight(
        std::max(ui_px(kToolbarHeight), date_chip_height() + ui_px(kToolbarPad)));
    return toolbar;
}

void MainWindow::refresh_nav_icons() {
#ifdef DHW_UI_FLUENT
    if (nav_model_ == nullptr) {
        return;
    }
    // NavigationView keeps item lists caller-owned; the pane items are a ListView with
    // a tiny QStandardItemModel. The vector pixmaps carry a colour, so a light/dark
    // switch re-tints them here.
    const QColor color = QApplication::palette().color(QPalette::WindowText);
    for (int row = 0; row < nav_model_->rowCount(); ++row) {
        QStandardItem* item = nav_model_->item(row);
        if (item == nullptr) {
            continue;
        }
        const QString role = item->data(Qt::UserRole).toString();
        const QPixmap pixmap = theme_icon(role, ui_px(18), color);
        if (!pixmap.isNull()) {
            item->setIcon(QIcon(pixmap));
        }
    }
    // The compact rail uses the same roles but the shell tints its icons with the
    // accent colour (like the toolbar), so it is re-tinted from the same sweep.
    const QColor rail_color = accent_color();
    for (ActionControl* button : nav_rail_buttons_) {
        if (button == nullptr) {
            continue;
        }
        const QString role = button->property("dhwRole").toString();
        const int side = ui_px(kCompactRailIcon);
        const QPixmap pixmap = theme_icon(role, side, rail_color);
        if (!pixmap.isNull()) {
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(side, side));
            button->setProperty("dhwIconSide", side);
        }
    }
#endif
}

void MainWindow::set_present_action_enabled(bool enabled) {
#ifdef DHW_UI_FLUENT
    if (present_action_ != nullptr) {
        present_action_->setEnabled(enabled);
    }
#else
    if (present_button_ != nullptr) {
        present_button_->setEnabled(enabled);
    }
#endif
}

void MainWindow::refresh_toolbar_actions() {
#ifdef DHW_UI_FLUENT
    // CommandBar keeps the borrowed QAction; re-setting its icon is what re-tints the
    // bar after a light/dark switch. Quit is the only destructive command.
    const auto set_icon = [](QAction* action, const QColor& color) {
        if (action == nullptr) {
            return;
        }
        const QString role = action->property("dhwRole").toString();
        const QPixmap pixmap = theme_icon(role, ui_px(16), color);
        if (!pixmap.isNull()) {
            action->setIcon(QIcon(pixmap));
        }
    };
    const QColor accent = accent_color();
    set_icon(today_action_, accent);
    set_icon(present_action_, accent);
    set_icon(image_action_, accent);
    set_icon(quit_action_, danger_color());
#else
    // Quitting is the only destructive action in the bar. QPalette::BrightText is the
    // role the platform reserves for a colour that has to stand out, and the platform
    // style paints a tool button label with ButtonText, so both carry the semantic
    // danger colour; the vector icon is re-tinted to match because a pixmap cannot.
    if (quit_button_ == nullptr) {
        return;
    }
    const QColor danger = danger_color();
    QPalette palette = quit_button_->palette();
    palette.setColor(QPalette::BrightText, danger);
    palette.setColor(QPalette::ButtonText, danger);
    quit_button_->setPalette(palette);
    const int side = std::max(12, quit_button_->iconSize().width());
    quit_button_->setIcon(QIcon(theme_icon(QStringLiteral("quit"), side, danger)));
    quit_button_->setIconSize(QSize(side, side));
#endif
}

void MainWindow::install_shortcuts() {
    add_shortcut(this, QKeySequence(Qt::Key_F5), [this]() { toggled_present_placeholder(); });
    add_shortcut(this, QKeySequence(Qt::Key_Escape), [this]() {
        if (presenting_) {
            exit_present();
        } else if (pages_ != nullptr && pages_->currentIndex() != 0) {
            show_page(QStringLiteral("edit"));
        }
    });
    add_shortcut(this, QKeySequence(Qt::Key_Left), [this]() {
        if (!presenting_ && pages_ != nullptr && pages_->currentIndex() == 0 &&
            !focus_accepts_text()) {
            navigate_day(-1);
        }
    });
    add_shortcut(this, QKeySequence(Qt::Key_Right), [this]() {
        if (!presenting_ && pages_ != nullptr && pages_->currentIndex() == 0 &&
            !focus_accepts_text()) {
            navigate_day(1);
        }
    });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+T")), [this]() { jump_to_today(); });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+K")), [this]() {
        if (!presenting_) {
            on_page_clicked(1);
        }
    });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+,")), [this]() {
        if (!presenting_) {
            on_page_clicked(2);
        }
    });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+S")), [this]() { save_now(); });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+D")), [this]() { export_image(); });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+Q")), [this]() { quit_app(); });
    add_shortcut(this, QKeySequence(QStringLiteral("Ctrl+B")), [this]() {
        if (!presenting_) {
            toggle_sidebar();
        }
    });
    add_shortcut(this, QKeySequence(QStringLiteral("Alt+1")), [this]() { focus_page_section(1); });
    add_shortcut(this, QKeySequence(QStringLiteral("Alt+2")), [this]() { focus_page_section(2); });
    add_shortcut(this, QKeySequence(QStringLiteral("Alt+3")), [this]() { focus_page_section(3); });
    add_shortcut(this, QKeySequence(QStringLiteral("Alt+4")), [this]() { focus_page_section(4); });
}

void MainWindow::toggled_present_placeholder() {
    if (presenting_) {
        exit_present();
    } else {
        enter_present();
    }
}

void MainWindow::focus_page_section(int section) {
    if (presenting_ || editor_ == nullptr) {
        return;
    }
    show_page(QStringLiteral("edit"));
    QLineEdit* target = nullptr;
    switch (section) {
        case 1:
            target = editor_->announcement_new_input();
            break;
        case 2:
            target = editor_->first_homework_input();
            break;
        case 3:
            target = editor_->first_speech_input();
            break;
        case 4:
            target = editor_->first_schedule_input();
            break;
        default:
            break;
    }
    if (target != nullptr) {
        target->setFocus();
        target->setCursorPosition(target->text().size());
    }
}

void MainWindow::on_controller_changed() {
    refresh_state();
}

void MainWindow::on_save_failed(const QString& message) {
    show_toast(message, ToastTone::Error);
    refresh_footer();
}

void MainWindow::on_appearance_changed() {
    apply_theme();
}

void MainWindow::refresh_state() {
    const QString date = controller_->current_date();
    if (date != current_date_) {
        current_date_ = date;
    }
    const LocalSettings settings = controller_->local_settings();
    const QString appearance = from_std(settings.appearance);
    if (appearance != appearance_) {
        appearance_ = appearance;
        apply_theme();
    }
    const std::vector<std::string> dates = controller_->navigation_dates();
    QStringList list;
    list.reserve(static_cast<int>(dates.size()));
    for (const std::string& entry : dates) {
        list << from_std(entry);
    }
    if (list != nav_cache_) {
        nav_cache_ = list;
        rebuild_date_bar();
    }
    // The day list can stay the same while the page day moves (←/→, a clicked chip), so
    // the chips are re-styled on every refresh instead of only when they were rebuilt.
    refresh_date_chips();
    updating_segments_ = true;
    segments_->setCurrentIndex(controller_->submit_only() ? 1 : 0);
    updating_segments_ = false;
    hamburger_->setChecked(sidebar_collapsed_);
    set_present_action_enabled(!presenting_);
    refresh_footer();
    if (editor_ != nullptr) {
        editor_->refresh_cards();
    }
    if (!date_chips_.isEmpty()) {
        // The strip width can change with the window and with the sidebar; the page day
        // is scrolled back into the visible part once the layout has settled.
        QTimer::singleShot(0, this, [this]() {
            update_date_strip_overflow();
            ensure_current_chip_visible();
        });
    }
}

void MainWindow::refresh_footer() {
    if (footer_text_ == nullptr) {
        return;
    }
    const bool dirty = controller_->dirty();
#ifdef DHW_UI_FLUENT
    // The WinUI info badge carries the saved/unsaved state semantically; the folder
    // name stays plain text next to it.
    if (footer_badge_ != nullptr) {
        footer_badge_->setStatus(
            dirty ? fluent::status_info::InfoBadge::InfoBadgeStatus::Attention
                  : fluent::status_info::InfoBadge::InfoBadgeStatus::Success);
        footer_badge_->setToolTip(dirty ? QStringLiteral("有未保存的修改")
                                        : QStringLiteral("已保存到数据目录"));
    }
#else
    // The dot is a plain label whose colour comes from the live palette: the platform
    // accent while everything is saved, the semantic red while there are unsaved edits.
    if (footer_dot_ != nullptr) {
        QPalette dot_palette = footer_dot_->palette();
        dot_palette.setColor(QPalette::WindowText, dirty ? danger_color() : accent_color());
        footer_dot_->setPalette(dot_palette);
    }
#endif
    footer_text_->setText(folder_name());
    footer_text_->setToolTip(QStringLiteral("数据目录；圆点 = 已保存 / 未保存"));
    if (controller_->save_failed() && !controller_->last_error().isEmpty()) {
        footer_text_->setToolTip(controller_->last_error());
    } else if (controller_->recovered_from_backup()) {
        footer_text_->setToolTip(QStringLiteral("已从备份恢复"));
    }
}

QString MainWindow::folder_name() const {
    QDir dir(controller_->data_dir());
    const QString name = dir.dirName();
    return name.isEmpty() ? QStringLiteral("data") : name;
}

void MainWindow::rebuild_date_bar() {
    date_chips_.clear();
    while (QLayoutItem* item = date_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    chip_dates_ = nav_cache_;
    for (const QString& date : chip_dates_) {
        // Two lines inside one ordinary tool button: the month.day in the monospaced
        // font, the weekday underneath. The labels are transparent for the mouse, so a
        // click anywhere on the chip reaches the button itself.
#ifdef DHW_UI_FLUENT
        // The Fluent Button paints its own text, and it wraps the weekday onto the second
        // line because the label contains a newline. A child label layout used to be
        // clipped by the fixed chip width, which hid the leading digit.
        auto* chip = new fluent::basicinput::Button(
            chip_day_month(date) + QLatin1Char('\n') +
                short_weekday(from_std(dhw::weekday_cn(to_std(date)))),
            nullptr);
        chip->setFluentStyle(fluent::basicinput::Button::Subtle);
        chip->setFluentSize(fluent::basicinput::Button::StandardSize);
#else
        auto* chip = new QToolButton();
        chip->setAutoRaise(false);
        chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
#endif
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("date", date);
        chip->setFixedSize(date_chip_width(), date_chip_height());
        chip->setToolTip(chip_tooltip(date));
        chip->setAccessibleName(chip->toolTip());

#ifndef DHW_UI_FLUENT
        auto* chip_layout = new QVBoxLayout(chip);
        chip_layout->setContentsMargins(0, 0, 0, 0);
        chip_layout->setSpacing(0);

        auto* number = new QLabel(chip_day_month(date), chip);
        number->setObjectName(QStringLiteral("chipDate"));
        number->setAlignment(Qt::AlignCenter);
        number->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* weekday = new QLabel(short_weekday(from_std(dhw::weekday_cn(to_std(date)))), chip);
        weekday->setObjectName(QStringLiteral("chipWeekday"));
        weekday->setAlignment(Qt::AlignCenter);
        weekday->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        chip_layout->addWidget(number);
        chip_layout->addWidget(weekday);
#endif

        connect(chip, &QAbstractButton::clicked, this, [this, chip, date]() {
            on_date_clicked(date);
            // Clicking the already selected day toggles a checkable button off without
            // any controller signal, so the state is re-asserted here.
            chip->setChecked(date == controller_->current_date());
        });
        date_layout_->addWidget(chip, 0, Qt::AlignVCenter);
        date_chips_.push_back(chip);
    }
    date_layout_->addStretch(1);
    chip_style_count_ = -1;
    refresh_date_chips();
    QTimer::singleShot(0, this, [this]() {
        update_date_strip_overflow();
        ensure_current_chip_visible();
    });
}

void MainWindow::refresh_date_chips() {
    if (controller_ == nullptr) {
        return;
    }
    const QString current = controller_->current_date();
    const QString today = from_std(controller_->effective_today());
    const int width = date_chip_width();
    const int height = date_chip_height();
    const QSize size(width, height);
    // Every controller change lands here, including the 300 ms typing commit; only touch
    // the eight buttons when the page day, today, the font size or the chip count moved.
    if (chip_style_count_ == date_chips_.size() && chip_style_size_ == size &&
        chip_style_current_ == current && chip_style_today_ == today) {
        return;
    }
    for (ActionControl* chip : date_chips_) {
        if (chip == nullptr) {
            continue;
        }
        const QString date = chip->property("date").toString();
        const bool is_today = date == today;
        chip->setFixedSize(width, height);
        chip->setToolTip(is_today ? chip_tooltip(date) + QStringLiteral("（今天）")
                                  : chip_tooltip(date));
        chip->setAccessibleName(chip->toolTip());
        style_date_chip(chip, date == current, is_today);
    }
    // The chips are two lines tall, so the strip and the bar follow the live font size.
    if (date_scroll_ != nullptr) {
        date_scroll_->setFixedHeight(height + ui_px(kChipScrollPad));
    }
    if (toolbar_ != nullptr) {
        toolbar_->setFixedHeight(std::max(ui_px(kToolbarHeight), height + ui_px(kToolbarPad)));
    }
    chip_style_count_ = date_chips_.size();
    chip_style_size_ = size;
    chip_style_current_ = current;
    chip_style_today_ = today;
}

void MainWindow::toast(const QString& message) {
    show_toast(message, ToastTone::Info);
}

void MainWindow::show_toast(const QString& message, ToastTone tone) {
#ifdef DHW_UI_FLUENT
    // WinUI in-app toast: an overlay inside this window, never a dialog and never an
    // operating system notification. It owns its lifetime and dismisses itself after
    // kToastMs, so the message text and the auto-hide stay exactly as they were.
    using Toast = fluent::status_info::Toast;
    Toast::Severity severity = Toast::Severity::Informational;
    switch (tone) {
        case ToastTone::Success:
            severity = Toast::Severity::Success;
            break;
        case ToastTone::Error:
            severity = Toast::Severity::Error;
            break;
        case ToastTone::Info:
            break;
    }
    // One update key keeps the shell on the status-bar semantics the old
    // showMessage() had: a newer message replaces the visible one and resets its
    // timer instead of stacking a second card for the same event.
    Toast::showOrUpdateToast(this, QStringLiteral("dhw-shell-toast"), message, severity,
                             kToastMs, Toast::Bottom,
                             QMargins(ui_px(16), ui_px(16), ui_px(16), ui_px(16)));
#else
    // Native skin: the platform status bar keeps the original behaviour.
    Q_UNUSED(tone);
    statusBar()->showMessage(message, kToastMs);
#endif
}

bool MainWindow::focus_accepts_text() const {
    QWidget* focus = QApplication::focusWidget();
    while (focus != nullptr) {
        if (qobject_cast<QLineEdit*>(focus) != nullptr ||
            qobject_cast<QAbstractSpinBox*>(focus) != nullptr ||
            qobject_cast<QComboBox*>(focus) != nullptr ||
            qobject_cast<QTextEdit*>(focus) != nullptr ||
            qobject_cast<QPlainTextEdit*>(focus) != nullptr) {
            return true;
        }
        focus = focus->parentWidget();
    }
    return false;
}

void MainWindow::on_toggle_sidebar() {
    toggle_sidebar();
}

void MainWindow::toggle_sidebar() {
    set_sidebar_collapsed(!sidebar_collapsed_);
    LocalSettings settings = controller_->local_settings();
    settings.sidebar_collapsed = sidebar_collapsed_;
    controller_->update_local_settings(settings);
}

void MainWindow::set_sidebar_collapsed(bool collapsed) {
    sidebar_collapsed_ = collapsed;
    if (sidebar_ == nullptr) {
        return;
    }
#ifdef DHW_UI_FLUENT
    // NavigationView animates the pane between the compact rail width and the expanded
    // column; Ctrl+B therefore becomes one paneOpen transition and the rail keeps the
    // icons on screen.
    apply_sidebar_mode();
    if (navigation_ != nullptr) {
        navigation_->setPaneOpen(!collapsed);
    }
#else
    sidebar_->setMinimumWidth(0);
    auto* animation = new QPropertyAnimation(sidebar_, "maximumWidth", sidebar_);
    animation->setDuration(180);
    animation->setStartValue(sidebar_->maximumWidth());
    animation->setEndValue(collapsed ? 0 : ui_px(kSidebarWidth));
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
#endif
    if (hamburger_ != nullptr) {
        hamburger_->setChecked(collapsed);
    }
}

void MainWindow::apply_sidebar_mode() {
#ifdef DHW_UI_FLUENT
    const bool compact = sidebar_collapsed_;
    if (brand_label_ != nullptr) {
        brand_label_->setVisible(!compact);
    }
    if (section_label_ != nullptr) {
        section_label_->setVisible(!compact);
    }
    if (nav_list_ != nullptr) {
        nav_list_->setVisible(!compact);
    }
    if (sidebar_rail_ != nullptr) {
        sidebar_rail_->setVisible(compact);
    }
    if (footer_text_ != nullptr) {
        footer_text_->setVisible(!compact);
    }
    if (footer_layout_ != nullptr) {
        footer_layout_->setSpacing(compact ? 0 : ui_px(7));
        footer_layout_->setAlignment(footer_badge_,
                                     compact ? Qt::AlignHCenter : Qt::AlignLeft);
    }
    if (sidebar_layout_ != nullptr) {
        // The rail is 40 px wide with 6 px of chrome on each side; the expanded column
        // keeps the original 8 px inset.
        const int side = compact ? ui_px(6) : ui_px(8);
        sidebar_layout_->setContentsMargins(side, compact ? ui_px(10) : ui_px(14), side,
                                            compact ? ui_px(10) : ui_px(12));
    }
    sync_nav_selection();
#else
    Q_UNUSED(sidebar_collapsed_);
#endif
}

void MainWindow::on_page_clicked(int index) {
    if (pages_ == nullptr) {
        return;
    }
    const bool same = pages_->currentIndex() == index;
    if (same) {
        pages_->setCurrentIndex(0);
    } else {
        pages_->setCurrentIndex(index);
    }
    sync_nav_selection();
}

void MainWindow::sync_nav_selection() {
    if (pages_ == nullptr) {
        return;
    }
    const int index = pages_->currentIndex();
#ifdef DHW_UI_FLUENT
    if (nav_list_ != nullptr) {
        updating_nav_ = true;
        nav_list_->setSelectedIndex(index);
        updating_nav_ = false;
    }
    for (int row = 0; row < nav_rail_buttons_.size(); ++row) {
        ActionControl* button = nav_rail_buttons_.at(row);
        if (button != nullptr) {
            button->setChecked(row == index);
        }
    }
#else
    if (nav_edit_ != nullptr) {
        nav_edit_->setChecked(index == 0);
        nav_week_->setChecked(index == 1);
        nav_settings_->setChecked(index == 2);
        nav_credits_->setChecked(index == 3);
    }
#endif
}

void MainWindow::apply_shell_metrics() {
    const int sidebar_width = ui_px(kSidebarWidth);
#ifdef DHW_UI_FLUENT
    if (navigation_ != nullptr) {
        navigation_->setExpandedPaneWidth(sidebar_width);
        navigation_->setCompactPaneWidth(ui_px(kCompactSidebarWidth));
    }
    const int item_height = ui_px(40);
    if (nav_model_ != nullptr) {
        for (int row = 0; row < nav_model_->rowCount(); ++row) {
            if (QStandardItem* item = nav_model_->item(row)) {
                item->setSizeHint(QSize(0, item_height));
            }
        }
    }
    if (nav_list_ != nullptr) {
        nav_list_->setIconSize(QSize(ui_px(18), ui_px(18)));
        nav_list_->setFixedHeight(nav_model_->rowCount() * item_height + ui_px(8));
    }
    const int rail_side = ui_px(kCompactRailSide);
    const int rail_icon = ui_px(kCompactRailIcon);
    for (ActionControl* button : nav_rail_buttons_) {
        if (button == nullptr) {
            continue;
        }
        button->setFixedSize(rail_side, rail_side);
        const QPixmap pixmap =
            theme_icon(button->property("dhwRole").toString(), rail_icon, accent_color());
        if (!pixmap.isNull()) {
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(rail_icon, rail_icon));
        }
        button->setProperty("dhwIconSide", rail_icon);
    }
    if (sidebar_layout_ != nullptr) {
        sidebar_layout_->setSpacing(ui_px(8));
    }
#else
    if (sidebar_ != nullptr) {
        if (sidebar_collapsed_) {
            sidebar_->setMinimumWidth(0);
            sidebar_->setMaximumWidth(0);
        } else {
            sidebar_->setFixedWidth(sidebar_width);
        }
    }
#endif
    // Arrow hit targets were fixed when the buttons were built; re-apply them for the
    // new display size (the icon side plus the same 14 px Fluent button padding).
    const auto resize_icon_button = [](ActionControl* button, int design_side) {
        if (button == nullptr) {
            return;
        }
        const int side = ui_px(design_side);
        button->setFixedSize(side + ui_px(14), side + ui_px(14));
        const QPixmap pixmap =
            theme_icon(button->property("dhwRole").toString(), side, accent_color());
        if (!pixmap.isNull()) {
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(side, side));
        }
        button->setProperty("dhwIconSide", side);
    };
    resize_icon_button(hamburger_, 18);
    resize_icon_button(previous_, 16);
    resize_icon_button(next_, 16);
    resize_icon_button(strip_left_, kStripArrowSide);
    resize_icon_button(strip_right_, kStripArrowSide);
    if (toolbar_layout_ != nullptr) {
        toolbar_layout_->setContentsMargins(ui_px(12), ui_px(5), ui_px(12), ui_px(5));
        toolbar_layout_->setSpacing(ui_px(8));
    }
    if (date_layout_ != nullptr) {
        date_layout_->setSpacing(ui_px(4));
    }
    if (date_scroll_ != nullptr) {
        date_scroll_->setMinimumWidth(ui_px(120));
    }
    // Forces the chips to re-measure at the live font, which also re-derives the
    // toolbar height from them.
    chip_style_count_ = -1;
    refresh_date_chips();
    // Re-derives the sidebar's mode-dependent margins at the new factor as well.
    apply_sidebar_mode();
    QTimer::singleShot(0, this, [this]() {
        update_date_strip_overflow();
        ensure_current_chip_visible();
    });
}

void MainWindow::ensure_current_chip_visible() {
    if (date_scroll_ == nullptr || date_chips_.isEmpty()) {
        return;
    }
    const QString current = controller_ != nullptr ? controller_->current_date() : QString();
    for (ActionControl* chip : date_chips_) {
        if (chip != nullptr && chip->property("date").toString() == current) {
            // Keep a little air on both sides so the neighbouring day is discoverable.
            date_scroll_->ensureWidgetVisible(chip, ui_px(24), 0);
            return;
        }
    }
}

void MainWindow::update_date_strip_overflow() {
    if (date_scroll_ == nullptr || strip_left_ == nullptr || strip_right_ == nullptr) {
        return;
    }
    QScrollBar* bar = date_scroll_->horizontalScrollBar();
    if (bar == nullptr) {
        return;
    }
    const bool overflow = bar->maximum() > bar->minimum();
    const bool showing = !strip_left_->isHidden();
    if (showing != overflow) {
        strip_left_->setVisible(overflow);
        strip_right_->setVisible(overflow);
        // Showing the arrows narrows the strip by their combined width, so whether the
        // strip still overflows is only known after the layout has run once more. The
        // state is monotonic (showing arrows can only add overflow, hiding them can
        // only remove it), so this settles after one extra pass.
        QTimer::singleShot(0, this, [this]() {
            update_date_strip_overflow();
            ensure_current_chip_visible();
        });
        return;
    }
    strip_left_->setEnabled(bar->value() > bar->minimum());
    strip_right_->setEnabled(bar->value() < bar->maximum());
}

void MainWindow::scroll_date_strip(int direction) {
    if (date_scroll_ == nullptr) {
        return;
    }
    QScrollBar* bar = date_scroll_->horizontalScrollBar();
    if (bar == nullptr) {
        return;
    }
    // Three chips per press: enough to cross a hidden week quickly, small enough to
    // keep the visual context of the strip.
    const int step = std::max(1, (date_chip_width() + ui_px(4)) * 3);
    bar->setValue(bar->value() + direction * step);
}

void MainWindow::show_page(const QString& page) {
    if (page == QStringLiteral("week")) {
        on_page_clicked(1);
    } else if (page == QStringLiteral("settings")) {
        on_page_clicked(2);
    } else if (page == QStringLiteral("credits")) {
        on_page_clicked(3);
    } else {
        on_page_clicked(0);
    }
}

void MainWindow::on_date_clicked(const QString& date) {
    set_current_date(date);
}

void MainWindow::set_current_date(const QString& date) {
    if (date.isEmpty() || date == controller_->current_date()) {
        return;
    }
    if (editor_ != nullptr) {
        editor_->commit_pending();
    }
    controller_->set_submit_only(false);
    controller_->set_current_date(date);
}

void MainWindow::navigate_day(int delta) {
    if (chip_dates_.isEmpty()) {
        return;
    }
    const int current = chip_dates_.indexOf(controller_->current_date());
    int target = current;
    if (current < 0) {
        target = delta > 0 ? 0 : chip_dates_.size() - 1;
        for (int i = 0; i < chip_dates_.size(); ++i) {
            if (chip_dates_.at(i) >= controller_->current_date()) {
                target = delta > 0 ? i : std::max(0, i - 1);
                break;
            }
        }
    } else {
        target = std::min(static_cast<int>(chip_dates_.size()) - 1, std::max(0, current + delta));
    }
    if (target == current) {
        return;
    }
    if (delta > 0 && current == chip_dates_.size() - 1) {
        return;
    }
    if (delta < 0 && current == 0) {
        return;
    }
    set_current_date(chip_dates_.at(target));
}

void MainWindow::jump_to_today() {
    const QString date = from_std(controller_->launch_date());
    if (editor_ != nullptr) {
        editor_->commit_pending();
    }
    controller_->set_current_date(date);
    controller_->set_submit_only(false);
    toast(QStringLiteral("已回到今天"));
}

void MainWindow::on_segment_clicked(int index) {
    if (updating_segments_) {
        return;
    }
    const bool submit_only = index == 1;
    if (submit_only == controller_->submit_only()) {
        return;
    }
    controller_->set_submit_only(submit_only);
    if (editor_ != nullptr) {
        editor_->refresh_cards();
    }
}

void MainWindow::on_previous_clicked() {
    navigate_day(-1);
}

void MainWindow::on_next_clicked() {
    navigate_day(1);
}

void MainWindow::on_today_clicked() {
    jump_to_today();
}

void MainWindow::enter_present() {
    if (presenting_ || present_ == nullptr) {
        return;
    }
    presenting_ = true;
    if (editor_ != nullptr) {
        editor_->commit_pending();
    }
    present_->start();
    present_->setFocus();
}

void MainWindow::exit_present() {
    if (!presenting_) {
        return;
    }
    presenting_ = false;
    if (present_ != nullptr) {
        present_->stop();
    }
    show_page(QStringLiteral("edit"));
    refresh_state();
    activateWindow();
}

void MainWindow::on_present_clicked() {
    toggled_present_placeholder();
}

void MainWindow::export_image() {
    const QString path = controller_->export_current_png();
    if (path.isEmpty()) {
        show_toast(controller_->last_error().isEmpty() ? QStringLiteral("生成图片失败")
                                                       : controller_->last_error(),
                   ToastTone::Error);
        return;
    }
    show_toast(QStringLiteral("已生成：%1").arg(path), ToastTone::Success);
}

void MainWindow::on_image_clicked() {
    export_image();
}

void MainWindow::save_now() {
    if (editor_ != nullptr) {
        editor_->commit_pending();
    }
    controller_->save_now();
    if (controller_->save_failed()) {
        show_toast(controller_->last_error(), ToastTone::Error);
    } else {
        show_toast(QStringLiteral("已保存"), ToastTone::Success);
    }
    refresh_footer();
}

void MainWindow::quit_app() {
    if (quitting_) {
        return;
    }
    quitting_ = true;
#ifdef DHW_UI_FLUENT
    if (quit_action_ != nullptr) {
        quit_action_->setEnabled(false);
    }
#else
    if (quit_button_ != nullptr) {
        quit_button_->setEnabled(false);
    }
#endif
    toast(QStringLiteral("正在生成今天的作业图片…"));
    if (editor_ != nullptr) {
        editor_->commit_pending();
    }
    controller_->save_now();
    controller_->export_today_png();
    QTimer::singleShot(0, this, []() { QCoreApplication::quit(); });
}

void MainWindow::on_quit_clicked() {
    quit_app();
}

void MainWindow::apply_theme() {
    // The widget style itself is installed once at startup (--widget-style); every later
    // appearance or display size change only re-applies the colour scheme and the scale.
    dhw::qt::apply_theme(appearance_, controller_->data().settings.ui_scale);
    apply_shell_metrics();
    refresh_theme_dependent();
}

void MainWindow::refresh_theme_dependent() {
    refresh_footer();
    // Toolbar colours are palette derived: the icons were just re-tinted, the chips need
    // their own colours and the destructive action its danger tint. The appearance can
    // change while the page day and the font stay put, so the chip cache is dropped.
    refresh_toolbar_actions();
    refresh_nav_icons();
    chip_style_count_ = -1;
    refresh_date_chips();
    if (week_ != nullptr) {
        // The week overview tints today's column and the swapped periods from the live
        // palette, so it has to be rebuilt when the appearance changes.
        week_->refresh();
    }
    if (editor_ != nullptr) {
        editor_->refresh_cards();
    }
    if (present_ != nullptr) {
        present_->update();
    }
    update();
}

bool MainWindow::smoke_edit(const QString& subject, const QString& text, bool submit_next) {
    if (controller_ == nullptr || editor_ == nullptr) {
        return false;
    }
    const std::string date = to_std(controller_->current_date());
    const bool added = editor_->smoke_add_homework(subject, text);
    if (added && submit_next) {
        const dhw::Day* stored = controller_->data().find_day(date);
        if (stored != nullptr) {
            dhw::Day day = *stored;
            for (dhw::Homework& item : day.homework) {
                if (from_std(item.subject) == subject && from_std(item.text) == text) {
                    item.submit_next = true;
                }
            }
            controller_->update_day(day);
        }
    }
    controller_->save_now();
    return added;
}

int MainWindow::run_selfcheck() {
    int failures = 0;
    const auto report = [&failures](const QString& name, bool ok, const QString& detail) {
        std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData(),
                    detail.isEmpty() ? "" : (" — " + detail).toUtf8().constData());
        if (!ok) {
            ++failures;
        }
    };

    const std::string date = to_std(controller_->current_date());
    const auto homework_of = [this](const std::string& day) {
        const dhw::Day* entry = controller_->data().find_day(day);
        return entry != nullptr ? static_cast<int>(entry->homework.size()) : 0;
    };
    const int before = homework_of(date);
    smoke_edit(QStringLiteral("数学"), QStringLiteral("自检：练习册 3-1"), true);
    const int after = homework_of(date);
    report(QStringLiteral("editor commit through the controller"), after == before + 1,
           QStringLiteral("homework %1 -> %2").arg(before).arg(after));

    QLineEdit* field = editor_ != nullptr ? editor_->first_homework_input() : nullptr;
    if (field != nullptr) {
        const QString typed = QStringLiteral("自检打字：把这条改成新内容");
        field->setText(typed);
        emit field->textEdited(typed);
        QEventLoop wait;
        QTimer::singleShot(450, &wait, &QEventLoop::quit);
        wait.exec();
        bool found = false;
        const dhw::Day* entry = controller_->data().find_day(date);
        if (entry != nullptr) {
            for (const dhw::Homework& item : entry->homework) {
                if (from_std(item.text) == typed) {
                    found = true;
                }
            }
        }
        report(QStringLiteral("in-place text edit reaches the model after the 300 ms debounce"),
               found, QString());
    } else {
        report(QStringLiteral("in-place text edit reaches the model after the 300 ms debounce"),
               false, QStringLiteral("no homework input"));
    }

    save_now();
    report(QStringLiteral("Ctrl+S saves without failure"), !controller_->save_failed(),
           controller_->last_error());

    const QString start = controller_->current_date();
    navigate_day(1);
    const QString forward = controller_->current_date();
    navigate_day(-1);
    const QString back = controller_->current_date();
    report(QStringLiteral("←/→ day navigation and back"),
           forward != start && back == start,
           QStringLiteral("%1 -> %2 -> %3").arg(start, forward, back));

    show_page(QStringLiteral("settings"));
    const bool settings = pages_->currentIndex() == 2 && settings_ != nullptr &&
                          settings_->morning_cutoff_text() ==
                              from_std(controller_->data().settings.morning_cutoff);
    show_page(QStringLiteral("week"));
    const bool week = pages_->currentIndex() == 1 && week_ != nullptr &&
                      week_->monday().size() == 10;
    show_page(QStringLiteral("edit"));
    report(QStringLiteral("Ctrl+, / Ctrl+K page switching"),
           settings && week && pages_->currentIndex() == 0, QString());

    enter_present();
    const bool presenting = presenting_;
    exit_present();
    report(QStringLiteral("F5 enters and Esc leaves the presentation"),
           presenting && !presenting_ && pages_->currentIndex() == 0, QString());

    toggle_sidebar();
    const bool collapsed = sidebar_collapsed_;
#ifdef DHW_UI_FLUENT
    // Let the pane transition land, then prove the collapsed pane is the compact rail
    // (a real narrow width with all three destination icons) rather than a blank gap.
    {
        QEventLoop wait;
        QTimer::singleShot(400, &wait, &QEventLoop::quit);
        wait.exec();
    }
    const int rail_width = sidebar_ != nullptr ? sidebar_->width() : 0;
    int rail_icons = 0;
    for (ActionControl* button : nav_rail_buttons_) {
        if (button != nullptr && button->isVisible()) {
            ++rail_icons;
        }
    }
    report(QStringLiteral("Ctrl+B keeps a compact icon rail"),
           rail_width >= ui_px(48) && rail_width <= ui_px(56) &&
               rail_icons == nav_rail_buttons_.size(),
           QStringLiteral("rail %1 px, %2 icons").arg(rail_width).arg(rail_icons));
#endif
    toggle_sidebar();
    report(QStringLiteral("Ctrl+B collapses and restores the sidebar"),
           collapsed && !sidebar_collapsed_, QString());

    const QString image = controller_->export_current_png();
    report(QStringLiteral("Ctrl+D writes the current screen PNG"), !image.isEmpty(), image);

    return failures;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (date_scroll_ != nullptr &&
        (watched == date_scroll_ || watched == date_scroll_->viewport())) {
        if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            QScrollBar* bar = date_scroll_->horizontalScrollBar();
            const int delta = std::abs(wheel->angleDelta().x()) > std::abs(wheel->angleDelta().y())
                                  ? wheel->angleDelta().x()
                                  : wheel->angleDelta().y();
            bar->setValue(bar->value() - delta);
            wheel->accept();
            return true;
        }
        if (event->type() == QEvent::Resize) {
            // The strip only knows whether the arrows fit after the new width is laid
            // out, and the page day must not fall out of the visible part on a resize.
            QTimer::singleShot(0, this, [this]() {
                update_date_strip_overflow();
                ensure_current_chip_visible();
            });
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

}  // namespace dhw::qt

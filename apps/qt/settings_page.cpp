#include "settings_page.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "dhw/datetime.h"
#include "dhw/fs.h"
#include "dhw/schedule.h"
#include "shell_widgets.h"
#include "theme.h"

#ifdef DHW_UI_FLUENT
#include <QAbstractButton>

#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/FluentQt.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#endif

namespace dhw::qt {

namespace {

constexpr int kImageIntervalMin = 10;
constexpr int kImageIntervalMax = 3600;
constexpr int kImageIntervalStep = 10;
constexpr int kScaleMin = 50;
constexpr int kScaleMax = 150;
constexpr int kScaleStep = 5;
constexpr int kScaleDefault = 100;
constexpr int kBadgePreviewSide = 72;
constexpr int kDayRowHeight = 32;
constexpr int kDayTableMinHeight = 220;
constexpr int kDayTableMaxHeight = 380;
// Below this page width the two form columns no longer fit side by side, so the page
// folds into one column and the scroll area does the rest.
constexpr int kTwoColumnMinWidth = 1040;

std::string to_std(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString from_std(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString weekday_short(const QString& date) {
    // weekday_cn() is "星期一"; slicing its UTF-8 bytes would produce "周期一". Work
    // from the weekday index instead and keep the short "周一" form.
    const std::optional<int> weekday = dhw::weekday_index(to_std(date));
    static const char* const kNames[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    if (!weekday.has_value() || *weekday < 0 || *weekday > 6) {
        return QString();
    }
    return from_std(kNames[*weekday]);
}

QString source_label(int weekday) {
    static const QStringList names = {QStringLiteral("上周一"), QStringLiteral("上周二"),
                                      QStringLiteral("上周三"), QStringLiteral("上周四"),
                                      QStringLiteral("上周五"), QStringLiteral("上周六"),
                                      QStringLiteral("上周日")};
    if (weekday < 0 || weekday >= names.size()) {
        return QStringLiteral("按当天星期");
    }
    return names.at(weekday);
}

QString date_text(const dhw::Date& date) {
    return from_std(dhw::to_string(date));
}

void fill_source_combo(QComboBox* combo) {
    combo->clear();
    combo->addItem(QStringLiteral("按当天星期"));
    for (int index = 0; index < 7; ++index) {
        combo->addItem(source_label(index) + QStringLiteral("的课"));
    }
}

QLabel* make_label(const QString& text, bool informational, QWidget* parent) {
    // Informational labels must never force the settings page wider than the window; the
    // short row labels keep the plain QLabel.
    if (informational) {
        return new InfoLabel(text, parent);
    }
    return make_fluent_label(text, false, parent);
}

// Field of one form row: the control, then its explanation under it. The help label
// keeps its natural height when there is room and wraps otherwise, so a long sentence
// never widens the whole column.
QWidget* make_field(QWidget* control, const QString& help, QWidget* parent) {
    auto* field = new QWidget(parent);
    auto* layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    layout->addWidget(control);
    if (!help.isEmpty()) {
        auto* hint = make_label(help, true, field);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }
    return field;
}

// Primary or secondary action button in the configured widget family.
ActionControl* make_action(const QString& text, const QString& role, bool primary,
                           QWidget* parent) {
    return make_fluent_action_button(text, role, primary, parent);
}

// The settings page still owns a dedicated "reset this value" affordance; both skins
// render it as a small push button in the field's own row.
QPushButton* make_reset_button(const QString& tip, QWidget* parent) {
#ifdef DHW_UI_FLUENT
    auto* button = make_fluent_action_button(QStringLiteral("恢复默认"), QString(), false,
                                             parent);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
#else
    auto* button = new QPushButton(QStringLiteral("恢复默认"), parent);
#endif
    button->setAutoDefault(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tip);
    return button;
}

QComboBox* make_combo(QWidget* parent = nullptr) {
    return make_fluent_combo(parent);
}

QLineEdit* make_line_edit(const QString& text, QWidget* parent = nullptr) {
    return make_fluent_line_edit(text, parent);
}

// QCheckBox and Fluent's ToggleSwitch share toggled(bool) but not the state API.
void toggle_set(ToggleControl* toggle, bool on) {
    set_fluent_toggle(toggle, on);
}

#ifndef DHW_UI_FLUENT
// Cell of the native day table: the cell widget keeps the style's own padding, and only
// the combo box stretches, so the buttons do not grow into a wide bar.
QWidget* make_cell(QWidget* content, bool wide, QWidget* parent) {
    auto* host = new QWidget(parent);
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(3, 1, 3, 1);
    layout->setSpacing(0);
    if (wide) {
        layout->addWidget(content, 1);
    } else {
        layout->addStretch(1);
        layout->addWidget(content);
        layout->addStretch(1);
    }
    return host;
}

QTableWidgetItem* make_item(const QString& text, Qt::Alignment alignment,
                            const QString& tooltip) {
    auto* item = new QTableWidgetItem(text);
    // The table is read only; only the cell widgets can be edited.
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setTextAlignment(alignment);
    if (!tooltip.isEmpty()) {
        item->setToolTip(tooltip);
    }
    return item;
}
#endif

}  // namespace

SettingsPage::SettingsPage(AppController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    // Idempotent: main.cpp initialises the Fluent resources right after QApplication,
    // but the page also constructs correctly when built on its own in a harness.
    ensure_fluent_ready();
    setObjectName(QStringLiteral("settingsPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    build_header(root);
    build_scroll_body(root);

    refresh();
    connect(controller_, &AppController::changed, this, &SettingsPage::on_controller_changed);
}

void SettingsPage::build_header(QVBoxLayout* root) {
    auto* head = new QWidget(this);
    head->setFixedHeight(52);
    auto* layout = new QHBoxLayout(head);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* hamburger = make_fluent_icon_button(QStringLiteral("hamburger"), 18, head);
    hamburger->setToolTip(QStringLiteral("收起 / 展开侧边栏（Ctrl+B）"));
    set_fluent_icon_raised(hamburger, false);
    connect(hamburger, &QAbstractButton::clicked, this, &SettingsPage::sidebar_toggle_requested);
    layout->addWidget(hamburger);

    auto* back = make_fluent_icon_button(QStringLiteral("previous"), 16, head);
    back->setToolTip(QStringLiteral("返回编辑页（Esc）"));
    set_fluent_icon_raised(back, false);
    connect(back, &QAbstractButton::clicked, this, &SettingsPage::back_requested);
    layout->addWidget(back);

    auto* title = make_fluent_title(QStringLiteral("设置"), head);
    layout->addWidget(title);
    layout->addStretch(1);
    root->addWidget(head);
    root->addWidget(make_fluent_divider(Qt::Horizontal, this));
}

void SettingsPage::build_scroll_body(QVBoxLayout* root) {
    auto* content = new QWidget();
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(16, 14, 16, 16);
    column->setSpacing(12);

#ifdef DHW_UI_FLUENT
    // Page level Fluent InfoBar: the badge state, which is the only fact on this page
    // that can block a control. It replaces no existing widget, so the native build
    // has no equivalent line here.
    notice_ = new fluent::status_info::InfoBar(content);
    notice_->setIsClosable(true);
    notice_->setIsIconVisible(true);
    notice_->setTitle(QStringLiteral("设置"));
    notice_->setMessage(QStringLiteral("改动即时保存，没有保存按钮。"));
    notice_->setIsOpen(true);
    column->addWidget(notice_);
#endif

    // Two equal columns of group boxes. apply_columns() moves every group between the
    // two column slots and the one column layout, so narrowing the window never needs a
    // second set of widgets or a rebuilt page.
    grid_ = new QGridLayout();
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(14);
    grid_->setVerticalSpacing(12);
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, 1);
    column->addLayout(grid_);
    column->addStretch(1);

    auto* scroll = make_scroll(content);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    root->addWidget(scroll, 1);

    general_card_ = build_general_card();
    present_card_ = build_present_card();
    data_card_ = build_data_card();
    days_card_ = build_days_card();
    shortcuts_card_ = build_shortcuts_card();
    about_card_ = build_about_card();

    // Reading order of the grid: general | presentation, data | days, shortcuts | about.
    SectionCard* cards[] = {general_card_, present_card_, data_card_, days_card_,
                             shortcuts_card_, about_card_};
    for (int index = 0; index < 6; ++index) {
        auto* slot = new QWidget();
        auto* slot_layout = new QVBoxLayout(slot);
        slot_layout->setContentsMargins(0, 0, 0, 0);
        slot_layout->setSpacing(0);
        slot_layout->addWidget(cards[index]);
        slot_layout->addStretch(1);
        slots_[index] = slot;
    }
    apply_columns(true);
}

QFormLayout* SettingsPage::make_form(SectionCard* card) {
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // With WrapLongRows a narrow column moves the field under its label instead of
    // forcing the whole page wider than the window.
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    card->body_layout()->addLayout(form);
    return form;
}

void SettingsPage::add_form_row(QFormLayout* form, const QString& label, QWidget* control,
                                const QString& help) {
    auto* name = make_label(label, false, nullptr);
    form->addRow(name, make_field(control, help, nullptr));
}

int SettingsPage::two_column_min_width() const {
    const double scale = std::clamp(controller_->data().settings.ui_scale, 0.5, 1.5);
    return static_cast<int>(std::lround(kTwoColumnMinWidth * scale));
}

void SettingsPage::apply_columns(bool two_columns) {
    if (grid_ == nullptr || two_columns == two_columns_) {
        return;
    }
    two_columns_ = two_columns;
    for (QWidget* slot : slots_) {
        if (slot != nullptr) {
            grid_->removeWidget(slot);
        }
    }
    int row = 0;
    int column = 0;
    for (QWidget* slot : slots_) {
        if (slot == nullptr) {
            continue;
        }
        grid_->addWidget(slot, row, column);
        if (!two_columns) {
            ++row;
            continue;
        }
        ++column;
        if (column == 2) {
            column = 0;
            ++row;
        }
    }
    grid_->setColumnStretch(1, two_columns ? 1 : 0);
}

SectionCard* SettingsPage::build_general_card() {
    auto* card = new SectionCard(dhw::icons::Icon::Sun, QStringLiteral("常规"), QString(), this);
    QFormLayout* form = make_form(card);

    // The shell follows the operating system appearance, so this row is a read only
    // statement instead of a disabled combo box that suggests a choice.
    auto* appearance = make_label(QStringLiteral("跟随系统"), false, nullptr);
    appearance->setToolTip(
        QStringLiteral("外观跟随系统（浅色/深色由操作系统决定）；旧数据里的设置仅作保留"));
    add_form_row(form, QStringLiteral("外观"), appearance,
                 QStringLiteral("跟随系统（深浅由操作系统决定）；放映画面固定浅色"));

    sidebar_default_ = make_fluent_toggle(false);
    connect(sidebar_default_, &ToggleControl::toggled, this, [this](bool checked) {
        if (refreshing_) {
            return;
        }
        dhw::LocalSettings settings = controller_->local_settings();
        settings.sidebar_collapsed = checked;
        controller_->update_local_settings(settings);
        emit toast_requested(QStringLiteral("设置已保存"));
    });
    add_form_row(form, QStringLiteral("侧边栏默认收起"), sidebar_default_,
                 QStringLiteral("只决定下次启动（冷启动/唤出窗口）时收不收；运行中用 Ctrl+B 或汉堡收起不会改这里"));

    scale_slider_ = make_fluent_slider();
    scale_slider_->setRange(kScaleMin, kScaleMax);
    scale_slider_->setSingleStep(kScaleStep);
    scale_slider_->setPageStep(kScaleStep);
    scale_slider_->setMinimumWidth(160);
    scale_slider_->setToolTip(QStringLiteral("拖动即时预览，松手保存"));
    connect(scale_slider_, &QSlider::valueChanged, this, &SettingsPage::preview_scale);
    connect(scale_slider_, &QSlider::sliderReleased, this,
            [this]() { commit_scale(scale_slider_->value()); });
    scale_title_ = make_label(QStringLiteral("100%"), false, nullptr);
    scale_title_->setMinimumWidth(52);
    scale_title_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scale_reset_ = make_reset_button(QStringLiteral("把显示大小恢复成默认的 100%"), nullptr);
    connect(scale_reset_, &QPushButton::clicked, this, &SettingsPage::reset_scale);
    auto* scale_row = new QWidget();
    auto* scale_layout = new QHBoxLayout(scale_row);
    scale_layout->setContentsMargins(0, 0, 0, 0);
    scale_layout->setSpacing(8);
    scale_layout->addWidget(scale_slider_, 1);
    scale_layout->addWidget(scale_title_);
    scale_layout->addWidget(scale_reset_);
    add_form_row(form, QStringLiteral("显示大小"), scale_row,
                 QStringLiteral("与系统显示缩放叠加，只影响编辑页/设置/课表，放映页保持原样"));

    english_split_ = make_fluent_toggle(false);
    connect(english_split_, &ToggleControl::toggled, this, [this](bool checked) {
        if (refreshing_) {
            return;
        }
        dhw::Settings settings = controller_->data().settings;
        settings.english_split = checked;
        save_settings(settings, QStringLiteral("设置已保存"));
    });
    add_form_row(form, QStringLiteral("英语分 A/B 班"), english_split_,
                 QStringLiteral("开启后作业/演讲的学科建议会把「英语」拆成 英语A / 英语B"));
    return card;
}

SectionCard* SettingsPage::build_data_card() {
    auto* card =
        new SectionCard(dhw::icons::Icon::Book, QStringLiteral("数据"), QString(), this);
    QFormLayout* form = make_form(card);

    data_dir_text_ = make_line_edit(controller_->data_dir());
    data_dir_text_->setReadOnly(true);
    data_dir_text_->setMinimumWidth(160);
    data_dir_text_->setToolTip(controller_->data_dir());
    auto* dir_row = new QWidget();
    auto* dir_layout = new QHBoxLayout(dir_row);
    dir_layout->setContentsMargins(0, 0, 0, 0);
    dir_layout->setSpacing(8);
    dir_layout->addWidget(data_dir_text_, 1);
    auto* open_button =
        make_action(QStringLiteral("打开文件夹"), QStringLiteral("folder"), false, dir_row);
    connect(open_button, &QAbstractButton::clicked, this, &SettingsPage::open_data_dir);
    dir_layout->addWidget(open_button);
    auto* copy_button = make_action(QStringLiteral("复制路径"), QString(), false, dir_row);
    copy_button->setToolTip(QStringLiteral("把完整路径复制到剪贴板"));
    connect(copy_button, &QAbstractButton::clicked, this, &SettingsPage::copy_data_dir);
    dir_layout->addWidget(copy_button);
    add_form_row(form, QStringLiteral("数据目录"), dir_row,
                 QStringLiteral("默认用 exe 同级的「data」目录；程序不在 APPDATA "
                                "等系统目录写任何东西，也不记住你选的目录——每次启动都从 "
                                "exe 同级开始，那里没有数据就会问你新建还是指定。"));

    auto* clear_button = make_action(QStringLiteral("清理 exe 同级图片"),
                                     QStringLiteral("image"), false, nullptr);
    connect(clear_button, &QAbstractButton::clicked, this, &SettingsPage::clear_images);
    add_form_row(form, QStringLiteral("清理图片"), clear_button,
                 QStringLiteral("把 exe 同级里除今天以外的家庭作业图移入数据目录的「images」存档"));
    return card;
}

SectionCard* SettingsPage::build_present_card() {
    auto* card = new SectionCard(dhw::icons::Icon::Sun, QStringLiteral("放映"),
                          QStringLiteral("这些设置只对本机生效，不写进共享的 data.json"), this);
    QFormLayout* form = make_form(card);

    interval_spin_ = make_fluent_number(kImageIntervalMin, kImageIntervalMax,
                                        kImageIntervalStep);
    interval_spin_->setFixedWidth(120);
    connect_fluent_number(interval_spin_, this, [this](int value) {
        if (refreshing_) {
            return;
        }
        dhw::Settings settings = controller_->data().settings;
        if (settings.image_interval_secs == value) {
            return;
        }
        settings.image_interval_secs = value;
        save_settings(settings, QStringLiteral("设置已保存"));
    });
    add_form_row(form, QStringLiteral("出图间隔（秒）"), interval_spin_,
                 QStringLiteral("修改后超过该时长自动生成家庭作业图片"));

    morning_cutoff_ = make_fluent_time(QTime(8, 0));
    morning_cutoff_->setToolTip(QStringLiteral("任意 HH:MM，非法输入不做高亮"));
    connect(morning_cutoff_, &TimeControl::timeChanged, this, [this](const QTime&) {
        commit_morning_cutoff();
    });
    add_form_row(form, QStringLiteral("早晨分界线"), morning_cutoff_,
                 QStringLiteral("早于该时间打开自动进入「仅要交」"));

    auto* export_button =
        make_action(QStringLiteral("立即出图"), QStringLiteral("image"), true, nullptr);
    export_button->setToolTip(
        QStringLiteral("按当前日期生成 1920×1080 的家庭作业图片（等同 Ctrl+D）"));
    connect(export_button, &QAbstractButton::clicked, this, &SettingsPage::export_image);
    add_form_row(form, QStringLiteral("出图"), export_button,
                 QStringLiteral("立刻按当前日期生成 1920×1080 的家庭作业图片（等同 Ctrl+D）"));

    badge_preview_ = new QLabel();
    badge_preview_->setFixedSize(kBadgePreviewSide, kBadgePreviewSide);
    badge_preview_->setAlignment(Qt::AlignCenter);
    badge_preview_->setFrameShape(QFrame::StyledPanel);
    badge_preview_->setToolTip(QStringLiteral("数据目录里的 badge.png，仅作预览"));
    badge_status_ = make_label(QStringLiteral("还没上传班徽"), true, nullptr);
    badge_upload_ =
        make_action(QStringLiteral("上传班徽…"), QStringLiteral("reload"), true, nullptr);
    badge_delete_ =
        make_action(QStringLiteral("删除班徽"), QStringLiteral("remove"), false, nullptr);
    connect(badge_upload_, &QAbstractButton::clicked, this, &SettingsPage::upload_badge);
    connect(badge_delete_, &QAbstractButton::clicked, this, &SettingsPage::remove_badge);
    auto* badge_field = new QWidget();
    auto* badge_row = new QHBoxLayout(badge_field);
    badge_row->setContentsMargins(0, 0, 0, 0);
    badge_row->setSpacing(10);
    badge_row->addWidget(badge_preview_, 0, Qt::AlignTop);
    auto* badge_column = new QVBoxLayout();
    badge_column->setContentsMargins(0, 0, 0, 0);
    badge_column->setSpacing(4);
    auto* badge_buttons = new QHBoxLayout();
    badge_buttons->setContentsMargins(0, 0, 0, 0);
    badge_buttons->setSpacing(8);
    badge_buttons->addWidget(badge_upload_);
    badge_buttons->addWidget(badge_delete_);
    badge_buttons->addStretch(1);
    badge_column->addLayout(badge_buttons);
    badge_column->addWidget(badge_status_);
    badge_column->addStretch(1);
    badge_row->addLayout(badge_column, 1);
    add_form_row(form, QStringLiteral("班徽"), badge_field,
                 QStringLiteral("建议上传透明背景的 PNG 图片（例如 512×512），放在蓝色"
                                "标题带上才不会出现一块白底。"));

    show_badge_check_ = make_fluent_toggle(false);
    connect(show_badge_check_, &ToggleControl::toggled, this, [this](bool checked) {
        if (refreshing_) {
            return;
        }
        dhw::Settings settings = controller_->data().settings;
        settings.show_badge = checked;
        save_settings(settings, QStringLiteral("设置已保存"));
    });
    show_badge_hint_ = make_label(QStringLiteral("上传班徽后才能打开"), true, nullptr);
    auto* show_row = new QWidget();
    auto* show_layout = new QHBoxLayout(show_row);
    show_layout->setContentsMargins(0, 0, 0, 0);
    show_layout->setSpacing(8);
    show_layout->addWidget(show_badge_check_);
    show_layout->addWidget(show_badge_hint_);
    show_layout->addStretch(1);
    add_form_row(form, QStringLiteral("放映时显示班徽"), show_row, QString());
    return card;
}

SectionCard* SettingsPage::build_days_card() {
    auto* card = new SectionCard(
        dhw::icons::Icon::Calendar, QStringLiteral("天数管理"),
        QStringLiteral("调休补课日用；「上哪天的课」决定课表模版来源；列表只列工作日（周末调休日用「新增"
                       "日期」加）；无内容的日期可「删除」出列表并「全部恢复」；编辑页日期栏只列「有作业/"
                       "演讲内容的天 + 今天 + 下一上学日 + ★」；「快捷显示」只让那天在首页日期栏多一格，"
                       "不改任何判定"),
        this);
    QFormLayout* form = make_form(card);

    new_date_ = make_fluent_date(QDate::currentDate());
    new_source_ = make_combo();
    fill_source_combo(new_source_);
    new_source_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    new_button_ = make_action(QStringLiteral("新增"), QStringLiteral("plus"), true, nullptr);
    new_button_->setToolTip(QStringLiteral("把这一天加入天数管理列表"));
    connect(new_button_, &QAbstractButton::clicked, this, &SettingsPage::add_date);
    auto* new_row = new QWidget();
    auto* new_layout = new QHBoxLayout(new_row);
    new_layout->setContentsMargins(0, 0, 0, 0);
    new_layout->setSpacing(8);
    new_layout->addWidget(new_date_);
    new_layout->addWidget(new_source_, 1);
    new_layout->addWidget(new_button_);
    add_form_row(form, QStringLiteral("新增日期"), new_row,
                 QStringLiteral("选好日期和课表来源后点「新增」；周末调休日也能加，之后可在下表调整课表"
                                "来源"));

#ifdef DHW_UI_FLUENT
    // Fluent ListView of row widgets. Each row keeps its own ComboBox and Buttons, so
    // the managed dates stay editable without a QTableWidget and its native chrome.
    day_list_ = new fluent::collections::ListView(this);
    day_model_ = new QStandardItemModel(this);
    day_list_->setModel(day_model_);
    day_list_->setSelectionMode(fluent::collections::SelectionMode::None);
    day_list_->setSelectionIndicatorVisible(false);
    day_list_->setBorderVisible(false);
    day_list_->setBackgroundVisible(false);
    day_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    day_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    day_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    day_list_->setUniformItemSizes(true);
    day_list_->setFixedHeight(kDayTableMinHeight);
    auto* list_wrap = new QWidget();
    auto* list_layout = new QVBoxLayout(list_wrap);
    list_layout->setContentsMargins(0, 8, 0, 0);
    list_layout->setSpacing(0);
    list_layout->addWidget(day_list_);
    card->body_layout()->addWidget(list_wrap);
#else
    // Native table instead of one row of widgets per date: four fixed columns (date,
    // weekday, schedule source) plus the two actions, and the table scrolls itself once
    // the list gets long.
    day_table_ = new QTableWidget(0, 5, this);
    day_table_->setHorizontalHeaderLabels({QStringLiteral("日期"), QStringLiteral("星期"),
                                           QStringLiteral("课表来源"),
                                           QStringLiteral("快捷显示"), QStringLiteral("删除")});
    day_table_->verticalHeader()->setVisible(false);
    day_table_->verticalHeader()->setDefaultSectionSize(kDayRowHeight);
    day_table_->horizontalHeader()->setHighlightSections(false);
    day_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // The source combo is the only column that benefits from extra width, so it takes
    // the slack; everything else stays at its content width. A minimum section keeps the
    // two action columns readable even when a large display scale and a narrow window
    // meet (the table then scrolls horizontally instead of collapsing a column).
    day_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    day_table_->horizontalHeader()->setMinimumSectionSize(72);
    day_table_->horizontalHeader()->setStretchLastSection(false);
    day_table_->setSelectionMode(QAbstractItemView::NoSelection);
    day_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    day_table_->setAlternatingRowColors(true);
    day_table_->setWordWrap(false);
    day_table_->setFixedHeight(kDayTableMinHeight);
    auto* table_wrap = new QWidget();
    auto* table_layout = new QVBoxLayout(table_wrap);
    table_layout->setContentsMargins(0, 8, 0, 0);
    table_layout->setSpacing(0);
    table_layout->addWidget(day_table_);
    card->body_layout()->addWidget(table_wrap);
#endif

    auto* footer = new QWidget();
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 6, 0, 0);
    footer_layout->setSpacing(8);
    hidden_count_ = make_label(QString(), true, footer);
    restore_button_ =
        make_action(QStringLiteral("全部恢复"), QStringLiteral("reload"), false, nullptr);
    connect(restore_button_, &QAbstractButton::clicked, this, [this]() {
        controller_->update_hidden_dates({});
        emit toast_requested(QStringLiteral("已恢复移出列表的日期"));
    });
    footer_layout->addWidget(hidden_count_);
    footer_layout->addWidget(restore_button_);
    footer_layout->addStretch(1);
    card->body_layout()->addWidget(footer);
    return card;
}

SectionCard* SettingsPage::build_shortcuts_card() {
    auto* card = new SectionCard(
        dhw::icons::Icon::Check, QStringLiteral("快捷键"),
        QStringLiteral("编辑时可以不离开键盘：Alt+数字跳区、回车加条目、Ctrl+回车标记"), this);
    const QStringList lines = {
        QStringLiteral("Alt+1~4 跳到 公告 / 作业 / 演讲 / 课表"),
        QStringLiteral("Enter 在输入框里新建下一条（课表跳下一节）"),
        QStringLiteral("Ctrl+Enter 标记当前条目（要交 / 公告完成）"),
        QStringLiteral("Alt+↑↓ 在条目之间上下切换"),
        QStringLiteral("←→ 切换上一天 / 下一天"),
        QStringLiteral("F5 进入放映 / 退出放映"),
        QStringLiteral("Ctrl+Z 撤销（Ctrl+Shift+Z 重做）"),
        QStringLiteral("Ctrl+T 回到今天"),
        QStringLiteral("Ctrl+D 立即生成图片"),
        QStringLiteral("Ctrl+K 课表总览"),
        QStringLiteral("Ctrl+S 立即保存"),
        QStringLiteral("Ctrl+Q 完全退出"),
        QStringLiteral("Ctrl+, 设置"),
        QStringLiteral("Esc 退出放映 / 从设置、课表总览返回")};
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(3);
    // Two short columns of seven lines keep the shortcut list from pushing the whole
    // page down by another fifteen rows.
    const int per_column = (lines.size() + 1) / 2;
    for (int index = 0; index < lines.size(); ++index) {
        grid->addWidget(make_label(lines.at(index), false, nullptr), index % per_column,
                        index / per_column);
    }
    grid->setColumnStretch(2, 1);
    card->body_layout()->addLayout(grid);
    return card;
}

SectionCard* SettingsPage::build_about_card() {
    auto* card = new SectionCard(dhw::icons::Icon::Check, QStringLiteral("关于"), QString(), this);
    QVBoxLayout* body = card->body_layout();
    body->addWidget(make_label(QStringLiteral("每日作业 v2.0.0"), false, card));
    const QString data_file = QDir(controller_->data_dir()).filePath(QStringLiteral("data.json"));
    auto* path = make_label(
        QStringLiteral("数据文件：%1（自动备份至 backups\\）").arg(data_file), true, card);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // The about group shares a column now, so the long path has to wrap instead of
    // being clipped at the group box edge.
    path->setWordWrap(true);
    body->addWidget(path);
    return card;
}

void SettingsPage::save_settings(const dhw::Settings& settings, const QString& toast_text) {
    controller_->update_settings(settings);
    emit toast_requested(toast_text);
}

void SettingsPage::open_data_dir() {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(controller_->data_dir()))) {
        emit toast_requested(QStringLiteral("打开失败：%1").arg(controller_->data_dir()));
        return;
    }
    emit toast_requested(QStringLiteral("已打开数据目录"));
}

void SettingsPage::copy_data_dir() {
    QApplication::clipboard()->setText(controller_->data_dir());
    emit toast_requested(QStringLiteral("已复制数据目录路径"));
}

void SettingsPage::export_image() {
    const QString path = controller_->export_current_png();
    if (path.isEmpty()) {
        emit toast_requested(QStringLiteral("出图失败：%1").arg(controller_->last_error()));
        return;
    }
    emit toast_requested(QStringLiteral("已生成图片：%1").arg(path));
}

void SettingsPage::commit_morning_cutoff() {
    if (refreshing_) {
        return;
    }
    const QString text = fluent_time(morning_cutoff_).toString(QStringLiteral("HH:mm"));
    const std::optional<dhw::Time> parsed = dhw::parse_time(to_std(text));
    const QString stored = parsed.has_value() ? text : QStringLiteral("08:00");
    dhw::Settings settings = controller_->data().settings;
    if (from_std(settings.morning_cutoff) == stored) {
        return;
    }
    settings.morning_cutoff = to_std(stored);
    save_settings(settings, QStringLiteral("设置已保存"));
}

void SettingsPage::preview_scale(int percent) {
    scale_title_->setText(QStringLiteral("%1%").arg(percent));
    if (scale_reset_ != nullptr) {
        scale_reset_->setEnabled(percent != kScaleDefault);
    }
    if (refreshing_) {
        return;
    }
    emit theme_preview_requested(static_cast<double>(percent) / 100.0);
}

void SettingsPage::commit_scale(int percent) {
    if (refreshing_) {
        return;
    }
    dhw::Settings settings = controller_->data().settings;
    const double scale = static_cast<double>(percent) / 100.0;
    if (std::abs(settings.ui_scale - scale) < 0.0001) {
        return;
    }
    settings.ui_scale = scale;
    save_settings(settings, QStringLiteral("设置已保存"));
}

void SettingsPage::reset_scale() {
    // The slider itself previews through apply_theme, so restoring the default is
    // visible before the write-through ever reaches the model.
    scale_slider_->setValue(kScaleDefault);
    dhw::Settings settings = controller_->data().settings;
    const double scale = static_cast<double>(kScaleDefault) / 100.0;
    if (std::abs(settings.ui_scale - scale) >= 0.0001) {
        settings.ui_scale = scale;
        save_settings(settings, QStringLiteral("显示大小已恢复默认 100%"));
        return;
    }
    emit toast_requested(QStringLiteral("显示大小已是默认的 100%"));
}

void SettingsPage::clear_images() {
    controller_->clear_images();
    emit toast_requested(QStringLiteral("已清理 exe 同级图片"));
}

void SettingsPage::upload_badge() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择班徽 PNG"),
                                                      controller_->data_dir(),
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image(path);
    if (image.isNull() || !controller_->store_badge(image)) {
        emit toast_requested(QStringLiteral("上传失败：%1").arg(path));
        return;
    }
    dhw::Settings settings = controller_->data().settings;
    settings.show_badge = true;
    controller_->update_settings(settings);
    refresh();
    emit toast_requested(QStringLiteral("班徽已上传；「放映时显示班徽」已打开"));
}

void SettingsPage::remove_badge() {
    const std::string source = dhw::badge_file(to_std(controller_->data_dir()));
    if (!dhw::fs::exists(source)) {
        emit toast_requested(QStringLiteral("还没有上传过班徽"));
        return;
    }
    if (!dhw::fs::rename(source, source + ".removed") && !dhw::fs::remove_file(source)) {
        emit toast_requested(QStringLiteral("删除失败：%1").arg(from_std(source)));
        return;
    }
    dhw::Settings settings = controller_->data().settings;
    settings.show_badge = false;
    controller_->update_settings(settings);
    refresh();
    emit toast_requested(QStringLiteral("班徽已删除"));
}

void SettingsPage::refresh() {
    if (refreshing_) {
        return;
    }
    refresh_pending_ = false;
    refreshing_ = true;
    refresh_general();
    refresh_days();
    refreshing_ = false;
    // The display size may have changed, which changes how much room the two columns
    // really need; the page width itself did not move, so the resize event would miss it.
    apply_columns(width() >= two_column_min_width());
}

void SettingsPage::refresh_general() {
    const dhw::LocalSettings local = controller_->local_settings();
    toggle_set(sidebar_default_, local.sidebar_collapsed);
    data_dir_text_->setText(controller_->data_dir());
    data_dir_text_->setToolTip(controller_->data_dir());

    const dhw::Settings settings = controller_->data().settings;
    set_fluent_number(interval_spin_,
                      std::clamp(settings.image_interval_secs, kImageIntervalMin,
                                 kImageIntervalMax));

    const std::optional<dhw::Time> cutoff = dhw::parse_time(settings.morning_cutoff);
    const dhw::Time time = cutoff.value_or(dhw::Time{8, 0});
    set_fluent_time(morning_cutoff_, QTime(time.hour, time.minute));

    toggle_set(english_split_, settings.english_split);

    const int percent = std::clamp(static_cast<int>(std::lround(settings.ui_scale * 100.0)),
                                   kScaleMin, kScaleMax);
    scale_slider_->setValue(percent);
    scale_title_->setText(QStringLiteral("%1%").arg(percent));
    scale_reset_->setEnabled(percent != kScaleDefault);

    update_badge_preview();
    const bool badge = controller_->badge_available();
    badge_status_->setText(badge ? QStringLiteral("已上传（存在数据目录的 badge.png）")
                                 : QStringLiteral("还没上传班徽"));
    badge_delete_->setEnabled(badge);
    show_badge_check_->setEnabled(badge);
#ifdef DHW_UI_FLUENT
    if (notice_ != nullptr) {
        using InfoBar = fluent::status_info::InfoBar;
        const InfoBar::InfoBarSeverity wanted =
            badge ? InfoBar::Success : InfoBar::Warning;
        const bool state_changed = notice_->severity() != wanted;
        notice_->setSeverity(wanted);
        notice_->setTitle(badge ? QStringLiteral("班徽已就绪")
                                : QStringLiteral("还没上传班徽"));
        notice_->setMessage(badge ? QStringLiteral("「放映时显示班徽」可以打开了。")
                                  : QStringLiteral("上传班徽后才能打开「放映时显示班徽」。"));
        if (state_changed) {
            // A bar the user closed comes back only when the state really flips.
            notice_->setIsOpen(true);
        }
    }
#endif
    show_badge_hint_->setVisible(!badge);
    // Without an uploaded badge the switch cannot stay on: there would be nothing to draw.
    const bool show_badge = settings.show_badge && badge;
    if (show_badge != settings.show_badge) {
        dhw::Settings fixed = settings;
        fixed.show_badge = show_badge;
        controller_->update_settings(fixed);
    }
    toggle_set(show_badge_check_, show_badge);
}

void SettingsPage::update_badge_preview() {
    if (!controller_->badge_available()) {
        badge_preview_->clear();
        badge_preview_->setText(QStringLiteral("无班徽"));
        return;
    }
    const QImage image = controller_->badge_image();
    badge_preview_->setPixmap(QPixmap::fromImage(image).scaled(
        kBadgePreviewSide - 8, kBadgePreviewSide - 8, Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

void SettingsPage::on_controller_changed() {
    // The settings page stays alive while another page is on top; rebuilding its table
    // for every keystroke there would be wasted work.
    if (!isVisible()) {
        refresh_pending_ = true;
        return;
    }
    refresh();
}

void SettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (refresh_pending_) {
        refresh_pending_ = false;
        refresh();
    }
}

void SettingsPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    apply_columns(width() >= two_column_min_width());
}

std::vector<QString> SettingsPage::managed_dates() const {
    std::vector<QString> dates;
    const auto remember = [&dates](const QString& date) {
        if (!date.isEmpty() && std::find(dates.begin(), dates.end(), date) == dates.end()) {
            dates.push_back(date);
        }
    };
    for (const auto& [date, day] : controller_->data().days) {
        if (dhw::day_has_content(&day)) {
            remember(from_std(date));
        }
    }
    const dhw::Date today = dhw::today_local();
    for (int offset = -7; offset <= 60; ++offset) {
        const dhw::Date date = dhw::add_days(today, offset);
        if (dhw::weekday(date) >= 5) {
            continue;
        }
        remember(date_text(date));
    }
    remember(date_text(today));
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    return dates;
}

bool SettingsPage::date_has_content(const QString& date) const {
    return dhw::day_has_content(controller_->data().find_day(to_std(date)));
}

bool SettingsPage::date_pinned(const QString& date) const {
    const std::vector<std::string>& pinned = controller_->data().pinned_dates;
    return std::find(pinned.begin(), pinned.end(), to_std(date)) != pinned.end();
}

bool SettingsPage::date_hidden(const QString& date) const {
    const std::vector<std::string>& hidden = controller_->data().hidden_dates;
    return std::find(hidden.begin(), hidden.end(), to_std(date)) != hidden.end();
}

bool SettingsPage::in_bar_by_default(const QString& date) const {
    if (date_has_content(date)) {
        return true;
    }
    const std::string today = controller_->effective_today();
    if (date == from_std(today)) {
        return true;
    }
    return date == from_std(dhw::next_school_day(controller_->data(), today));
}

void SettingsPage::set_date_pinned(const QString& date, bool pinned) {
    std::vector<std::string> dates = controller_->data().pinned_dates;
    const std::string key = to_std(date);
    dates.erase(std::remove(dates.begin(), dates.end(), key), dates.end());
    if (pinned) {
        dates.push_back(key);
    }
    controller_->update_pinned_dates(std::move(dates));
    emit toast_requested(pinned ? QStringLiteral("%1 已显示在首页日期栏").arg(date)
                                : QStringLiteral("已取消 %1 的快捷显示").arg(date));
}

void SettingsPage::set_copied_weekday(const QString& date, int weekday) {
    const std::string key = to_std(date);
    dhw::Day day;
    if (const dhw::Day* stored = controller_->data().find_day(key)) {
        day = *stored;
    }
    if (weekday >= 0 && weekday < 7) {
        day.copied_from_weekday = weekday;
    } else {
        day.copied_from_weekday.reset();
    }
    if (controller_->current_date() != date) {
        controller_->set_current_date(date);
    }
    controller_->update_day(day);
    emit toast_requested(QStringLiteral("%1 的课表已按%2")
                             .arg(date, weekday >= 0 ? source_label(weekday)
                                                     : QStringLiteral("当天星期")));
}

void SettingsPage::remove_date(const QString& date) {
    if (date_has_content(date)) {
        controller_->delete_day(date);
        emit toast_requested(QStringLiteral("已删除 %1").arg(date));
        return;
    }
    std::vector<std::string> hidden = controller_->data().hidden_dates;
    const std::string key = to_std(date);
    if (std::find(hidden.begin(), hidden.end(), key) == hidden.end()) {
        hidden.push_back(key);
    }
    controller_->update_hidden_dates(std::move(hidden));
    emit toast_requested(QStringLiteral("已把 %1 移出列表").arg(date));
}

void SettingsPage::confirm_remove_date(const QString& date) {
    if (!date_has_content(date)) {
        // Nothing to lose: the date only leaves this list and "全部恢复" brings it back.
        remove_date(date);
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("删除这一天"));
    box.setText(QStringLiteral("%1 有作业 / 演讲 / 公告内容。").arg(date));
    box.setInformativeText(
        QStringLiteral("删除后这些内容会从数据里移除，不能撤销；需要时请从 backups 备份找回。"));
    auto* confirm = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != confirm) {
        return;
    }
    remove_date(date);
}

void SettingsPage::add_date() {
    const QString date = fluent_date(new_date_).toString(QStringLiteral("yyyy-MM-dd"));
    if (!dhw::parse_date(to_std(date)).has_value()) {
        emit toast_requested(QStringLiteral("新增失败：日期无效"));
        return;
    }
    const std::string key = to_std(date);
    std::vector<std::string> hidden = controller_->data().hidden_dates;
    hidden.erase(std::remove(hidden.begin(), hidden.end(), key), hidden.end());
    controller_->update_hidden_dates(std::move(hidden));
    const int weekday = new_source_->currentIndex() - 1;
    if (weekday >= 0 || controller_->data().find_day(key) == nullptr) {
        set_copied_weekday(date, weekday);
    }
    emit toast_requested(QStringLiteral("已新增 %1（有作业内容后才会出现在编辑页日期栏，也可用★快捷"
                                        "显示）")
                             .arg(date));
}

void SettingsPage::rebuild_day_table() {
#ifdef DHW_UI_FLUENT
    if (day_list_ == nullptr || day_model_ == nullptr) {
        return;
    }
#else
    if (day_table_ == nullptr) {
        return;
    }
#endif
    const QString today = date_text(dhw::today_local());
    const QString editing = controller_->current_date();

    std::vector<QString> dates;
    QStringList signature;
    signature << from_std(controller_->effective_today())
              << from_std(controller_->next_school_day()) << editing
              << QString::number(day_row_height());
    for (const QString& date : managed_dates()) {
        if (date_hidden(date) && !date_has_content(date)) {
            continue;
        }
        dates.push_back(date);
        const dhw::Day* stored = controller_->data().find_day(to_std(date));
        const int copied = stored != nullptr && stored->copied_from_weekday.has_value()
                               ? *stored->copied_from_weekday
                               : -1;
        // Every fact the list paints, so an unrelated controller change does not throw
        // away the scroll position or the combo box that is being used.
        signature << QStringLiteral("%1|%2|%3|%4|%5")
                         .arg(date)
                         .arg(date_has_content(date) ? 1 : 0)
                         .arg(date_pinned(date) ? 1 : 0)
                         .arg(copied)
                         .arg(in_bar_by_default(date) ? 1 : 0);
    }
    const QString joined = signature.join(QLatin1Char('\n'));
    const int row_count = static_cast<int>(dates.size());
#ifdef DHW_UI_FLUENT
    const int current_rows = day_model_->rowCount();
#else
    const int current_rows = day_table_->rowCount();
#endif
    if (joined == day_signature_ && current_rows == row_count) {
        return;
    }
    day_signature_ = joined;

    const bool first_build = current_rows == 0;

#ifdef DHW_UI_FLUENT
    const int keep_scroll = day_list_->verticalScrollBar()->value();
    // Row widgets own the combo boxes and buttons; drop them explicitly before the model
    // rows go away so a later refresh never leaks their connections.
    for (int row = 0; row < day_model_->rowCount(); ++row) {
        day_list_->setIndexWidget(day_model_->index(row, 0), nullptr);
    }
    day_list_->setUpdatesEnabled(false);
    day_model_->clear();

    int scroll_target = -1;
    for (int row = 0; row < row_count; ++row) {
        const QString& date = dates.at(static_cast<std::size_t>(row));
        const bool is_today = date == today;
        const bool is_editing = date == editing;
        QString date_text_value = date;
        if (is_today) {
            date_text_value += QStringLiteral("（今天）");
        }
        if (is_editing) {
            date_text_value += QStringLiteral("（正在编辑）");
        }

        auto* item = new QStandardItem();
        item->setEditable(false);
        item->setSelectable(false);
        item->setSizeHint(QSize(0, day_row_height()));
        day_model_->appendRow(item);
        const QModelIndex index = day_model_->index(row, 0);
        if (scroll_target < 0 && date >= today) {
            scroll_target = row;
        }

        auto* row_widget = new QWidget(day_list_);
        auto* row_layout = new QHBoxLayout(row_widget);
        row_layout->setContentsMargins(4, 2, 4, 2);
        row_layout->setSpacing(8);

        auto* date_label =
            new fluent::textfields::Label(date_text_value, row_widget);
        date_label->setFluentTypography(Typography::FontRole::Body);
        if (is_today || is_editing) {
            date_label->setTextColorRole(
                fluent::textfields::Label::TextColorRole::Accent);
            QFont date_font = date_label->font();
            date_font.setBold(true);
            date_label->setFont(date_font);
        }
        date_label->setMinimumWidth(150);
        date_label->setToolTip(date);
        row_layout->addWidget(date_label);

        auto* weekday = new fluent::textfields::Label(weekday_short(date), row_widget);
        weekday->setFluentTypography(Typography::FontRole::Body);
        weekday->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        weekday->setFixedWidth(44);
        weekday->setAlignment(Qt::AlignCenter);
        row_layout->addWidget(weekday);

        auto* source = new fluent::basicinput::ComboBox(row_widget);
        fill_source_combo(source);
        const dhw::Day* stored = controller_->data().find_day(to_std(date));
        const int copied = stored != nullptr && stored->copied_from_weekday.has_value()
                               ? *stored->copied_from_weekday + 1
                               : 0;
        source->setCurrentIndex(copied);
        connect(source, &QComboBox::activated, this, [this, date](int source_index) {
            // Deferred: the rebuild that follows the write would delete this combo while
            // it is still emitting activated().
            QTimer::singleShot(0, this, [this, date, source_index]() {
                set_copied_weekday(date, source_index - 1);
            });
        });
        row_layout->addWidget(source, 1);

        if (in_bar_by_default(date)) {
            auto* fixed = new fluent::textfields::Label(QStringLiteral("默认显示"), row_widget);
            fixed->setFluentTypography(Typography::FontRole::Body);
            fixed->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
            fixed->setAlignment(Qt::AlignCenter);
            fixed->setFixedWidth(110);
            fixed->setToolTip(QStringLiteral(
                "有内容 / 今天 / 下一上学日默认就在首页日期栏，无需快捷显示"));
            row_layout->addWidget(fixed);
        } else {
            const bool pinned = date_pinned(date);
            auto* star = new fluent::basicinput::Button(
                pinned ? QStringLiteral("★ 快捷显示") : QStringLiteral("☆ 快捷显示"),
                row_widget);
            star->setFluentStyle(fluent::basicinput::Button::Subtle);
            star->setFluentSize(fluent::basicinput::Button::Small);
            star->setCursor(Qt::PointingHandCursor);
            star->setToolTip(pinned
                                 ? QStringLiteral("已在首页日期栏显示，点击取消（只影响显示）")
                                 : QStringLiteral("在首页日期栏里给这一天留一格入口（只影响显示，"
                                                  "不改调休/休息日/工作日/今天）"));
            connect(star, &QAbstractButton::clicked, this, [this, date, pinned]() {
                QTimer::singleShot(0, this,
                                   [this, date, pinned]() { set_date_pinned(date, !pinned); });
            });
            row_layout->addWidget(star);
        }

        auto* remove = new fluent::basicinput::Button(QStringLiteral("删除"), row_widget);
        remove->setFluentStyle(fluent::basicinput::Button::Subtle);
        remove->setFluentSize(fluent::basicinput::Button::Small);
        remove->setCriticalOnHover(true);
        remove->setCursor(Qt::PointingHandCursor);
        if (!date_has_content(date)) {
            remove->setToolTip(QStringLiteral("这一天还没有内容，删除只是把它从列表里移出（可全部"
                                              "恢复）"));
        }
        connect(remove, &QAbstractButton::clicked, this, [this, date]() {
            QTimer::singleShot(0, this, [this, date]() { confirm_remove_date(date); });
        });
        row_layout->addWidget(remove);

        day_list_->setIndexWidget(index, row_widget);
    }

    day_list_->setUpdatesEnabled(true);
    if (first_build) {
        if (scroll_target >= 0) {
            day_list_->scrollTo(day_model_->index(scroll_target, 0),
                                QAbstractItemView::PositionAtCenter);
        }
    } else {
        day_list_->verticalScrollBar()->setValue(keep_scroll);
    }
#else
    const int keep_scroll = day_table_->verticalScrollBar()->value();

    // Remove the cell widgets explicitly before the rows go away; the combo boxes and
    // buttons own the connections, and a later refresh must not leak them.
    for (int row = 0; row < day_table_->rowCount(); ++row) {
        for (int column = 2; column <= 4; ++column) {
            day_table_->removeCellWidget(row, column);
        }
    }
    day_table_->setUpdatesEnabled(false);
    day_table_->setRowCount(0);
    day_table_->setRowCount(row_count);

    QTableWidgetItem* scroll_target = nullptr;
    for (int row = 0; row < row_count; ++row) {
        const QString& date = dates.at(static_cast<std::size_t>(row));
        const bool is_today = date == today;
        const bool is_editing = date == editing;
        QString date_label = date;
        if (is_today) {
            date_label += QStringLiteral("（今天）");
        }
        if (is_editing) {
            date_label += QStringLiteral("（正在编辑）");
        }
        auto* date_item = make_item(date_label, Qt::AlignLeft | Qt::AlignVCenter, date);
        QFont date_font = date_item->font();
        date_font.setBold(is_today || is_editing);
        date_item->setFont(date_font);
        day_table_->setItem(row, 0, date_item);
        day_table_->setItem(row, 1,
                            make_item(weekday_short(date), Qt::AlignCenter | Qt::AlignVCenter,
                                      QString()));
        if (scroll_target == nullptr && date >= today) {
            scroll_target = date_item;
        }

        auto* source = make_combo(day_table_);
        fill_source_combo(source);
        const dhw::Day* stored = controller_->data().find_day(to_std(date));
        const int copied = stored != nullptr && stored->copied_from_weekday.has_value()
                               ? *stored->copied_from_weekday + 1
                               : 0;
        source->setCurrentIndex(copied);
        connect(source, &QComboBox::activated, this, [this, date](int index) {
            // Deferred: the rebuild that follows the write would delete this combo while
            // it is still emitting activated().
            QTimer::singleShot(0, this, [this, date, index]() {
                set_copied_weekday(date, index - 1);
            });
        });
        day_table_->setCellWidget(row, 2, make_cell(source, true, day_table_));

        if (in_bar_by_default(date)) {
            auto* fixed = new QLabel(QStringLiteral("默认显示"), day_table_);
            fixed->setAlignment(Qt::AlignCenter);
            fixed->setToolTip(QStringLiteral("有内容 / 今天 / 下一上学日默认就在首页日期栏，无需快捷"
                                             "显示"));
            day_table_->setCellWidget(row, 3, make_cell(fixed, false, day_table_));
        } else {
            const bool pinned = date_pinned(date);
            auto* star = new QPushButton(
                pinned ? QStringLiteral("★ 快捷显示") : QStringLiteral("☆ 快捷显示"),
                day_table_);
            star->setAutoDefault(false);
            star->setCursor(Qt::PointingHandCursor);
            star->setToolTip(pinned
                                 ? QStringLiteral("已在首页日期栏显示，点击取消（只影响显示）")
                                 : QStringLiteral("在首页日期栏里给这一天留一格入口（只影响显示，"
                                                  "不改调休/休息日/工作日/今天）"));
            connect(star, &QPushButton::clicked, this, [this, date, pinned]() {
                QTimer::singleShot(0, this,
                                   [this, date, pinned]() { set_date_pinned(date, !pinned); });
            });
            day_table_->setCellWidget(row, 3, make_cell(star, false, day_table_));
        }

        auto* remove = new QPushButton(QStringLiteral("删除"), day_table_);
        remove->setAutoDefault(false);
        remove->setCursor(Qt::PointingHandCursor);
        if (!date_has_content(date)) {
            remove->setToolTip(QStringLiteral("这一天还没有内容，删除只是把它从列表里移出（可全部"
                                              "恢复）"));
        }
        connect(remove, &QPushButton::clicked, this, [this, date]() {
            QTimer::singleShot(0, this, [this, date]() { confirm_remove_date(date); });
        });
        day_table_->setCellWidget(row, 4, make_cell(remove, false, day_table_));
    }

    day_table_->setUpdatesEnabled(true);

    if (first_build) {
        if (scroll_target != nullptr) {
            day_table_->scrollToItem(scroll_target, QAbstractItemView::PositionAtCenter);
        }
    } else {
        day_table_->verticalScrollBar()->setValue(keep_scroll);
    }
#endif
}

void SettingsPage::refresh_footer_bar() {
    const int hidden = static_cast<int>(controller_->data().hidden_dates.size());
    hidden_count_->setText(QStringLiteral("已移出列表 %1 天").arg(hidden));
    hidden_count_->setVisible(hidden > 0);
    restore_button_->setVisible(hidden > 0);
}

void SettingsPage::update_day_table_height() {
#ifdef DHW_UI_FLUENT
    if (day_list_ == nullptr || day_model_ == nullptr) {
        return;
    }
    const int wanted = day_row_height() * day_model_->rowCount() + 4;
    day_list_->setFixedHeight(std::clamp(wanted, kDayTableMinHeight, kDayTableMaxHeight));
#else
    if (day_table_ == nullptr) {
        return;
    }
    const int row_height = day_table_->verticalHeader()->defaultSectionSize();
    const int header = std::max(26, day_table_->horizontalHeader()->sizeHint().height());
    const int wanted = header + row_height * day_table_->rowCount() + 2;
    day_table_->setFixedHeight(std::clamp(wanted, kDayTableMinHeight, kDayTableMaxHeight));
#endif
}

void SettingsPage::refresh_days() {
    // Rows carry combos and buttons, so they follow the application font; the page
    // itself can be scaled up to 150%.
#ifdef DHW_UI_FLUENT
    if (day_list_ == nullptr) {
        return;
    }
#else
    day_table_->verticalHeader()->setDefaultSectionSize(day_row_height());
#endif
    rebuild_day_table();
    update_day_table_height();
    refresh_footer_bar();
}

int SettingsPage::day_row_height() const {
    return std::max(kDayRowHeight, fontMetrics().height() + 18);
}

QString SettingsPage::morning_cutoff_text() const {
    return fluent_time(morning_cutoff_).toString(QStringLiteral("HH:mm"));
}

int SettingsPage::ui_scale_percent() const {
    return scale_slider_->value();
}

bool SettingsPage::set_morning_cutoff(const QString& text) {
    const std::optional<dhw::Time> parsed = dhw::parse_time(to_std(text));
    const QString stored = parsed.has_value() ? text : QStringLiteral("08:00");
    dhw::Settings settings = controller_->data().settings;
    if (from_std(settings.morning_cutoff) == stored) {
        return parsed.has_value();
    }
    set_fluent_time(morning_cutoff_, parsed.has_value()
                                        ? QTime(parsed->hour, parsed->minute)
                                        : QTime(8, 0));
    settings.morning_cutoff = to_std(stored);
    save_settings(settings, QStringLiteral("设置已保存"));
    return parsed.has_value();
}

bool SettingsPage::set_ui_scale(int percent) {
    const int clamped = std::clamp(percent, kScaleMin, kScaleMax);
    scale_slider_->setValue(clamped);
    commit_scale(clamped);
    return true;
}

}  // namespace dhw::qt

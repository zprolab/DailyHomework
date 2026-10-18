#include "fluent_style.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QTimeEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "shell_widgets.h"
#include "theme.h"

#ifdef DHW_UI_FLUENT
#include <FluentQt/FluentQt.h>
#include <FluentQt/Foundation.h>
#endif

namespace dhw::qt {
namespace {

#ifdef DHW_UI_FLUENT
constexpr int kControlIconSize = 16;
constexpr int kButtonPad = 14;

void apply_role_icon(ActionControl* button, const QString& role, int side) {
    const QPixmap pixmap = theme_icon(role, side, accent_color());
    if (pixmap.isNull()) {
        return;
    }
    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(side, side));
    button->setProperty("dhwRole", role);
    button->setProperty("dhwIconSide", side);
}
#endif

}  // namespace

#ifdef DHW_UI_FLUENT

FluentCard::FluentCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
                       QWidget* parent)
    : QWidget(parent) {
    Q_UNUSED(icon);
    auto* host = new QVBoxLayout(this);
    host->setContentsMargins(0, 0, 0, 0);
    host->setSpacing(0);

    auto* surface = new fluent::layout::Card(this);
    surface->setAppearance(fluent::layout::Card::Layer);
    surface->setBorderVisible(true);
    host->addWidget(surface);

    auto* column = new QVBoxLayout(surface);
    column->setContentsMargins(ui_px(16), ui_px(14), ui_px(16), ui_px(16));
    column->setSpacing(ui_px(10));

    auto* heading = new fluent::textfields::Label(title, surface);
    heading->setFluentTypography(Typography::FontRole::BodyStrong);
    column->addWidget(heading);

    if (!hint.isEmpty()) {
        auto* hint_label = new InfoLabel(hint, surface);
        hint_label->setWordWrap(true);
        column->addWidget(hint_label);
    }

    auto* body = new QWidget(surface);
    body_layout_ = new QVBoxLayout(body);
    body_layout_->setContentsMargins(0, 0, 0, 0);
    body_layout_->setSpacing(ui_px(10));
    column->addWidget(body);
}

void ensure_fluent_ready() {
    static bool ready = false;
    if (ready) {
        return;
    }
    ready = true;
    fluent::initializeResources();
    // Built-in tokens plus any user token file; applying never writes to disk.
    fluent::UserTheme::apply();
    sync_fluent_theme();
}

void sync_fluent_theme() {
    const bool dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    // WinUI follows the system accent, so the token accent follows the live
    // platform accent instead of keeping the library's Windows blue. The on-accent
    // foreground is pinned from the same colour instead of the built-in pairing:
    // the macOS accent is darker than the Fluent dark palette's accent and would
    // otherwise get black text on a strong blue fill.
    const QColor accent = accent_color();
    const QColor on_accent =
        accent.lightnessF() > 0.6 ? QColor(Qt::black) : QColor(Qt::white);

    // Page vs card hierarchy. Fluent's surface ladder is canvas (page) below layer
    // (card), but NavigationView paints its content host with bgLayerOverlay, a
    // translucent mask meant for a Mica window. Over the shell's opaque backdrop it
    // lands within a few RGB steps of the card, which flattens every page. Point that
    // one mask at the opaque canvas token of its mode instead: the page then sits
    // exactly one step below Card::Layer in both schemes, from tokens only.
    const auto& registry = fluent::ThemeRegistry::instance();
    const QColor light_canvas = registry.colors(fluent::FluentElement::Light).bgCanvas;
    const QColor dark_canvas = registry.colors(fluent::FluentElement::Dark).bgCanvas;

    const auto entry = [&](const QColor& canvas) {
        return QJsonObject{
            {QStringLiteral("accentDefault"), accent.name(QColor::HexRgb)},
            {QStringLiteral("textOnAccent"), on_accent.name(QColor::HexRgb)},
            {QStringLiteral("bgLayerOverlay"), canvas.name(QColor::HexRgb)}};
    };
    const QJsonObject spec{{QStringLiteral("light"), entry(light_canvas)},
                           {QStringLiteral("dark"), entry(dark_canvas)}};
    fluent::UserTheme::applyOverrides(spec);
    fluent::FluentElement::setTheme(dark ? fluent::FluentElement::Dark
                                         : fluent::FluentElement::Light);
}

void sync_fluent_surfaces() {
    const auto& colors = fluent::ThemeRegistry::instance().colors(
        fluent::FluentElement::currentTheme());
    QPalette palette = QApplication::palette();
    // Roles a plain QWidget / QScrollArea viewport really reads. Only the two surfaces
    // are changed; every other role still comes from the live platform palette.
    if (palette.color(QPalette::Window) == colors.bgCanvas &&
        palette.color(QPalette::Base) == colors.bgLayer) {
        return;
    }
    palette.setColor(QPalette::Window, colors.bgCanvas);
    palette.setColor(QPalette::Base, colors.bgLayer);
    QApplication::setPalette(palette);
}

void sync_fluent_scale(double scale) {
    const qreal clamped = static_cast<qreal>(std::clamp(scale, 0.5, 1.5));
    fluent::ThemeRegistry::instance().setFontScale(clamped);
}

QWidget* make_fluent_divider(Qt::Orientation orientation, QWidget* parent) {
    auto* divider = new fluent::layout::Divider(orientation, parent);
    divider->setThickness(1.0);
    return divider;
}

QLabel* make_fluent_label(const QString& text, bool secondary, QWidget* parent) {
    auto* label = new fluent::textfields::Label(text, parent);
    label->setFluentTypography(Typography::FontRole::Body);
    if (secondary) {
        label->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    }
    return label;
}

QLabel* make_fluent_subtitle(const QString& text, QWidget* parent) {
    auto* label = new fluent::textfields::Label(text, parent);
    label->setFluentTypography(Typography::FontRole::BodyLargeStrong);
    return label;
}

QLabel* make_fluent_title(const QString& text, QWidget* parent) {
    auto* label = new fluent::textfields::Label(text, parent);
    label->setFluentTypography(Typography::FontRole::Title);
    return label;
}

ActionControl* make_fluent_icon_button(const QString& role, int side, QWidget* parent) {
    auto* button = new fluent::basicinput::Button(parent);
    button->setFluentStyle(fluent::basicinput::Button::Subtle);
    button->setFluentSize(fluent::basicinput::Button::StandardSize);
    button->setFluentLayout(fluent::basicinput::Button::IconOnly);
    button->setCursor(Qt::PointingHandCursor);
    // The icon side is a design metric like every other shell measurement: scale it so
    // a 150% display enlarges the glyph with the text instead of leaving it small.
    const int icon_side = ui_px(side);
    button->setFixedSize(icon_side + ui_px(kButtonPad), icon_side + ui_px(kButtonPad));
    apply_role_icon(button, role, icon_side);
    return button;
}

void set_fluent_icon_raised(ActionControl* button, bool raised) {
    if (button == nullptr) {
        return;
    }
    button->setFluentStyle(raised ? fluent::basicinput::Button::Standard
                                  : fluent::basicinput::Button::Subtle);
}

ActionControl* make_fluent_action_button(const QString& text, const QString& role,
                                         bool primary, QWidget* parent) {
    auto* button = new fluent::basicinput::Button(text, parent);
    button->setFluentStyle(primary ? fluent::basicinput::Button::Accent
                                   : fluent::basicinput::Button::Standard);
    button->setCursor(Qt::PointingHandCursor);
    // WinUI buttons hug their label; a form field would otherwise stretch them
    // across the whole column.
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (!role.isEmpty()) {
        const int icon_side = ui_px(kControlIconSize);
        const QPixmap pixmap = theme_icon(role, icon_side, accent_color());
        if (!pixmap.isNull()) {
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(icon_side, icon_side));
            button->setFluentLayout(fluent::basicinput::Button::IconBefore);
            button->setProperty("dhwRole", role);
            button->setProperty("dhwIconSide", icon_side);
        }
    }
    return button;
}

ToggleControl* make_fluent_toggle(bool on, QWidget* parent) {
    auto* toggle = new fluent::basicinput::ToggleSwitch(parent);
    toggle->setIsOn(on);
    return toggle;
}

void set_fluent_toggle(ToggleControl* toggle, bool on) {
    if (toggle != nullptr) {
        toggle->setIsOn(on);
    }
}

bool fluent_toggle_value(const ToggleControl* toggle) {
    return toggle != nullptr && toggle->isOn();
}

QComboBox* make_fluent_combo(QWidget* parent) {
    return new fluent::basicinput::ComboBox(parent);
}

QLineEdit* make_fluent_line_edit(const QString& text, QWidget* parent) {
    auto* edit = new fluent::textfields::LineEdit(parent);
    edit->setText(text);
    return edit;
}

QSlider* make_fluent_slider(QWidget* parent) {
    auto* slider = new fluent::basicinput::Slider(Qt::Horizontal, parent);
    slider->setHandleSize(ui_px(18));
    slider->setTrackHeight(ui_px(4));
    return slider;
}

NumberControl* make_fluent_number(int minimum, int maximum, int step, QWidget* parent) {
    auto* box = new fluent::textfields::NumberBox(parent);
    box->setRange(static_cast<double>(minimum), static_cast<double>(maximum));
    box->setSmallChange(static_cast<double>(step));
    box->setLargeChange(static_cast<double>(step * 10));
    box->setDisplayPrecision(0);
    box->setSpinButtonPlacementMode(
        fluent::textfields::NumberBox::SpinButtonPlacementMode::Compact);
    return box;
}

void set_fluent_number(NumberControl* control, int value) {
    if (control != nullptr) {
        control->setValue(static_cast<double>(value));
    }
}

int fluent_number(const NumberControl* control) {
    return control != nullptr ? static_cast<int>(std::lround(control->value())) : 0;
}

void connect_fluent_number(NumberControl* control, QObject* context,
                           std::function<void(int)> handler) {
    if (control == nullptr) {
        return;
    }
    QObject::connect(control, &fluent::textfields::NumberBox::valueChanged, context,
                     [handler = std::move(handler)](double value) {
                         handler(static_cast<int>(std::lround(value)));
                     });
}

TimeControl* make_fluent_time(const QTime& time, QWidget* parent) {
    auto* picker = new fluent::date_time::TimePicker(parent);
    picker->setLocale(QLocale(QLocale::Chinese, QLocale::China));
    picker->setClockIdentifier(
        fluent::date_time::TimePicker::ClockIdentifier::TwentyFourHourClock);
    picker->setMinuteIncrement(1);
    // The picker paints the committed selected time; setTime() alone only stages the
    // next flyout value and would leave the field blank.
    picker->setSelectedTime(time);
    return picker;
}

void set_fluent_time(TimeControl* control, const QTime& time) {
    if (control != nullptr) {
        control->setSelectedTime(time);
    }
}

QTime fluent_time(const TimeControl* control) {
    return control != nullptr ? control->time() : QTime(8, 0);
}

DateControl* make_fluent_date(const QDate& date, QWidget* parent) {
    // A single formatted field plus a calendar flyout, matching the old QDateEdit.
    auto* picker = new fluent::date_time::CalendarDatePicker(parent);
    picker->setLocale(QLocale(QLocale::Chinese, QLocale::China));
    picker->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    picker->setDateRange(QDate(2000, 1, 1), QDate(2100, 12, 31));
    picker->setDate(date);
    return picker;
}

void set_fluent_date(DateControl* control, const QDate& date) {
    if (control != nullptr) {
        control->setDate(date);
    }
}

QDate fluent_date(const DateControl* control) {
    return control != nullptr ? control->date() : QDate::currentDate();
}

#else  // !DHW_UI_FLUENT

void ensure_fluent_ready() {}
void sync_fluent_theme() {}
void sync_fluent_surfaces() {}
void sync_fluent_scale(double scale) { Q_UNUSED(scale); }

QWidget* make_fluent_divider(Qt::Orientation orientation, QWidget* parent) {
    return make_separator(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine, parent);
}

QLabel* make_fluent_label(const QString& text, bool secondary, QWidget* parent) {
    Q_UNUSED(secondary);
    return new QLabel(text, parent);
}

QLabel* make_fluent_subtitle(const QString& text, QWidget* parent) {
    return make_title_label(text, parent);
}

QLabel* make_fluent_title(const QString& text, QWidget* parent) {
    return make_title_label(text, parent);
}

ActionControl* make_fluent_icon_button(const QString& role, int side, QWidget* parent) {
    auto* button = make_icon_button(role, side, parent);
    // make_icon_button already pushed the design side through ui_px(); reuse the live
    // icon extent so the hit target keeps the same proportion at every display size.
    const int icon_side =
        button->iconSize().width() > 0 ? button->iconSize().width() : ui_px(side);
    button->setFixedSize(icon_side + ui_px(14), icon_side + ui_px(14));
    return button;
}

void set_fluent_icon_raised(ActionControl* button, bool raised) {
    if (button != nullptr) {
        button->setAutoRaise(!raised);
    }
}

ActionControl* make_fluent_action_button(const QString& text, const QString& role,
                                         bool primary, QWidget* parent) {
    Q_UNUSED(primary);
    return make_action_button(text, role, parent);
}

ToggleControl* make_fluent_toggle(bool on, QWidget* parent) {
    auto* toggle = new QCheckBox(parent);
    toggle->setChecked(on);
    return toggle;
}

void set_fluent_toggle(ToggleControl* toggle, bool on) {
    if (toggle != nullptr) {
        toggle->setChecked(on);
    }
}

bool fluent_toggle_value(const ToggleControl* toggle) {
    return toggle != nullptr && toggle->isChecked();
}

QComboBox* make_fluent_combo(QWidget* parent) {
    return new QComboBox(parent);
}

QLineEdit* make_fluent_line_edit(const QString& text, QWidget* parent) {
    auto* edit = new QLineEdit(parent);
    edit->setText(text);
    return edit;
}

QSlider* make_fluent_slider(QWidget* parent) {
    return new QSlider(Qt::Horizontal, parent);
}

NumberControl* make_fluent_number(int minimum, int maximum, int step, QWidget* parent) {
    auto* box = new QSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setSingleStep(step);
    return box;
}

void set_fluent_number(NumberControl* control, int value) {
    if (control != nullptr) {
        control->setValue(value);
    }
}

int fluent_number(const NumberControl* control) {
    return control != nullptr ? control->value() : 0;
}

void connect_fluent_number(NumberControl* control, QObject* context,
                           std::function<void(int)> handler) {
    if (control == nullptr) {
        return;
    }
    QObject::connect(control, &QSpinBox::valueChanged, context, std::move(handler));
}

TimeControl* make_fluent_time(const QTime& time, QWidget* parent) {
    auto* edit = new QTimeEdit(time, parent);
    edit->setDisplayFormat(QStringLiteral("HH:mm"));
    return edit;
}

void set_fluent_time(TimeControl* control, const QTime& time) {
    if (control != nullptr) {
        control->setTime(time);
    }
}

QTime fluent_time(const TimeControl* control) {
    return control != nullptr ? control->time() : QTime(8, 0);
}

DateControl* make_fluent_date(const QDate& date, QWidget* parent) {
    auto* edit = new QDateEdit(date, parent);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    edit->setCalendarPopup(true);
    return edit;
}

void set_fluent_date(DateControl* control, const QDate& date) {
    if (control != nullptr) {
        control->setDate(date);
    }
}

QDate fluent_date(const DateControl* control) {
    return control != nullptr ? control->date() : QDate::currentDate();
}

#endif  // DHW_UI_FLUENT

}  // namespace dhw::qt

#pragma once

#include <QAbstractButton>
#include <QDate>
#include <QString>
#include <QTime>
#include <QWidget>
#include <functional>

#include "dhw/icons.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QTimeEdit;
class QToolButton;
class QVBoxLayout;

#ifdef DHW_UI_FLUENT
#include <FluentQt/BasicInput.h>
#include <FluentQt/Collections.h>
#include <FluentQt/DateTime.h>
#include <FluentQt/Layout.h>
#include <FluentQt/MenusToolbars.h>
#include <FluentQt/Navigation.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#endif

namespace dhw::qt {

class Card;

// ---------------------------------------------------------------------------
// One place that owns the Fluent (WinUI 3) widget family.
//
// Every page keeps using the plain Qt base API (QAbstractButton::clicked,
// QComboBox::currentIndex, QSlider::valueChanged, ...); only the construction
// sites and the few value accessors that do not share a base class differ. With
// -DDHW_UI_FLUENT=OFF the aliases below resolve to the stock Qt widgets, so a
// page compiles unchanged against the native skin.
//
// The presentation canvas never includes this header: it is widgets only.
// ---------------------------------------------------------------------------

#ifdef DHW_UI_FLUENT
using ActionControl = fluent::basicinput::Button;
using ToggleControl = fluent::basicinput::ToggleSwitch;
using NumberControl = fluent::textfields::NumberBox;
using TimeControl = fluent::date_time::TimePicker;
using DateControl = fluent::date_time::CalendarDatePicker;
using ListControl = fluent::collections::ListView;
#else
using ActionControl = QToolButton;
using ToggleControl = QCheckBox;
using NumberControl = QSpinBox;
using TimeControl = QTimeEdit;
using DateControl = QDateEdit;
#endif

// Section surface of a page: a rounded Fluent Card in the Fluent build, the
// platform QGroupBox in the native build. Both expose body_layout(), so call
// sites are identical.
#ifdef DHW_UI_FLUENT
class FluentCard : public QWidget {
public:
    FluentCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
               QWidget* parent = nullptr);

    QVBoxLayout* body_layout() const { return body_layout_; }

private:
    QVBoxLayout* body_layout_ = nullptr;
};
using SectionCard = FluentCard;
#else
using SectionCard = Card;
#endif

// Registers the vendored resources/fonts, installs the design tokens and makes
// the token theme follow the live platform palette. Idempotent; a no-op in the
// native build. Call once after QApplication and after the widget style is in
// effect.
void ensure_fluent_ready();

// Re-applies light/dark and the platform accent from the live QPalette. Called
// from theme.cpp on every appearance change, so a running light/dark switch
// reaches the Fluent controls too. A no-op in the native build.
void sync_fluent_theme();

// Pins the Fluent surface ladder into the live QPalette: QPalette::Window becomes the
// page canvas and QPalette::Base the card layer. Plain Qt widgets and scroll-area
// viewports read those roles, so this is what gives a page its one-step recess below a
// Card without any stylesheet; without it the platform palette paints the page within
// a few RGB steps of the card. Called by theme.cpp after the platform palette settles.
// A no-op in the native build.
void sync_fluent_surfaces();

// Pushes the display-size factor into the Fluent typography tokens. The Fluent
// widgets use the token font sizes instead of QApplication::font(), so the
// shell's "ui_scale only scales fonts" rule needs both paths.
void sync_fluent_scale(double scale);

// Hairline separator painted by the active skin. The native build uses the
// platform style's QFrame; the Fluent build uses the Fluent Divider.
QWidget* make_fluent_divider(Qt::Orientation orientation, QWidget* parent);

// Plain text label and page/section title in the active skin.
QLabel* make_fluent_label(const QString& text, bool secondary, QWidget* parent);
// Small heading (toolbar / sidebar brand) and page title.
QLabel* make_fluent_subtitle(const QString& text, QWidget* parent);
QLabel* make_fluent_title(const QString& text, QWidget* parent);

// Icon-only and text+icon action buttons. `role` is one of the shell's vector
// icon roles from theme.h; an unknown role yields a text-only button.
ActionControl* make_fluent_icon_button(const QString& role, int side, QWidget* parent);
void set_fluent_icon_raised(ActionControl* button, bool raised);
ActionControl* make_fluent_action_button(const QString& text, const QString& role,
                                         bool primary, QWidget* parent);

// Binary switch. `toggled(bool)` is the same signal in both skins.
ToggleControl* make_fluent_toggle(bool on, QWidget* parent = nullptr);
void set_fluent_toggle(ToggleControl* toggle, bool on);
bool fluent_toggle_value(const ToggleControl* toggle);

// Text entry, choice and slider share their Qt base class in both skins.
QComboBox* make_fluent_combo(QWidget* parent = nullptr);
QLineEdit* make_fluent_line_edit(const QString& text, QWidget* parent = nullptr);
QSlider* make_fluent_slider(QWidget* parent = nullptr);

// Numeric, time and date entry: the Fluent controls are not QAbstractSpinBox,
// so these three families get value accessors.
NumberControl* make_fluent_number(int minimum, int maximum, int step,
                                  QWidget* parent = nullptr);
void set_fluent_number(NumberControl* control, int value);
int fluent_number(const NumberControl* control);
void connect_fluent_number(NumberControl* control, QObject* context,
                           std::function<void(int)> handler);

TimeControl* make_fluent_time(const QTime& time, QWidget* parent = nullptr);
void set_fluent_time(TimeControl* control, const QTime& time);
QTime fluent_time(const TimeControl* control);

DateControl* make_fluent_date(const QDate& date, QWidget* parent = nullptr);
void set_fluent_date(DateControl* control, const QDate& date);
QDate fluent_date(const DateControl* control);

}  // namespace dhw::qt

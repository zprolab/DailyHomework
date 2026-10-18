#pragma once

#include <QColor>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "dhw/draw_cmd.h"
#include "dhw/icons.h"

namespace dhw::qt {

// ---------------------------------------------------------------------------
// Widget style, colour scheme and UI scale.
//
// The native shell uses the platform style that ships with Qt (windows11 /
// modernwindows / windowsvista / macos / fusion) and lets it own every control.
// -DDHW_UI_FLUENT=ON swaps the widget family for the vendored Fluent-Qt library
// (see fluent_style.h); this header stays the single source of the palette roles,
// the vector icons and the ui scale for both skins. There is no hand written
// stylesheet and no second colour source.
// ---------------------------------------------------------------------------

// Every style key QStyleFactory advertises on this platform, in platform order.
QStringList available_style_keys();

// The style the shell picks on its own for the running OS, using the first key
// that QStyleFactory really advertises:
//   Windows 11: windows11 -> modernwindows -> windowsvista -> fusion
//   Windows 10: modernwindows -> windowsvista -> fusion
//   macOS:      macos -> fusion
//   other:      fusion
// An override_key that names an available style wins over the chain; an unknown
// override is ignored (apply_platform_style warns once on stderr).
QString preferred_style_key(const QString& override_key = {});

// preferred_style_key + QApplication::setStyle. Returns the style key that is in
// effect afterwards and re-polishes the widgets against the new palette.
QString apply_platform_style(const QString& override_key = {});

// Key of the style currently installed, or its QStyle class name as a fallback.
QString current_style_key();

// The shell follows the operating system appearance: the platform palette the style
// installs is the only source of colour. The stored value is kept for compatibility
// with data written by earlier versions but never forces a scheme, because a forced
// palette is ignored by styles that paint their own surfaces.
void apply_color_scheme(const QString& appearance);

// Scales the application font against the font that was active at the first call,
// which is the startup system font; the result is clamped to 50%..150%.
//
// LIMITATION: this is font scaling, not a real layout zoom. It resizes control
// text and everything whose size follows the font, but not hard coded pixel
// metrics, the vector icons or the platform style's own padding. The presentation
// canvas is not affected at all: the slide is painted by the DrawCmd renderer at
// its own fixed size and is deliberately independent of the widget theme.
void apply_ui_scale(double scale);

// apply_color_scheme + apply_ui_scale; used when the settings page changes either
// the appearance or the display size.
void apply_theme(const QString& appearance, double ui_scale);

// Rounds one design metric (a pixel value at 100%) through the live display size.
// Font scaling alone leaves every fixed metric behind, so the shell's toolbar height,
// sidebar widths, chip padding and icon sides all go through this. A non-positive
// metric stays 0 (margins), everything else is at least one pixel.
int ui_px(double design_px);

// ---------------------------------------------------------------------------
// Colours read back from the live QPalette (there is no hand written palette).
// ---------------------------------------------------------------------------
QColor accent_color();       // QPalette::Highlight, lifted when it is unreadable
QColor soft_accent_color();  // accent blended into Base (today's column)
QColor danger_color();       // the one semantic colour no palette role covers

// `device_pixel_ratio` 0 means "use the primary screen", so call sites that do not
// know their window still get a Retina/150%-scaled pixmap instead of an upscaled one.
QPixmap theme_icon(const QString& role, int size, const QColor& color,
                   qreal device_pixel_ratio = 0.0);
QPixmap card_icon(dhw::icons::Icon icon, int size, const QString& color,
                  qreal device_pixel_ratio = 0.0);

// QLabel for informational text (card hints, row help). The native font is wider
// than the old hand sized one, so a long sentence would otherwise push the whole page
// wider than the viewport and get cut off on the right. This label keeps its natural
// size hint when there is room and reports a small minimum width otherwise, so the text
// wraps (with word wrap) or is clipped (single line) instead of stretching its container.
class InfoLabel : public QLabel {
    Q_OBJECT

public:
    explicit InfoLabel(const QString& text = QString(), QWidget* parent = nullptr);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
};

}  // namespace dhw::qt

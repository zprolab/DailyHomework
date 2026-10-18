#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "dhw/icons.h"
#include "dhw/qt/renderer.h"
#include "fluent_style.h"
#include "shell_widgets.h"

namespace dhw::qt {
namespace {

constexpr double kUiScaleMin = 0.5;
constexpr double kUiScaleMax = 1.5;

// The app font captured the first time the UI scale is applied, i.e. the startup
// system font. Every later call scales this capture and never the previous result,
// so dragging the display size slider cannot compound.
std::optional<QFont> g_base_font;
QString g_style_key;
bool g_scheme_watcher_installed = false;
// The live display size. Font scaling alone would leave every fixed metric behind;
// ui_px() is the one place that turns a design pixel into a live pixel.
double g_ui_scale = 1.0;

void repolish_widgets() {
    // A palette swap only repaints reliably once every widget re-polishes.
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        if (widget == nullptr || widget->style() == nullptr) {
            continue;
        }
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

double clamp_scale(double scale) {
    if (!(scale > 0.0)) {
        return 1.0;
    }
    return std::min(kUiScaleMax, std::max(kUiScaleMin, scale));
}

QColor palette_color(QPalette::ColorRole role) {
    return QApplication::palette().color(role);
}

QColor base_color() {
    return palette_color(QPalette::Base);
}

QColor blend(const QColor& top, const QColor& bottom, double alpha) {
    const double a = std::clamp(alpha, 0.0, 1.0);
    return QColor::fromRgbF(top.redF() * a + bottom.redF() * (1.0 - a),
                            top.greenF() * a + bottom.greenF() * (1.0 - a),
                            top.blueF() * a + bottom.blueF() * (1.0 - a));
}

double relative_luminance(const QColor& color) {
    return 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
}

// WCAG style contrast ratio, used only to decide whether a platform colour is
// readable enough to be trusted as the secondary text colour.
double contrast_ratio(const QColor& first, const QColor& second) {
    const double a = relative_luminance(first);
    const double b = relative_luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

bool dark_appearance() {
    return palette_color(QPalette::Window).lightness() < 128;
}

// The platform's own spelling of `wanted`, or an empty string when this platform
// does not have that style. QStyleFactory compares keys case insensitively and so
// does this lookup.
QString available_key(const QString& wanted) {
    const QStringList keys = QStyleFactory::keys();
    for (const QString& key : keys) {
        if (key.compare(wanted, Qt::CaseInsensitive) == 0) {
            return key;
        }
    }
    return QString();
}

QString first_available(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        const QString key = available_key(candidate);
        if (!key.isEmpty()) {
            return key;
        }
    }
    return QString();
}

QStringList platform_style_chain() {
#if defined(Q_OS_WIN)
    QStringList chain;
    bool windows_11 = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    windows_11 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#endif
    if (windows_11) {
        // Qt 6.7 and newer ship this style on top of the native uxtheme API.
        chain << QStringLiteral("windows11");
    }
    chain << QStringLiteral("modernwindows") << QStringLiteral("windowsvista")
          << QStringLiteral("fusion");
    return chain;
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("macos"), QStringLiteral("fusion")};
#else
    return {QStringLiteral("fusion")};
#endif
}

void ensure_scheme_watcher() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (g_scheme_watcher_installed) {
        return;
    }
    g_scheme_watcher_installed = true;
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        // A system light/dark switch hands the app a new platform palette. Qt repaints
        // every control from it by itself; only the vector pixmaps carry a colour of
        // their own and have to be redrawn.
        QObject::connect(hints, &QStyleHints::colorSchemeChanged, hints, []() {
            // The Fluent token set and the shell's vector pixmaps both carry colours
            // that Qt cannot re-derive from the palette by itself. The surface ladder
            // is re-pinned too, because the platform palette was just replaced.
            sync_fluent_theme();
            sync_fluent_surfaces();
            refresh_role_icons();
        });
    }
#endif
}

QApplication* app_instance() {
    return qobject_cast<QApplication*>(QApplication::instance());
}

QStyle::StandardPixmap icon_for_role(const QString& role) {
    if (role == QStringLiteral("today")) {
        return QStyle::SP_DialogApplyButton;
    }
    if (role == QStringLiteral("image")) {
        return QStyle::SP_DialogSaveButton;
    }
    if (role == QStringLiteral("edit")) {
        return QStyle::SP_FileDialogDetailedView;
    }
    if (role == QStringLiteral("week")) {
        return QStyle::SP_FileDialogListView;
    }
    if (role == QStringLiteral("settings")) {
        return QStyle::SP_FileDialogInfoView;
    }
    return QStyle::SP_CustomBase;
}

}  // namespace

QStringList available_style_keys() {
    return QStyleFactory::keys();
}

QString preferred_style_key(const QString& override_key) {
    const QString forced = available_key(override_key.trimmed());
    if (!forced.isEmpty()) {
        return forced;
    }
    const QString key = first_available(platform_style_chain());
    return key.isEmpty() ? current_style_key() : key;
}

QString current_style_key() {
    if (!g_style_key.isEmpty()) {
        return g_style_key;
    }
    const QStyle* style = QApplication::style();
    if (style == nullptr) {
        return QString();
    }
    return QString::fromLatin1(style->metaObject()->className());
}

QString apply_platform_style(const QString& override_key) {
    const QString requested = override_key.trimmed();
    QString key = available_key(requested);
    if (key.isEmpty() && !requested.isEmpty()) {
        std::fprintf(stderr, "widget style \"%s\" is not available here; using \"%s\"\n",
                     requested.toUtf8().constData(), preferred_style_key().toUtf8().constData());
    }
    if (key.isEmpty()) {
        key = preferred_style_key();
    }
    if (!key.isEmpty()) {
        if (QStyle* style = QStyleFactory::create(key)) {
            QApplication::setStyle(style);
            g_style_key = key;
        }
    }
    if (g_style_key.isEmpty()) {
        g_style_key = current_style_key();
    }
    ensure_scheme_watcher();
    // setStyle installs the style's own palette; widgets that already exist have to be
    // re-polished against it, and the vector icons have to be redrawn from it.
    repolish_widgets();
    refresh_role_icons();
    return g_style_key;
}

void apply_color_scheme(const QString& appearance) {
    // The shell follows the operating system appearance. The stored value is kept for
    // compatibility with data written by earlier versions but never forces a scheme:
    // a manual override proved unreliable, because styles that paint their own surfaces
    // ignore an application palette override, which left some pages light in dark mode.
    //
    // No application palette is installed here on purpose. The platform palette the
    // style installs is already the live one, and Qt 6.5+ swaps in the matching one by
    // itself when the OS switches; re-installing a palette captured at startup would
    // pin the app to the appearance it happened to launch with. The only thing left to
    // do is to undo a scheme an earlier version may have forced.
    Q_UNUSED(appearance);
    ensure_scheme_watcher();
    sync_fluent_theme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        if (hints->colorScheme() != Qt::ColorScheme::Unknown) {
            hints->setColorScheme(Qt::ColorScheme::Unknown);
        }
    }
#endif
    // Asking for the system scheme can hand the application a fresh platform palette,
    // which would drop the Fluent page/card ladder installed earlier; re-pin it after
    // the platform has settled.
    sync_fluent_surfaces();
    repolish_widgets();
    refresh_role_icons();
}

void apply_ui_scale(double scale) {
    QApplication* app = app_instance();
    if (app == nullptr) {
        return;
    }
    const double clamped = clamp_scale(scale);
    if (!g_base_font.has_value()) {
        g_base_font = app->font();
    }
    g_ui_scale = clamped;
    QFont font = *g_base_font;
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * clamped);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * clamped))));
    }
    app->setFont(font);
    // Fluent widgets paint their typography from the token font scale, not from
    // QApplication::font(); keep the two paths in step so ui_scale still only scales
    // text and never the layout metrics.
    sync_fluent_scale(clamped);
}

int ui_px(double design_px) {
    if (!(design_px > 0.0)) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(design_px * g_ui_scale)));
}

void apply_theme(const QString& appearance, double ui_scale) {
    apply_color_scheme(appearance);
    apply_ui_scale(ui_scale);
}

QColor accent_color() {
    // Highlight is meant as a selection *background*, so on a dark system palette it is
    // too dark to read as text (macOS dark reports #314f78). Fall back to the Link role,
    // which the platform does intend as readable text, and finally lift the highlight
    // towards the text colour until it is legible on a card.
    //
    // The reference surface is the page canvas (QPalette::Window), not the card
    // (QPalette::Base): the accent is used both as text/icon colour and as the fill of a
    // selected control, and the canvas is the surface the shell sits on. Judging by the
    // card would reject the platform Link on the Fluent dark palette and turn every
    // accent into the washed-out lifted highlight.
    const QColor page = palette_color(QPalette::Window);
    const QColor highlight = palette_color(QPalette::Highlight);
    if (highlight.isValid() && contrast_ratio(highlight, page) >= 3.0) {
        return highlight;
    }
    const QColor link = palette_color(QPalette::Link);
    if (link.isValid() && contrast_ratio(link, page) >= 3.0) {
        return link;
    }
    if (highlight.isValid()) {
        return blend(highlight, palette_color(QPalette::WindowText), 0.55);
    }
    return dark_appearance() ? QColor(0x8F, 0xB4, 0xE8) : QColor(0x2E, 0x5F, 0xA3);
}

QColor soft_accent_color() {
    // Today's column in the week overview: the platform accent blended into the card
    // surface, so the tint follows a light/dark switch.
    return blend(accent_color(), base_color(), dark_appearance() ? 0.28 : 0.14);
}

QColor danger_color() {
    // No QPalette role means "error", and the week legend still promises red for a
    // swapped period, so this is the one colour the shell picks itself; only its
    // lightness comes from the theme.
    return dark_appearance() ? QColor(0xE5, 0x73, 0x7A) : QColor(0xC0, 0x27, 0x2D);
}

QPixmap theme_icon(const QString& role, int size, const QColor& color) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, std::max(1.5, size / 9.0)));
    painter.setBrush(Qt::NoBrush);
    const double s = static_cast<double>(size);
    if (role == QStringLiteral("hamburger")) {
        for (int i = 0; i < 3; ++i) {
            const double y = s * (0.26 + 0.24 * i);
            painter.drawLine(QPointF(s * 0.17, y), QPointF(s * 0.83, y));
        }
    } else if (role == QStringLiteral("previous") || role == QStringLiteral("next")) {
        const double dir = role == QStringLiteral("previous") ? -1.0 : 1.0;
        painter.drawPolyline(QPolygonF({QPointF(s * (0.5 - 0.17 * dir), s * 0.22),
                                        QPointF(s * (0.5 + 0.17 * dir), s * 0.5),
                                        QPointF(s * (0.5 - 0.17 * dir), s * 0.78)}));
    } else if (role == QStringLiteral("strip-left") || role == QStringLiteral("strip-right")) {
        // Double chevron: the date strip scrolls, it does not step one day like the
        // single chevrons of the previous/next day buttons next to it.
        const double dir = role == QStringLiteral("strip-left") ? -1.0 : 1.0;
        for (int i = 0; i < 2; ++i) {
            const double x = s * (0.30 + 0.26 * i);
            painter.drawPolyline(QPolygonF({QPointF(x - s * 0.09 * dir, s * 0.30),
                                            QPointF(x + s * 0.09 * dir, s * 0.50),
                                            QPointF(x - s * 0.09 * dir, s * 0.70)}));
        }
    } else if (role == QStringLiteral("today")) {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(s * 0.5, s * 0.4), s * 0.24, s * 0.24);
        painter.drawPolygon(QPolygonF({QPointF(s * 0.5, s * 0.94), QPointF(s * 0.3, s * 0.58),
                                       QPointF(s * 0.7, s * 0.58)}));
    } else if (role == QStringLiteral("play")) {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF({QPointF(s * 0.28, s * 0.18), QPointF(s * 0.84, s * 0.5),
                                       QPointF(s * 0.28, s * 0.82)}));
    } else if (role == QStringLiteral("image")) {
        painter.drawRoundedRect(QRectF(s * 0.16, s * 0.2, s * 0.68, s * 0.6), s * 0.1, s * 0.1);
        painter.drawPolyline(QPolygonF({QPointF(s * 0.22, s * 0.68), QPointF(s * 0.42, s * 0.45),
                                        QPointF(s * 0.58, s * 0.62), QPointF(s * 0.7, s * 0.5),
                                        QPointF(s * 0.82, s * 0.68)}));
    } else if (role == QStringLiteral("quit") || role == QStringLiteral("remove")) {
        painter.drawLine(QPointF(s * 0.25, s * 0.25), QPointF(s * 0.75, s * 0.75));
        painter.drawLine(QPointF(s * 0.75, s * 0.25), QPointF(s * 0.25, s * 0.75));
    } else if (role == QStringLiteral("edit")) {
        painter.drawLine(QPointF(s * 0.22, s * 0.78), QPointF(s * 0.76, s * 0.24));
        painter.drawLine(QPointF(s * 0.3, s * 0.86), QPointF(s * 0.84, s * 0.32));
    } else if (role == QStringLiteral("week")) {
        painter.drawRoundedRect(QRectF(s * 0.16, s * 0.2, s * 0.68, s * 0.6), s * 0.1, s * 0.1);
        painter.drawLine(QPointF(s * 0.16, s * 0.42), QPointF(s * 0.84, s * 0.42));
        painter.drawLine(QPointF(s * 0.5, s * 0.42), QPointF(s * 0.5, s * 0.8));
    } else if (role == QStringLiteral("settings")) {
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.15, s * 0.15);
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.32, s * 0.32);
    } else if (role == QStringLiteral("reload")) {
        painter.drawArc(QRectF(s * 0.2, s * 0.2, s * 0.6, s * 0.6), 40 * 16, 280 * 16);
    } else if (role == QStringLiteral("plus")) {
        painter.drawLine(QPointF(s * 0.5, s * 0.2), QPointF(s * 0.5, s * 0.8));
        painter.drawLine(QPointF(s * 0.2, s * 0.5), QPointF(s * 0.8, s * 0.5));
    } else {
        const QStyle::StandardPixmap standard = icon_for_role(role);
        if (standard != QStyle::SP_CustomBase && QApplication::style() != nullptr) {
            painter.end();
            return QApplication::style()->standardIcon(standard).pixmap(size, size);
        }
        // Neither a vector nor a standard icon is known for this role: hand back a null
        // pixmap so the button stays text only instead of reserving space for a blank.
        return QPixmap();
    }
    return pixmap;
}

QPixmap card_icon(dhw::icons::Icon icon, int size, const QString& color) {
    const QtTextMetrics metrics;
    const std::vector<DrawCmd> commands =
        dhw::icons::draw(icon, 0.0, 0.0, static_cast<double>(size),
                         static_cast<dhw::Color>(QColor(color).rgba()));
    return QPixmap::fromImage(render_commands(commands, size, size, {}, metrics));
}

InfoLabel::InfoLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {}

QSize InfoLabel::sizeHint() const {
    // The natural width, so the label is laid out at its full length whenever the row
    // really has that much room.
    return QLabel::sizeHint();
}

QSize InfoLabel::minimumSizeHint() const {
    QSize hint = QLabel::minimumSizeHint();
    // A single character is enough to keep the text legible; everything beyond that is
    // wrapped (word wrap) or clipped instead of widening the page.
    hint.setWidth(std::min(hint.width(), 24));
    return hint;
}

}  // namespace dhw::qt

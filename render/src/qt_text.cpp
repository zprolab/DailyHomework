#include "dhw/qt/renderer.h"

#include <QFontDatabase>
#include <QFontMetricsF>

namespace dhw::qt {
namespace {

QString first_available(const QStringList& candidates) {
    const QStringList families = QFontDatabase::families();
    for (const QString& candidate : candidates) {
        if (families.contains(candidate, Qt::CaseInsensitive)) {
            return candidate;
        }
    }
    return QString();
}

}  // namespace

FontSet default_fonts() {
    FontSet fonts;
    fonts.ui = first_available({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"),
                                QStringLiteral("PingFang SC"), QStringLiteral("Hiragino Sans GB"),
                                QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Source Han Sans SC")});
    fonts.icon = first_available({QStringLiteral("Segoe Fluent Icons"),
                                  QStringLiteral("Segoe MDL2 Assets")});
    if (fonts.ui.isEmpty()) {
        fonts.ui = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    }
    return fonts;
}

QtTextMetrics::QtTextMetrics(FontSet fonts) : fonts_(std::move(fonts)) {}

const QFont& QtTextMetrics::font_for(double size, bool bold, Face face) const {
    const QString family = (face == Face::Icon && !fonts_.icon.isEmpty()) ? fonts_.icon : fonts_.ui;
    const QString key = family + QStringLiteral("|") + QString::number(size, 'f', 2) +
                        (bold ? QStringLiteral("|b") : QStringLiteral("|n"));
    for (const auto& entry : cache_) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    QFont font(family);
    font.setPixelSize(static_cast<int>(size + 0.5));
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    font.setHintingPreference(QFont::PreferFullHinting);
    cache_.emplace_back(key, font);
    return cache_.back().second;
}

double QtTextMetrics::measure(std::string_view text, double size, bool bold, Face face) const {
    const QFontMetricsF metrics(font_for(size, bold, face));
    return metrics.horizontalAdvance(QString::fromUtf8(text.data(), static_cast<int>(text.size())));
}

LineMetrics QtTextMetrics::line_metrics(double size, bool bold, Face face) const {
    const QFontMetricsF metrics(font_for(size, bold, face));
    LineMetrics result;
    result.ascent = metrics.ascent();
    result.descent = metrics.descent();
    result.baseline = metrics.ascent();
    return result;
}

}  // namespace dhw::qt

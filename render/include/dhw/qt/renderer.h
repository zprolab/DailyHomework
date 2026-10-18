#pragma once

#include <QFont>
#include <QImage>
#include <QString>
#include <vector>

#include "dhw/draw_cmd.h"
#include "dhw/text.h"

namespace dhw::qt {

// Font faces used by the shell. The layout only ever asks for Ui; Icon stays for
// renderers that can rely on a platform symbol font.
struct FontSet {
    QString ui;    // main UI face
    QString icon;  // symbol face, may be empty
};

// Resolves a sensible default face per platform (PingFang SC / Microsoft YaHei UI).
FontSet default_fonts();

// Real text metrics from the Qt font engine: the same object is used for layout
// measurement and for painting, which is what keeps screen and PNG identical.
class QtTextMetrics final : public TextMetrics {
public:
    explicit QtTextMetrics(FontSet fonts = default_fonts());

    double measure(std::string_view text, double size, bool bold, Face face = Face::Ui) const override;
    LineMetrics line_metrics(double size, bool bold, Face face = Face::Ui) const override;

    // QFont for a (size, weight, face) triple, cached.
    const QFont& font_for(double size, bool bold, Face face) const;

private:
    FontSet fonts_;
    mutable std::vector<std::pair<QString, QFont>> cache_;
};

// Draws a DrawCmd list with QPainter. Text `y` is the vertical centre of the ink
// (see dhw/draw_cmd.h); images are laid out by their own ink bounds so a badge
// with asymmetric transparent padding stays optically centred.
QImage render_commands(const std::vector<DrawCmd>& commands, int width, int height,
                       const std::vector<QImage>& images, const QtTextMetrics& metrics,
                       double pixel_ratio = 1.0);

// Ink bounding box of an image, used for the badge centring rule and for the
// sidebar thumbnail. Empty rect means the image is fully transparent.
QRect ink_bounds(const QImage& image);

}  // namespace dhw::qt

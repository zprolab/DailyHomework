#include "dhw/qt/renderer.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace dhw::qt {
namespace {

QColor to_color(Color value) {
    return QColor(static_cast<int>((value >> 16) & 0xFFu), static_cast<int>((value >> 8) & 0xFFu),
                  static_cast<int>(value & 0xFFu), static_cast<int>((value >> 24) & 0xFFu));
}

QString from_utf8(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QRectF make_rect(const DrawCmd& cmd) {
    return QRectF(cmd.x, cmd.y, cmd.w, cmd.h);
}

void fill_shape(QPainter& painter, const DrawCmd& cmd, const QRectF& box) {
    if (cmd.gradient == DrawCmd::Gradient::None) {
        if (cmd.has_fill) {
            painter.setBrush(to_color(cmd.fill));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(box, cmd.r, cmd.r);
        }
        return;
    }

    QBrush brush;
    if (cmd.gradient == DrawCmd::Gradient::Radial) {
        QRadialGradient gradient(QPointF(cmd.grad_cx, cmd.grad_cy), std::max(1.0, cmd.grad_r));
        gradient.setColorAt(0.0, to_color(cmd.fill));
        gradient.setColorAt(1.0, to_color(cmd.fill2));
        brush = QBrush(gradient);
    } else {
        QPointF start(box.left(), box.top());
        QPointF end(box.left(), box.bottom());
        if (cmd.gradient == DrawCmd::Gradient::Horizontal) {
            end = QPointF(box.right(), box.top());
        } else if (cmd.gradient == DrawCmd::Gradient::Diagonal) {
            end = QPointF(box.right(), box.bottom());
        }
        QLinearGradient gradient(start, end);
        gradient.setColorAt(0.0, to_color(cmd.fill));
        gradient.setColorAt(1.0, to_color(cmd.fill2));
        brush = QBrush(gradient);
    }
    painter.setBrush(brush);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(box, cmd.r, cmd.r);
}

void draw_text(QPainter& painter, const DrawCmd& cmd, const QtTextMetrics& metrics) {
    const QFont& font = metrics.font_for(cmd.size, cmd.bold, cmd.face_hint);
    painter.setFont(font);
    painter.setPen(to_color(cmd.color));
    const QString text = from_utf8(cmd.text);
    if (text.isEmpty()) {
        return;
    }
    const QFontMetricsF font_metrics(font);
    const double advance = font_metrics.horizontalAdvance(text);
    double left = cmd.x;
    if (cmd.align == DrawCmd::Align::Center) {
        left = cmd.x - advance / 2.0;
    } else if (cmd.align == DrawCmd::Align::Right) {
        left = cmd.x - advance;
    }
    // The ink box is measured relative to the baseline, so shifting the baseline by
    // its own centre lands the visible ink centre exactly on cmd.y.
    const QRectF ink = font_metrics.tightBoundingRect(text);
    const double baseline = cmd.y - (ink.top() + ink.bottom()) / 2.0;
    painter.drawText(QPointF(left, baseline), text);
}

void draw_path(QPainter& painter, const DrawCmd& cmd) {
    if (cmd.points.size() < 2) {
        return;
    }
    QPainterPath path;
    path.moveTo(cmd.points.front().first, cmd.points.front().second);
    for (std::size_t index = 1; index < cmd.points.size(); ++index) {
        path.lineTo(cmd.points[index].first, cmd.points[index].second);
    }
    if (cmd.closed) {
        path.closeSubpath();
    }
    QPen pen(to_color(cmd.has_stroke ? cmd.stroke : cmd.fill));
    pen.setWidthF(std::max(0.5, cmd.line_width));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(cmd.has_stroke ? pen : QPen(Qt::NoPen));
    painter.setBrush(cmd.has_fill ? QBrush(to_color(cmd.fill)) : QBrush(Qt::NoBrush));
    painter.drawPath(path);
}

void draw_image(QPainter& painter, const DrawCmd& cmd, const std::vector<QImage>& images) {
    if (cmd.image_id < 0 || static_cast<std::size_t>(cmd.image_id) >= images.size()) {
        return;
    }
    const QImage& image = images[static_cast<std::size_t>(cmd.image_id)];
    if (image.isNull()) {
        return;
    }
    const QRect ink = ink_bounds(image);
    const QRect source = ink.isNull() ? image.rect() : ink;
    const double scale =
        std::min(cmd.w / static_cast<double>(source.width()), cmd.h / static_cast<double>(source.height()));
    const double width = source.width() * scale;
    const double height = source.height() * scale;
    const QRectF target(cmd.x + (cmd.w - width) / 2.0, cmd.y + (cmd.h - height) / 2.0, width, height);
    painter.drawImage(target, image, QRectF(source));
}

}  // namespace

QImage render_commands(const std::vector<DrawCmd>& commands, int width, int height,
                       const std::vector<QImage>& images, const QtTextMetrics& metrics,
                       double pixel_ratio) {
    QImage canvas(static_cast<int>(std::round(width * pixel_ratio)),
                  static_cast<int>(std::round(height * pixel_ratio)), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(pixel_ratio, pixel_ratio);

    bool clipped = false;
    for (const DrawCmd& cmd : commands) {
        if (cmd.clip) {
            painter.setClipRect(QRectF(cmd.clip_x, cmd.clip_y, cmd.clip_w, cmd.clip_h));
            clipped = true;
        } else if (clipped) {
            painter.setClipRect(QRectF(0.0, 0.0, width, height));
            clipped = false;
        }
        switch (cmd.kind) {
            case DrawCmd::Kind::Rect: {
                const QRectF box = make_rect(cmd);
                fill_shape(painter, cmd, box);
                if (cmd.has_stroke) {
                    QPen pen(to_color(cmd.stroke));
                    pen.setWidthF(std::max(0.5, cmd.line_width));
                    painter.setPen(pen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRoundedRect(box, cmd.r, cmd.r);
                }
                break;
            }
            case DrawCmd::Kind::Text:
                draw_text(painter, cmd, metrics);
                break;
            case DrawCmd::Kind::Path:
                draw_path(painter, cmd);
                break;
            case DrawCmd::Kind::Image:
                draw_image(painter, cmd, images);
                break;
        }
    }
    painter.end();
    return canvas;
}

QRect ink_bounds(const QImage& image) {
    if (image.isNull()) {
        return QRect();
    }
    const QImage alpha = image.convertToFormat(QImage::Format_ARGB32);
    int left = alpha.width();
    int top = alpha.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < alpha.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
        for (int x = 0; x < alpha.width(); ++x) {
            if (qAlpha(row[x]) > 8) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < left || bottom < top) {
        return QRect();
    }
    return QRect(left, top, right - left + 1, bottom - top + 1);
}

}  // namespace dhw::qt

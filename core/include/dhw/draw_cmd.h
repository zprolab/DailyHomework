#pragma once

#include <string>
#include <utility>
#include <vector>

#include "dhw/text.h"

namespace dhw {

// 0xAARRGGBB.
using Color = unsigned int;

constexpr Color make_color(unsigned r, unsigned g, unsigned b, unsigned a = 0xFF) {
    return ((a & 0xFFu) << 24) | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

constexpr Color with_alpha(unsigned a, Color color) {
    return (color & 0x00FFFFFFu) | ((a & 0xFFu) << 24);
}

constexpr unsigned alpha_of(Color color) {
    return (color >> 24) & 0xFFu;
}

constexpr Color kTransparent = 0x00000000u;

// The only output of the layout engine; every backend (QPainter on screen, the
// offscreen PNG export) consumes exactly this list, so screen and export agree.
//
// Text convention: `x` is the layout origin, `y` is the **vertical centre of the
// ink** of the line (not the baseline). Renderers convert to a baseline with the
// metrics of the face. One command draws exactly one line; the layout wraps.
struct DrawCmd {
    enum class Kind { Rect, Text, Path, Image };
    enum class Align { Left, Center, Right };
    enum class Gradient { None, Vertical, Horizontal, Diagonal, Radial };

    Kind kind = Kind::Rect;
    Align align = Align::Left;

    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double r = 0.0;  // corner radius

    Color fill = 0u;
    Color fill2 = 0u;
    Color stroke = 0u;
    bool has_fill = false;
    bool has_stroke = false;
    double line_width = 0.0;

    Gradient gradient = Gradient::None;
    // Radial gradient centre and radius, independent of the box, so a caller can
    // clamp the box to the canvas without moving the glow.
    double grad_cx = 0.0;
    double grad_cy = 0.0;
    double grad_r = 0.0;

    std::string text;
    double size = 0.0;
    bool bold = false;
    Color color = 0u;
    Face face_hint = Face::Ui;
    bool icon_font = false;

    std::vector<std::pair<double, double>> points;
    bool closed = false;

    int image_id = -1;

    // Clip applied to this and every following command until the next one that
    // changes it; renderers keep a single clip level.
    bool clip = false;
    double clip_x = 0.0;
    double clip_y = 0.0;
    double clip_w = 0.0;
    double clip_h = 0.0;

    static DrawCmd rect(double px, double py, double pw, double ph, double radius, Color fill_color);
    static DrawCmd rect_stroked(double px, double py, double pw, double ph, double radius,
                                Color fill_color, Color stroke_color, double width);
    static DrawCmd outline(double px, double py, double pw, double ph, double radius,
                           Color stroke_color, double width);
    static DrawCmd gradient_rect(double px, double py, double pw, double ph, double radius,
                                 Color from, Color to, Gradient direction);
    static DrawCmd radial_glow(double px, double py, double pw, double ph, double radius,
                               double glow_cx, double glow_cy, double glow_radius, Color inner,
                               Color outer);
    static DrawCmd text_cmd(double px, double py, std::string content, double font_size,
                            bool is_bold, Color text_color, Align alignment = Align::Left);
    static DrawCmd icon(double px, double py, std::string glyph, double font_size, Color text_color,
                        Align alignment = Align::Center);
    static DrawCmd path(std::vector<std::pair<double, double>> path_points, Color stroke_color,
                        double width, bool is_closed = false);
    static DrawCmd filled_path(std::vector<std::pair<double, double>> path_points, Color fill_color);
    static DrawCmd image(double px, double py, double pw, double ph, int image_index);

    DrawCmd& with_clip(double px, double py, double pw, double ph);
};

}  // namespace dhw

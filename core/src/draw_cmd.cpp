#include "dhw/draw_cmd.h"

#include <utility>

namespace dhw {

DrawCmd DrawCmd::rect(double px, double py, double pw, double ph, double radius, Color fill_color) {
    DrawCmd cmd;
    cmd.kind = Kind::Rect;
    cmd.x = px;
    cmd.y = py;
    cmd.w = pw;
    cmd.h = ph;
    cmd.r = radius;
    cmd.fill = fill_color;
    cmd.has_fill = true;
    return cmd;
}

DrawCmd DrawCmd::rect_stroked(double px, double py, double pw, double ph, double radius,
                              Color fill_color, Color stroke_color, double width) {
    DrawCmd cmd = rect(px, py, pw, ph, radius, fill_color);
    cmd.stroke = stroke_color;
    cmd.has_stroke = true;
    cmd.line_width = width;
    return cmd;
}

DrawCmd DrawCmd::outline(double px, double py, double pw, double ph, double radius,
                         Color stroke_color, double width) {
    DrawCmd cmd;
    cmd.kind = Kind::Rect;
    cmd.x = px;
    cmd.y = py;
    cmd.w = pw;
    cmd.h = ph;
    cmd.r = radius;
    cmd.stroke = stroke_color;
    cmd.has_stroke = true;
    cmd.line_width = width;
    return cmd;
}

DrawCmd DrawCmd::gradient_rect(double px, double py, double pw, double ph, double radius, Color from,
                               Color to, Gradient direction) {
    DrawCmd cmd = rect(px, py, pw, ph, radius, from);
    cmd.gradient = direction;
    cmd.fill2 = to;
    return cmd;
}

DrawCmd DrawCmd::radial_glow(double px, double py, double pw, double ph, double radius,
                             double glow_cx, double glow_cy, double glow_radius, Color inner,
                             Color outer) {
    DrawCmd cmd = rect(px, py, pw, ph, radius, inner);
    cmd.gradient = Gradient::Radial;
    cmd.grad_cx = glow_cx;
    cmd.grad_cy = glow_cy;
    cmd.grad_r = glow_radius;
    cmd.fill2 = outer;
    return cmd;
}

DrawCmd DrawCmd::text_cmd(double px, double py, std::string content, double font_size, bool is_bold,
                          Color text_color, Align alignment) {
    DrawCmd cmd;
    cmd.kind = Kind::Text;
    cmd.x = px;
    cmd.y = py;
    cmd.text = std::move(content);
    cmd.size = font_size;
    cmd.bold = is_bold;
    cmd.color = text_color;
    cmd.align = alignment;
    return cmd;
}

DrawCmd DrawCmd::icon(double px, double py, std::string glyph, double font_size, Color text_color,
                      Align alignment) {
    DrawCmd cmd = text_cmd(px, py, std::move(glyph), font_size, false, text_color, alignment);
    cmd.icon_font = true;
    cmd.face_hint = Face::Icon;
    return cmd;
}

DrawCmd DrawCmd::path(std::vector<std::pair<double, double>> path_points, Color stroke_color,
                      double width, bool is_closed) {
    DrawCmd cmd;
    cmd.kind = Kind::Path;
    cmd.points = std::move(path_points);
    cmd.stroke = stroke_color;
    cmd.has_stroke = true;
    cmd.line_width = width;
    cmd.closed = is_closed;
    return cmd;
}

DrawCmd DrawCmd::filled_path(std::vector<std::pair<double, double>> path_points, Color fill_color) {
    DrawCmd cmd;
    cmd.kind = Kind::Path;
    cmd.points = std::move(path_points);
    cmd.fill = fill_color;
    cmd.has_fill = true;
    cmd.closed = true;
    return cmd;
}

DrawCmd DrawCmd::image(double px, double py, double pw, double ph, int image_index) {
    DrawCmd cmd;
    cmd.kind = Kind::Image;
    cmd.x = px;
    cmd.y = py;
    cmd.w = pw;
    cmd.h = ph;
    cmd.image_id = image_index;
    return cmd;
}

DrawCmd& DrawCmd::with_clip(double px, double py, double pw, double ph) {
    clip = true;
    clip_x = px;
    clip_y = py;
    clip_w = pw;
    clip_h = ph;
    return *this;
}

}  // namespace dhw

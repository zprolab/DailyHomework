#include "dhw/draw_cmd.h"
#include "dhw_test.h"

#include <string>

DHW_TEST(draw_cmd_colors) {
    const dhw::Color red = dhw::make_color(255, 0, 0);
    DHW_CHECK_EQ(red, 0xFFFF0000u);
    DHW_CHECK_EQ(dhw::make_color(0x12, 0x34, 0x56, 0x78), 0x78123456u);
    DHW_CHECK_EQ(dhw::with_alpha(0x00, red), 0x00FF0000u);
    DHW_CHECK_EQ(dhw::alpha_of(dhw::with_alpha(0x80, red)), 0x80u);
    DHW_CHECK_EQ(dhw::kTransparent, 0x00000000u);
}

DHW_TEST(draw_cmd_rect_factories) {
    const dhw::DrawCmd filled = dhw::DrawCmd::rect(1, 2, 30, 40, 8, 0xFF112233u);
    DHW_CHECK(filled.kind == dhw::DrawCmd::Kind::Rect);
    DHW_CHECK_EQ(filled.x, 1.0);
    DHW_CHECK_EQ(filled.h, 40.0);
    DHW_CHECK_EQ(filled.r, 8.0);
    DHW_CHECK_EQ(filled.fill, 0xFF112233u);
    DHW_CHECK(filled.has_fill);
    DHW_CHECK(!filled.has_stroke);
    DHW_CHECK(!filled.clip);

    const dhw::DrawCmd stroked =
        dhw::DrawCmd::rect_stroked(0, 0, 10, 10, 0, 0xFF000000u, 0xFFFFFFFFu, 2.0);
    DHW_CHECK(stroked.has_fill);
    DHW_CHECK(stroked.has_stroke);
    DHW_CHECK_EQ(stroked.line_width, 2.0);

    const dhw::DrawCmd outline = dhw::DrawCmd::outline(0, 0, 10, 10, 4, 0xFF00FF00u, 1.5);
    DHW_CHECK(!outline.has_fill);
    DHW_CHECK(outline.has_stroke);
    DHW_CHECK_EQ(outline.stroke, 0xFF00FF00u);
}

DHW_TEST(draw_cmd_gradient_factories) {
    const dhw::DrawCmd vertical =
        dhw::DrawCmd::gradient_rect(0, 0, 100, 200, 0, 0xFFFFFFFFu, 0xFF000000u,
                                    dhw::DrawCmd::Gradient::Vertical);
    DHW_CHECK(vertical.gradient == dhw::DrawCmd::Gradient::Vertical);
    DHW_CHECK_EQ(vertical.fill2, 0xFF000000u);
    DHW_CHECK_EQ(vertical.fill, 0xFFFFFFFFu);

    const dhw::DrawCmd glow = dhw::DrawCmd::radial_glow(0, 0, 1920, 1080, 0, 200, 150, 600,
                                                        0x804488FFu, dhw::kTransparent);
    DHW_CHECK(glow.gradient == dhw::DrawCmd::Gradient::Radial);
    DHW_CHECK_EQ(glow.grad_cx, 200.0);
    DHW_CHECK_EQ(glow.grad_cy, 150.0);
    DHW_CHECK_EQ(glow.grad_r, 600.0);
    DHW_CHECK_EQ(dhw::alpha_of(glow.fill2), 0u);
}

DHW_TEST(draw_cmd_text_and_icon) {
    const dhw::DrawCmd text = dhw::DrawCmd::text_cmd(10, 20, "语文", 38, true, 0xFF203040u,
                                                 dhw::DrawCmd::Align::Center);
    DHW_CHECK(text.kind == dhw::DrawCmd::Kind::Text);
    DHW_CHECK_EQ(text.text, std::string("语文"));
    DHW_CHECK_EQ(text.size, 38.0);
    DHW_CHECK(text.bold);
    DHW_CHECK(text.align == dhw::DrawCmd::Align::Center);
    DHW_CHECK(!text.icon_font);
    DHW_CHECK(text.face_hint == dhw::Face::Ui);

    const dhw::DrawCmd icon = dhw::DrawCmd::icon(0, 0, "\uE7E7", 24, 0xFFFFFFFFu);
    DHW_CHECK(icon.icon_font);
    DHW_CHECK(icon.face_hint == dhw::Face::Icon);
}

DHW_TEST(draw_cmd_path_and_image) {
    const dhw::DrawCmd path =
        dhw::DrawCmd::path({{0, 0}, {4, 6}, {10, 1}}, 0xFFFF8800u, 3.0, true);
    DHW_CHECK(path.kind == dhw::DrawCmd::Kind::Path);
    DHW_CHECK_EQ(path.points.size(), std::size_t{3});
    DHW_CHECK_EQ(path.points[1].first, 4.0);
    DHW_CHECK_EQ(path.points[1].second, 6.0);
    DHW_CHECK(path.closed);
    DHW_CHECK(path.has_stroke);
    DHW_CHECK(!path.has_fill);

    const dhw::DrawCmd filled = dhw::DrawCmd::filled_path({{0, 0}, {1, 0}, {0, 1}}, 0xFF00FFFFu);
    DHW_CHECK(filled.has_fill);
    DHW_CHECK(filled.closed);

    const dhw::DrawCmd image = dhw::DrawCmd::image(5, 6, 120, 120, 3);
    DHW_CHECK(image.kind == dhw::DrawCmd::Kind::Image);
    DHW_CHECK_EQ(image.image_id, 3);
    DHW_CHECK_EQ(image.w, 120.0);
}

DHW_TEST(draw_cmd_clip_helper) {
    dhw::DrawCmd cmd = dhw::DrawCmd::rect(0, 0, 10, 10, 0, 0xFFFFFFFFu);
    DHW_CHECK(!cmd.clip);
    cmd.with_clip(1, 2, 3, 4);
    DHW_CHECK(cmd.clip);
    DHW_CHECK_EQ(cmd.clip_x, 1.0);
    DHW_CHECK_EQ(cmd.clip_y, 2.0);
    DHW_CHECK_EQ(cmd.clip_w, 3.0);
    DHW_CHECK_EQ(cmd.clip_h, 4.0);
}

#pragma once

#include <vector>

#include "dhw/draw_cmd.h"

namespace dhw::icons {

// Vector icons drawn with DrawCmd primitives: no symbol font is needed, so every
// platform shows exactly the same glyphs.
enum class Icon {
    Megaphone,
    Book,
    Calendar,
    Microphone,
    Warning,
    Check,
    Sun,
};

// Emits the icon scaled into a size x size box whose top left corner is (x, y).
// Icons are stroke based and keep a small optical margin inside the box.
std::vector<DrawCmd> draw(Icon icon, double x, double y, double size, Color color);

// The check mark used for "要交" entries: orange ring with a bold polyline inside.
std::vector<DrawCmd> submit_mark(double x, double y, double size, Color color);

}  // namespace dhw::icons

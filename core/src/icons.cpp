#include "dhw/icons.h"

#include <cmath>
#include <utility>

namespace dhw::icons {
namespace {

// Icons are authored on a 24 x 24 grid and scaled to the requested box.
constexpr double kGrid = 24.0;

std::vector<std::pair<double, double>> scale_points(
    std::vector<std::pair<double, double>> points, double x, double y, double unit) {
    for (std::pair<double, double>& point : points) {
        point.first = x + point.first * unit;
        point.second = y + point.second * unit;
    }
    return points;
}

}  // namespace

std::vector<DrawCmd> draw(Icon icon, double x, double y, double size, Color color) {
    const double unit = size / kGrid;
    const double thin = std::max(1.0, 1.7 * unit);
    const double thick = std::max(1.0, 2.2 * unit);
    std::vector<DrawCmd> commands;

    switch (icon) {
        case Icon::Megaphone: {
            commands.push_back(DrawCmd::filled_path(
                scale_points({{2.5, 10.0}, {11.0, 4.5}, {11.0, 19.5}, {2.5, 14.0}}, x, y, unit),
                color));
            commands.push_back(DrawCmd::rect(x + 4.5 * unit, y + 14.0 * unit, 4.0 * unit, 6.0 * unit,
                                             1.5 * unit, color));
            commands.push_back(DrawCmd::path(
                scale_points({{13.5, 8.5}, {17.0, 5.5}}, x, y, unit), color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{13.5, 15.5}, {17.0, 18.5}}, x, y, unit), color, thin));
            break;
        }
        case Icon::Book: {
            commands.push_back(DrawCmd::outline(x + 3.0 * unit, y + 3.5 * unit, 18.0 * unit, 17.0 * unit,
                                                2.6 * unit, color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{12.0, 3.8}, {12.0, 20.2}}, x, y, unit), color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{6.0, 8.5}, {9.4, 8.5}}, x, y, unit), color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{6.0, 13.0}, {9.4, 13.0}}, x, y, unit), color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{14.6, 8.5}, {18.0, 8.5}}, x, y, unit), color, thin));
            break;
        }
        case Icon::Calendar: {
            commands.push_back(DrawCmd::outline(x + 3.0 * unit, y + 4.5 * unit, 18.0 * unit, 16.0 * unit,
                                                2.6 * unit, color, thin));
            commands.push_back(DrawCmd::rect(x + 3.0 * unit, y + 4.5 * unit, 18.0 * unit, 4.6 * unit,
                                             2.4 * unit, color));
            commands.push_back(DrawCmd::rect(x + 6.6 * unit, y + 2.4 * unit, 1.6 * unit, 3.4 * unit,
                                             0.8 * unit, color));
            commands.push_back(DrawCmd::rect(x + 15.8 * unit, y + 2.4 * unit, 1.6 * unit, 3.4 * unit,
                                             0.8 * unit, color));
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 3; ++column) {
                    commands.push_back(DrawCmd::rect(x + (6.4 + column * 5.2) * unit,
                                                     y + (12.4 + row * 3.4) * unit, 1.7 * unit,
                                                     1.7 * unit, 0.85 * unit, color));
                }
            }
            break;
        }
        case Icon::Microphone: {
            commands.push_back(DrawCmd::outline(x + 9.0 * unit, y + 2.8 * unit, 6.0 * unit, 11.4 * unit,
                                                3.0 * unit, color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{5.8, 11.4}, {6.4, 15.0}, {10.0, 17.4}, {14.0, 17.4}, {17.6, 15.0},
                              {18.2, 11.4}},
                             x, y, unit),
                color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{12.0, 17.4}, {12.0, 20.8}}, x, y, unit), color, thin));
            commands.push_back(DrawCmd::path(
                scale_points({{8.6, 20.8}, {15.4, 20.8}}, x, y, unit), color, thick));
            break;
        }
        case Icon::Warning: {
            commands.push_back(DrawCmd::filled_path(
                scale_points({{12.0, 3.0}, {22.6, 20.6}, {1.4, 20.6}}, x, y, unit), color));
            commands.push_back(DrawCmd::rect(x + 11.0 * unit, y + 9.0 * unit, 2.0 * unit, 5.6 * unit,
                                             1.0 * unit, 0xFFFFFFFFu));
            commands.push_back(DrawCmd::rect(x + 11.0 * unit, y + 16.4 * unit, 2.0 * unit, 2.0 * unit,
                                             1.0 * unit, 0xFFFFFFFFu));
            break;
        }
        case Icon::Check: {
            commands.push_back(DrawCmd::path(
                scale_points({{4.2, 12.8}, {9.6, 18.0}, {19.8, 6.2}}, x, y, unit), color, thick));
            break;
        }
        case Icon::Sun: {
            const double centre = 12.0 * unit;
            const double radius = 5.0 * unit;
            commands.push_back(DrawCmd::outline(x + centre - radius, y + centre - radius,
                                                radius * 2.0, radius * 2.0, radius, color, thin));
            for (int i = 0; i < 8; ++i) {
                const double angle = static_cast<double>(i) * 3.14159265358979 / 4.0;
                const double inner = 7.2 * unit;
                const double outer = 10.2 * unit;
                commands.push_back(DrawCmd::path(
                    {{x + centre + std::cos(angle) * inner, y + centre + std::sin(angle) * inner},
                     {x + centre + std::cos(angle) * outer, y + centre + std::sin(angle) * outer}},
                    color, thin));
            }
            break;
        }
    }
    return commands;
}

std::vector<DrawCmd> submit_mark(double x, double y, double size, Color color) {
    std::vector<DrawCmd> commands;
    const double centre_y = y + size / 2.0;
    const double ring_width = std::max(3.0, std::round(size * 0.11));
    commands.push_back(DrawCmd::rect_stroked(x, y, size, size, size / 2.0, 0xFFFFFFFFu, color,
                                             ring_width));
    const double tick_width = std::max(3.0, std::round(size * 0.15));
    commands.push_back(DrawCmd::path({{x + 0.30 * size, centre_y + 0.02 * size},
                                      {x + 0.45 * size, centre_y + 0.18 * size},
                                      {x + 0.73 * size, centre_y - 0.19 * size}},
                                     color, tick_width));
    return commands;
}

}  // namespace dhw::icons

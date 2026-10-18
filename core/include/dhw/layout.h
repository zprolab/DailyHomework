#pragma once

#include <string>
#include <vector>

#include "dhw/draw_cmd.h"
#include "dhw/schedule.h"
#include "dhw/text.h"

namespace dhw {

// The presentation canvas is a fixed design space. Screen and export both use it,
// so the on-screen picture and the PNG are byte for byte the same layout.
namespace layout_geometry {
inline constexpr double kWidth = 1920.0;
inline constexpr double kHeight = 1080.0;
inline constexpr double kMargin = 36.0;
inline constexpr double kGap = 22.0;
inline constexpr double kHeadHeight = 132.0;
inline constexpr double kPanelHeaderHeight = 96.0;
inline constexpr double kLeftWidth = 1464.0;
inline constexpr double kRightWidth = 362.0;
inline constexpr double kBodyTop = kMargin + kHeadHeight + kGap;
inline constexpr double kBottom = kHeight - kMargin;
inline constexpr double kContentWidth = kWidth - 2.0 * kMargin;
inline constexpr double kSpeechTagWidth = 112.0;
}  // namespace layout_geometry

namespace layout_theme {
inline constexpr Color kBackgroundTop = 0xFFF8FAFFu;
inline constexpr Color kBackgroundBottom = 0xFFE4EDFAu;
inline constexpr Color kGlowBlue = 0x59669AE6u;
inline constexpr Color kGlowCyan = 0x475AC8DCu;
inline constexpr Color kHeadFrom = 0xFF5E93DEu;
inline constexpr Color kHeadTo = 0xFF2A589Eu;
inline constexpr Color kCardFace = 0xFFFFFFFFu;
inline constexpr Color kCardStroke = 0xFFE1E8F4u;
inline constexpr Color kShadow = 0xFF1E3358u;
inline constexpr Color kBlue = 0xFF2E5FA3u;
inline constexpr Color kBlueSoft = 0xFFE8F0FBu;
inline constexpr Color kInk = 0xFF1F2937u;
inline constexpr Color kGray = 0xFF8A93A3u;
inline constexpr Color kAmberFrom = 0xFFFFE2AEu;
inline constexpr Color kAmberTo = 0xFFF5C489u;
inline constexpr Color kAmberInk = 0xFF6B4308u;
inline constexpr Color kOrange = 0xFFE07B00u;
inline constexpr Color kRed = 0xFFD64545u;
inline constexpr Color kRedSoft = 0xFFFBE9E9u;
inline constexpr Color kWarnFace = 0xFFFDECECu;
inline constexpr Color kWarnInk = 0xFFC0392Bu;
inline constexpr Color kWhite = 0xFFFFFFFFu;
}  // namespace layout_theme

// Every adjustable visual parameter of the presentation, in the order the style
// file writes them out. Load it from "<data dir>/style.json" with dhw/style.h.
struct LayoutStyle {
    double headline_font = 48.0;
    double headline_sub_font = 26.0;
    double panel_title_font = 42.0;
    double panel_radius = 28.0;
    double panel_padding = 26.0;
    double announcement_font_max = 40.0;
    double announcement_font_min = 26.0;
    double homework_font_max = 36.0;
    double homework_font_min = 26.0;
    int homework_label_max_chars = 5;
    double badge_size = 44.0;
    double badge_radius = 15.0;
    double badge_font = 26.0;
    double schedule_pitch_max = 52.0;
    double schedule_font_max = 32.0;
    double schedule_font_min = 22.0;
    double schedule_row_ratio = 0.62;
    double schedule_chip_max = 30.0;
    double speech_font_max = 32.0;
    double speech_font_min = 24.0;
    double empty_state_font = 30.0;
    double warning_font = 26.0;
    double mode_pill_font = 26.0;
};

struct LayoutOptions {
    // Image id of the class badge, or -1 when there is none.
    int badge_image_id = -1;
    bool show_badge = false;
    LayoutStyle style;
};

// What the layout actually chose, plus the panel boxes so tests can assert that
// nothing overlaps or escapes the canvas.
struct LayoutReport {
    // Typographic hierarchy: headline >= panel title > every card body text.
    double headline_font_size = 0.0;
    double panel_title_font_size = 0.0;
    double announcement_font_size = 0.0;
    double homework_font_size = 0.0;
    double schedule_font_size = 0.0;
    double speech_font_size = 0.0;
    double badge_size = 0.0;
    double badge_font_size = 0.0;
    int homework_groups = 0;
    int homework_rows = 0;
    int homework_badges = 0;
    int schedule_rows = 0;
    bool announcement_bar = false;
    bool speech_warning = false;
    bool homework_truncated = false;
    bool announcement_truncated = false;
};

struct LayoutResult {
    std::vector<DrawCmd> commands;
    LayoutReport report;
};

LayoutResult layout_presentation(const ViewModel& view, const TextMetrics& metrics,
                                const LayoutOptions& options = {});

}  // namespace dhw

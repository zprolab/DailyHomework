#include "dhw/style.h"

#include <optional>
#include <string>

#include "dhw/fs.h"

namespace dhw {
namespace {

const Json* member(const Json& root, const char* key) {
    return root.is_object() ? root.find(key) : nullptr;
}

double number_or(const Json& root, const char* key, double fallback) {
    const Json* value = member(root, key);
    return value != nullptr ? value->as_number(fallback) : fallback;
}

int int_or(const Json& root, const char* key, int fallback) {
    const Json* value = member(root, key);
    return value != nullptr ? static_cast<int>(value->as_int(fallback)) : fallback;
}

}  // namespace

LayoutStyle style_from_json(const Json& root) {
    LayoutStyle style;
    style.headline_font = number_or(root, "headline_font", style.headline_font);
    style.headline_sub_font = number_or(root, "headline_sub_font", style.headline_sub_font);
    style.panel_title_font = number_or(root, "panel_title_font", style.panel_title_font);
    style.panel_radius = number_or(root, "panel_radius", style.panel_radius);
    style.panel_padding = number_or(root, "panel_padding", style.panel_padding);
    style.announcement_font_max =
        number_or(root, "announcement_font_max", style.announcement_font_max);
    style.announcement_font_min =
        number_or(root, "announcement_font_min", style.announcement_font_min);
    style.homework_font_max = number_or(root, "homework_font_max", style.homework_font_max);
    style.homework_font_min = number_or(root, "homework_font_min", style.homework_font_min);
    style.homework_label_max_chars =
        int_or(root, "homework_label_max_chars", style.homework_label_max_chars);
    style.badge_size = number_or(root, "badge_size", style.badge_size);
    style.badge_radius = number_or(root, "badge_radius", style.badge_radius);
    style.badge_font = number_or(root, "badge_font", style.badge_font);
    style.schedule_pitch_max = number_or(root, "schedule_pitch_max", style.schedule_pitch_max);
    style.schedule_font_max = number_or(root, "schedule_font_max", style.schedule_font_max);
    style.schedule_font_min = number_or(root, "schedule_font_min", style.schedule_font_min);
    style.schedule_row_ratio = number_or(root, "schedule_row_ratio", style.schedule_row_ratio);
    style.schedule_chip_max = number_or(root, "schedule_chip_max", style.schedule_chip_max);
    style.speech_font_max = number_or(root, "speech_font_max", style.speech_font_max);
    style.speech_font_min = number_or(root, "speech_font_min", style.speech_font_min);
    style.empty_state_font = number_or(root, "empty_state_font", style.empty_state_font);
    style.warning_font = number_or(root, "warning_font", style.warning_font);
    style.mode_pill_font = number_or(root, "mode_pill_font", style.mode_pill_font);
    return style;
}

Json style_to_json(const LayoutStyle& style) {
    Json root = Json::object();
    root.set("headline_font", Json(style.headline_font));
    root.set("headline_sub_font", Json(style.headline_sub_font));
    root.set("panel_title_font", Json(style.panel_title_font));
    root.set("panel_radius", Json(style.panel_radius));
    root.set("panel_padding", Json(style.panel_padding));
    root.set("announcement_font_max", Json(style.announcement_font_max));
    root.set("announcement_font_min", Json(style.announcement_font_min));
    root.set("homework_font_max", Json(style.homework_font_max));
    root.set("homework_font_min", Json(style.homework_font_min));
    root.set("homework_label_max_chars", Json(style.homework_label_max_chars));
    root.set("badge_size", Json(style.badge_size));
    root.set("badge_radius", Json(style.badge_radius));
    root.set("badge_font", Json(style.badge_font));
    root.set("schedule_pitch_max", Json(style.schedule_pitch_max));
    root.set("schedule_font_max", Json(style.schedule_font_max));
    root.set("schedule_font_min", Json(style.schedule_font_min));
    root.set("schedule_row_ratio", Json(style.schedule_row_ratio));
    root.set("schedule_chip_max", Json(style.schedule_chip_max));
    root.set("speech_font_max", Json(style.speech_font_max));
    root.set("speech_font_min", Json(style.speech_font_min));
    root.set("empty_state_font", Json(style.empty_state_font));
    root.set("warning_font", Json(style.warning_font));
    root.set("mode_pill_font", Json(style.mode_pill_font));
    return root;
}

StyleLoad load_style_file(const std::string& path) {
    StyleLoad result;
    if (!fs::exists(path)) {
        return result;
    }
    const std::optional<std::string> text = fs::read_text(path);
    if (!text.has_value()) {
        result.error = "读取 " + path + " 失败";
        return result;
    }
    const JsonParse parsed = Json::parse(*text);
    if (!parsed.ok) {
        result.error = "解析 " + path + " 失败：" + parsed.error;
        return result;
    }
    if (!parsed.value.is_object()) {
        result.error = "解析 " + path + " 失败：根节点不是 JSON 对象";
        return result;
    }
    result.style = style_from_json(parsed.value);
    result.loaded = true;
    return result;
}

bool save_style_file(const std::string& path, const LayoutStyle& style) {
    const std::string dir = fs::parent(path);
    if (!dir.empty()) {
        fs::create_directories(dir);
    }
    return fs::write_text_atomic(path, style_to_json(style).dump(2) + "\n");
}

}  // namespace dhw

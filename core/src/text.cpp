#include "dhw/text.h"

#include "dhw/utf8.h"

namespace dhw {
namespace {

double code_point_width(char32_t code_point) {
    if (utf8::is_wide(code_point)) {
        return 1.0;
    }
    if (code_point == U' ') {
        return 0.28;
    }
    if (code_point < 0x80) {
        return 0.5;
    }
    return 0.6;
}

}  // namespace

std::vector<std::string> wrap_text(const TextMetrics& metrics, std::string_view text, double size,
                                   bool bold, double max_width, Face face) {
    std::vector<std::string> lines;
    const std::u32string code_points = utf8::decode(text);

    std::u32string current;
    std::string current_text;
    for (char32_t code_point : code_points) {
        if (code_point == U'\n') {
            lines.push_back(current_text);
            current.clear();
            current_text.clear();
            continue;
        }
        current.push_back(code_point);
        const std::string candidate = utf8::encode(current);
        if (!current_text.empty() && max_width > 0.0 &&
            metrics.measure(candidate, size, bold, face) > max_width) {
            lines.push_back(current_text);
            current.assign(1, code_point);
            current_text = utf8::encode(current);
            continue;
        }
        current_text = candidate;
    }
    lines.push_back(current_text);
    return lines;
}

std::string ellipsize(const TextMetrics& metrics, std::string_view text, double size, bool bold,
                      double max_width, Face face) {
    if (max_width <= 0.0 || metrics.measure(text, size, bold, face) <= max_width) {
        return std::string(text);
    }
    const std::u32string code_points = utf8::decode(text);
    const std::string dots = "…";
    std::u32string kept;
    std::string result;
    for (char32_t code_point : code_points) {
        kept.push_back(code_point);
        const std::string candidate = utf8::encode(kept) + dots;
        if (metrics.measure(candidate, size, bold, face) > max_width) {
            kept.pop_back();
            break;
        }
        result = utf8::encode(kept);
    }
    return result + dots;
}

double StubTextMetrics::measure(std::string_view text, double size, bool bold, Face face) const {
    (void)face;
    double total = 0.0;
    for (char32_t code_point : utf8::decode(text)) {
        total += code_point_width(code_point);
    }
    return total * size * (bold ? 1.03 : 1.0);
}

LineMetrics StubTextMetrics::line_metrics(double size, bool bold, Face face) const {
    (void)bold;
    (void)face;
    return LineMetrics{size * 0.88, size * 0.22, size * 0.92};
}

}  // namespace dhw

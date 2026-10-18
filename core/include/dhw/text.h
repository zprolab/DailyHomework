#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dhw {

// Font faces used by the presentation layout.
enum class Face {
    Ui,
    Icon,
};

struct LineMetrics {
    double ascent = 0.0;
    double descent = 0.0;
    // Distance from the line box top to the alphabetic baseline.
    double baseline = 0.0;
};

// Text measurement is the only font dependent part of the layout engine. The Qt
// shell supplies the real implementation, tests supply a deterministic stub, so
// the layout itself stays platform free and reproducible.
class TextMetrics {
public:
    virtual ~TextMetrics() = default;

    // Advance width of a single line of text.
    virtual double measure(std::string_view text, double size, bool bold, Face face = Face::Ui) const = 0;
    virtual LineMetrics line_metrics(double size, bool bold, Face face = Face::Ui) const = 0;
};

// Greedy per code point wrapping, CJK friendly: mirrors the behaviour of the old
// layout.ts wrapText. Explicit '\n' always starts a new line. A single code point
// wider than max_width still gets its own line.
std::vector<std::string> wrap_text(const TextMetrics& metrics, std::string_view text, double size,
                                   bool bold, double max_width, Face face = Face::Ui);

// Truncates to max_width, appending "…" when something was dropped.
std::string ellipsize(const TextMetrics& metrics, std::string_view text, double size, bool bold,
                      double max_width, Face face = Face::Ui);

// Deterministic metrics for tests and for headless golden runs: half an em per
// ASCII code point, one em per wide (CJK) code point.
class StubTextMetrics final : public TextMetrics {
public:
    double measure(std::string_view text, double size, bool bold, Face face = Face::Ui) const override;
    LineMetrics line_metrics(double size, bool bold, Face face = Face::Ui) const override;
};

}  // namespace dhw

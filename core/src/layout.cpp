#include "dhw/layout.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "dhw/icons.h"
#include "dhw/utf8.h"

namespace dhw {
namespace {

using namespace layout_geometry;
using namespace layout_theme;

struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double right() const { return x + w; }
    double bottom() const { return y + h; }
    double center_x() const { return x + w / 2.0; }
    double center_y() const { return y + h / 2.0; }
};

void push_shadow(std::vector<DrawCmd>& out, const Box& box, double radius) {
    const double layers[][3] = {{4.0, 0.0, 0.055}, {9.0, 3.0, 0.040}, {16.0, 8.0, 0.026}};
    for (const auto& layer : layers) {
        DrawCmd cmd = DrawCmd::rect(box.x - layer[1], box.y + layer[0] - layer[1],
                                    box.w + 2.0 * layer[1], box.h + 2.0 * layer[1],
                                    radius + layer[1], with_alpha(static_cast<unsigned>(layer[2] * 255.0),
                                                                  kShadow));
        out.push_back(cmd);
    }
}

void panel(std::vector<DrawCmd>& out, const Box& box, const LayoutStyle& style) {
    push_shadow(out, box, style.panel_radius);
    out.push_back(DrawCmd::rect_stroked(box.x, box.y, box.w, box.h, style.panel_radius, kCardFace,
                                        kCardStroke, 1.6));
}

// Panel header: icon capsule + title + optional count chip. Returns the body box.
Box panel_header(std::vector<DrawCmd>& out, const Box& box, icons::Icon icon, const std::string& title,
                 const std::string& count_text, const TextMetrics& metrics,
                 const LayoutStyle& style) {
    const double capsule = 52.0;
    const double title_x = box.x + style.panel_padding + capsule + 14.0;
    const double center_y = box.y + 48.0;

    out.push_back(DrawCmd::rect(box.x + style.panel_padding, center_y - capsule / 2.0, capsule,
                                capsule, 16.0, kBlueSoft));
    for (const DrawCmd& cmd : icons::draw(icon, box.x + style.panel_padding + 13.0,
                                          center_y - 13.0, 26.0, kBlue)) {
        out.push_back(cmd);
    }
    const double title_size = style.panel_title_font;
    out.push_back(DrawCmd::text_cmd(title_x, center_y, title, title_size, true, kBlue));

    if (!count_text.empty()) {
        const double chip_size = 24.0;
        const double text_width = metrics.measure(count_text, chip_size, true);
        const double chip_w = text_width + 26.0;
        const double chip_x = box.right() - style.panel_padding - chip_w;
        out.push_back(DrawCmd::rect(chip_x, center_y - 15.0, chip_w, 30.0, 15.0, kBlueSoft));
        out.push_back(DrawCmd::text_cmd(chip_x + chip_w / 2.0, center_y, count_text, chip_size, true,
                                        kBlue, DrawCmd::Align::Center));
    }

    const double divider_y = box.y + kPanelHeaderHeight - 14.0;
    out.push_back(DrawCmd::rect(box.x + style.panel_padding, divider_y,
                                box.w - 2.0 * style.panel_padding, 1.5, 0.0, kCardStroke));

    Box body;
    body.x = box.x + style.panel_padding;
    body.y = box.y + kPanelHeaderHeight;
    body.w = box.w - 2.0 * style.panel_padding;
    body.h = box.h - kPanelHeaderHeight - style.panel_padding;
    return body;
}

void number_badge(std::vector<DrawCmd>& out, double x, double center_y, int number,
                  const LayoutStyle& style) {
    Box box{x, center_y - style.badge_size / 2.0, style.badge_size, style.badge_size};
    for (const auto& layer : {std::pair<double, double>{3.0, 0.0}, {6.0, 2.0}}) {
        out.push_back(DrawCmd::rect(box.x - layer.second, box.y + layer.first - layer.second,
                                    box.w + 2.0 * layer.second, box.h + 2.0 * layer.second,
                                    style.badge_radius + layer.second,
                                    with_alpha(38, 0x2E5FA3u)));
    }
    out.push_back(DrawCmd::gradient_rect(box.x, box.y, box.w, box.h, style.badge_radius, 0xFF5C94DEu,
                                         kBlue, DrawCmd::Gradient::Vertical));
    out.push_back(DrawCmd::text_cmd(box.center_x(), box.center_y(), std::to_string(number),
                                    style.badge_font, true, kWhite, DrawCmd::Align::Center));
}

struct HwEntry {
    std::string text;
    bool submit = false;
};

struct HwRow {
    std::string label;
    std::vector<HwEntry> entries;
};

struct HwGroup {
    std::vector<HwRow> rows;
};

bool is_english_subject(const std::string& subject) {
    return subject == "英语" || subject == "英语A" || subject == "英语B";
}

std::vector<HwGroup> build_groups(const ViewModel& view) {
    std::vector<std::string> subjects;
    const auto remember = [&subjects](const std::string& subject) {
        if (std::find(subjects.begin(), subjects.end(), subject) == subjects.end()) {
            subjects.push_back(subject);
        }
    };
    for (const std::string& subject : view.homework_suggestions) {
        if (!subject.empty()) {
            remember(subject);
        }
    }
    for (const Homework& item : view.homework) {
        remember(item.subject);
    }

    std::vector<HwGroup> groups;
    bool english_done = false;
    for (const std::string& subject : subjects) {
        if (is_english_subject(subject)) {
            if (english_done) {
                continue;
            }
            english_done = true;
            HwGroup group;
            for (const std::string& candidate : subjects) {
                if (!is_english_subject(candidate)) {
                    continue;
                }
                HwRow row;
                row.label = candidate;
                for (const Homework& item : view.homework) {
                    if (item.subject == candidate) {
                        row.entries.push_back(HwEntry{item.text, item.submit_next});
                    }
                }
                if (!row.entries.empty()) {
                    group.rows.push_back(std::move(row));
                }
            }
            if (!group.rows.empty()) {
                groups.push_back(std::move(group));
            }
            continue;
        }
        HwRow row;
        row.label = subject;
        for (const Homework& item : view.homework) {
            if (item.subject == subject) {
                row.entries.push_back(HwEntry{item.text, item.submit_next});
            }
        }
        if (!row.entries.empty()) {
            HwGroup group;
            group.rows.push_back(std::move(row));
            groups.push_back(std::move(group));
        }
    }
    return groups;
}

struct Piece {
    std::string text;
    bool check = false;
    double width = 0.0;
};

struct FlowLine {
    std::vector<Piece> pieces;
    double width = 0.0;
};

struct LaidRow {
    double y = 0.0;
    double height = 0.0;
    double label_width = 0.0;
    std::string label;
    bool badge = false;
    int badge_number = 0;
    std::vector<FlowLine> lines;
};

struct HomeworkLayout {
    double height = 0.0;
    double label_width = 0.0;
    std::vector<LaidRow> rows;
    bool truncated = false;
};

double label_budget(const TextMetrics& metrics, double size, const LayoutStyle& style) {
    std::string sample;
    for (int index = 0; index < std::max(0, style.homework_label_max_chars); ++index) {
        sample += "字";
    }
    sample += "：";
    return metrics.measure(sample, size, true);
}

std::string fit_label(const TextMetrics& metrics, const std::string& subject, double size,
                      double budget) {
    std::string label = subject + "：";
    if (metrics.measure(label, size, true) <= budget) {
        return label;
    }
    const std::u32string code_points = utf8::decode(subject);
    if (code_points.empty()) {
        return label;
    }
    for (std::size_t keep = code_points.size(); keep >= 1; --keep) {
        const std::u32string kept(code_points.begin(), code_points.begin() + static_cast<long>(keep));
        const std::string candidate = utf8::encode(kept) + "…：";
        if (metrics.measure(candidate, size, true) <= budget) {
            return candidate;
        }
    }
    return "…：";
}

// Records the exact flow used for drawing, so measuring and painting can never
// drift apart.
HomeworkLayout layout_homework(const std::vector<HwGroup>& groups, const TextMetrics& metrics,
                               double size, double text_x, double right, bool submit_only,
                               const LayoutStyle& style) {
    HomeworkLayout result;
    const double line_height = size * 1.4;
    const double content_x = text_x;
    const double available = right - content_x;
    const double group_gap = line_height * 0.35;

    double widest_label = 0.0;
    for (const HwGroup& group : groups) {
        for (const HwRow& row : group.rows) {
            widest_label = std::max(widest_label, metrics.measure(row.label + "：", size, true));
        }
    }
    result.label_width = std::min(widest_label, label_budget(metrics, size, style));

    double cursor = 0.0;
    int number = 0;
    for (const HwGroup& group : groups) {
        number += 1;
        if (cursor > 0.0) {
            cursor += group_gap;
        }
        bool first_row = true;
        for (const HwRow& row : group.rows) {
            LaidRow laid;
            laid.label = fit_label(metrics, row.label, size, result.label_width);
            laid.y = cursor;
            laid.badge = first_row;
            laid.badge_number = number;

            std::vector<FlowLine> lines(1);
            const auto new_line = [&lines]() { lines.emplace_back(); };
            for (std::size_t index = 0; index < row.entries.size(); ++index) {
                const HwEntry& entry = row.entries[index];
                std::string prefix;
                if (row.entries.size() > 1) {
                    prefix = std::to_string(index + 1) + " ";
                }
                const std::string content = prefix + entry.text;
                Piece text_piece;
                text_piece.text.clear();
                lines.back().pieces.push_back(text_piece);
                std::string built;
                for (char32_t code_point : utf8::decode(content)) {
                    const std::string candidate = built + utf8::encode(std::u32string(1, code_point));
                    const double candidate_width = metrics.measure(candidate, size, true);
                    const double previous = lines.back().pieces.back().width;
                    if (!built.empty() && lines.back().width - previous + candidate_width > available) {
                        new_line();
                        lines.back().pieces.push_back(Piece{});
                        built = utf8::encode(std::u32string(1, code_point));
                    } else {
                        built = candidate;
                    }
                    const double width = metrics.measure(built, size, true);
                    lines.back().width += width - lines.back().pieces.back().width;
                    lines.back().pieces.back().text = built;
                    lines.back().pieces.back().width = width;
                }
                if (index + 1 < row.entries.size()) {
                    const double space = metrics.measure("　", size, true);
                    if (lines.back().width + space > available && !lines.back().pieces.empty()) {
                        new_line();
                    }
                    lines.back().pieces.push_back(Piece{"　", false, space});
                    lines.back().width += space;
                }
                if (entry.submit && !submit_only) {
                    const double mark = std::round(size * 1.05);
                    const double space = metrics.measure(" ", size, true);
                    if (lines.back().width + mark + space > available && !lines.back().pieces.empty()) {
                        new_line();
                    } else {
                        lines.back().pieces.push_back(Piece{" ", false, space});
                        lines.back().width += space;
                    }
                    lines.back().pieces.push_back(Piece{"", true, mark});
                    lines.back().width += mark;
                }
            }

            laid.lines = std::move(lines);
            laid.height = static_cast<double>(laid.lines.size()) * line_height;
            cursor += laid.height + 4.0;
            first_row = false;
            result.rows.push_back(std::move(laid));
        }
        cursor -= 4.0;
    }
    result.height = cursor;
    return result;
}

struct AnnouncementLayout {
    double height = 0.0;
    std::vector<std::vector<std::string>> items;
    bool truncated = false;
};

AnnouncementLayout layout_announcements(const std::vector<Announcement>& items,
                                        const TextMetrics& metrics, double size, double width) {
    AnnouncementLayout result;
    const double padding = 20.0;
    const double icon = size * 1.0;
    const double dot = std::max(8.0, size * 0.18);
    const double icon_gap = 20.0;
    const double dot_gap = 14.0;
    const double text_width = width - padding * 2.0 - icon - icon_gap - dot - dot_gap;
    const double line_height = size * 1.34;
    const double item_gap = size * 0.5;

    double cursor = padding;
    for (const Announcement& item : items) {
        const std::vector<std::string> lines =
            wrap_text(metrics, item.text, size, true, std::max(40.0, text_width));
        const double block = static_cast<double>(lines.size()) * line_height;
        cursor += block + item_gap;
        result.items.push_back(lines);
    }
    if (items.empty()) {
        result.height = 0.0;
    } else {
        result.height = cursor - item_gap + padding;
    }
    return result;
}

struct ScheduleRow {
    int period = 0;
    std::string subject;
    bool changed = false;
};

std::vector<ScheduleRow> schedule_rows(const ViewModel& view, int limit) {
    std::vector<ScheduleRow> rows;
    int period = 0;
    for (const Slot& slot : view.periods) {
        period += 1;
        if (slot.name.empty()) {
            continue;
        }
        rows.push_back(ScheduleRow{period, slot.name, slot.changed});
        if (static_cast<int>(rows.size()) >= limit) {
            break;
        }
    }
    return rows;
}

struct SpeechRow {
    std::string subject;
    std::string students;
};

std::vector<SpeechRow> speech_rows(const ViewModel& view) {
    std::vector<SpeechRow> rows;
    for (const Speech& item : view.speeches) {
        if (!item.students.empty()) {
            rows.push_back(SpeechRow{item.subject, item.students});
        }
    }
    return rows;
}

double fit_size(const std::function<double(double)>& height_of, double available, double largest,
                double smallest) {
    for (double size = largest; size >= smallest; size -= 1.0) {
        if (height_of(size) <= available) {
            return size;
        }
    }
    return smallest;
}

std::string date_headline(const std::string& date, const std::string& weekday) {
    const std::optional<Date> parsed = parse_date(date);
    if (!parsed.has_value()) {
        return date;
    }
    const std::string text =
        std::to_string(parsed->month) + "月" + std::to_string(parsed->day) + "日";
    return weekday.empty() ? text : text + " " + weekday;
}

}  // namespace

LayoutResult layout_presentation(const ViewModel& view, const TextMetrics& metrics,
                                 const LayoutOptions& options) {
    LayoutResult result;
    std::vector<DrawCmd>& out = result.commands;
    LayoutReport& report = result.report;
    const LayoutStyle& style = options.style;

    // Background: a soft vertical wash plus two off-centre glows.
    out.push_back(DrawCmd::gradient_rect(0.0, 0.0, kWidth, kHeight, 0.0, kBackgroundTop,
                                         kBackgroundBottom, DrawCmd::Gradient::Vertical));
    out.push_back(DrawCmd::radial_glow(0.0, 0.0, kWidth, kHeight, 0.0, 330.0, 60.0, 780.0, kGlowBlue,
                                       kTransparent));
    out.push_back(DrawCmd::radial_glow(0.0, 0.0, kWidth, kHeight, 0.0, 1660.0, 1010.0, 820.0,
                                       kGlowCyan, kTransparent));

    const Box head{kMargin, kMargin, kContentWidth, kHeadHeight};
    push_shadow(out, head, 30.0);
    out.push_back(DrawCmd::gradient_rect(head.x, head.y, head.w, head.h, 30.0, kHeadFrom, kHeadTo,
                                         DrawCmd::Gradient::Diagonal));
    out.push_back(DrawCmd::outline(head.x, head.y, head.w, head.h, 30.0, with_alpha(0x33, kWhite),
                                   2.0));

    double text_x = head.x + 44.0;
    if (options.show_badge && options.badge_image_id >= 0) {
        const double badge = 92.0;
        out.push_back(DrawCmd::image(head.x + 34.0, head.center_y() - badge / 2.0, badge, badge,
                                     options.badge_image_id));
        text_x = head.x + 34.0 + badge + 24.0;
    }

    const double main_size = style.headline_font;
    const double sub_size = style.headline_sub_font;
    report.headline_font_size = main_size;
    report.panel_title_font_size = style.panel_title_font;
    const double block = main_size + 14.0 + sub_size;
    const double block_top = head.y + (head.h - block) / 2.0;
    out.push_back(DrawCmd::text_cmd(text_x, block_top + main_size / 2.0,
                                    date_headline(view.display_date, view.weekday), main_size, true,
                                    kWhite));
    out.push_back(DrawCmd::text_cmd(text_x, block_top + main_size + 14.0 + sub_size / 2.0,
                                    view.submit_only ? "每日作业 · 仅要交" : "每日作业", sub_size,
                                    false, with_alpha(0xDD, kWhite)));

    const std::string mode_text = view.submit_only ? "仅要交" : "全部";
    const double mode_size = style.mode_pill_font;
    const double mode_w = metrics.measure(mode_text, mode_size, true) + 48.0;
    const Box mode_box{head.right() - 44.0 - mode_w, head.center_y() - 22.0, mode_w, 44.0};
    out.push_back(DrawCmd::rect(mode_box.x, mode_box.y, mode_box.w, mode_box.h, 22.0,
                                with_alpha(0x2E, kWhite)));
    out.push_back(DrawCmd::text_cmd(mode_box.center_x(), mode_box.center_y(), mode_text, mode_size,
                                    true, kWhite, DrawCmd::Align::Center));

    const std::vector<HwGroup> groups = build_groups(view);
    const std::vector<Announcement> announcements = view.announcements;

    // The announcement bar and the homework card share the left column, and both
    // font sizes are picked with the same height function so they shrink together.
    const double left_x = kMargin;
    const double left_bottom = kBottom;
    double homework_size = style.homework_font_max;
    double announcement_size = style.announcement_font_max;
    AnnouncementLayout announcement_layout;
    HomeworkLayout homework_layout;
    double announcement_height = 0.0;
    bool announcement_bar = !announcements.empty();

    // A hand edited style file may declare an absurd range; the cap keeps the
    // candidate scan finite without changing any sane range.
    constexpr std::size_t kMaxCandidates = 32;
    std::vector<double> announcement_sizes;
    for (double size = style.announcement_font_max;
         size > style.announcement_font_min && announcement_sizes.size() < kMaxCandidates;
         size -= 4.0) {
        announcement_sizes.push_back(size);
    }
    announcement_sizes.push_back(style.announcement_font_min);

    const Box homework_card_base{left_x, 0.0, kLeftWidth, 0.0};
    const double homework_label_x =
        homework_card_base.x + style.panel_padding + style.badge_size + 16.0;
    const double homework_text_x = homework_label_x + 10.0;
    const double homework_right = homework_card_base.right() - style.panel_padding;

    bool settled = false;
    for (double candidate : announcement_sizes) {
        AnnouncementLayout candidate_layout;
        double candidate_height = 0.0;
        if (announcement_bar) {
            candidate_layout = layout_announcements(announcements, metrics, candidate, kLeftWidth);
            candidate_height = candidate_layout.height + kGap;
        }
        const double card_top = kBodyTop + candidate_height;
        const double card_height = left_bottom - card_top;
        if (card_height < kPanelHeaderHeight + style.panel_padding + 40.0) {
            continue;
        }
        const double available = card_height - kPanelHeaderHeight - style.panel_padding;
        const double size = fit_size(
            [&](double value) {
                return layout_homework(groups, metrics, value, homework_text_x, homework_right,
                                       view.submit_only, style)
                    .height;
            },
            available, style.homework_font_max, style.homework_font_min);
        const HomeworkLayout laid = layout_homework(groups, metrics, size, homework_text_x,
                                                    homework_right, view.submit_only, style);
        if (laid.height <= available || !announcement_bar) {
            announcement_size = candidate;
            announcement_layout = candidate_layout;
            announcement_height = candidate_height;
            homework_size = size;
            homework_layout = laid;
            settled = true;
            break;
        }
    }
    if (!settled) {
        announcement_size = style.announcement_font_min;
        if (announcement_bar) {
            announcement_layout =
                layout_announcements(announcements, metrics, announcement_size, kLeftWidth);
            announcement_height = announcement_layout.height + kGap;
        }
        homework_size = style.homework_font_min;
        homework_layout = layout_homework(groups, metrics, homework_size, homework_text_x,
                                          homework_right, view.submit_only, style);
    }

    const double homework_top = kBodyTop + announcement_height;
    const double homework_available =
        left_bottom - homework_top - kPanelHeaderHeight - style.panel_padding;

    // Homework may still not fit at the smallest size: drop trailing groups.
    const double limit = std::max(0.0, homework_available);
    if (homework_layout.height > limit && !groups.empty()) {
        std::vector<HwGroup> trimmed = groups;
        while (!trimmed.empty()) {
            trimmed.pop_back();
            const HomeworkLayout candidate = layout_homework(trimmed, metrics, homework_size,
                                                             homework_text_x, homework_right,
                                                             view.submit_only, style);
            if (candidate.height <= limit) {
                homework_layout = candidate;
                report.homework_truncated = true;
                break;
            }
        }
        if (trimmed.empty()) {
            homework_layout = layout_homework({}, metrics, homework_size, homework_text_x,
                                              homework_right, view.submit_only, style);
            report.homework_truncated = true;
        }
    }
    if (homework_layout.height > limit) {
        report.homework_truncated = true;
    }

    // --- announcement bar -------------------------------------------------
    if (announcement_bar) {
        const Box bar{left_x, kBodyTop, kLeftWidth, announcement_layout.height};
        push_shadow(out, bar, 24.0);
        out.push_back(DrawCmd::gradient_rect(bar.x, bar.y, bar.w, bar.h, 24.0, kAmberFrom, kAmberTo,
                                             DrawCmd::Gradient::Vertical));
        out.push_back(DrawCmd::outline(bar.x, bar.y, bar.w, bar.h, 24.0, with_alpha(0x8C, kWhite),
                                       2.0));
        const double icon_size = announcement_size;
        const double icon_x = bar.x + 20.0;
        for (const DrawCmd& cmd :
             icons::draw(icons::Icon::Megaphone, icon_x, bar.y + 20.0, icon_size, kAmberInk)) {
            out.push_back(cmd);
        }
        const double dot = std::max(8.0, announcement_size * 0.18);
        const double dot_gap = 14.0;
        const double text_x_ann = icon_x + icon_size + 20.0 + dot + dot_gap;
        const double line_height = announcement_size * 1.34;
        double cursor = bar.y + 20.0;
        for (const std::vector<std::string>& lines : announcement_layout.items) {
            const double text_block = static_cast<double>(lines.size()) * line_height;
            out.push_back(DrawCmd::rect(icon_x + icon_size + 20.0,
                                        cursor + line_height / 2.0 - dot / 2.0, dot, dot, dot / 2.0,
                                        kAmberInk));
            double line_y = cursor + line_height / 2.0;
            for (const std::string& line : lines) {
                out.push_back(DrawCmd::text_cmd(text_x_ann, line_y, line, announcement_size, true,
                                                kAmberInk));
                line_y += line_height;
            }
            cursor += text_block + announcement_size * 0.5;
        }
        report.announcement_bar = true;
    }

    // --- homework card ----------------------------------------------------
    const Box homework_card{left_x, homework_top, kLeftWidth, left_bottom - homework_top};
    panel(out, homework_card, style);
    const std::string homework_title =
        view.submit_only
            ? "今天要交的作业 · 来自 " + std::string(view.homework_date.size() > 5
                                                        ? view.homework_date.substr(5)
                                                        : view.homework_date)
            : "家庭作业";
    std::string count_text;
    if (!groups.empty()) {
        count_text = std::to_string(groups.size()) + " 科";
    }
    const Box homework_body =
        panel_header(out, homework_card, icons::Icon::Book, homework_title, count_text, metrics,
                     style);

    if (groups.empty()) {
        const std::string empty_text = view.submit_only ? "今天没有要交的作业" : "今天还没有作业";
        const double empty_size = style.empty_state_font;
        const double icon_size = empty_size * 1.15;
        const double text_w = metrics.measure(empty_text, empty_size, false);
        const double total = icon_size + 16.0 + text_w;
        const double start_x = homework_body.center_x() - total / 2.0;
        const double center_y = homework_body.center_y();
        for (const DrawCmd& cmd :
             icons::draw(icons::Icon::Check, start_x, center_y - icon_size / 2.0, icon_size, kGray)) {
            out.push_back(cmd);
        }
        out.push_back(DrawCmd::text_cmd(start_x + icon_size + 16.0, center_y, empty_text, empty_size,
                                        false, kGray));
    } else {
        const double label_x = homework_body.x + style.badge_size + 16.0;
        const double content_x = label_x + homework_layout.label_width + 10.0;
        for (const LaidRow& row : homework_layout.rows) {
            const double line_height = homework_size * 1.4;
            if (row.badge) {
                number_badge(out, homework_body.x, homework_body.y + row.y + line_height / 2.0,
                             row.badge_number, style);
                report.homework_badges += 1;
            }
            out.push_back(DrawCmd::text_cmd(label_x, homework_body.y + row.y + line_height / 2.0,
                                            row.label, homework_size, true, kInk));
            double y = homework_body.y + row.y + line_height / 2.0;
            for (const FlowLine& line : row.lines) {
                double x = content_x;
                for (const Piece& piece : line.pieces) {
                    if (piece.check) {
                        const double mark = std::round(homework_size * 1.05);
                        for (const DrawCmd& cmd :
                             icons::submit_mark(x, y - mark / 2.0, mark, kOrange)) {
                            out.push_back(cmd);
                        }
                        x += piece.width;
                    } else if (!piece.text.empty()) {
                        out.push_back(DrawCmd::text_cmd(x, y, piece.text, homework_size, true, kInk));
                        x += piece.width;
                    }
                }
                y += line_height;
            }
            report.homework_rows += 1;
        }
    }
    report.announcement_font_size = announcement_size;
    report.homework_font_size = homework_size;
    report.badge_size = style.badge_size;
    report.badge_font_size = style.badge_font;
    report.homework_groups = static_cast<int>(groups.size());

    // --- right column: schedule on top, speeches below --------------------
    const double right_x = kMargin + kLeftWidth + kGap;
    const std::vector<ScheduleRow> rows = schedule_rows(view, 9);
    const std::vector<SpeechRow> speeches = speech_rows(view);
    const double right_available = kBottom - kBodyTop;

    const int per_column = std::max(1, (static_cast<int>(rows.size()) + 1) / 2);
    double schedule_pitch = style.schedule_pitch_max;
    double schedule_height = kPanelHeaderHeight + 2.0 * style.panel_padding + schedule_pitch;
    if (!rows.empty()) {
        const double preferred =
            kPanelHeaderHeight + 2.0 * style.panel_padding + per_column * style.schedule_pitch_max;
        schedule_height = std::min(
            preferred,
            std::max(right_available * 0.55,
                     kPanelHeaderHeight + 2.0 * style.panel_padding + per_column * 20.0));
        schedule_pitch = std::clamp(
            (schedule_height - kPanelHeaderHeight - 2.0 * style.panel_padding) / per_column, 20.0,
            style.schedule_pitch_max);
        schedule_height =
            kPanelHeaderHeight + 2.0 * style.panel_padding + schedule_pitch * per_column;
    }
    const Box schedule_card{right_x, kBodyTop, kRightWidth, schedule_height};
    panel(out, schedule_card, style);
    const std::string schedule_title = "今日课表";
    std::string schedule_count;
    if (!rows.empty()) {
        schedule_count = std::to_string(rows.size()) + " 节";
    }
    const Box schedule_body =
        panel_header(out, schedule_card, icons::Icon::Calendar, schedule_title, schedule_count,
                     metrics, style);

    if (rows.empty()) {
        const std::string empty_text = "今日无课";
        const double empty_size = style.empty_state_font;
        const double icon_size = empty_size * 1.15;
        const double text_w = metrics.measure(empty_text, empty_size, false);
        const double total = icon_size + 14.0 + text_w;
        const double start_x = schedule_body.center_x() - total / 2.0;
        const double center_y = schedule_body.center_y();
        for (const DrawCmd& cmd : icons::draw(icons::Icon::Sun, start_x,
                                              center_y - icon_size / 2.0, icon_size, kGray)) {
            out.push_back(cmd);
        }
        out.push_back(DrawCmd::text_cmd(start_x + icon_size + 14.0, center_y, empty_text,
                                        empty_size, false, kGray));
    } else {
        const double row_size =
            std::clamp(std::round(schedule_pitch * style.schedule_row_ratio), style.schedule_font_min,
                       style.schedule_font_max);
        const double chip = std::clamp(schedule_pitch - 6.0, 16.0, style.schedule_chip_max);
        const double column_gap = 24.0;
        const double column_width = (schedule_body.w - column_gap) / 2.0;
        const double block_height = per_column * schedule_pitch;
        const double top_y = schedule_body.y + std::max(0.0, (schedule_body.h - block_height) / 2.0);
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const int column = static_cast<int>(index) / per_column;
            const int row_index = static_cast<int>(index) % per_column;
            const double column_x = schedule_body.x + column * (column_width + column_gap);
            const double center_y = top_y + row_index * schedule_pitch + schedule_pitch / 2.0;
            const ScheduleRow& row = rows[index];
            out.push_back(DrawCmd::rect(column_x, center_y - chip / 2.0, chip, chip, chip * 0.3,
                                        row.changed ? kRed : kBlueSoft));
            out.push_back(DrawCmd::text_cmd(column_x + chip / 2.0, center_y,
                                            std::to_string(row.period), std::round(chip * 0.63), true,
                                            row.changed ? kWhite : kGray,
                                            DrawCmd::Align::Center));
            const double text_start = column_x + chip + 12.0;
            const double text_room = column_x + column_width - text_start;
            const std::string subject =
                metrics.measure(row.subject, row_size, true) <= text_room
                    ? row.subject
                    : ellipsize(metrics, row.subject, row_size, true, text_room);
            out.push_back(DrawCmd::text_cmd(text_start, center_y, subject, row_size, true,
                                            row.changed ? kRed : kInk));
            report.schedule_rows += 1;
        }
        report.schedule_font_size = row_size;
    }

    const double speech_top = schedule_card.bottom() + kGap;
    const double speech_height = std::max(0.0, kBottom - speech_top);
    if (speech_height > kPanelHeaderHeight + style.panel_padding) {
        const Box speech_card{right_x, speech_top, kRightWidth, speech_height};
        panel(out, speech_card, style);
        const Box speech_body =
            panel_header(out, speech_card, icons::Icon::Microphone, "演讲提醒", "", metrics, style);
        if (view.speech_warn || speeches.empty()) {
            const std::string warn_text = "演讲提醒未填写";
            const double warn_size = style.warning_font;
            const double icon_size = warn_size * 1.15;
            const double text_w = metrics.measure(warn_text, warn_size, true);
            const double block_h = std::min(100.0, std::max(56.0, speech_body.h));
            Box warning{speech_body.x, speech_body.y, speech_body.w, block_h};
            out.push_back(DrawCmd::rect(warning.x, warning.y, warning.w, warning.h, 18.0, kWarnFace));
            const double total = icon_size + 12.0 + text_w;
            const double start_x = warning.center_x() - total / 2.0;
            for (const DrawCmd& cmd : icons::draw(icons::Icon::Warning, start_x,
                                                  warning.center_y() - icon_size / 2.0, icon_size,
                                                  kWarnInk)) {
                out.push_back(cmd);
            }
            out.push_back(DrawCmd::text_cmd(start_x + icon_size + 12.0, warning.center_y(), warn_text,
                                            warn_size, true, kWarnInk));
            report.speech_warning = true;
        } else {
            std::vector<double> candidates;
            for (double size = style.speech_font_max;
                 size > style.speech_font_min && candidates.size() < kMaxCandidates;
                 size -= 2.0) {
                candidates.push_back(size);
            }
            candidates.push_back(style.speech_font_min);
            const double tag_width = std::min(kSpeechTagWidth, speech_body.w * 0.45);
            double chosen = style.speech_font_min;
            for (double candidate : candidates) {
                double height = 0.0;
                for (const SpeechRow& row : speeches) {
                    const double line_height = candidate * 1.4;
                    const std::vector<std::string> lines = wrap_text(
                        metrics, row.students, candidate, false, speech_body.w - tag_width);
                    height += static_cast<double>(lines.size()) * line_height + line_height * 0.35;
                }
                if (height <= speech_body.h) {
                    chosen = candidate;
                    break;
                }
            }
            double cursor = speech_body.y;
            const double line_height = chosen * 1.4;
            for (const SpeechRow& row : speeches) {
                const double first_center = cursor + line_height / 2.0;
                if (!row.subject.empty()) {
                    const double tag_size = std::max(20.0, chosen * 0.72);
                    const double tag_w = metrics.measure(row.subject, tag_size, true) + 24.0;
                    out.push_back(DrawCmd::rect(speech_body.x, first_center - tag_size * 0.85,
                                                std::min(tag_w, tag_width - 8.0), tag_size * 1.7,
                                                tag_size * 0.85, kBlueSoft));
                    out.push_back(DrawCmd::text_cmd(speech_body.x + std::min(tag_w, tag_width - 8.0) / 2.0,
                                                    first_center, row.subject, tag_size, true, kBlue,
                                                    DrawCmd::Align::Center));
                }
                const std::vector<std::string> lines =
                    wrap_text(metrics, row.students, chosen, false, speech_body.w - tag_width);
                double y = first_center;
                for (const std::string& line : lines) {
                    out.push_back(DrawCmd::text_cmd(speech_body.x + tag_width, y, line, chosen, false,
                                                    kInk));
                    y += line_height;
                }
                cursor += static_cast<double>(lines.size()) * line_height + line_height * 0.35;
            }
            report.speech_font_size = chosen;
        }
    }

    return result;
}

}  // namespace dhw

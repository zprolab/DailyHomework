#include "dhw/layout.h"
#include "dhw_test.h"

#include <string>
#include <vector>

namespace {

const dhw::StubTextMetrics kMetrics;
const dhw::LayoutStyle kStyle;

dhw::Homework hw(const std::string& subject, const std::string& text, bool submit = false) {
    return dhw::Homework{subject, text, submit};
}

dhw::ViewModel model_with(std::vector<dhw::Homework> homework, std::vector<std::string> suggestions = {},
                          bool submit_only = false, const std::string& date = "2026-10-12") {
    dhw::AppData data = dhw::AppData::defaults();
    if (!homework.empty()) {
        data.day(date).homework = std::move(homework);
    }
    dhw::ViewModel view = dhw::build_view_model(data, date, submit_only);
    if (!suggestions.empty()) {
        view.homework_suggestions = std::move(suggestions);
    }
    return view;
}

// Number badges: white bold digits centred in the gradient capsule. The mode pill
// is white and centred too, so the single digit and the badge font are the tell.
std::vector<const dhw::DrawCmd*> badge_numbers(const std::vector<dhw::DrawCmd>& commands) {
    std::vector<const dhw::DrawCmd*> found;
    for (const dhw::DrawCmd& cmd : commands) {
        const bool digit = cmd.text.size() == 1 && cmd.text[0] >= '0' && cmd.text[0] <= '9';
        if (cmd.kind == dhw::DrawCmd::Kind::Text && digit && cmd.bold &&
            cmd.size == kStyle.badge_font && cmd.color == dhw::layout_theme::kWhite &&
            cmd.align == dhw::DrawCmd::Align::Center) {
            found.push_back(&cmd);
        }
    }
    return found;
}

bool has_text(const std::vector<dhw::DrawCmd>& commands, const std::string& text) {
    for (const dhw::DrawCmd& cmd : commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Text && cmd.text == text) {
            return true;
        }
    }
    return false;
}

int count_submit_rings(const std::vector<dhw::DrawCmd>& commands) {
    int total = 0;
    for (const dhw::DrawCmd& cmd : commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Rect && cmd.has_stroke && cmd.r > 0.0 &&
            cmd.stroke == dhw::layout_theme::kOrange && cmd.fill == 0xFFFFFFFFu) {
            ++total;
        }
    }
    return total;
}

void check_everything_is_inside_the_canvas(const std::vector<dhw::DrawCmd>& commands) {
    for (const dhw::DrawCmd& cmd : commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Text) {
            const double width = kMetrics.measure(cmd.text, cmd.size, cmd.bold, cmd.face_hint);
            double left = cmd.x;
            if (cmd.align == dhw::DrawCmd::Align::Center) {
                left = cmd.x - width / 2.0;
            } else if (cmd.align == dhw::DrawCmd::Align::Right) {
                left = cmd.x - width;
            }
            DHW_CHECK(left >= -0.5);
            DHW_CHECK(left + width <= dhw::layout_geometry::kWidth + 0.5);
            DHW_CHECK(cmd.y - cmd.size / 2.0 >= -0.5);
            DHW_CHECK(cmd.y + cmd.size / 2.0 <= dhw::layout_geometry::kHeight + 0.5);
            continue;
        }
        if (cmd.kind == dhw::DrawCmd::Kind::Path) {
            for (const auto& point : cmd.points) {
                DHW_CHECK(point.first >= -0.5);
                DHW_CHECK(point.first <= dhw::layout_geometry::kWidth + 0.5);
                DHW_CHECK(point.second >= -0.5);
                DHW_CHECK(point.second <= dhw::layout_geometry::kHeight + 0.5);
            }
            continue;
        }
        DHW_CHECK(cmd.x >= -9.0);
        DHW_CHECK(cmd.x + cmd.w <= dhw::layout_geometry::kWidth + 9.0);
        DHW_CHECK(cmd.y >= -9.0);
        DHW_CHECK(cmd.y + cmd.h <= dhw::layout_geometry::kHeight + 9.0);
    }
}

}  // namespace

DHW_TEST(layout_always_stays_inside_the_canvas) {
    const dhw::ViewModel plain = model_with({hw("语文", "抄写第 3 课")});
    DHW_CHECK(dhw::layout_presentation(plain, kMetrics).commands.size() > 20);

    dhw::ViewModel busy = model_with(
        {hw("语文", "抄写第 3 课并背诵全文", true), hw("语文", "写作文 800 字"),
         hw("数学", "练习册 P12 第 3 到第 9 题", true), hw("英语A", "背诵 Unit 4 单词"),
         hw("英语B", "朗读课文三遍"), hw("物理", "实验报告"), hw("历史", "整理时间轴", true)},
        {"语文", "数学", "英语A", "英语B", "物理", "历史"});
    busy.announcements.push_back(dhw::Announcement{1, "明天带手工材料，别忘了带剪刀和胶水", false});
    busy.announcements.push_back(dhw::Announcement{2, "周五穿校服", false});
    check_everything_is_inside_the_canvas(dhw::layout_presentation(busy, kMetrics).commands);

    dhw::ViewModel submit = model_with({hw("数学", "卷子签意见", true)}, {"数学"}, true);
    check_everything_is_inside_the_canvas(dhw::layout_presentation(submit, kMetrics).commands);
}

DHW_TEST(layout_is_deterministic) {
    const dhw::ViewModel view = model_with({hw("语文", "抄写", true)}, {"语文"});
    const dhw::LayoutResult first = dhw::layout_presentation(view, kMetrics);
    const dhw::LayoutResult second = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK_EQ(first.commands.size(), second.commands.size());
    for (std::size_t i = 0; i < first.commands.size(); ++i) {
        const dhw::DrawCmd& a = first.commands[i];
        const dhw::DrawCmd& b = second.commands[i];
        DHW_CHECK(a.kind == b.kind);
        DHW_CHECK_EQ(a.x, b.x);
        DHW_CHECK_EQ(a.y, b.y);
        DHW_CHECK_EQ(a.text, b.text);
        DHW_CHECK_EQ(a.fill, b.fill);
    }
}

DHW_TEST(layout_announcement_bar_only_when_needed) {
    const dhw::ViewModel without = model_with({hw("语文", "抄写")});
    const dhw::LayoutResult plain = dhw::layout_presentation(without, kMetrics);
    DHW_CHECK(!plain.report.announcement_bar);

    dhw::ViewModel with = model_with({hw("语文", "抄写")});
    with.announcements.push_back(dhw::Announcement{1, "明天带跳绳", false});
    const dhw::LayoutResult bar = dhw::layout_presentation(with, kMetrics);
    DHW_CHECK(bar.report.announcement_bar);
    DHW_CHECK(has_text(bar.commands, "明天带跳绳"));
    DHW_CHECK_EQ(bar.report.announcement_font_size, 40.0);
}

DHW_TEST(layout_numbers_groups_and_merges_english) {
    const dhw::ViewModel view = model_with(
        {hw("数学", "练习册", true), hw("语文", "抄写"), hw("语文", "背诵"), hw("英语A", "背单词"),
         hw("英语B", "读课文")},
        {"语文", "数学", "英语A", "英语B"});
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK_EQ(result.report.homework_groups, 3);
    DHW_CHECK_EQ(result.report.homework_badges, 3);
    DHW_CHECK_EQ(result.report.homework_rows, 4);

    const std::vector<const dhw::DrawCmd*> badges = badge_numbers(result.commands);
    DHW_CHECK_EQ(badges.size(), std::size_t{3});
    DHW_CHECK_EQ(badges[0]->text, std::string("1"));
    DHW_CHECK_EQ(badges[1]->text, std::string("2"));
    DHW_CHECK_EQ(badges[2]->text, std::string("3"));
}

DHW_TEST(layout_shows_index_only_for_multiple_entries) {
    const dhw::ViewModel single = model_with({hw("语文", "抄写")});
    const dhw::LayoutResult one = dhw::layout_presentation(single, kMetrics);
    DHW_CHECK(has_text(one.commands, "抄写"));
    DHW_CHECK(!has_text(one.commands, "1 抄写"));

    const dhw::ViewModel multiple = model_with({hw("语文", "抄写"), hw("语文", "背诵")});
    const dhw::LayoutResult two = dhw::layout_presentation(multiple, kMetrics);
    DHW_CHECK(has_text(two.commands, "1 抄写"));
    DHW_CHECK(has_text(two.commands, "2 背诵"));
}

DHW_TEST(layout_submit_mark_follows_the_rules) {
    const dhw::ViewModel view = model_with({hw("数学", "卷子签意见", true), hw("语文", "抄写")});
    const dhw::LayoutResult full = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK_EQ(count_submit_rings(full.commands), 1);

    dhw::ViewModel only = view;
    only.submit_only = true;
    only.homework = {hw("数学", "卷子签意见", true)};
    const dhw::LayoutResult submit_mode = dhw::layout_presentation(only, kMetrics);
    DHW_CHECK_EQ(count_submit_rings(submit_mode.commands), 0);
    DHW_CHECK(has_text(submit_mode.commands, "今天要交的作业 · 来自 10-12"));
}

DHW_TEST(layout_empty_states) {
    // A weekend date without an entry has an empty template and no homework.
    const dhw::ViewModel empty = model_with({}, {}, false, "2026-10-17");
    const dhw::LayoutResult result = dhw::layout_presentation(empty, kMetrics);
    DHW_CHECK(has_text(result.commands, "今天还没有作业"));
    DHW_CHECK(has_text(result.commands, "今日无课"));
    DHW_CHECK(has_text(result.commands, "演讲提醒未填写"));
    DHW_CHECK(result.report.speech_warning);
    DHW_CHECK_EQ(result.report.homework_groups, 0);
    DHW_CHECK(!result.report.homework_truncated);
}

DHW_TEST(layout_shrinks_announcements_and_homework_together) {
    dhw::ViewModel view = model_with({});
    for (int i = 0; i < 18; ++i) {
        view.homework.push_back(hw("学科" + std::to_string(i),
                                   "这是一条很长的作业内容，用来测试自适应字号与换行行为是否稳定可靠", true));
    }
    for (int i = 0; i < 6; ++i) {
        view.announcements.push_back(
            dhw::Announcement{i + 1, "公告内容比较长，需要多行换行显示，才能把公告条撑高", false});
    }
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK(result.report.announcement_bar);
    DHW_CHECK(result.report.announcement_font_size < 40.0);
    DHW_CHECK(result.report.homework_font_size < 36.0);
    DHW_CHECK(result.report.homework_font_size >= 26.0);
    DHW_CHECK(result.report.announcement_font_size >= 26.0);
    check_everything_is_inside_the_canvas(result.commands);
}

DHW_TEST(layout_truncates_overflowing_homework) {
    dhw::ViewModel view = model_with({});
    for (int i = 0; i < 80; ++i) {
        view.homework.push_back(hw("学科" + std::to_string(i), "作业内容占位", false));
    }
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK(result.report.homework_truncated);
    DHW_CHECK_EQ(result.report.homework_font_size, 26.0);
    check_everything_is_inside_the_canvas(result.commands);
}

DHW_TEST(layout_renders_schedule_in_two_columns) {
    dhw::AppData data = dhw::AppData::defaults();
    data.settings.english_split = false;
    dhw::Day& day = data.day("2026-10-12");
    day.homework.push_back(hw("语文", "抄写"));
    const dhw::ViewModel view = dhw::build_view_model(data, "2026-10-12", false);
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);

    DHW_CHECK_EQ(result.report.schedule_rows, 9);
    DHW_CHECK(result.report.schedule_font_size >= 22.0);
    DHW_CHECK(result.report.schedule_font_size <= 32.0);

    std::vector<double> chip_x;
    const double right_column_x =
        dhw::layout_geometry::kMargin + dhw::layout_geometry::kLeftWidth + dhw::layout_geometry::kGap;
    for (const dhw::DrawCmd& cmd : result.commands) {
        const bool single_digit = cmd.text.size() == 1 && cmd.text[0] >= '1' && cmd.text[0] <= '9';
        if (cmd.kind == dhw::DrawCmd::Kind::Text && single_digit && cmd.size < 25.0 &&
            cmd.align == dhw::DrawCmd::Align::Center && cmd.x > right_column_x) {
            chip_x.push_back(cmd.x);
        }
    }
    DHW_CHECK_EQ(chip_x.size(), std::size_t{9});
    const double left_column = chip_x.front();
    int left_count = 0;
    int right_count = 0;
    for (double x : chip_x) {
        if (std::abs(x - left_column) < 1.0) {
            ++left_count;
        } else {
            ++right_count;
        }
    }
    DHW_CHECK_EQ(left_count, 5);
    DHW_CHECK_EQ(right_count, 4);
}

DHW_TEST(layout_marks_swapped_periods) {
    dhw::AppData data = dhw::AppData::defaults();
    dhw::Day& day = data.day("2026-10-12");
    day.schedule_override[0] = std::string("体育");
    const dhw::ViewModel view = dhw::build_view_model(data, "2026-10-12", false);
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);

    bool red_chip = false;
    bool red_text = false;
    for (const dhw::DrawCmd& cmd : result.commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Rect && cmd.fill == dhw::layout_theme::kRed) {
            red_chip = true;
        }
        if (cmd.kind == dhw::DrawCmd::Kind::Text && cmd.text == "体育" &&
            cmd.color == dhw::layout_theme::kRed) {
            red_text = true;
        }
    }
    DHW_CHECK(red_chip);
    DHW_CHECK(red_text);
}

DHW_TEST(layout_renders_speeches) {
    dhw::AppData data = dhw::AppData::defaults();
    dhw::Day& day = data.day("2026-10-12");
    day.homework.push_back(hw("语文", "抄写"));
    day.speeches.push_back(dhw::Speech{"英语", "1号 张三；2号 李四；3号 王五"});
    const dhw::ViewModel view = dhw::build_view_model(data, "2026-10-12", false);
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);

    DHW_CHECK(!result.report.speech_warning);
    DHW_CHECK(result.report.speech_font_size >= 24.0);
    DHW_CHECK(result.report.speech_font_size <= 32.0);
    bool tag = false;
    bool body = false;
    for (const dhw::DrawCmd& cmd : result.commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Text && cmd.text == "英语" && cmd.color == dhw::layout_theme::kBlue) {
            tag = true;
        }
        if (cmd.kind == dhw::DrawCmd::Kind::Text && cmd.text.find("1号") != std::string::npos) {
            body = true;
            DHW_CHECK(std::abs(cmd.x - (dhw::layout_geometry::kMargin + dhw::layout_geometry::kLeftWidth +
                                        dhw::layout_geometry::kGap + kStyle.panel_padding +
                                        dhw::layout_geometry::kSpeechTagWidth)) < 0.5);
        }
    }
    DHW_CHECK(tag);
    DHW_CHECK(body);
}

DHW_TEST(layout_draws_badge_when_available) {
    const dhw::ViewModel view = model_with({hw("语文", "抄写")});
    dhw::LayoutOptions options;
    options.show_badge = true;
    options.badge_image_id = 7;
    const dhw::LayoutResult with = dhw::layout_presentation(view, kMetrics, options);
    int images = 0;
    for (const dhw::DrawCmd& cmd : with.commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Image) {
            ++images;
            DHW_CHECK_EQ(cmd.image_id, 7);
        }
    }
    DHW_CHECK_EQ(images, 1);
    const dhw::LayoutResult without = dhw::layout_presentation(view, kMetrics);
    for (const dhw::DrawCmd& cmd : without.commands) {
        DHW_CHECK(cmd.kind != dhw::DrawCmd::Kind::Image);
    }
}

DHW_TEST(layout_headline_shows_date_and_weekday) {
    const dhw::ViewModel view = model_with({hw("语文", "抄写")});
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics);
    DHW_CHECK(has_text(result.commands, "10月12日 星期一"));
    DHW_CHECK(has_text(result.commands, "每日作业"));
    DHW_CHECK(has_text(result.commands, "全部"));
}

DHW_TEST(layout_type_hierarchy_is_never_inverted) {
    // Regression guard for the "title smaller than body text" bug: the headline is
    // the largest, then the card titles, then the announcement, the homework body
    // and finally the schedule / speech bodies.
    dhw::ViewModel view = model_with(
        {hw("语文", "抄写第 3 课并背诵", true), hw("数学", "练习册 P12 第 3 到第 9 题"),
         hw("英语A", "背诵 Unit 4"), hw("英语B", "朗读课文"), hw("物理", "实验报告")},
        {"语文", "数学", "英语A", "英语B", "物理"});
    view.speeches.push_back(dhw::Speech{"英语", "1号 张三；2号 李四"});
    view.speech_warn = false;
    view.announcements.push_back(dhw::Announcement{1, "明天带手工材料", false});

    dhw::AppData data = dhw::AppData::defaults();
    dhw::Day& day = data.day("2026-10-12");
    day.homework = view.homework;
    day.speeches = view.speeches;
    data.announcements = view.announcements;
    const dhw::ViewModel real = dhw::build_view_model(data, "2026-10-12", false);
    const dhw::LayoutResult result = dhw::layout_presentation(real, kMetrics);

    const dhw::LayoutReport& report = result.report;
    DHW_CHECK(report.headline_font_size >= report.panel_title_font_size);
    DHW_CHECK(report.panel_title_font_size > report.announcement_font_size);
    DHW_CHECK(report.announcement_font_size >= report.homework_font_size);
    DHW_CHECK(report.homework_font_size > report.schedule_font_size);
    DHW_CHECK(report.homework_font_size > report.speech_font_size);
    DHW_CHECK(report.panel_title_font_size > report.schedule_font_size);
    DHW_CHECK(report.panel_title_font_size > report.speech_font_size);
    DHW_CHECK(report.headline_font_size >= report.announcement_font_size);
    DHW_CHECK_EQ(report.panel_title_font_size, 42.0);
}

DHW_TEST(layout_honors_a_custom_style) {
    dhw::ViewModel view = model_with({hw("语文", "抄写第 3 课"), hw("数学", "练习册 P12")},
                                     {"语文", "数学"});
    view.announcements.push_back(dhw::Announcement{1, "明天带跳绳", false});

    dhw::LayoutOptions options;
    options.style.badge_size = 30.0;
    options.style.badge_radius = 8.0;
    options.style.badge_font = 18.0;
    options.style.announcement_font_max = 28.0;
    options.style.announcement_font_min = 24.0;
    const dhw::LayoutResult result = dhw::layout_presentation(view, kMetrics, options);

    DHW_CHECK_EQ(result.report.badge_size, 30.0);
    DHW_CHECK_EQ(result.report.badge_font_size, 18.0);
    DHW_CHECK_EQ(result.report.announcement_font_size, 28.0);

    int badges = 0;
    int numbers = 0;
    int announcement_lines = 0;
    for (const dhw::DrawCmd& cmd : result.commands) {
        if (cmd.kind == dhw::DrawCmd::Kind::Rect &&
            cmd.gradient == dhw::DrawCmd::Gradient::Vertical && cmd.fill == 0xFF5C94DEu) {
            ++badges;
            DHW_CHECK_EQ(cmd.w, 30.0);
            DHW_CHECK_EQ(cmd.h, 30.0);
            DHW_CHECK_EQ(cmd.r, 8.0);
        }
        if (cmd.kind == dhw::DrawCmd::Kind::Text && cmd.bold && cmd.size == 18.0 &&
            cmd.color == dhw::layout_theme::kWhite) {
            ++numbers;
        }
        if (cmd.kind == dhw::DrawCmd::Kind::Text &&
            cmd.color == dhw::layout_theme::kAmberInk) {
            ++announcement_lines;
            DHW_CHECK_EQ(cmd.size, 28.0);
        }
    }
    DHW_CHECK_EQ(badges, 2);
    DHW_CHECK_EQ(numbers, 2);
    DHW_CHECK_EQ(announcement_lines, 1);
    check_everything_is_inside_the_canvas(result.commands);
}

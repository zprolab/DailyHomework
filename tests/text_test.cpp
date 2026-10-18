#include "dhw/text.h"
#include "dhw/utf8.h"
#include "dhw_test.h"

#include <string>
#include <vector>

DHW_TEST(stub_metrics_are_deterministic) {
    const dhw::StubTextMetrics metrics;
    DHW_CHECK_EQ(metrics.measure("abc", 10.0, false), 15.0);
    DHW_CHECK_EQ(metrics.measure("语文", 10.0, false), 20.0);
    DHW_CHECK_EQ(metrics.measure("", 10.0, false), 0.0);
    DHW_CHECK(metrics.measure("abc", 10.0, true) > metrics.measure("abc", 10.0, false));
    DHW_CHECK_EQ(metrics.line_metrics(10.0, false).baseline, 10.0 * 0.92);
}

DHW_TEST(wrap_text_breaks_between_cjk_code_points) {
    const dhw::StubTextMetrics metrics;
    const std::vector<std::string> lines = dhw::wrap_text(metrics, "语文数学英语", 10.0, false, 30.0);
    DHW_CHECK_EQ(lines.size(), std::size_t{2});
    DHW_CHECK_EQ(lines[0], std::string("语文数"));
    DHW_CHECK_EQ(lines[1], std::string("学英语"));
}

DHW_TEST(wrap_text_never_splits_code_points) {
    const dhw::StubTextMetrics metrics;
    const std::vector<std::string> lines = dhw::wrap_text(metrics, "语文", 10.0, false, 5.0);
    DHW_CHECK_EQ(lines.size(), std::size_t{2});
    DHW_CHECK_EQ(lines[0], std::string("语"));
    DHW_CHECK_EQ(lines[1], std::string("文"));
    for (const std::string& line : lines) {
        DHW_CHECK(dhw::utf8::is_valid(line));
    }
}

DHW_TEST(wrap_text_respects_explicit_newlines) {
    const dhw::StubTextMetrics metrics;
    const std::vector<std::string> lines = dhw::wrap_text(metrics, "语文\n数学\n", 10.0, false, 100.0);
    DHW_CHECK_EQ(lines.size(), std::size_t{3});
    DHW_CHECK_EQ(lines[0], std::string("语文"));
    DHW_CHECK_EQ(lines[1], std::string("数学"));
    DHW_CHECK_EQ(lines[2], std::string(""));
}

DHW_TEST(wrap_text_edge_cases) {
    const dhw::StubTextMetrics metrics;
    DHW_CHECK_EQ(dhw::wrap_text(metrics, "", 10.0, false, 50.0).size(), std::size_t{1});
    DHW_CHECK_EQ(dhw::wrap_text(metrics, "", 10.0, false, 50.0)[0], std::string(""));
    const std::vector<std::string> no_limit = dhw::wrap_text(metrics, "语文数学", 10.0, false, 0.0);
    DHW_CHECK_EQ(no_limit.size(), std::size_t{1});
    DHW_CHECK_EQ(no_limit[0], std::string("语文数学"));
    const std::vector<std::string> wide = dhw::wrap_text(metrics, "语文数学", 10.0, false, 19.0);
    DHW_CHECK_EQ(wide.size(), std::size_t{4});
}

DHW_TEST(ellipsize_truncates_to_fit) {
    const dhw::StubTextMetrics metrics;
    DHW_CHECK_EQ(dhw::ellipsize(metrics, "语文", 10.0, false, 100.0), std::string("语文"));
    const std::string short_text = dhw::ellipsize(metrics, "语文数学英语", 10.0, false, 25.0);
    DHW_CHECK(short_text.size() < std::string("语文数学英语").size());
    DHW_CHECK(short_text.back() != ' ');
    DHW_CHECK(short_text.find("…") != std::string::npos);
    DHW_CHECK(metrics.measure(short_text, 10.0, false) <= 25.0);
    const std::string single = dhw::ellipsize(metrics, "数学", 10.0, false, 1.0);
    DHW_CHECK_EQ(single, std::string("…"));
    DHW_CHECK_EQ(dhw::ellipsize(metrics, "", 10.0, false, 5.0), std::string(""));
}

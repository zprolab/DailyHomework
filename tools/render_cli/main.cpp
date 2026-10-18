// Headless renderer: turns a data.json into the exact 1920x1080 presentation PNG.
// The desktop shell uses the same layout engine and the same renderer, so what the
// classroom screen shows is what this tool writes.

#include <QGuiApplication>
#include <QImage>
#include <QString>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "dhw/datetime.h"
#include "dhw/fs.h"
#include "dhw/json.h"
#include "dhw/layout.h"
#include "dhw/model.h"
#include "dhw/qt/renderer.h"
#include "dhw/schedule.h"
#include "dhw/style.h"

namespace {

struct Options {
    std::string data_file;
    std::string out_file = "presentation.png";
    std::string date;
    std::string badge_file;
    std::string style_file;
    std::string dump_style_file;
    bool submit_only = false;
    bool list_only = false;
    double scale = 1.0;
};

void usage() {
    std::printf(
        "dhw_render_cli --data <data.json> [--out out.png] [--date YYYY-MM-DD]\n"
        "               [--submit-only] [--badge 班徽.png] [--scale 2] [--list]\n"
        "               [--style style.json] [--dump-style style.json]\n");
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&](std::string& target) {
            if (i + 1 >= argc) {
                return false;
            }
            target = argv[++i];
            return true;
        };
        if (flag == "--data") {
            if (!value(options.data_file)) {
                return false;
            }
        } else if (flag == "--out") {
            if (!value(options.out_file)) {
                return false;
            }
        } else if (flag == "--date") {
            if (!value(options.date)) {
                return false;
            }
        } else if (flag == "--badge") {
            if (!value(options.badge_file)) {
                return false;
            }
        } else if (flag == "--style") {
            if (!value(options.style_file)) {
                return false;
            }
        } else if (flag == "--dump-style") {
            if (!value(options.dump_style_file)) {
                return false;
            }
        } else if (flag == "--scale") {
            std::string text;
            if (!value(text)) {
                return false;
            }
            options.scale = std::strtod(text.c_str(), nullptr);
        } else if (flag == "--submit-only") {
            options.submit_only = true;
        } else if (flag == "--list") {
            options.list_only = true;
        } else if (flag == "--help" || flag == "-h") {
            usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", flag.c_str());
            return false;
        }
    }
    return !options.data_file.empty() || !options.dump_style_file.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage();
        return 2;
    }

    dhw::LayoutStyle style;
    if (!options.style_file.empty()) {
        const dhw::StyleLoad loaded = dhw::load_style_file(options.style_file);
        if (!loaded.error.empty()) {
            std::fprintf(stderr, "%s: %s\n", options.style_file.c_str(), loaded.error.c_str());
        }
        style = loaded.style;
    }
    if (!options.dump_style_file.empty()) {
        if (!dhw::save_style_file(options.dump_style_file, style)) {
            std::fprintf(stderr, "cannot write %s\n", options.dump_style_file.c_str());
            return 1;
        }
        std::printf("style=%s\n", options.dump_style_file.c_str());
        return 0;
    }

    const std::optional<std::string> text = dhw::fs::read_text(options.data_file);
    if (!text.has_value()) {
        std::fprintf(stderr, "cannot read %s\n", options.data_file.c_str());
        return 1;
    }
    const dhw::JsonParse parsed = dhw::Json::parse(*text);
    if (!parsed.ok) {
        std::fprintf(stderr, "%s: %s\n", options.data_file.c_str(), parsed.error.c_str());
        return 1;
    }
    const dhw::DataParse loaded = dhw::parse_app_data(parsed.value);
    if (!loaded.ok) {
        std::fprintf(stderr, "%s: %s\n", options.data_file.c_str(), loaded.error.c_str());
        return 1;
    }

    dhw::AppData data = loaded.data;
    const dhw::Date today = dhw::today_local();
    data.normalize(today);

    std::string date = options.date;
    if (date.empty() || !dhw::parse_date(date).has_value()) {
        date = loaded.data.days.empty() ? dhw::to_string(today) : loaded.data.days.rbegin()->first;
    }

    QGuiApplication app(argc, argv);
    Q_UNUSED(app);

    const dhw::ViewModel view = dhw::build_view_model(data, date, options.submit_only);
    const dhw::qt::QtTextMetrics metrics;

    std::vector<QImage> images;
    dhw::LayoutOptions layout_options;
    layout_options.style = style;
    if (!options.badge_file.empty()) {
        QImage badge(QString::fromUtf8(options.badge_file.c_str()));
        if (badge.isNull()) {
            std::fprintf(stderr, "cannot read badge %s\n", options.badge_file.c_str());
            return 1;
        }
        images.push_back(badge);
        layout_options.badge_image_id = 0;
        layout_options.show_badge = true;
    }

    const dhw::LayoutResult result = dhw::layout_presentation(view, metrics, layout_options);
    const QImage image = dhw::qt::render_commands(result.commands, 1920, 1080, images, metrics,
                                                  options.scale <= 0.0 ? 1.0 : options.scale);
    if (!image.save(QString::fromUtf8(options.out_file.c_str()), "PNG")) {
        std::fprintf(stderr, "cannot write %s\n", options.out_file.c_str());
        return 1;
    }

    std::printf("date=%s display=%s submit_only=%d commands=%zu out=%s (%dx%d)\n", date.c_str(),
                view.display_date.c_str(), view.submit_only ? 1 : 0, result.commands.size(),
                options.out_file.c_str(), image.width(), image.height());
    if (options.list_only) {
        for (const dhw::DrawCmd& cmd : result.commands) {
            if (cmd.kind == dhw::DrawCmd::Kind::Text) {
                std::printf("text %.0f,%.0f size=%.0f %s\n", cmd.x, cmd.y, cmd.size, cmd.text.c_str());
            }
        }
    }
    return 0;
}

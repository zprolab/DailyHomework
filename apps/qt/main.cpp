#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QLayout>
#include <QStringList>
#include <QTimer>
#include <cstdio>
#include <cstring>
#include <string>

#include "app_controller.h"
#include "dhw/fs.h"
#include "dhw/qt/renderer.h"
#include "fluent_style.h"
#include "main_window.h"
#include "settings_page.h"
#include "theme.h"
#include "week_page.h"

namespace {

struct Options {
    std::string data_dir;
    std::string render_path;
    std::string screenshot_path;
    std::string date;
    std::string window_size;
    std::string page;
    std::string set_cutoff;
    std::string set_scale;
    std::string widget_style;
    bool list_styles = false;
    bool submit_only = false;
    bool smoke_edit = false;
    bool self_check = false;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&](std::string& target) {
            if (i + 1 < argc) {
                target = argv[++i];
            }
        };
        if (flag == "--render") {
            value(options.render_path);
        } else if (flag == "--screenshot") {
            value(options.screenshot_path);
        } else if (flag == "--date") {
            value(options.date);
        } else if (flag == "--size") {
            value(options.window_size);
        } else if (flag == "--data-dir") {
            value(options.data_dir);
        } else if (flag == "--set-cutoff") {
            value(options.set_cutoff);
        } else if (flag == "--set-scale") {
            value(options.set_scale);
        } else if (flag == "--widget-style") {
            // Overrides the platform style chain for one run (see theme.h); only names
            // QStyleFactory really advertises are accepted.
            value(options.widget_style);
        } else if (flag == "--list-styles") {
            options.list_styles = true;
        } else if (flag == "--page") {
            // Only changes the page the shell starts on (edit | week | settings | credits).
            value(options.page);
        } else if (flag == "--submit-only") {
            options.submit_only = true;
        } else if (flag == "--smoke-edit") {
            options.smoke_edit = true;
        } else if (flag == "--selfcheck") {
            options.self_check = true;
        } else if (flag.rfind("--", 0) != 0) {
            options.data_dir = flag;
        }
    }
    if (options.data_dir.empty()) {
        // Keeps using the pre-rewrite data directory when it holds data, so an
        // existing installation never starts from an empty data set.
        options.data_dir = dhw::resolve_data_dir();
    }
    return options;
}

QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const Options options = parse_options(argc, argv);

    if (options.list_styles) {
        // Read only debug output: what this build can offer and what it picks.
        const QString selected = dhw::qt::apply_platform_style(to_qstring(options.widget_style));
        const QStringList keys = dhw::qt::available_style_keys();
        std::printf("available styles:");
        for (const QString& key : keys) {
            std::printf(" %s", key.toUtf8().constData());
        }
        std::printf("\nplatform chain picks: %s\n",
                    dhw::qt::preferred_style_key().toUtf8().constData());
        std::printf("selected style: %s\n", selected.toUtf8().constData());
        // Read only palette probe: what the platform style hands the shell.
        const QPalette palette = app.palette();
        const auto role = [&palette](QPalette::ColorRole value) {
            return palette.color(value).name(QColor::HexRgb).toStdString();
        };
        std::printf("palette: window=%s base=%s text=%s windowText=%s highlight=%s "
                    "highlightedText=%s placeholder=%s mid=%s link=%s\n",
                    role(QPalette::Window).c_str(), role(QPalette::Base).c_str(),
                    role(QPalette::Text).c_str(), role(QPalette::WindowText).c_str(),
                    role(QPalette::Highlight).c_str(), role(QPalette::HighlightedText).c_str(),
                    role(QPalette::PlaceholderText).c_str(), role(QPalette::Mid).c_str(),
                    role(QPalette::Link).c_str());
        return 0;
    }

    const QString data_dir = to_qstring(options.data_dir);
    if (!dhw::fs::is_directory(options.data_dir)) {
        dhw::fs::create_directories(options.data_dir);
    }

    dhw::qt::AppController controller(data_dir);
    // The widget style is a platform decision and never changes at run time; the colour
    // scheme and the display size come from the user settings.
    dhw::qt::apply_platform_style(to_qstring(options.widget_style));
    // The vendored Fluent resources, fonts and design tokens must exist before the first
    // Fluent widget is constructed (the shell builds its sidebar before the pages). The
    // call is idempotent and compiled out in the native build.
    dhw::qt::ensure_fluent_ready();
    dhw::qt::apply_color_scheme(to_qstring(controller.local_settings().appearance));
    dhw::qt::apply_ui_scale(controller.data().settings.ui_scale);
    if (!options.date.empty()) {
        controller.set_current_date(to_qstring(options.date));
    }
    if (options.submit_only) {
        controller.set_submit_only(true);
    }

    if (!options.render_path.empty()) {
        const QImage image = controller.render_current_png();
        if (!image.save(to_qstring(options.render_path), "PNG")) {
            std::fprintf(stderr, "cannot write %s\n", options.render_path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", options.render_path.c_str());
        return 0;
    }

    dhw::qt::MainWindow window(&controller);
    QSize forced;
    if (!options.window_size.empty()) {
        const QString size = to_qstring(options.window_size);
        const QStringList parts = size.split(QLatin1Char('x'));
        if (parts.size() == 2) {
            forced = QSize(parts.at(0).toInt(), parts.at(1).toInt());
            window.resize(forced);
        }
    } else {
        window.resize(1440, 900);
    }
    window.show();
    controller.start_timers();

    if (!options.page.empty()) {
        // Startup page switch used by the screenshot harness; it does not touch any data.
        // "<page>:bottom" additionally scrolls the page body to its end.
        QString page = to_qstring(options.page);
        const bool scroll_bottom = page.endsWith(QStringLiteral(":bottom"));
        const bool edit_template = page.endsWith(QStringLiteral(":edit"));
        if (scroll_bottom || edit_template) {
            page.chop(scroll_bottom ? 7 : 5);
        }
        const auto scroll_page = [&window, page]() {
            QScrollArea* target = window.findChild<QScrollArea*>(page + QStringLiteral("Scroll"));
            if (target == nullptr) {
                target = window.findChild<QScrollArea*>();
            }
            if (target != nullptr) {
                target->verticalScrollBar()->setValue(target->verticalScrollBar()->maximum());
            }
        };
        QTimer::singleShot(0, &window, [&window, page, scroll_bottom, edit_template,
                                        scroll_page]() {
            window.show_page(page);
            if (edit_template && window.week_page() != nullptr) {
                window.week_page()->begin_template_edit();
            }
            if (scroll_bottom || edit_template) {
                scroll_page();
                QTimer::singleShot(60, scroll_page);
            }
        });
        if (scroll_bottom) {
            // The page body is rebuilt by the controller signal, so scroll again once it
            // has settled; this only affects the screenshot harness.
            QTimer::singleShot(150, &window, scroll_page);
        }
    }

    if (!options.set_cutoff.empty() || !options.set_scale.empty()) {
        // Headless write-through probe for the settings page: it runs the same edits the
        // widgets perform and saves data.json at once.
        if (!options.set_cutoff.empty()) {
            window.settings_page()->set_morning_cutoff(to_qstring(options.set_cutoff));
        }
        if (!options.set_scale.empty()) {
            window.settings_page()->set_ui_scale(to_qstring(options.set_scale).toInt());
        }
        controller.save_now();
        std::printf("saved morning_cutoff=%s ui_scale=%.2f\n",
                    controller.data().settings.morning_cutoff.c_str(),
                    controller.data().settings.ui_scale);
        return 0;
    }

    if (options.self_check) {
        const int failures = window.run_selfcheck();
        std::printf("selfcheck failures=%d\n", failures);
        return failures == 0 ? 0 : 1;
    }

    if (options.smoke_edit) {
        const bool added = window.smoke_edit(QStringLiteral("数学"), QStringLiteral("冒烟测试：练习册 3-1"), true);
        std::printf("smoke-edit %s\n", added ? "added" : "already present");
        return 0;
    }

    if (!options.screenshot_path.empty()) {
        const QString path = to_qstring(options.screenshot_path);
        QTimer::singleShot(10, &window, [&window, forced]() {
            if (forced.width() <= 0 || forced.height() <= 0) {
                return;
            }
            window.layout()->setSizeConstraint(QLayout::SetNoConstraint);
            // The Fluent shell's central widget (NavigationView) positions its pane and
            // content itself and has no layout to relax.
            if (QWidget* central = window.centralWidget();
                central != nullptr && central->layout() != nullptr) {
                central->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            }
            window.setMinimumSize(0, 0);
            window.resize(forced);
        });
        QTimer::singleShot(300, &window, [&window, path]() {
            const bool ok = window.grab().save(path, "PNG");
            std::printf("%s %s\n", ok ? "wrote" : "cannot write", path.toUtf8().constData());
            QApplication::quit();
        });
    }

    return app.exec();
}

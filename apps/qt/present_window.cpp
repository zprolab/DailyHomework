#include "present_window.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace dhw::qt {
namespace {

constexpr int kMenuIdleMs = 6000;
constexpr int kCursorIdleMs = 1200;
constexpr double kDesignWidth = 1920.0;
constexpr double kDesignHeight = 1080.0;

}  // namespace

PresentWindow::PresentWindow(AppController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setWindowTitle(QStringLiteral("每日作业 · 放映"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::Window, true);
    setAutoFillBackground(true);
    QPalette black = palette();
    black.setColor(QPalette::Window, Qt::black);
    setPalette(black);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);

    menu_ = new QMenu(this);
    idle_ = new QTimer(this);
    idle_->setSingleShot(true);
    idle_->setInterval(kMenuIdleMs);
    connect(idle_, &QTimer::timeout, this, &PresentWindow::hide_menu);

    cursor_ = new QTimer(this);
    cursor_->setSingleShot(true);
    cursor_->setInterval(kCursorIdleMs);
    connect(cursor_, &QTimer::timeout, this, &PresentWindow::update_cursor);

    connect(controller_, &AppController::changed, this, [this]() {
        if (menu_open_) {
            rebuild_menu();
        }
        update();
    });
}

void PresentWindow::start() {
    move_to_screen(screen());
    showFullScreen();
    setFocus();
    raise();
    update_cursor();
}

void PresentWindow::stop() {
    hide_menu();
    hide();
}

void PresentWindow::ensure_menu_closed() {
    hide_menu();
}

void PresentWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    const QImage image = controller_->render_current_png();
    if (image.isNull()) {
        return;
    }
    const double scale =
        std::min(width() / kDesignWidth, height() / kDesignHeight);
    const double target_width = kDesignWidth * scale;
    const double target_height = kDesignHeight * scale;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRectF((width() - target_width) / 2.0, (height() - target_height) / 2.0,
                             target_width, target_height),
                      image);
}

void PresentWindow::contextMenuEvent(QContextMenuEvent* event) {
    event->accept();
    show_menu(event->globalPos());
}

void PresentWindow::mouseMoveEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    if (menu_open_) {
        reset_idle();
    }
    setCursor(Qt::BlankCursor);
    cursor_->start();
}

void PresentWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F5) {
        if (menu_open_ && event->key() == Qt::Key_Escape) {
            hide_menu();
            event->accept();
            return;
        }
        hide_menu();
        emit closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PresentWindow::hide_menu() {
    menu_open_ = false;
    idle_->stop();
    menu_->hide();
    update_cursor();
}

void PresentWindow::reset_idle() {
    if (menu_open_) {
        idle_->start();
    }
}

void PresentWindow::update_cursor() {
    setCursor(menu_open_ ? Qt::ArrowCursor : Qt::BlankCursor);
}

void PresentWindow::rebuild_menu() {
    menu_->clear();
    QAction* toggle = menu_->addAction(controller_->submit_only()
                                           ? QStringLiteral("切换为显示全部")
                                           : QStringLiteral("切换为仅要交"));
    connect(toggle, &QAction::triggered, this, [this]() {
        controller_->set_submit_only(!controller_->submit_only());
        update();
        hide_menu();
    });
    QAction* image = menu_->addAction(QStringLiteral("生成作业图片"));
    connect(image, &QAction::triggered, this, [this]() {
        const QString path = controller_->export_current_png();
        hide_menu();
        Q_UNUSED(path);
    });
    menu_->addSeparator();

    QMenu* screens = menu_->addMenu(QStringLiteral("放映屏幕 ▸"));
    QAction* auto_screen = screens->addAction(QStringLiteral("自动（窗口当前所在屏幕）"));
    connect(auto_screen, &QAction::triggered, this, [this]() { move_to_screen(screen()); });
    for (QScreen* candidate : QGuiApplication::screens()) {
        const QString label = QStringLiteral("%1（%2×%3）%4")
                                  .arg(candidate->name())
                                  .arg(candidate->geometry().width())
                                  .arg(candidate->geometry().height())
                                  .arg(candidate == screen() ? QStringLiteral(" · 当前")
                                                             : QString());
        QAction* action = screens->addAction((candidate == screen() ? QStringLiteral("● ") : QString()) +
                                             label);
        connect(action, &QAction::triggered, this,
                [this, candidate]() { move_to_screen(candidate); });
    }
    menu_->addSeparator();
    QAction* quit = menu_->addAction(QStringLiteral("退出放映"));
    connect(quit, &QAction::triggered, this, [this]() {
        hide_menu();
        emit closed();
    });
}

void PresentWindow::show_menu(const QPoint& position) {
    rebuild_menu();
    menu_open_ = true;
    update_cursor();
    const QSize hint = menu_->sizeHint();
    const QRect available = screen() != nullptr ? screen()->availableGeometry() : rect();
    int x = position.x();
    int y = position.y();
    x = std::min(std::max(x, available.left() + 8), available.right() - hint.width() - 8);
    y = std::min(std::max(y, available.top() + 8), available.bottom() - hint.height() - 8);
    menu_->popup(QPoint(x, y));
    idle_->start();
}

void PresentWindow::move_to_screen(QScreen* target) {
    if (target == nullptr) {
        return;
    }
    setScreen(target);
    setGeometry(target->geometry());
    if (isVisible()) {
        showFullScreen();
    }
}

}  // namespace dhw::qt

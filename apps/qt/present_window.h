#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

#include "app_controller.h"

class QAction;
class QMenu;
class QScreen;
class QTimer;

namespace dhw::qt {

// Frameless full screen presentation: the shared 1920x1080 layout re-rendered at the
// screen's physical resolution (not an upscaled 1080p bitmap) with letterboxing, a
// hidden mouse pointer, and a context menu that folds itself away after six idle
// seconds.
class PresentWindow : public QWidget {
    Q_OBJECT

public:
    explicit PresentWindow(AppController* controller, QWidget* parent = nullptr);

    void start();
    void stop();
    void ensure_menu_closed();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild_menu();
    void show_menu(const QPoint& position);
    void hide_menu();
    void reset_idle();
    void update_cursor();
    void move_to_screen(QScreen* screen);

    AppController* controller_ = nullptr;
    QMenu* menu_ = nullptr;
    QTimer* idle_ = nullptr;
    QTimer* cursor_ = nullptr;
    bool menu_open_ = false;
    // Last frame, keyed by the device pixel ratio it was rendered for. The trace
    // content changes only through controller signals, so a repaint during the menu
    // animation does not need to rasterise a 4K/5K canvas again.
    QImage frame_;
    double frame_ratio_ = 0.0;
    bool frame_valid_ = false;
};

}  // namespace dhw::qt

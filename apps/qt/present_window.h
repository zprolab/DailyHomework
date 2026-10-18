#pragma once

#include <QPoint>
#include <QWidget>

#include "app_controller.h"

class QAction;
class QMenu;
class QScreen;
class QTimer;

namespace dhw::qt {

// Frameless full screen presentation: the shared 1920x1080 PNG scaled with
// letterboxing, hidden mouse pointer, and a context menu that folds itself away
// after six idle seconds.
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
};

}  // namespace dhw::qt

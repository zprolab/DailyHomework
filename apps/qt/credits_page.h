#pragma once

#include <QWidget>

class QVBoxLayout;

namespace dhw::qt {

// Static credits page (sidebar -> Credits): the people behind the project. It has no
// state and never touches the data store; the shell owns navigation, exactly like the
// settings page does.
class CreditsPage : public QWidget {
    Q_OBJECT

public:
    explicit CreditsPage(QWidget* parent = nullptr);

signals:
    // The 52 px page header owns the hamburger and the back button; the shell performs
    // the actual sidebar toggle and page switch.
    void sidebar_toggle_requested();
    void back_requested();

private:
    void build_header(QVBoxLayout* root);
    void build_body(QVBoxLayout* root);
};

}  // namespace dhw::qt

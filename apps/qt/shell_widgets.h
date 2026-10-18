#pragma once

#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include "dhw/icons.h"

class QHBoxLayout;
class QScrollArea;
class QVBoxLayout;

namespace dhw::qt {

// ---------------------------------------------------------------------------
// Native skin helpers. Every control below is a stock Qt widget and the platform
// style paints it; what the shell supplies is content, not chrome: the vector
// pixmaps of dhw/icons.h and the group box that wraps one page section.
//
// With -DDHW_UI_FLUENT=ON new code builds its widgets through fluent_style.h.
// These helpers stay for the pages still on the native skin (editor, week) and
// for the native build, so the two skins never grow two icon systems: both use
// the theme_icon() roles and the same palette/accent source.
// ---------------------------------------------------------------------------

// Native tool button that shows one of the shell's vector icons. Only the icon,
// the cursor and auto raise are set here; size, colours and metrics come from the
// platform style. A role the icon set does not know yields a bare button.
QToolButton* make_icon_button(const QString& role, int side, QWidget* parent);

// Native tool button with `text` beside the vector icon of `role`. An unknown or
// empty role makes it a text only button. autoRaise is off, so the platform style
// draws a real bezel.
QToolButton* make_action_button(const QString& text, const QString& role, QWidget* parent,
                                int side = 17);

// Rebuilds every role icon from the live palette. The icons are pixmaps drawn by
// the shell, so a system light/dark switch has to re-tint them by hand; Qt
// repaints the controls themselves.
void refresh_role_icons();

// Page or section title: a plain QLabel whose emphasis is the platform font in bold.
// The colour stays the palette's own text colour.
QLabel* make_title_label(const QString& text, QWidget* parent);

// Native QGroupBox: the platform style draws the frame and the title. The count is a
// plain text suffix of that title (title, a middle dot, then the count) and the icon
// plus the hint are the first ordinary row inside the group. This replaces the hand
// drawn card.
class Card : public QGroupBox {
    Q_OBJECT

public:
    Card(dhw::icons::Icon icon, const QString& title, const QString& hint,
         QWidget* parent = nullptr);

    QWidget* body() const { return body_; }
    QVBoxLayout* body_layout() const { return body_layout_; }
    void add_header_widget(QWidget* widget);
    void set_title(const QString& title);
    void set_badge(const QString& text);

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh_icon();
    void refresh_title();

    dhw::icons::Icon icon_;
    QString title_text_;
    QString badge_text_;
    QLabel* icon_label_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    QHBoxLayout* header_layout_ = nullptr;
};

// Scroll area whose content is a plain widget; the vertical bar only appears when the
// content really does not fit.
QScrollArea* make_scroll(QWidget* content);

// Hairline drawn by the platform style. The panels used to get their separators from
// the deleted stylesheet.
QFrame* make_separator(QFrame::Shape shape, QWidget* parent);

}  // namespace dhw::qt

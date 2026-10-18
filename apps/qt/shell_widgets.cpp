#include "shell_widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>

#include "theme.h"

namespace dhw::qt {
namespace {

constexpr int kCardIconSide = 18;

// One place that turns a role name into a button icon, so a palette change can redo
// it for every button (the pixmap carries a colour, the button cannot re-tint it).
void apply_role_icon(QToolButton* button, const QString& role, int side) {
    if (button == nullptr) {
        return;
    }
    if (role.isEmpty()) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        return;
    }
    const QPixmap pixmap = theme_icon(role, side, accent_color());
    if (pixmap.isNull()) {
        // No vector and no standard icon for this role: stay a text only button
        // instead of reserving space for a blank icon.
        button->setToolButtonStyle(button->text().isEmpty() ? Qt::ToolButtonIconOnly
                                                            : Qt::ToolButtonTextOnly);
        return;
    }
    button->setProperty("dhwRole", role);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(side, side));
}

}  // namespace

QToolButton* make_icon_button(const QString& role, int side, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setCursor(Qt::PointingHandCursor);
    // The side is a design metric: it follows the display size like the text does.
    apply_role_icon(button, role, ui_px(side));
    return button;
}

QToolButton* make_action_button(const QString& text, const QString& role, QWidget* parent,
                                int side) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(false);
    button->setCursor(Qt::PointingHandCursor);
    apply_role_icon(button, role, ui_px(side));
    return button;
}

void refresh_role_icons() {
    // Both skins tag their icon buttons with dhwRole: the native QToolButton and the
    // Fluent Button are QAbstractButton subclasses, so one sweep re-tints either.
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (button == nullptr) {
            continue;
        }
        const QString role = button->property("dhwRole").toString();
        if (role.isEmpty()) {
            continue;
        }
        int side = button->property("dhwIconSide").toInt();
        if (side <= 0) {
            side = button->iconSize().width();
        }
        side = std::max(8, side);
        const QPixmap pixmap = theme_icon(role, side, accent_color());
        if (pixmap.isNull()) {
            continue;
        }
        button->setIcon(QIcon(pixmap));
        button->setIconSize(QSize(side, side));
    }
}

QLabel* make_title_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

Card::Card(dhw::icons::Icon icon, const QString& title, const QString& hint, QWidget* parent)
    : QGroupBox(parent), icon_(icon), title_text_(title) {
    refresh_title();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ui_px(9), ui_px(6), ui_px(9), ui_px(8));
    root->setSpacing(ui_px(6));

    auto* header = new QWidget(this);
    header_layout_ = new QHBoxLayout(header);
    header_layout_->setContentsMargins(0, 0, 0, 0);
    header_layout_->setSpacing(ui_px(8));

    icon_label_ = new QLabel(header);
    icon_label_->setFixedSize(ui_px(kCardIconSide), ui_px(kCardIconSide));
    icon_label_->setAlignment(Qt::AlignCenter);
    header_layout_->addWidget(icon_label_);

    if (hint.isEmpty()) {
        header_layout_->addStretch(1);
    } else {
        auto* hint_label = new InfoLabel(hint, header);
        hint_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        hint_label->setWordWrap(true);
        hint_label->setToolTip(hint);
        header_layout_->addWidget(hint_label, 1);
    }
    root->addWidget(header);

    body_ = new QWidget(this);
    body_layout_ = new QVBoxLayout(body_);
    body_layout_->setContentsMargins(0, 0, 0, 0);
    body_layout_->setSpacing(ui_px(2));
    root->addWidget(body_, 1);
    refresh_icon();
}

void Card::add_header_widget(QWidget* widget) {
    header_layout_->addWidget(widget);
}

void Card::set_title(const QString& title) {
    title_text_ = title;
    refresh_title();
}

void Card::set_badge(const QString& text) {
    badge_text_ = text;
    refresh_title();
}

void Card::refresh_title() {
    // The count rides along as plain text in the group box title, so there is no badge
    // widget with a background and a corner radius left to keep consistent.
    const QString suffix =
        badge_text_.isEmpty() ? QString() : QStringLiteral(" · ") + badge_text_;
    QGroupBox::setTitle(title_text_ + suffix);
}

void Card::changeEvent(QEvent* event) {
    QGroupBox::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        refresh_icon();
    }
}

void Card::refresh_icon() {
    if (icon_label_ == nullptr) {
        // A style or palette change can arrive while the constructor is still building
        // the header; there is nothing to paint yet.
        return;
    }
    icon_label_->setPixmap(
        card_icon(icon_, ui_px(kCardIconSide), accent_color().name(QColor::HexRgb)));
}

QScrollArea* make_scroll(QWidget* content) {
    auto* scroll = new QScrollArea();
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // A scroll area's viewport is painted by the style rather than by the palette it
    // inherits, so the viewport and the content are pinned to the live palette
    // explicitly. Without this a page inside a scroll area stays light in dark mode.
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    if (content != nullptr) {
        content->setAutoFillBackground(true);
        content->setBackgroundRole(QPalette::Window);
    }
    return scroll;
}

QFrame* make_separator(QFrame::Shape shape, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setFrameShape(shape);
    frame->setFrameShadow(QFrame::Sunken);
    frame->setLineWidth(1);
    frame->setSizePolicy(shape == QFrame::HLine ? QSizePolicy::Expanding : QSizePolicy::Fixed,
                         shape == QFrame::HLine ? QSizePolicy::Fixed : QSizePolicy::Expanding);
    return frame;
}

}  // namespace dhw::qt

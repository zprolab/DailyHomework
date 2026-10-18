#include "credits_page.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "fluent_style.h"
#include "shell_widgets.h"
#include "theme.h"

namespace dhw::qt {

CreditsPage::CreditsPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    build_header(root);
    build_body(root);
}

void CreditsPage::build_header(QVBoxLayout* root) {
    auto* head = new QWidget(this);
    head->setFixedHeight(52);
    auto* layout = new QHBoxLayout(head);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* hamburger = make_fluent_icon_button(QStringLiteral("hamburger"), 18, head);
    hamburger->setToolTip(QStringLiteral("收起 / 展开侧边栏（Ctrl+B）"));
    set_fluent_icon_raised(hamburger, false);
    connect(hamburger, &QAbstractButton::clicked, this, &CreditsPage::sidebar_toggle_requested);
    layout->addWidget(hamburger);

    auto* back = make_fluent_icon_button(QStringLiteral("previous"), 16, head);
    back->setToolTip(QStringLiteral("返回编辑页（Esc）"));
    set_fluent_icon_raised(back, false);
    connect(back, &QAbstractButton::clicked, this, &CreditsPage::back_requested);
    layout->addWidget(back);

    auto* title = make_fluent_title(QStringLiteral("Credits"), head);
    layout->addWidget(title);
    layout->addStretch(1);
    root->addWidget(head);
    root->addWidget(make_fluent_divider(Qt::Horizontal, this));
}

void CreditsPage::build_body(QVBoxLayout* root) {
    auto* content = new QWidget();
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(16, 14, 16, 16);
    column->setSpacing(12);

    auto* card = new SectionCard(dhw::icons::Icon::Check, QStringLiteral("贡献者"), QString(),
                                 this);
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    const auto add_row = [card, form](const QString& role, const QString& names) {
        auto* label = make_fluent_label(role, true, card);
        auto* value = make_fluent_label(names, false, card);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, value);
    };
    add_row(QStringLiteral("Original Creator"),
            QStringLiteral("Yunhuan Zhang, Yuchen Tao"));
    add_row(QStringLiteral("Refactor"), QStringLiteral("Chenyun Zhang"));
    add_row(QStringLiteral("Developers"),
            QStringLiteral("Yuchen Tao, Yunhuan Zhang, Chenyun Zhang"));
    add_row(QStringLiteral("Debuggers"), QStringLiteral("Yuchen Tao"));
    card->body_layout()->addLayout(form);

    auto* note = make_fluent_label(
        QStringLiteral("每日作业 v2.0.0 · 教室用「每日作业 / 公告 / 演讲提醒」放映应用 · MIT License"),
        true, card);
    note->setWordWrap(true);
    card->body_layout()->addWidget(note);

    column->addWidget(card, 0);
    column->addStretch(1);

    auto* scroll = make_scroll(content);
    scroll->setObjectName(QStringLiteral("creditsScroll"));
    root->addWidget(scroll, 1);
}

}  // namespace dhw::qt

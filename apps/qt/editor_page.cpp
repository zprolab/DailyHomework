#include "editor_page.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "dhw/layout.h"
#include "dhw/schedule.h"
#include "fluent_style.h"
#include "theme.h"
#include "shell_widgets.h"

#ifdef DHW_UI_FLUENT
#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#endif

namespace dhw::qt {

// ---------------------------------------------------------------------------
// Section card of the edit page.
//
// Both skins expose the same small surface - body_layout(), add_header_widget(),
// set_title(), set_badge() - so the row builders below never branch on the skin.
// The native build keeps the platform QGroupBox from shell_widgets.h; the Fluent
// build paints a Fluent Card with the section heading inside it, which is what
// fluent_style.h's SectionCard does, plus the mutable title and the count that
// this page needs on every refresh.
// ---------------------------------------------------------------------------
#ifdef DHW_UI_FLUENT
class EditorCard : public QWidget {
public:
    EditorCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
               QWidget* parent = nullptr);

    QVBoxLayout* body_layout() const { return body_layout_; }
    void add_header_widget(QWidget* widget);
    void set_title(const QString& title);
    void set_badge(const QString& text);

private:
    void refresh_title();

    QString title_text_;
    QString badge_text_;
    fluent::textfields::Label* title_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
};

EditorCard::EditorCard(dhw::icons::Icon icon, const QString& title, const QString& hint,
                       QWidget* parent)
    : QWidget(parent), title_text_(title) {
    // A Fluent card is a content surface, not a decorated box: the section glyph of the
    // group box it replaces has no counterpart in the WinUI card anatomy.
    Q_UNUSED(icon);
    auto* host = new QVBoxLayout(this);
    host->setContentsMargins(0, 0, 0, 0);
    host->setSpacing(0);

    auto* surface = new fluent::layout::Card(this);
    surface->setAppearance(fluent::layout::Card::Layer);
    surface->setBorderVisible(true);
    host->addWidget(surface);

    auto* column = new QVBoxLayout(surface);
    column->setContentsMargins(16, 12, 16, 14);
    column->setSpacing(8);

    header_ = new QHBoxLayout();
    header_->setContentsMargins(0, 0, 0, 0);
    header_->setSpacing(8);
    title_ = new fluent::textfields::Label(title, surface);
    title_->setFluentTypography(Typography::FontRole::BodyStrong);
    header_->addWidget(title_);
    // The trailing head controls (the schedule card's template button) sit at the far
    // right, exactly where the native group box put them.
    header_->addStretch(1);
    column->addLayout(header_);

    if (!hint.isEmpty()) {
        // Word wrapped plain label: the Fluent Label renders one line and elides, while
        // these hints are full sentences.
        auto* hint_label = new InfoLabel(hint, surface);
        hint_label->setWordWrap(true);
        hint_label->setToolTip(hint);
        column->addWidget(hint_label);
    }

    auto* body = new QWidget(surface);
    body_layout_ = new QVBoxLayout(body);
    body_layout_->setContentsMargins(0, 0, 0, 0);
    body_layout_->setSpacing(4);
    column->addWidget(body, 1);
}

void EditorCard::add_header_widget(QWidget* widget) {
    if (widget != nullptr && header_ != nullptr) {
        header_->addWidget(widget);
    }
}

void EditorCard::set_title(const QString& title) {
    title_text_ = title;
    refresh_title();
}

void EditorCard::set_badge(const QString& text) {
    badge_text_ = text;
    refresh_title();
}

void EditorCard::refresh_title() {
    if (title_ == nullptr) {
        return;
    }
    // The count is plain text in the heading, like the native group box title suffix:
    // no second chip, no background, nothing to keep in sync.
    const QString suffix =
        badge_text_.isEmpty() ? QString() : QStringLiteral(" · ") + badge_text_;
    title_->setText(title_text_ + suffix);
}

#else  // !DHW_UI_FLUENT

class EditorCard : public Card {
public:
    using Card::Card;
};

#endif  // DHW_UI_FLUENT

namespace {

constexpr int kEditDebounceMs = 300;
constexpr int kPreviewDebounceMs = 120;
constexpr int kSubjectWidth = 60;
constexpr int kIndexWidth = 20;
constexpr int kTotalPeriods = 9;
// One rhythm for every editable row: the same minimum height, vertical margin and
// inter-column gap, so the three cards read as one list instead of three styles.
constexpr int kRowHeight = 32;
constexpr int kRowMarginV = 2;
constexpr int kRowSpacing = 6;
// Row actions: a compact icon only button per action. The Fluent skin paints an iconfont
// glyph in it, the native skin a QToolButton with the platform's standard pixmap.
constexpr int kActionSide = 24;
constexpr int kActionIconSide = 16;
constexpr int kActionSpacing = 2;
constexpr int kActionCount = 4;
constexpr int kActionCellWidth =
    kActionSide * kActionCount + kActionSpacing * (kActionCount - 1);
// The scroll content of the homework and speech cards keeps this much slack below its
// last row, so the last row is never sliced in half by the card edge once it overflows.
constexpr int kRowSlack = 36;
constexpr int kPreviewMinWidth = 200;
constexpr int kPreviewHintWidth = 320;
constexpr double kSlideAspect = 9.0 / 16.0;

const std::vector<QString>& all_subjects() {
    static const std::vector<QString> subjects = {
        QStringLiteral("语文"),  QStringLiteral("数学"),  QStringLiteral("英语A"),
        QStringLiteral("英语B"), QStringLiteral("物理"),  QStringLiteral("化学"),
        QStringLiteral("生物"),  QStringLiteral("历史"),  QStringLiteral("地理"),
        QStringLiteral("道法"),  QStringLiteral("政治"),  QStringLiteral("体育"),
        QStringLiteral("美术"),  QStringLiteral("音乐"),  QStringLiteral("信息"),
        QStringLiteral("心理"),  QStringLiteral("劳技"),  QStringLiteral("自习"),
        QStringLiteral("班会")};
    return subjects;
}

// The fixed school list the subject completer always offers, in the order teachers
// expect. Current data subjects are appended after these.
const QStringList& completer_subject_seed() {
    static const QStringList subjects = {
        QStringLiteral("语文"),  QStringLiteral("数学"),  QStringLiteral("英语A"),
        QStringLiteral("英语B"), QStringLiteral("物理"),  QStringLiteral("化学"),
        QStringLiteral("生物"),  QStringLiteral("历史"),  QStringLiteral("地理"),
        QStringLiteral("道法"),  QStringLiteral("体育"),  QStringLiteral("音乐"),
        QStringLiteral("美术"),  QStringLiteral("信息"),  QStringLiteral("班会")};
    return subjects;
}

std::string to_std(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString from_std(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString week_days(int index) {
    return QStringLiteral("一二三四五六日").mid(index, 1);
}

// A QLabel that forwards a left click. Used for the row number and subject cells so the
// whole row can hand focus to its text field without a stylesheet or a custom control.
class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(QWidget* parent = nullptr) : QLabel(parent) {}
    ClickableLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {}

    std::function<void()> clicked;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && clicked) {
            clicked();
        }
        QLabel::mouseReleaseEvent(event);
    }
};

// One vector warning triangle for both skins. The platform's own standard icon is a
// pixmap the platform style paints, and on macOS that style resolves it through AppKit;
// a skin independent glyph keeps the 调课 / 演讲提醒 marker identical in the Fluent build
// and in the native fallback.
QPixmap warning_pixmap(int side, const QColor& color) {
#ifdef DHW_UI_FLUENT
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // paintGlyph draws with the painter's own pen, so the semantic colour rides on it.
    painter.setPen(color);
    Typography::Icons::paintGlyph(painter, QRectF(0, 0, side, side),
                                  Typography::Icons::Warning, side);
    painter.end();
    return pixmap;
#else
    Q_UNUSED(color);
    const QStyle* widget_style = QApplication::style();
    if (widget_style == nullptr) {
        return QPixmap();
    }
    return widget_style->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(side, side);
#endif
}

// Row action glyphs. In the Fluent build these are iconfont glyphs painted by the
// button itself (the WinUI anatomy); the native build keeps the platform's standard
// pixmaps on a QToolButton. Both are QAbstractButton, so every connection below is
// written against QAbstractButton::clicked and no call site branches on the skin.
enum class RowAction { Up, Down, Remove, Add };

ActionControl* make_row_button(QWidget* parent, RowAction action, const QString& tooltip,
                               const QString& role = QString()) {
#ifdef DHW_UI_FLUENT
    // The role only names a shell vector pixmap for the native fallback; the Fluent
    // button carries an iconfont glyph instead, so it is unused here.
    Q_UNUSED(role);
    auto* button = new fluent::basicinput::Button(parent);
    button->setFluentStyle(fluent::basicinput::Button::Subtle);
    button->setFluentSize(fluent::basicinput::Button::Small);
    button->setFluentLayout(fluent::basicinput::Button::IconOnly);
    button->setFixedSize(kActionSide, kActionSide);
    switch (action) {
        case RowAction::Up:
            button->setIconGlyph(Typography::Icons::ChevronUp, kActionIconSide);
            break;
        case RowAction::Down:
            button->setIconGlyph(Typography::Icons::ChevronDown, kActionIconSide);
            break;
        case RowAction::Remove:
            button->setIconGlyph(Typography::Icons::Delete, kActionIconSide);
            break;
        case RowAction::Add:
            button->setIconGlyph(Typography::Icons::Add, kActionIconSide);
            break;
    }
#else
    QStyle::StandardPixmap standard = QStyle::SP_ArrowUp;
    switch (action) {
        case RowAction::Up:
            standard = QStyle::SP_ArrowUp;
            break;
        case RowAction::Down:
            standard = QStyle::SP_ArrowDown;
            break;
        case RowAction::Remove:
            standard = QStyle::SP_DialogCloseButton;
            break;
        case RowAction::Add:
            standard = QStyle::SP_FileDialogNewFolder;
            break;
    }
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(kActionSide, kActionSide);
    button->setIconSize(QSize(kActionIconSide, kActionIconSide));
    const QStyle* widget_style = parent != nullptr ? parent->style() : QApplication::style();
    // The shell's own vector glyph is preferred where it exists (it reads as "+" rather
    // than as a folder); otherwise the platform's standard icon is the native fallback.
    const QPixmap vector = role.isEmpty()
                               ? QPixmap()
                               : theme_icon(role, kActionIconSide, accent_color());
    if (!vector.isNull()) {
        button->setProperty("dhwRole", role);
        button->setIcon(QIcon(vector));
    } else if (widget_style != nullptr) {
        button->setIcon(widget_style->standardIcon(standard));
    }
#endif
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// Builds the fixed-width action cell shared by all row types. The trailing stretch
// right-aligns whatever is visible, so a ghost row with only "+" keeps its "+" on the
// same vertical line as the real rows above it. The buttons keep their own square side
// and are centred in the row, so a taller row never stretches the icons.
QWidget* make_action_cell(QWidget* parent, ActionControl*& up, ActionControl*& down,
                          ActionControl*& remove, ActionControl*& add) {
    auto* cell = new QWidget(parent);
    cell->setFixedWidth(kActionCellWidth);
    auto* layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kActionSpacing);
    up = make_row_button(cell, RowAction::Up, QStringLiteral("上移"));
    down = make_row_button(cell, RowAction::Down, QStringLiteral("下移"));
    remove = make_row_button(cell, RowAction::Remove, QStringLiteral("删除这一条"));
    add = make_row_button(cell, RowAction::Add, QStringLiteral("在下面加一条"),
                          QStringLiteral("plus"));
    layout->addStretch(1);
    layout->addWidget(up, 0, Qt::AlignVCenter);
    layout->addWidget(down, 0, Qt::AlignVCenter);
    layout->addWidget(remove, 0, Qt::AlignVCenter);
    layout->addWidget(add, 0, Qt::AlignVCenter);
    return cell;
}

// Every editable row is a plain container marked with this property. The row rhythm is
// resolved page wide from those containers (see EditorPage::sync_row_heights), so a row
// type that carries no subject combo still lines up with one that does.
constexpr const char* kRowMarker = "dhwEditorRow";

// Which direct children of a row take part in the shared row height. The two input
// controls are the reason this exists: a Fluent ComboBox is pinned to its token control
// height while the Fluent LineEdit keeps the platform style's size hint (the native skin
// has the same mismatch the other way round), so a row that lets each widget size itself
// shows two baselines and two field heights. Labels follow the row too; the square row
// action buttons keep their own 24 px side and are centred by their cell.
bool sizes_with_row(QWidget* widget) {
    if (widget == nullptr) {
        return false;
    }
    // Check first: a QCheckBox is a QAbstractButton, but it belongs to the row rhythm.
    if (qobject_cast<QCheckBox*>(widget) != nullptr) {
        return true;
    }
    if (qobject_cast<QAbstractButton*>(widget) != nullptr) {
        return false;
    }
    return qobject_cast<QComboBox*>(widget) != nullptr ||
           qobject_cast<QLineEdit*>(widget) != nullptr ||
           qobject_cast<QLabel*>(widget) != nullptr;
}

// Extra padding between the announcement checkbox and its text field: 8 px at 100 %,
// scaled with the shell's display size like the other shell metrics. Only the
// announcement rows carry it, so no other row gets wider.
int announcement_check_padding(double ui_scale) {
    const double scaled = 8.0 * std::clamp(ui_scale, 0.5, 1.5);
    return std::max(4, static_cast<int>(std::lround(scaled)));
}

// Adds that padding as a tracked spacer, so a later display-size change can resize it
// without rebuilding the row.
void add_announcement_gap(QBoxLayout* layout, double ui_scale,
                          std::vector<QSpacerItem*>& out) {
    auto* gap = new QSpacerItem(announcement_check_padding(ui_scale), 0,
                                QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addItem(gap);
    out.push_back(gap);
}

// Fills a row editor with an existing value. QLineEdit::setText parks the cursor at the
// end and scrolls the view there, so a value wider than the field would show its tail;
// a row that is being read, not typed into, has to start at the first character.
void set_row_text(QLineEdit* line, const QString& text) {
    if (line == nullptr) {
        return;
    }
    line->setText(text);
    line->setCursorPosition(0);
}

// The 要交 checkbox is a Fluent CheckBox (a QCheckBox subclass, so the toggled()
// connections and isChecked() reads below are unchanged).
QCheckBox* make_check(const QString& text, QWidget* parent) {
#ifdef DHW_UI_FLUENT
    auto* check = new fluent::basicinput::CheckBox(text, parent);
    // The default Fluent box is a touch sized 20 px square with wide margins; an inline
    // row control wants the compact metrics or it eats the text field's width.
    check->setBoxSize(18);
    check->setBoxMargin(2);
    check->setTextGap(6);
    return check;
#else
    return new QCheckBox(text, parent);
#endif
}

// Text row editor in the active skin. Both are QLineEdit, so text(), setText(),
// setPlaceholderText(), the returnPressed / textEdited signals and the font or palette
// emphasis below keep working unchanged.
QLineEdit* make_row_edit(const QString& text, QWidget* parent) {
    QLineEdit* line = make_fluent_line_edit(text, parent);
#ifdef DHW_UI_FLUENT
    // Row fields sit in a narrow four zone row; the standard 12 px text inset costs more
    // characters than it buys at this width.
    if (auto* fluent_line = qobject_cast<fluent::textfields::LineEdit*>(line)) {
        const QMargins margins = fluent_line->contentMargins();
        fluent_line->setContentMargins(QMargins(8, margins.top(), 8, margins.bottom()));
    }
#endif
    return line;
}

// Inline completer: current-data subjects plus the fixed school list, matching without
// regard to case. A completion only fills the line edit; it never inserts a row.
void attach_subject_completer(QComboBox* box, const QStringList& subjects) {
    if (box == nullptr) {
        return;
    }
    auto* completer = new QCompleter(subjects, box);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::InlineCompletion);
    completer->setFilterMode(Qt::MatchStartsWith);
    box->setCompleter(completer);
}

// Assigns text to a label of either skin. The Fluent Label keeps the string it was built
// with and re-renders from that cache on every resize, so a plain QLabel-typed setText is
// undone by the next layout pass; routing every run time text change through the concrete
// type keeps the label's cache and its painted text in step.
void set_label_text(QLabel* label, const QString& text) {
    if (label == nullptr) {
        return;
    }
#ifdef DHW_UI_FLUENT
    if (auto* fluent_label = qobject_cast<fluent::textfields::Label*>(label)) {
        fluent_label->setText(text);
        return;
    }
#endif
    label->setText(text);
}

// Row emphasis carried by the editor font and palette: the "done" strike through of a
// finished announcement and the semantic red of a swapped period. A Fluent LineEdit
// re-derives both from its design tokens on every theme refresh, so this is applied by
// value (never toggled) and can be re-applied at any time.
void apply_row_emphasis(QLineEdit* line, bool strike_out, bool danger) {
    if (line == nullptr) {
        return;
    }
    QFont font = line->font();
    if (font.strikeOut() != strike_out || font.bold() != danger) {
        font.setStrikeOut(strike_out);
        font.setBold(danger);
        line->setFont(font);
    }
    if (!danger) {
        return;
    }
    // A swapped period is the only red row; a row that stops being one is rebuilt, so the
    // colour is applied and never has to be taken back.
    const QColor danger_tone = danger_color();
    QPalette palette = line->palette();
    if (palette.color(QPalette::Text) != danger_tone) {
        palette.setColor(QPalette::Text, danger_tone);
        line->setPalette(palette);
    }
}

// Scroll area inside a card. The native skin fills the viewport with the platform
// window colour, which reads as a grey block inside the group box; the Fluent card is
// already the surface, so the viewport stays transparent.
QScrollArea* make_card_scroll(QWidget* content) {
    QScrollArea* scroll = make_scroll(content);
#ifdef DHW_UI_FLUENT
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
#endif
    return scroll;
}
}  // namespace

// Thumbnail of the whole slide. It is a plain QLabel; the native skin paints the
// platform's own panel frame as the hairline, the Fluent card that wraps it supplies the
// border instead. The only additions are the 16:9 height-for-width rule and the
// drag-out gesture used to hand the PNG to another app.
class PreviewLabel : public QLabel {
public:
    explicit PreviewLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setScaledContents(true);
        setAlignment(Qt::AlignCenter);
#ifdef DHW_UI_FLUENT
        // The surrounding Fluent Card draws the 1 px token border and the corner radius.
        setFrameShape(QFrame::NoFrame);
#else
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Plain);
        setLineWidth(1);
#endif
        setMinimumSize(kPreviewMinWidth, heightForWidth(kPreviewMinWidth));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QStringLiteral("拖动缩略图可交给外部程序；双击或点「放大」查看原尺寸"));
    }

    void set_preview(const QImage& image) {
        image_ = image;
        setPixmap(QPixmap::fromImage(image));
        updateGeometry();
    }

    const QImage& preview_image() const { return image_; }

    // Called when a drag actually starts; returns a real PNG path under <data>/images.
    std::function<QString()> drag_file;
    std::function<void()> zoom;

protected:
    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override {
        return qMax(1, qRound(width * kSlideAspect)) + 2 * frameWidth();
    }

    QSize sizeHint() const override {
        return QSize(kPreviewHintWidth, heightForWidth(kPreviewHintWidth));
    }

    QSize minimumSizeHint() const override {
        return QSize(kPreviewMinWidth, heightForWidth(kPreviewMinWidth));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            drag_start_ = event->position().toPoint();
        }
        QLabel::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && zoom) {
            zoom();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) || drag_start_.isNull()) {
            QLabel::mouseMoveEvent(event);
            return;
        }
        if ((event->position().toPoint() - drag_start_).manhattanLength() <
            QApplication::startDragDistance()) {
            return;
        }
        const QString path = drag_file ? drag_file() : QString();
        drag_start_ = QPoint();
        if (path.isEmpty()) {
            return;
        }
        auto* mime = new QMimeData();
        mime->setUrls({QUrl::fromLocalFile(path)});
        QDrag drag(this);
        drag.setMimeData(mime);
        const QPixmap preview = pixmap();
        if (!preview.isNull()) {
            drag.setPixmap(preview.scaled(320, 180, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
        }
        drag.exec(Qt::CopyAction);
    }

private:
    QPoint drag_start_;
    QImage image_;
};

EditorPage::EditorPage(AppController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setObjectName(QStringLiteral("editorPage"));
    build_cards();
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(kEditDebounceMs);
    connect(debounce_, &QTimer::timeout, this, &EditorPage::on_debounce);
    preview_timer_ = new QTimer(this);
    preview_timer_->setSingleShot(true);
    preview_timer_->setInterval(kPreviewDebounceMs);
    connect(preview_timer_, &QTimer::timeout, this, &EditorPage::refresh_preview);
    install_history_shortcuts();
    connect(controller_, &AppController::changed, this, &EditorPage::on_controller_changed);
    reload_all();
}

void EditorPage::install_history_shortcuts() {
    const auto add = [this](QKeySequence::StandardKey standard, void (EditorPage::*slot)(),
                            std::vector<QShortcut*>* target) {
        QList<QKeySequence> sequences = QKeySequence::keyBindings(standard);
        if (standard == QKeySequence::Redo) {
            // Qt binds Redo to Ctrl+Y on Windows. Ctrl+Shift+Z is what everyone presses
            // there; on macOS QKeySequence spells Command as Ctrl, so this alias is the
            // very same sequence as the standard one and the de-duplication below drops
            // it instead of registering an ambiguous shortcut.
            sequences.append(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
        }
        for (const QKeySequence& sequence : sequences) {
            if (sequence.isEmpty()) {
                continue;
            }
            bool known = false;
            for (const QShortcut* existing : *target) {
                if (existing->key() == sequence) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            auto* shortcut = new QShortcut(sequence, this);
            // Inactive outside this page: focus must be on the page or one of its rows.
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut, &QShortcut::activated, this, slot);
            target->push_back(shortcut);
        }
    };
    add(QKeySequence::Undo, &EditorPage::undo_edit, &undo_shortcuts_);
    add(QKeySequence::Redo, &EditorPage::redo_edit, &redo_shortcuts_);
}

bool EditorPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        // Every row editor installs this filter when it is created. A focused QLineEdit
        // otherwise claims Cmd+Z for its private text history: it rewrites the visible
        // text, the model never hears about it, and the page history stalls. matches()
        // covers every standard binding (Cmd+Z / Cmd+Shift+Z on macOS, Ctrl+Z / Ctrl+Y /
        // Ctrl+Shift+Z elsewhere).
        if (key->matches(QKeySequence::Undo)) {
            undo_edit();
            return true;
        }
        if (key->matches(QKeySequence::Redo)) {
            redo_edit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditorPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event == nullptr) {
        return;
    }
    if (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange &&
        event->type() != QEvent::FontChange) {
        return;
    }
    // Deferred: the row editors receive their own change event in the same pass and a
    // skin widget re-derives font and palette there, so re-applying the emphasis from the
    // current draft has to wait until that has settled. The row height depends on the new
    // font too, so it is re-solved in the same deferred pass.
    QTimer::singleShot(0, this, [this]() {
        refresh_row_emphasis();
        sync_row_heights();
        refresh_announcement_gaps();
    });
}

void EditorPage::sync_row_heights() {
    // Collect the marked row containers first, then measure every participating control
    // before pinning anything: reading a control whose height was already pinned by the
    // previous pass would keep the row from growing back after a font change.
    QList<QWidget*> rows;
    for (QWidget* candidate : findChildren<QWidget*>()) {
        if (candidate != nullptr && candidate->property(kRowMarker).toBool()) {
            rows.push_back(candidate);
        }
    }
    if (rows.isEmpty()) {
        return;
    }
    int height = kRowHeight;
    for (QWidget* row : rows) {
        const QList<QWidget*> controls =
            row->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* control : controls) {
            if (sizes_with_row(control)) {
                height = std::max(height, control->sizeHint().height());
            }
        }
    }
    for (QWidget* row : rows) {
        const QList<QWidget*> controls =
            row->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* control : controls) {
            if (sizes_with_row(control)) {
                control->setFixedHeight(height);
            }
        }
        row->setFixedHeight(height + 2 * kRowMarginV);
    }
    // The announcement list keeps its intended window (four entries plus the ghost row)
    // at the measured row height, and opens one more row before it starts to scroll, so a
    // slightly longer list is never clipped by the old fixed pixel height.
    if (announcement_scroll_ != nullptr && announcement_page_ != nullptr) {
        const int visible = std::clamp(announcement_page_->count(), 5, 6);
        announcement_scroll_->setFixedHeight(visible * (height + 2 * kRowMarginV) + 22);
    }
}

void EditorPage::refresh_announcement_gaps() {
    const int padding = announcement_check_padding(controller_->data().settings.ui_scale);
    for (QSpacerItem* gap : announcement_gaps_) {
        if (gap != nullptr) {
            gap->changeSize(padding, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
    }
    if (announcement_page_ != nullptr) {
        announcement_page_->invalidate();
    }
}

void EditorPage::refresh_row_emphasis() {
    if (!draft_ready_) {
        return;
    }
    const std::vector<dhw::Announcement>& items = controller_->data().announcements;
    const std::size_t announcements = std::min(announcement_inputs_.size(), items.size());
    for (std::size_t index = 0; index < announcements; ++index) {
        apply_row_emphasis(announcement_inputs_[index], items[index].done, false);
    }
    const std::vector<dhw::Slot> periods =
        dhw::effective_schedule(controller_->data(), to_std(current_date_));
    const std::size_t rendered = std::min<std::size_t>(schedule_inputs_.size(), periods.size());
    for (std::size_t period = 0; period < rendered; ++period) {
        apply_row_emphasis(schedule_inputs_[period], false, periods[period].changed);
    }
}

bool EditorPage::page_has_focus() const {
    if (!isVisible()) {
        return false;
    }
    const QWidget* focus = QApplication::focusWidget();
    // A null focus widget still means the keyboard belongs to this window; a widget that
    // sits outside the page means some other view owns the shortcut.
    return focus == nullptr || focus == this || isAncestorOf(focus);
}

void EditorPage::restore_edit_focus() {
    // The rebuild destroyed the row that had the keyboard. Hand it back to a row of this
    // page: with the focus outside, the page scoped shortcut would stop firing and a
    // second Cmd+Z would do nothing.
    QLineEdit* target = nullptr;
    if (!homework_input_order_.empty()) {
        target = homework_input_order_.front();
    } else if (announcement_ghost_ != nullptr) {
        target = announcement_ghost_;
    } else if (!speech_inputs_.empty()) {
        target = speech_inputs_.front();
    }
    if (target != nullptr) {
        target->setFocus(Qt::OtherFocusReason);
    }
}

void EditorPage::undo_edit() {
    if (!page_has_focus()) {
        return;
    }
    // Text typed in the last 300 ms is not in the model yet. Commit it first so this
    // undo reverts that typing (a new edit also clears the redo branch, as usual).
    flush_pending();
    if (!controller_->can_undo()) {
        return;
    }
    // From here the draft describes the pre-undo data. Invalidate it before the
    // controller broadcasts changed(), which reloads this page from the restored data.
    draft_ready_ = false;
    controller_->undo();
    if (!draft_ready_) {
        reload_all();
    }
    restore_edit_focus();
}

void EditorPage::redo_edit() {
    if (!page_has_focus()) {
        return;
    }
    flush_pending();
    if (!controller_->can_redo()) {
        return;
    }
    draft_ready_ = false;
    controller_->redo();
    if (!draft_ready_) {
        reload_all();
    }
    restore_edit_focus();
}

void EditorPage::build_cards() {
    auto* grid = new QGridLayout(this);
    grid->setSizeConstraint(QLayout::SetNoConstraint);
    grid->setContentsMargins(16, 12, 16, 12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    // The edit rows are the widest content on the page (index, subject, text, 要交 and
    // four row actions), so the left column gets the larger share of the spare width.
    grid->setColumnStretch(0, 160);
    grid->setColumnStretch(1, 100);
    grid->setColumnStretch(2, 0);
    grid->setColumnMinimumWidth(2, 300);

    announcement_card_ = new EditorCard(
        dhw::icons::Icon::Megaphone, QStringLiteral("公告"),
        QStringLiteral("所有日期共用这一份公告；勾选完成后放映不再显示；回车切换完成，文字可直接修改"),
        this);
    auto* announcement_wrap = new QWidget();
    announcement_page_ = new QVBoxLayout(announcement_wrap);
    announcement_page_->setContentsMargins(0, 0, 0, 0);
    announcement_page_->setSpacing(0);
    announcement_scroll_ = make_card_scroll(announcement_wrap);
    // Room for four entries plus the trailing empty row, so the list does not start
    // scrolled with its first row sliced by the card edge.
    announcement_scroll_->setFixedHeight(kRowHeight * 5 + 22);
    announcement_card_->body_layout()->addWidget(announcement_scroll_);
    grid->addWidget(announcement_card_, 0, 0, 1, 2);

    homework_card_ = new EditorCard(
        dhw::icons::Icon::Book, QStringLiteral("家庭作业"),
        QStringLiteral("一个学科可加多条，每条单独勾「要交」；回车加一条，Ctrl+回车切换要交；留空不显示"),
        this);
    auto* homework_wrap = new QWidget();
    homework_page_ = new QVBoxLayout(homework_wrap);
    homework_page_->setContentsMargins(0, 0, 0, kRowSlack);
    homework_page_->setSpacing(0);
    homework_scroll_ = make_card_scroll(homework_wrap);
    homework_card_->body_layout()->addWidget(homework_scroll_);
    grid->addWidget(homework_card_, 1, 0);

    auto* right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);

    schedule_card_ = new EditorCard(
        dhw::icons::Icon::Calendar, QStringLiteral("今日课表"),
        QStringLiteral("修改自动标红，改回模版自动取消；回车跳下一节"), this);
    auto* edit_button = make_fluent_action_button(QStringLiteral("修改周课表模版"), QString(),
                                                  false, schedule_card_);
    edit_button->setToolTip(QStringLiteral("编辑每周一到周日的课表模版"));
    connect(edit_button, &QAbstractButton::clicked, this, &EditorPage::open_schedule_dialog);
    schedule_card_->add_header_widget(edit_button);
    auto* schedule_wrap = new QWidget();
    schedule_grid_ = new QGridLayout(schedule_wrap);
    schedule_grid_->setContentsMargins(0, 0, 0, 0);
    schedule_grid_->setHorizontalSpacing(14);
    // The rows now carry their own 2 px margins; a small vertical gap keeps the five
    // periods of a column inside the card without a scrollbar at the default window size.
    schedule_grid_->setVerticalSpacing(2);
    schedule_scroll_ = make_card_scroll(schedule_wrap);
    schedule_card_->body_layout()->addWidget(schedule_scroll_);
    right->addWidget(schedule_card_, 1);

    speech_card_ = new EditorCard(
        dhw::icons::Icon::Microphone, QStringLiteral("演讲提醒"),
        QStringLiteral("按下一上学日课表预填学科；同学科多学号用空格分隔；回车补一个学科"), this);

    // The warning used to be a hand drawn red block with a border and a corner radius;
    // it is now the skin's own warning glyph plus a bold sentence, no frame at all. The
    // glyph is drawn by the shell (see warning_pixmap) instead of coming from the
    // platform style, so it survives a skin without a standard icon provider.
    speech_warning_ = new QWidget(speech_card_);
    auto* warn_layout = new QHBoxLayout(speech_warning_);
    warn_layout->setContentsMargins(0, 2, 0, 2);
    warn_layout->setSpacing(6);
    auto* warn_icon = new QLabel(speech_warning_);
    warn_icon->setFixedSize(16, 16);
    warn_icon->setPixmap(warning_pixmap(16, danger_color()));
    warn_layout->addWidget(warn_icon);
    auto* warn = new QLabel(QStringLiteral("演讲提醒未填写，请填写下一上学日的学号"),
                            speech_warning_);
    QFont warn_font = warn->font();
    warn_font.setBold(true);
    warn->setFont(warn_font);
    warn_layout->addWidget(warn);
    warn_layout->addStretch(1);
    speech_card_->body_layout()->addWidget(speech_warning_);
    auto* speech_wrap = new QWidget();
    speech_page_ = new QVBoxLayout(speech_wrap);
    speech_page_->setContentsMargins(0, 0, 0, kRowSlack);
    speech_page_->setSpacing(0);
    speech_scroll_ = make_card_scroll(speech_wrap);
    speech_card_->body_layout()->addWidget(speech_scroll_, 1);
    right->addWidget(speech_card_, 1);

    // The preview column spans both card rows on the far right. It is the same section
    // card as the four edit cards; its only custom parts are the 16:9 thumbnail and the
    // drag gesture.
    preview_card_ = new EditorCard(dhw::icons::Icon::Check, QStringLiteral("预览"), QString(),
                                   this);
    preview_card_->setMinimumWidth(280);
    QVBoxLayout* preview_layout = preview_card_->body_layout();
    preview_ = new PreviewLabel(preview_card_);
    preview_->drag_file = [this]() { return preview_file_for_drag(); };
    preview_->zoom = [this]() { open_preview_zoom(); };
#ifdef DHW_UI_FLUENT
    // A thumbnail of a white slide on a white card needs its own hairline surface, or it
    // reads as empty space.
    auto* thumbnail = new fluent::layout::Card(preview_card_);
    thumbnail->setAppearance(fluent::layout::Card::LayerAlt);
    thumbnail->setBorderVisible(true);
    auto* thumbnail_layout = new QVBoxLayout(thumbnail);
    thumbnail_layout->setContentsMargins(3, 3, 3, 3);
    thumbnail_layout->addWidget(preview_);
    preview_layout->addWidget(thumbnail);
#else
    preview_layout->addWidget(preview_);
#endif
    auto* preview_buttons = new QHBoxLayout();
    preview_buttons->setSpacing(8);
    auto* export_button = make_fluent_action_button(QStringLiteral("出图"), QStringLiteral("image"),
                                                    true, preview_card_);
    export_button->setToolTip(QStringLiteral("按当前编辑内容导出整版 1920×1080 PNG"));
    auto* zoom_button = make_fluent_action_button(QStringLiteral("放大"), QString(), false,
                                                  preview_card_);
    zoom_button->setToolTip(QStringLiteral("按原尺寸查看当前预览"));
    preview_buttons->addWidget(export_button);
    preview_buttons->addWidget(zoom_button);
    preview_buttons->addStretch(1);
    preview_layout->addLayout(preview_buttons);
    preview_status_ = make_fluent_label(
        QStringLiteral("编辑后自动刷新；拖动缩略图可交给外部程序"), true, preview_card_);
    preview_status_->setWordWrap(true);
    preview_status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_layout->addWidget(preview_status_);
    preview_layout->addStretch(1);
    connect(export_button, &QAbstractButton::clicked, this, &EditorPage::export_preview);
    connect(zoom_button, &QAbstractButton::clicked, this, &EditorPage::open_preview_zoom);
    grid->addWidget(preview_card_, 0, 2, 2, 1);

    grid->addLayout(right, 1, 1);
    grid->setRowStretch(0, 0);
    grid->setRowStretch(1, 1);
}

QString EditorPage::subject_of(int subject_index) const {
    return subjects_.at(static_cast<std::size_t>(subject_index));
}

QStringList EditorPage::unused_homework_subjects() const {
    QStringList used;
    for (const dhw::Homework& item : draft_.homework) {
        if (from_std(item.text).trimmed().isEmpty()) {
            continue;
        }
        const QString subject = from_std(item.subject);
        if (!subject.isEmpty() && !used.contains(subject)) {
            used << subject;
        }
    }
    QStringList unused;
    for (const QString& subject : subjects_) {
        if (!used.contains(subject)) {
            unused << subject;
        }
    }
    return unused;
}

QStringList EditorPage::unused_speech_subjects() const {
    QStringList used;
    for (const dhw::Speech& item : draft_.speeches) {
        if (from_std(item.students).trimmed().isEmpty()) {
            continue;
        }
        const QString subject = from_std(item.subject);
        if (!subject.isEmpty() && !used.contains(subject)) {
            used << subject;
        }
    }
    QStringList universe = speech_suggestions_;
    for (const dhw::Speech& item : draft_.speeches) {
        const QString subject = from_std(item.subject);
        if (!subject.isEmpty() && !universe.contains(subject)) {
            universe << subject;
        }
    }
    for (const QString& subject : all_subjects()) {
        if (!universe.contains(subject)) {
            universe << subject;
        }
    }
    QStringList unused;
    for (const QString& subject : universe) {
        if (!used.contains(subject)) {
            unused << subject;
        }
    }
    return unused;
}

QString EditorPage::default_homework_ghost_subject() const {
    const QStringList unused = unused_homework_subjects();
    for (const QString& suggestion : homework_suggestions_) {
        if (unused.contains(suggestion)) {
            return suggestion;
        }
    }
    return unused.isEmpty() ? QString() : unused.first();
}

QString EditorPage::default_speech_ghost_subject() const {
    const QStringList unused = unused_speech_subjects();
    for (const QString& suggestion : speech_suggestions_) {
        if (unused.contains(suggestion)) {
            return suggestion;
        }
    }
    return unused.isEmpty() ? QString() : unused.first();
}

dhw::Day EditorPage::pruned_draft() const {
    dhw::Day day = draft_;
    day.homework.erase(std::remove_if(day.homework.begin(), day.homework.end(),
                                      [](const dhw::Homework& item) {
                                          return from_std(item.text).trimmed().isEmpty();
                                      }),
                       day.homework.end());
    day.speeches.erase(std::remove_if(day.speeches.begin(), day.speeches.end(),
                                      [](const dhw::Speech& item) {
                                          return from_std(item.students).trimmed().isEmpty();
                                      }),
                       day.speeches.end());
    return day;
}

void EditorPage::reload_all() {
    const QString controller_date = controller_->current_date();
    if (draft_ready_ && controller_date == current_date_) {
        // Same page day: the pending draft belongs to the data about to be re-read, so
        // commit it first. On a day change the draft belongs to the *old* day and
        // AppController::update_day writes to the controller's page day, so committing it
        // here would copy the old rows into the new date; drop it instead. (Every caller
        // that switches the day commits the editor first.)
        flush_pending();
    } else if (debounce_ != nullptr) {
        debounce_->stop();
    }
    current_date_ = controller_date;
    const dhw::Day* day = controller_->data().find_day(to_std(current_date_));
    draft_ = day != nullptr ? *day : dhw::Day();
    if (draft_.schedule_override.size() < static_cast<std::size_t>(kTotalPeriods)) {
        draft_.schedule_override.resize(static_cast<std::size_t>(kTotalPeriods));
    }
    draft_ready_ = true;
    rebuild_cards();
    refresh_preview();
}

void EditorPage::refresh_cards() {
    refresh_badges();
    schedule_preview_refresh();
}

void EditorPage::commit_pending() {
    flush_pending();
}

bool EditorPage::smoke_add_homework(const QString& subject, const QString& text) {
    if (!draft_ready_) {
        return false;
    }
    for (const dhw::Homework& item : draft_.homework) {
        if (from_std(item.subject) == subject && from_std(item.text) == text) {
            return false;
        }
    }
    int subject_index = -1;
    for (int i = 0; i < static_cast<int>(subjects_.size()); ++i) {
        if (subject_of(i) == subject) {
            subject_index = i;
        }
    }
    if (subject_index < 0) {
        return false;
    }
    add_homework_entry(subject_index, text);
    return true;
}

void EditorPage::on_controller_changed() {
    // Every controller update is a potential preview change, including the edits this
    // page makes itself while `applying_` suppresses the card rebuild.
    schedule_preview_refresh();
    if (applying_) {
        return;
    }
    if (controller_->current_date() != current_date_ || !draft_ready_) {
        reload_all();
        return;
    }
    refresh_badges();
    schedule_preview_refresh();
}

void EditorPage::on_debounce() {
    commit(true);
}

void EditorPage::flush_pending() {
    if (debounce_ != nullptr) {
        debounce_->stop();
    }
    if (draft_ready_) {
        commit(true);
    }
}

void EditorPage::commit(bool immediate) {
    if (!draft_ready_) {
        return;
    }
    if (!immediate) {
        debounce_->start();
        return;
    }
    debounce_->stop();
    applying_ = true;

    // Blank homework rows and speech rows without a student number never reach the
    // controller, so they cannot end up in data.json. The draft itself keeps them.
    controller_->update_day(pruned_draft());
    applying_ = false;
    refresh_badges();
    schedule_preview_refresh();
}

void EditorPage::rebuild_cards() {
    template_subjects_.clear();
    for (const dhw::Slot& slot : dhw::effective_schedule(controller_->data(), to_std(current_date_))) {
        template_subjects_ << from_std(slot.name);
    }
    const dhw::ViewModel view = controller_->view_model();
    homework_suggestions_.clear();
    for (const std::string& subject : view.homework_suggestions) {
        const QString text = from_std(subject);
        if (!homework_suggestions_.contains(text)) {
            homework_suggestions_ << text;
        }
    }
    speech_suggestions_.clear();
    for (const std::string& subject : view.speech_suggestions) {
        const QString text = from_std(subject);
        if (!speech_suggestions_.contains(text)) {
            speech_suggestions_ << text;
        }
    }
    subjects_.clear();
    for (const QString& subject : homework_suggestions_) {
        if (std::find(subjects_.begin(), subjects_.end(), subject) == subjects_.end()) {
            subjects_.push_back(subject);
        }
    }
    for (const dhw::Homework& item : draft_.homework) {
        const QString text = from_std(item.subject);
        if (!text.isEmpty() &&
            std::find(subjects_.begin(), subjects_.end(), text) == subjects_.end()) {
            subjects_.push_back(text);
        }
    }
    for (const QString& subject : all_subjects()) {
        if (std::find(subjects_.begin(), subjects_.end(), subject) == subjects_.end()) {
            subjects_.push_back(subject);
        }
    }
    refresh_completer_subjects();
    rebuild_announcement_rows();
    rebuild_homework_rows();
    rebuild_schedule_rows();
    rebuild_speech_rows();
    refresh_badges();
}

void EditorPage::refresh_completer_subjects() {
    completer_subjects_.clear();
    const auto add = [this](const QString& subject) {
        const QString cleaned = subject.trimmed();
        if (!cleaned.isEmpty() && !completer_subjects_.contains(cleaned)) {
            completer_subjects_ << cleaned;
        }
    };
    // Fixed school list first, then whatever this data set already contains, so a
    // custom subject still completes after it has been typed once.
    for (const QString& subject : completer_subject_seed()) {
        add(subject);
    }
    for (const dhw::Homework& item : draft_.homework) {
        add(from_std(item.subject));
    }
    for (const dhw::Speech& item : draft_.speeches) {
        add(from_std(item.subject));
    }
    for (const QString& subject : homework_suggestions_) {
        add(subject);
    }
    for (const QString& subject : speech_suggestions_) {
        add(subject);
    }
}

void EditorPage::rebuild_announcement_rows() {
    while (QLayoutItem* item = announcement_page_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    announcement_inputs_.clear();
    // The old spacers die with their rows; only the rebuilt ones may be touched later.
    announcement_gaps_.clear();
    const std::vector<dhw::Announcement>& items = controller_->data().announcements;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        create_announcement_row(i);
    }
    create_announcement_ghost();
    sync_row_heights();
    QTimer::singleShot(0, this, [this]() {
        if (announcement_scroll_ != nullptr) {
            QScrollBar* bar = announcement_scroll_->verticalScrollBar();
            bar->setValue(bar->maximum());
        }
    });
}

void EditorPage::create_announcement_row(int index) {
    const std::vector<dhw::Announcement>& items = controller_->data().announcements;
    const dhw::Announcement item = items.at(static_cast<std::size_t>(index));
    auto* row = new QWidget();
    row->setProperty(kRowMarker, true);
    row->setMinimumHeight(kRowHeight);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setSpacing(kRowSpacing);
    auto* check = make_check(QString(), row);
    check->setChecked(item.done);
    check->setToolTip(QStringLiteral("完成（回车切换）"));
    layout->addWidget(check);
    // A little air between the tick and the text. Only the announcement rows carry this
    // extra gap; every other card keeps the shared row spacing.
    add_announcement_gap(layout, controller_->data().settings.ui_scale, announcement_gaps_);
    auto* line = make_row_edit(QString(), row);
    line->installEventFilter(this);
    set_row_text(line, from_std(item.text));
    // Finished announcements are struck through with the widget font instead of a
    // stylesheet "text-decoration" rule.
    apply_row_emphasis(line, item.done, false);
    announcement_inputs_.push_back(line);
    layout->addWidget(line, 1);
    const long long id = item.id;
    connect(line, &QLineEdit::textEdited, this, [this, id](const QString& text) {
        std::vector<dhw::Announcement> updated = controller_->data().announcements;
        for (dhw::Announcement& entry : updated) {
            if (entry.id == id) {
                entry.text = to_std(text);
            }
        }
        applying_ = true;
        controller_->update_announcements(updated);
        applying_ = false;
    });
    connect(check, &QCheckBox::toggled, this, [this, id](bool value) {
        std::vector<dhw::Announcement> updated = controller_->data().announcements;
        for (dhw::Announcement& entry : updated) {
            if (entry.id == id) {
                entry.done = value;
            }
        }
        applying_ = true;
        controller_->update_announcements(updated);
        applying_ = false;
        rebuild_announcement_rows();
    });
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(row, up, down, remove, add);
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < static_cast<int>(items.size()));
    connect(up, &QAbstractButton::clicked, this,
            [this, id]() { move_announcement(id, -1); });
    connect(down, &QAbstractButton::clicked, this,
            [this, id]() { move_announcement(id, 1); });
    connect(remove, &QAbstractButton::clicked, this, [this, id]() {
        std::vector<dhw::Announcement> updated;
        for (const dhw::Announcement& entry : controller_->data().announcements) {
            if (entry.id != id) {
                updated.push_back(entry);
            }
        }
        applying_ = true;
        controller_->update_announcements(updated);
        applying_ = false;
        rebuild_announcement_rows();
    });
    connect(add, &QAbstractButton::clicked, this, &EditorPage::focus_announcement_ghost);
    layout->addWidget(actions);
    announcement_page_->addWidget(row);
}

void EditorPage::create_announcement_ghost() {
    auto* row = new QWidget();
    row->setProperty(kRowMarker, true);
    row->setMinimumHeight(kRowHeight);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setSpacing(kRowSpacing);
    auto* check = make_check(QString(), row);
    check->setEnabled(false);
    check->setToolTip(QStringLiteral("保存后即可勾选完成"));
    layout->addWidget(check);
    add_announcement_gap(layout, controller_->data().settings.ui_scale, announcement_gaps_);
    announcement_ghost_ = make_row_edit(QString(), row);
    announcement_ghost_->installEventFilter(this);
    announcement_ghost_->setPlaceholderText(
        QStringLiteral("新公告，回车或点「＋」保存…"));
    layout->addWidget(announcement_ghost_, 1);
    auto submit = [this]() {
        if (announcement_ghost_ == nullptr) {
            return;
        }
        const QString text = announcement_ghost_->text().trimmed();
        if (!text.isEmpty()) {
            add_announcement(text);
        }
    };
    connect(announcement_ghost_, &QLineEdit::returnPressed, this, submit);
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(row, up, down, remove, add);
    // A blank row keeps "+" visible and hides the actions that need a saved entry.
    up->hide();
    down->hide();
    remove->hide();
    connect(add, &QAbstractButton::clicked, this, submit);
    layout->addWidget(actions);
    announcement_page_->addWidget(row);
}

void EditorPage::add_announcement(const QString& text) {
    std::vector<dhw::Announcement> updated = controller_->data().announcements;
    dhw::Announcement item;
    item.id = controller_->data().next_announcement_id();
    item.text = to_std(text);
    updated.push_back(item);
    applying_ = true;
    controller_->update_announcements(updated);
    applying_ = false;
    rebuild_announcement_rows();
    if (announcement_ghost_ != nullptr) {
        announcement_ghost_->setFocus();
    }
}

void EditorPage::focus_announcement_ghost() {
    if (announcement_ghost_ == nullptr) {
        return;
    }
    announcement_ghost_->setFocus();
    announcement_ghost_->setCursorPosition(announcement_ghost_->text().size());
    if (announcement_scroll_ != nullptr) {
        QScrollBar* bar = announcement_scroll_->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void EditorPage::move_announcement(long long id, int delta) {
    std::vector<dhw::Announcement> items = controller_->data().announcements;
    int from = -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (items.at(static_cast<std::size_t>(i)).id == id) {
            from = i;
            break;
        }
    }
    if (from < 0) {
        return;
    }
    const int to = from + delta;
    if (to < 0 || to >= static_cast<int>(items.size())) {
        return;
    }
    std::swap(items.at(static_cast<std::size_t>(from)),
              items.at(static_cast<std::size_t>(to)));
    applying_ = true;
    controller_->update_announcements(items);
    applying_ = false;
    rebuild_announcement_rows();
    schedule_preview_refresh();
}

void EditorPage::create_empty_state(QVBoxLayout* page, const QString& text) {
    auto* label = make_fluent_label(text, true, nullptr);
    label->setContentsMargins(2, 6, 0, 6);
    page->addWidget(label);
}

void EditorPage::rebuild_homework_rows() {
    refresh_completer_subjects();
    while (QLayoutItem* item = homework_page_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    homework_input_order_.clear();
    homework_inputs_.clear();
    homework_ghost_input_ = nullptr;
    homework_ghost_subject_ = nullptr;
    homework_ghost_submit_ = nullptr;

    // Group order: suggested subjects first, then every subject in the order its first
    // real entry appears in the data. A group only exists while it has a real entry.
    QStringList order;
    const auto remember = [&order](const QString& subject) {
        if (!subject.isEmpty() && !order.contains(subject)) {
            order << subject;
        }
    };
    for (const QString& subject : homework_suggestions_) {
        remember(subject);
    }
    for (const dhw::Homework& item : draft_.homework) {
        if (!from_std(item.text).trimmed().isEmpty()) {
            remember(from_std(item.subject));
        }
    }
    for (const QString& subject : order) {
        std::vector<int> indices;
        for (int i = 0; i < static_cast<int>(draft_.homework.size()); ++i) {
            const dhw::Homework& item = draft_.homework.at(static_cast<std::size_t>(i));
            if (from_std(item.subject) == subject && !from_std(item.text).trimmed().isEmpty()) {
                indices.push_back(i);
            }
        }
        for (std::size_t k = 0; k < indices.size(); ++k) {
            create_homework_row(indices.at(k), static_cast<int>(k));
        }
    }
    if (!unused_homework_subjects().isEmpty()) {
        create_homework_ghost();
    } else if (homework_input_order_.empty()) {
        create_empty_state(homework_page_, QStringLiteral("没有可添加的学科"));
    }
    homework_page_->addStretch(1);
    sync_row_heights();
}

void EditorPage::create_homework_row(int draft_index, int display_index) {
    const dhw::Homework item = draft_.homework.at(static_cast<std::size_t>(draft_index));
    const QString subject = from_std(item.subject);
    auto* group = new QWidget();
    group->setProperty(kRowMarker, true);
    group->setMinimumHeight(kRowHeight);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setHorizontalSpacing(kRowSpacing);
    layout->setVerticalSpacing(0);
    layout->setColumnMinimumWidth(0, kIndexWidth);
    layout->setColumnMinimumWidth(1, kSubjectWidth);
    layout->setColumnMinimumWidth(4, kActionCellWidth);
    layout->setColumnStretch(2, 1);
    auto* index = new ClickableLabel(group);
    index->setText(QString::number(display_index + 1));
    index->setFixedWidth(kIndexWidth);
    index->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    index->setCursor(Qt::PointingHandCursor);
    index->setToolTip(QStringLiteral("点击聚焦这一行"));
    layout->addWidget(index, 0, 0);
    auto* label = new ClickableLabel(subject + QStringLiteral("："), group);
    label->setFixedWidth(kSubjectWidth);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setCursor(Qt::PointingHandCursor);
    label->setToolTip(QStringLiteral("点击聚焦这一行"));
    layout->addWidget(label, 0, 1);
    auto* line = make_row_edit(QString(), group);
    line->installEventFilter(this);
    line->setPlaceholderText(QStringLiteral("作业内容…"));
    set_row_text(line, from_std(item.text));
    layout->addWidget(line, 0, 2);
    auto* check = make_check(QStringLiteral("要交"), group);
    check->setChecked(item.submit_next);
    layout->addWidget(check, 0, 3);
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(group, up, down, remove, add);
    layout->addWidget(actions, 0, 4);

    // Up/down only move within the same subject group; the rendered order is grouped by
    // subject, so crossing a group boundary would look like a jump, not a move.
    std::vector<int> siblings;
    for (int i = 0; i < static_cast<int>(draft_.homework.size()); ++i) {
        const dhw::Homework& other = draft_.homework.at(static_cast<std::size_t>(i));
        if (from_std(other.subject) == subject && !from_std(other.text).trimmed().isEmpty()) {
            siblings.push_back(i);
        }
    }
    const auto position = std::find(siblings.begin(), siblings.end(), draft_index);
    up->setEnabled(position != siblings.end() && position != siblings.begin());
    down->setEnabled(position != siblings.end() && position + 1 != siblings.end());

    index->clicked = [line]() { line->setFocus(Qt::MouseFocusReason); };
    label->clicked = [line]() { line->setFocus(Qt::MouseFocusReason); };
    connect(line, &QLineEdit::textEdited, this, [this, draft_index](const QString& text) {
        if (draft_index < 0 || draft_index >= static_cast<int>(draft_.homework.size())) {
            return;
        }
        draft_.homework.at(static_cast<std::size_t>(draft_index)).text = to_std(text);
        commit(false);
    });
    connect(line, &QLineEdit::returnPressed, this, [this, subject, check]() {
        const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
        if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
            check->setChecked(!check->isChecked());
            return;
        }

        // A blank entry is not rendered any more, so "add one more" hands the keyboard
        // to the single ghost row with this subject preselected.
        focus_homework_ghost(subject);
    });
    connect(check, &QCheckBox::toggled, this, [this, draft_index](bool value) {
        if (draft_index < 0 || draft_index >= static_cast<int>(draft_.homework.size())) {
            return;
        }
        draft_.homework.at(static_cast<std::size_t>(draft_index)).submit_next = value;
        commit(true);
    });
    connect(up, &QAbstractButton::clicked, this,
            [this, draft_index]() { move_homework(draft_index, -1); });
    connect(down, &QAbstractButton::clicked, this,
            [this, draft_index]() { move_homework(draft_index, 1); });
    connect(remove, &QAbstractButton::clicked, this,
            [this, draft_index]() { remove_homework(draft_index); });
    connect(add, &QAbstractButton::clicked, this,
            [this, subject]() { focus_homework_ghost(subject); });
    homework_input_order_.push_back(line);
    homework_inputs_.insert(draft_index, line);
    homework_page_->addWidget(group);
}

void EditorPage::create_homework_ghost() {
    auto* group = new QWidget();
    group->setProperty(kRowMarker, true);
    group->setMinimumHeight(kRowHeight);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setHorizontalSpacing(kRowSpacing);
    layout->setVerticalSpacing(0);
    layout->setColumnMinimumWidth(0, kIndexWidth);
    layout->setColumnMinimumWidth(1, kSubjectWidth);
    layout->setColumnMinimumWidth(4, kActionCellWidth);
    layout->setColumnStretch(2, 1);
    auto* box = make_fluent_combo(group);
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);

    // Spans the index and subject columns of the rows above, so the content field of
    // this row starts on the same vertical line as every other content field.
    box->setFixedWidth(kIndexWidth + kRowSpacing + kSubjectWidth);
    box->addItems(unused_homework_subjects());
    attach_subject_completer(box, completer_subjects_);
    {
        const QSignalBlocker blocker(box);
        const QString current = homework_ghost_subject_text_.isEmpty()
                                    ? default_homework_ghost_subject()
                                    : homework_ghost_subject_text_;
        box->setCurrentText(current);
    }
    layout->addWidget(box, 0, 0, 1, 2);
    homework_ghost_subject_ = box;
    auto* line = make_row_edit(QString(), group);
    line->installEventFilter(this);
    line->setPlaceholderText(QStringLiteral("作业内容…"));
    layout->addWidget(line, 0, 2);
    homework_ghost_input_ = line;
    auto* check = make_check(QStringLiteral("要交"), group);
    {
        const QSignalBlocker blocker(check);
        check->setChecked(homework_ghost_submit_state_);
    }
    layout->addWidget(check, 0, 3);
    homework_ghost_submit_ = check;

    // A blank row keeps "+" visible and hides the actions that need a saved entry.
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(group, up, down, remove, add);
    up->hide();
    down->hide();
    remove->hide();
    connect(add, &QAbstractButton::clicked, this, [this]() {
        if (homework_ghost_input_ == nullptr) {
            return;
        }
        if (!homework_ghost_input_->text().trimmed().isEmpty()) {
            submit_homework_ghost();
            return;
        }
        homework_ghost_input_->setFocus(Qt::MouseFocusReason);
    });
    layout->addWidget(actions, 0, 4);
    connect(box, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        homework_ghost_subject_text_ = text;
    });
    connect(check, &QCheckBox::toggled, this,
            [this](bool value) { homework_ghost_submit_state_ = value; });
    connect(line, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!text.trimmed().isEmpty()) {
            submit_homework_ghost();
        }
    });
    connect(line, &QLineEdit::returnPressed, this, [this]() { submit_homework_ghost(); });
    homework_page_->addWidget(group);
}

void EditorPage::submit_homework_ghost() {
    if (homework_ghost_input_ == nullptr) {
        return;
    }
    const QString text = homework_ghost_input_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    QString subject = homework_ghost_subject_ != nullptr
                          ? homework_ghost_subject_->currentText().trimmed()
                          : QString();
    if (subject.isEmpty()) {
        subject = default_homework_ghost_subject();
    }
    if (subject.isEmpty()) {
        return;
    }
    int subject_index = -1;
    for (int i = 0; i < static_cast<int>(subjects_.size()); ++i) {
        if (subject_of(i) == subject) {
            subject_index = i;
        }
    }
    if (subject_index < 0) {
        subjects_.push_back(subject);
        subject_index = static_cast<int>(subjects_.size()) - 1;
    }
    const bool submit_next =
        homework_ghost_submit_ != nullptr && homework_ghost_submit_->isChecked();

    // The ghost clears itself: the subject falls back to the first unused suggestion and
    // the submit flag resets, exactly like the next empty row that is appended after it.
    homework_ghost_subject_text_.clear();
    homework_ghost_submit_state_ = false;
    add_homework_entry(subject_index, text, submit_next);
}

void EditorPage::focus_homework_ghost(const QString& subject) {
    if (!subject.isEmpty()) {
        homework_ghost_subject_text_ = subject;
    }
    rebuild_homework_rows();
    QTimer::singleShot(0, this, [this]() {
        if (homework_ghost_input_ != nullptr) {
            homework_ghost_input_->setFocus();
            homework_ghost_input_->setCursorPosition(homework_ghost_input_->text().size());
        }
    });
}

void EditorPage::add_homework_entry(int subject_index, const QString& text, bool submit_next) {
    if (subject_index < 0 || subject_index >= static_cast<int>(subjects_.size())) {
        return;
    }
    const QString subject = subject_of(subject_index);
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {

        // A subject without content is not an entry at all: it is never stored and never
        // rendered, so "add" only re-renders the card.
        commit(true);
        rebuild_homework_rows();
        return;
    }
    int target = -1;
    for (int i = 0; i < static_cast<int>(draft_.homework.size()); ++i) {
        const dhw::Homework& item = draft_.homework.at(static_cast<std::size_t>(i));
        if (from_std(item.subject) != subject) {
            continue;
        }
        if (from_std(item.text).trimmed().isEmpty()) {
            target = i;  // reuse a blank (invisible) entry instead of growing the draft
            break;
        }
        if (target < 0 && from_std(item.text) == cleaned) {
            target = i;
        }
    }
    if (target < 0) {
        dhw::Homework item;
        item.subject = to_std(subject);
        item.text = to_std(cleaned);
        item.submit_next = submit_next;
        draft_.homework.push_back(item);
        target = static_cast<int>(draft_.homework.size()) - 1;
    } else {
        draft_.homework.at(static_cast<std::size_t>(target)).text = to_std(cleaned);
        if (submit_next) {
            draft_.homework.at(static_cast<std::size_t>(target)).submit_next = true;
        }
    }
    commit(true);
    rebuild_homework_rows();
    const int focus_index = target;
    QTimer::singleShot(0, this, [this, focus_index]() {
        QLineEdit* widget = homework_inputs_.value(focus_index);
        if (widget == nullptr) {
            widget = homework_ghost_input_;
        }
        if (widget != nullptr) {
            widget->setFocus();
            widget->setCursorPosition(widget->text().size());
        }
    });
}

void EditorPage::remove_homework(int draft_index) {
    if (draft_index < 0 || draft_index >= static_cast<int>(draft_.homework.size())) {
        return;
    }
    draft_.homework.erase(draft_.homework.begin() + draft_index);
    commit(true);
    rebuild_homework_rows();
}

void EditorPage::move_homework(int draft_index, int delta) {
    if (draft_index < 0 || draft_index >= static_cast<int>(draft_.homework.size())) {
        return;
    }
    const dhw::Homework moved = draft_.homework.at(static_cast<std::size_t>(draft_index));
    const QString subject = from_std(moved.subject);
    std::vector<int> siblings;
    for (int i = 0; i < static_cast<int>(draft_.homework.size()); ++i) {
        const dhw::Homework& other = draft_.homework.at(static_cast<std::size_t>(i));
        if (from_std(other.subject) == subject && !from_std(other.text).trimmed().isEmpty()) {
            siblings.push_back(i);
        }
    }
    const auto position = std::find(siblings.begin(), siblings.end(), draft_index);
    if (position == siblings.end()) {
        return;
    }
    const int target_position = static_cast<int>(position - siblings.begin()) + delta;
    if (target_position < 0 || target_position >= static_cast<int>(siblings.size())) {
        return;
    }
    const int target = siblings.at(static_cast<std::size_t>(target_position));
    std::swap(draft_.homework.at(static_cast<std::size_t>(draft_index)),
              draft_.homework.at(static_cast<std::size_t>(target)));
    commit(true);
    rebuild_homework_rows();
    QTimer::singleShot(0, this, [this, target]() {
        QLineEdit* widget = homework_inputs_.value(target);
        if (widget != nullptr) {
            widget->setFocus();
            widget->setCursorPosition(widget->text().size());
        }
    });
}

void EditorPage::rebuild_schedule_rows() {
    while (QLayoutItem* item = schedule_grid_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    schedule_inputs_.clear();
    schedule_inputs_.resize(static_cast<std::size_t>(kTotalPeriods), nullptr);
    for (int period = 0; period < kTotalPeriods; ++period) {
        create_schedule_row(period);
    }
    sync_row_heights();
}

void EditorPage::create_schedule_row(int period) {
    const std::vector<dhw::Slot> periods =
        dhw::effective_schedule(controller_->data(), to_std(current_date_));
    const dhw::Slot& slot = periods.at(static_cast<std::size_t>(period));
    const QString template_text =
        period < template_subjects_.size() ? template_subjects_.at(period) : QString();
    const bool has_override =
        period < static_cast<int>(draft_.schedule_override.size()) &&
        draft_.schedule_override.at(static_cast<std::size_t>(period)).has_value();
    auto* row = new QWidget();
    row->setProperty(kRowMarker, true);
    row->setMinimumHeight(kRowHeight);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setSpacing(kRowSpacing);
    auto* label = new ClickableLabel(row);
    label->setText(QString::number(period + 1));
    label->setFixedWidth(14);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setCursor(Qt::PointingHandCursor);
    label->setToolTip(QStringLiteral("点击聚焦这一节"));
    layout->addWidget(label);
    auto* line = make_row_edit(QString(), row);
    line->installEventFilter(this);
    label->clicked = [line]() { line->setFocus(Qt::MouseFocusReason); };
    if (has_override) {
        set_row_text(line,
                     from_std(*draft_.schedule_override.at(static_cast<std::size_t>(period))));
    }
    line->setPlaceholderText(template_text.isEmpty() ? QStringLiteral("（空）") : template_text);
    if (slot.changed) {
        // A swapped period keeps the red the week legend promises, plus a bold face. Both
        // come from the widget font and the live palette, not from a stylesheet rule.
        apply_row_emphasis(line, false, true);
        line->setToolTip(QStringLiteral("这一节是调课"));
    }
    layout->addWidget(line, 1);
    connect(line, &QLineEdit::textEdited, this, [this, period, template_text](const QString& text) {
        if (draft_.schedule_override.size() < static_cast<std::size_t>(kTotalPeriods)) {
            draft_.schedule_override.resize(static_cast<std::size_t>(kTotalPeriods));
        }
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed == template_text) {
            draft_.schedule_override.at(static_cast<std::size_t>(period)) = std::nullopt;
        } else {
            draft_.schedule_override.at(static_cast<std::size_t>(period)) = to_std(trimmed);
        }
        commit(true);
        rebuild_schedule_rows();
    });
    connect(line, &QLineEdit::returnPressed, this, [this, period]() {
        const int next = period + 1;
        if (next < kTotalPeriods && next < static_cast<int>(schedule_inputs_.size())) {
            QLineEdit* widget = schedule_inputs_.at(static_cast<std::size_t>(next));
            if (widget != nullptr) {
                widget->setFocus();
                widget->selectAll();
            }
        }
    });
    schedule_inputs_.at(static_cast<std::size_t>(period)) = line;
    const int per_column = static_cast<int>(std::ceil(kTotalPeriods / 2.0));
    const int column = period / per_column;
    const int row_index = period % per_column;
    schedule_grid_->addWidget(row, row_index, column);
}

void EditorPage::rebuild_speech_rows() {
    refresh_completer_subjects();
    while (QLayoutItem* item = speech_page_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    speech_inputs_.clear();
    speech_inputs_by_subject_.clear();
    speech_ghost_ = nullptr;
    speech_ghost_subject_ = nullptr;

    // Only entries that really carry a student number are rendered. Suggested subjects
    // are options of the ghost row, never entries of the draft.
    int rendered = 0;
    for (int i = 0; i < static_cast<int>(draft_.speeches.size()); ++i) {
        if (from_std(draft_.speeches.at(static_cast<std::size_t>(i)).students).trimmed().isEmpty()) {
            continue;
        }
        create_speech_row(i);
        ++rendered;
    }
    if (!unused_speech_subjects().isEmpty()) {
        create_speech_ghost();
    } else if (rendered == 0) {
        create_empty_state(speech_page_, QStringLiteral("没有可添加的学科"));
    }
    speech_page_->addStretch(1);
    sync_row_heights();
    refresh_speech_warning();
}

void EditorPage::create_speech_row(int draft_index) {
    const dhw::Speech item = draft_.speeches.at(static_cast<std::size_t>(draft_index));
    const QString subject = from_std(item.subject);
    auto* row = new QWidget();
    row->setProperty(kRowMarker, true);
    row->setMinimumHeight(kRowHeight);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setSpacing(kRowSpacing);
    auto* label = new ClickableLabel(subject + QStringLiteral("："), row);
    label->setFixedWidth(kSubjectWidth);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setCursor(Qt::PointingHandCursor);
    label->setToolTip(QStringLiteral("点击聚焦这一行"));
    layout->addWidget(label);
    auto* line = make_row_edit(QString(), row);
    line->installEventFilter(this);
    line->setPlaceholderText(QStringLiteral("学号"));
    set_row_text(line, from_std(item.students));
    layout->addWidget(line, 1);
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(row, up, down, remove, add);
    layout->addWidget(actions);

    std::vector<int> siblings;
    for (int i = 0; i < static_cast<int>(draft_.speeches.size()); ++i) {
        if (!from_std(draft_.speeches.at(static_cast<std::size_t>(i)).students)
                 .trimmed()
                 .isEmpty()) {
            siblings.push_back(i);
        }
    }
    const auto position = std::find(siblings.begin(), siblings.end(), draft_index);
    up->setEnabled(position != siblings.end() && position != siblings.begin());
    down->setEnabled(position != siblings.end() && position + 1 != siblings.end());

    label->clicked = [line]() { line->setFocus(Qt::MouseFocusReason); };
    connect(line, &QLineEdit::textEdited, this, [this, draft_index](const QString& text) {
        if (draft_index < 0 || draft_index >= static_cast<int>(draft_.speeches.size())) {
            return;
        }
        draft_.speeches.at(static_cast<std::size_t>(draft_index)).students = to_std(text);
        commit(false);
    });
    connect(line, &QLineEdit::returnPressed, this, [this]() {

        // "Enter adds a subject": the only empty row is the trailing ghost, so the
        // keyboard moves there with the next unused subject preselected.
        focus_speech_ghost(default_speech_ghost_subject());
    });
    connect(up, &QAbstractButton::clicked, this,
            [this, draft_index]() { move_speech(draft_index, -1); });
    connect(down, &QAbstractButton::clicked, this,
            [this, draft_index]() { move_speech(draft_index, 1); });
    connect(remove, &QAbstractButton::clicked, this,
            [this, draft_index]() { remove_speech(draft_index); });
    connect(add, &QAbstractButton::clicked, this,
            [this]() { focus_speech_ghost(default_speech_ghost_subject()); });
    speech_inputs_.push_back(line);
    speech_inputs_by_subject_.insert(subject, line);
    speech_page_->addWidget(row);
}

void EditorPage::create_speech_ghost() {
    auto* row = new QWidget();
    row->setProperty(kRowMarker, true);
    row->setMinimumHeight(kRowHeight);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, kRowMarginV, 0, kRowMarginV);
    layout->setSpacing(kRowSpacing);
    auto* box = make_fluent_combo(row);
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);

    // Exactly as wide as the subject cell of the rows above, so the student field lines
    // up with every other student field.
    box->setFixedWidth(kSubjectWidth);
    box->addItems(unused_speech_subjects());
    attach_subject_completer(box, completer_subjects_);
    {
        const QSignalBlocker blocker(box);
        const QString current = speech_ghost_subject_text_.isEmpty()
                                    ? default_speech_ghost_subject()
                                    : speech_ghost_subject_text_;
        box->setCurrentText(current);
    }
    layout->addWidget(box);
    speech_ghost_subject_ = box;
    auto* line = make_row_edit(QString(), row);
    line->installEventFilter(this);
    line->setPlaceholderText(QStringLiteral("学号"));
    layout->addWidget(line, 1);
    speech_ghost_ = line;

    // A blank row keeps "+" visible and hides the actions that need a saved entry.
    ActionControl* up = nullptr;
    ActionControl* down = nullptr;
    ActionControl* remove = nullptr;
    ActionControl* add = nullptr;
    QWidget* actions = make_action_cell(row, up, down, remove, add);
    up->hide();
    down->hide();
    remove->hide();
    connect(add, &QAbstractButton::clicked, this, [this]() {
        if (speech_ghost_ == nullptr) {
            return;
        }
        if (!speech_ghost_->text().trimmed().isEmpty()) {
            submit_speech_ghost();
            return;
        }
        speech_ghost_->setFocus(Qt::MouseFocusReason);
    });
    layout->addWidget(actions);
    connect(box, &QComboBox::currentTextChanged, this,
            [this](const QString& text) { speech_ghost_subject_text_ = text; });
    connect(line, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!text.trimmed().isEmpty()) {
            submit_speech_ghost();
        }
    });
    connect(line, &QLineEdit::returnPressed, this, [this]() { submit_speech_ghost(); });
    speech_page_->addWidget(row);
}

void EditorPage::submit_speech_ghost() {
    if (speech_ghost_ == nullptr) {
        return;
    }
    const QString students = speech_ghost_->text().trimmed();
    if (students.isEmpty()) {
        return;
    }
    QString subject = speech_ghost_subject_ != nullptr
                          ? speech_ghost_subject_->currentText().trimmed()
                          : QString();
    if (subject.isEmpty()) {
        subject = default_speech_ghost_subject();
    }
    if (subject.isEmpty()) {
        return;
    }

    // The ghost clears itself; the next empty row uses the next unused subject.
    speech_ghost_subject_text_.clear();
    add_speech_entry(subject, students);
}

void EditorPage::focus_speech_ghost(const QString& subject) {
    if (!subject.isEmpty()) {
        speech_ghost_subject_text_ = subject;
    }
    rebuild_speech_rows();
    QTimer::singleShot(0, this, [this]() {
        if (speech_ghost_ != nullptr) {
            speech_ghost_->setFocus();
            speech_ghost_->setCursorPosition(speech_ghost_->text().size());
        }
    });
}

void EditorPage::add_speech_entry(const QString& subject, const QString& students) {
    if (subject.trimmed().isEmpty() || students.trimmed().isEmpty()) {
        return;
    }
    bool merged = false;
    for (dhw::Speech& speech : draft_.speeches) {
        if (from_std(speech.subject) == subject) {
            QStringList parts;
            const QString existing = from_std(speech.students).trimmed();
            if (!existing.isEmpty()) {
                parts << existing;
            }
            parts << students.trimmed();
            speech.students = to_std(parts.join(QStringLiteral(" ")));
            merged = true;
            break;
        }
    }
    if (!merged) {
        draft_.speeches.push_back(dhw::Speech{to_std(subject), to_std(students.trimmed())});
    }
    commit(true);
    rebuild_speech_rows();
    QTimer::singleShot(0, this, [this, subject]() {
        QLineEdit* widget = speech_inputs_by_subject_.value(subject);
        if (widget != nullptr) {
            widget->setFocus();
            widget->setCursorPosition(widget->text().size());
        }
    });
}

void EditorPage::remove_speech(int draft_index) {
    if (draft_index < 0 || draft_index >= static_cast<int>(draft_.speeches.size())) {
        return;
    }
    draft_.speeches.erase(draft_.speeches.begin() + draft_index);
    commit(true);
    rebuild_speech_rows();
}

void EditorPage::move_speech(int draft_index, int delta) {
    if (draft_index < 0 || draft_index >= static_cast<int>(draft_.speeches.size())) {
        return;
    }
    std::vector<int> siblings;
    for (int i = 0; i < static_cast<int>(draft_.speeches.size()); ++i) {
        if (!from_std(draft_.speeches.at(static_cast<std::size_t>(i)).students)
                 .trimmed()
                 .isEmpty()) {
            siblings.push_back(i);
        }
    }
    const auto position = std::find(siblings.begin(), siblings.end(), draft_index);
    if (position == siblings.end()) {
        return;
    }
    const int target_position = static_cast<int>(position - siblings.begin()) + delta;
    if (target_position < 0 || target_position >= static_cast<int>(siblings.size())) {
        return;
    }
    const int target = siblings.at(static_cast<std::size_t>(target_position));
    const QString moved_subject =
        from_std(draft_.speeches.at(static_cast<std::size_t>(draft_index)).subject);
    std::swap(draft_.speeches.at(static_cast<std::size_t>(draft_index)),
              draft_.speeches.at(static_cast<std::size_t>(target)));
    commit(true);
    rebuild_speech_rows();
    QTimer::singleShot(0, this, [this, moved_subject]() {
        QLineEdit* widget = speech_inputs_by_subject_.value(moved_subject);
        if (widget != nullptr) {
            widget->setFocus();
            widget->setCursorPosition(widget->text().size());
        }
    });
}

void EditorPage::refresh_badges() {
    int undone = 0;
    for (const dhw::Announcement& item : controller_->data().announcements) {
        if (!item.done) {
            ++undone;
        }
    }
    announcement_card_->set_badge(undone > 0 ? QStringLiteral("%1 条未完成").arg(undone) : QString());
    int homework_items = 0;
    for (const dhw::Homework& item : draft_.homework) {
        if (!from_std(item.text).trimmed().isEmpty()) {
            ++homework_items;
        }
    }
    homework_card_->set_badge(homework_items > 0 ? QStringLiteral("共 %1 条").arg(homework_items)
                                                 : QString());
    int changed = 0;
    for (const dhw::Slot& slot : controller_->view_model().periods) {
        if (slot.changed) {
            ++changed;
        }
    }
    schedule_card_->set_badge(changed > 0 ? QStringLiteral("%1 节调课").arg(changed) : QString());
    const std::string weekday = dhw::weekday_cn(to_std(current_date_));
    QString schedule_title = QStringLiteral("今日课表（%1）").arg(from_std(weekday));
    const Day* day = controller_->data().find_day(to_std(current_date_));
    if (day != nullptr && day->copied_from_weekday.has_value()) {
        schedule_title += QStringLiteral("·调休");
    }
    schedule_card_->set_title(schedule_title);
    refresh_speech_warning();
}

void EditorPage::refresh_speech_warning() {
    if (speech_warning_ == nullptr) {
        return;
    }
    bool empty = true;
    for (const dhw::Speech& speech : draft_.speeches) {
        if (!from_std(speech.students).trimmed().isEmpty()) {
            empty = false;
        }
    }
    speech_warning_->setVisible(empty);
}

void EditorPage::open_schedule_dialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("周课表模版"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    std::vector<std::vector<QLineEdit*>> edits;
    const std::vector<std::vector<std::string>> weekly = controller_->data().weekly_schedule;
    for (int day = 0; day < dhw::kWeekdays; ++day) {
        auto* row = new QWidget(&dialog);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(6);
        std::vector<QLineEdit*> line_edits;
        for (int period = 0; period < dhw::kPeriods; ++period) {
            auto* line = make_row_edit(QString(), row);
            line->setMinimumWidth(74);
            if (day < static_cast<int>(weekly.size()) &&
                period < static_cast<int>(weekly.at(static_cast<std::size_t>(day)).size())) {
                line->setText(from_std(
                    weekly.at(static_cast<std::size_t>(day)).at(static_cast<std::size_t>(period))));
            }
            row_layout->addWidget(line);
            line_edits.push_back(line);
        }
        edits.push_back(line_edits);
        form->addRow(QStringLiteral("周%1").arg(week_days(day)), row);
    }
    layout->addLayout(form);
    // The dialog keeps Qt's modal shell (it is a secondary window, not a page), but its
    // controls are the skin's own; the texts and the accept/reject semantics are the
    // same pair of buttons as the old QDialogButtonBox.
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto* cancel = make_fluent_action_button(QStringLiteral("取消"), QString(), false, &dialog);
    connect(cancel, &QAbstractButton::clicked, &dialog, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* save = make_fluent_action_button(QStringLiteral("保存模版"), QString(), true, &dialog);
    save->setToolTip(QStringLiteral("保存每周一到周日的课表模版"));
    connect(save, &QAbstractButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const std::vector<QLineEdit*>& line_edits : edits) {
        std::vector<std::string> periods;
        for (QLineEdit* line : line_edits) {
            periods.push_back(to_std(line->text().trimmed()));
        }
        rows.push_back(periods);
    }
    applying_ = true;
    controller_->update_weekly_schedule(rows);
    applying_ = false;
    rebuild_cards();
}

void EditorPage::schedule_preview_refresh() {
    if (preview_ == nullptr) {
        return;
    }
    if (preview_timer_ == nullptr) {
        refresh_preview();
        return;
    }
    preview_timer_->start();
}

void EditorPage::refresh_preview() {
    if (preview_ == nullptr) {
        return;
    }
    // One renderer for both the presentation window and this thumbnail, so what the
    // editor shows is exactly what "出图" and the projector produce.
    preview_->set_preview(controller_->render_current_png());
}

void EditorPage::export_preview() {
    // Flush the 300 ms edit debounce first: exporting must include the character that
    // is still sitting in the focused line edit.
    commit_pending();
    const QString path = controller_->export_current_png();
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("出图"),
                             controller_->last_error().isEmpty()
                                 ? QStringLiteral("生成图片失败")
                                 : controller_->last_error());
        return;
    }
    set_label_text(preview_status_,
                   QStringLiteral("已生成：%1").arg(QFileInfo(path).fileName()));
    if (preview_status_ != nullptr) {
        preview_status_->setToolTip(QDir::toNativeSeparators(path));
    }
    schedule_preview_refresh();
}

void EditorPage::open_preview_zoom() {
    // Flush the pending keystroke first, then ask the controller for a fresh frame so
    // the zoom window is never one edit behind the thumbnail.
    commit_pending();
    QImage image = controller_->render_current_png();
    if (image.isNull() && preview_ != nullptr) {
        image = preview_->preview_image();
    }
    if (image.isNull()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("预览（%1 × %2）").arg(image.width()).arg(image.height()));
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);
    auto* image_label = new QLabel(scroll);
    image_label->setPixmap(QPixmap::fromImage(image));
    image_label->setFixedSize(image.size());
    scroll->setWidget(image_label);
    layout->addWidget(scroll, 1);
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto* close = make_fluent_action_button(QStringLiteral("关闭"), QString(), false, &dialog);
    connect(close, &QAbstractButton::clicked, &dialog, &QDialog::reject);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    dialog.resize(1100, 720);
    dialog.exec();
}

QString EditorPage::preview_file_for_drag() {
    // The drag gesture reuses the controller's export path and then places the very same
    // PNG under <data>/images, because an external program cannot read a pixmap.
    commit_pending();
    const QString image_dir_path =
        QDir(controller_->data_dir()).filePath(QStringLiteral("images"));
    const bool have_dir = QDir().mkpath(image_dir_path);
    const QString exported = controller_->export_current_png();
    if (!exported.isEmpty() && have_dir) {
        const QString target = QDir(image_dir_path).filePath(QFileInfo(exported).fileName());
        if (QFileInfo(exported).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
            if (QFileInfo::exists(target)) {
                QFile::remove(target);
            }
            if (QFile::copy(exported, target)) {
                return target;
            }
        } else {
            return target;
        }
    }
    // Fallback for a read-only executable directory: write the rendered frame itself.
    QImage image = preview_ != nullptr ? preview_->preview_image() : QImage();
    if (image.isNull()) {
        image = controller_->render_current_png();
    }
    if (image.isNull() || !have_dir) {
        return exported;
    }
    const QString target = QDir(image_dir_path).filePath(
        from_std(dhw::image_file_name(to_std(current_date_))));
    if (!image.save(target, "PNG")) {
        return exported;
    }
    return target;
}

QLineEdit* EditorPage::first_homework_input() const {
    for (QLineEdit* widget : homework_input_order_) {
        if (widget != nullptr && !widget->isHidden()) {
            return widget;
        }
    }
    return homework_ghost_input_;
}

QLineEdit* EditorPage::first_speech_input() const {
    for (QLineEdit* widget : speech_inputs_) {
        if (widget != nullptr && !widget->isHidden()) {
            return widget;
        }
    }
    return speech_ghost_;
}

QLineEdit* EditorPage::first_schedule_input() const {
    for (QLineEdit* widget : schedule_inputs_) {
        if (widget != nullptr && !widget->isHidden()) {
            return widget;
        }
    }
    return nullptr;
}

}  // namespace dhw::qt

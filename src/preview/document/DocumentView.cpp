#include "DocumentView.h"
#include "DocumentRecords.h"
#include "MarkdownLayout.h"
#include "preview/PreviewProvider.h"
#include <QLabel>
#include <QScopedValueRollback>
#include <QTextImageFormat>
#include <QMenu>
#include <QSet>
#include <QEvent>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QToolButton>
#include <QVBoxLayout>

namespace dzurkel {
namespace {
constexpr int SourceColor = QTextFormat::UserProperty + 1;
qreal luminance(const QColor &color)
{
    const auto linear = [](qreal value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
class ContentDocument : public QTextDocument {
public:
    ContentDocument(std::shared_ptr<const FormattedDocument> content, QObject *parent)
        : QTextDocument(parent), content_(std::move(content)) {}
protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        // Never delegate to Qt's file/URL loader. All retained resources have
        // already been validated and decoded on the worker thread.
        if (type == QTextDocument::ImageResource && content_->images.contains(name.toString()))
            return content_->images.value(name.toString());
        return {};
    }
private:
    std::shared_ptr<const FormattedDocument> content_;
};
class ContentBrowser : public QTextBrowser {
public:
    ContentBrowser(std::shared_ptr<const FormattedDocument> content, QWidget *parent) : QTextBrowser(parent)
    {
        setDocument(new ContentDocument(std::move(content), this));
    }
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
}
DocumentView::DocumentView(std::shared_ptr<const FormattedDocument> document, QWidget *parent)
    : ZoomPreview(parent), view_(new ContentBrowser(document, this))
{
    setObjectName(QStringLiteral("documentPreview"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_->setObjectName(QStringLiteral("documentText"));
    view_->setReadOnly(true);
    view_->setUndoRedoEnabled(false);
    view_->setAcceptDrops(false);
    view_->setOpenLinks(false);
    view_->setOpenExternalLinks(false);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setLineWrapMode(QTextEdit::WidgetWidth);
    baseFont_ = font();
    baseFont_.setPointSizeF(std::max(11.0, baseFont_.pointSizeF()));
    view_->document()->setDefaultFont(baseFont_);
    view_->document()->setDocumentMargin(20);
    auto sections = document->sections;
    if (!document->markdown.isEmpty()) {
        view_->document()->setMarkdown(document->markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
        normalizeMarkdownTables(view_->document());
        if (view_->toPlainText().trimmed().isEmpty()) view_->setPlainText(document->markdown);
        bool firstHeading = true;
        for (auto block = view_->document()->begin(); block.isValid() && sections.size() < 64; block = block.next()) {
            if (!block.blockFormat().headingLevel() || block.text().isEmpty()) continue;
            const auto anchor = firstHeading && block.blockFormat().headingLevel() == 1
                ? QStringLiteral("dzurkel-title-markdown") : QStringLiteral("markdown-%1").arg(sections.size());
            firstHeading = false;
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setAnchor(true);
            format.setAnchorNames({anchor});
            cursor.mergeCharFormat(format);
            sections.append({block.text().left(100), anchor});
        }
    } else view_->setHtml(document->html);
    applyColors(true);
    applyTypography();
    if (sections.size() > 1) {
        auto *contents = new QToolButton(this);
        contents->setObjectName(QStringLiteral("documentContents"));
        contents->setText(QStringLiteral("Contents"));
        contents->setToolTip(QStringLiteral("Sections included in this preview"));
        contents->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(contents);
        for (const auto &section : sections) {
            // QAction interprets '&' as a mnemonic unless escaped.
            auto title = section.title;
            title.replace(QStringLiteral("&"), QStringLiteral("&&"));
            auto *action = menu->addAction(title);
            connect(action, &QAction::triggered, this, [this, anchor = section.anchor] {
                view_->scrollToAnchor(anchor); view_->setFocus();
            });
        }
        contents->setMenu(menu);
        contents_ = contents;
        contents->hide(); // Shown by PreviewWindow in the shared toolbar.
    }
    connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) {
            if (coordinator_) jumpTo(url.fragment());
            else view_->scrollToAnchor(url.fragment());
        }
    });
    // Direct QTextDocument imports leave the browser cursor at the end. Qt
    // reveals that cursor on focus, so reset it before presenting the preview.
    view_->moveCursor(QTextCursor::Start);
    view_->verticalScrollBar()->setValue(0);
    view_->horizontalScrollBar()->setValue(0);
    layout->addWidget(view_);
    setFocusProxy(view_);
}

DocumentView::DocumentView(const PreviewData &data, QWidget *parent) : DocumentView(data.document, parent)
{
    if (!data.lazy) return;
    coordinator_ = data.lazy;
    navigation_ = data.document->navigation;
    // Public Qt frame insertion wraps the imported rich document, including
    // lists/tables. Portion removal then releases its entire native subtree.
    QTextCursor cursor(view_->document());
    cursor.select(QTextCursor::Document);
    auto *frame = cursor.insertFrame(QTextFrameFormat());
    portions_.append({0, frame, data.document});
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("documentLoadingStatus"));
    layout()->addWidget(status_);
    window_ = new LazyWindow(coordinator_, {QStringLiteral("0"), data.lazyPrevious, data.lazyNext, {}, 0}, this);
    watcher_ = new LazyBoundaryWatcher(view_, window_, this);
    connectLazyStatus(window_, status_);
    status_->setText(data.document->notice);
    connect(window_, &LazyWindow::changed, this, &DocumentView::acceptPortion);
    connect(coordinator_.get(), &LazyCoordinator::ready, this, [this](const LazyPortion &portion) {
        if (jump_.isEmpty() || portion.token != jump_) return;
        acceptPortion(portion, {}, 0);
    });
    connect(coordinator_.get(), &LazyCoordinator::failed, this, [this] {
        lazyFailed_ = true;
        jump_.clear(); watcher_->paused = false;
    });
    if (!contents_) {
        contents_ = new QToolButton(this);
        contents_->setObjectName(QStringLiteral("documentContents"));
        contents_->setText(QStringLiteral("Contents"));
        contents_->setPopupMode(QToolButton::InstantPopup);
        contents_->setMenu(new QMenu(contents_));
        contents_->hide();
    }
    updateContents();
    view_->moveCursor(QTextCursor::Start);
    view_->verticalScrollBar()->setValue(0);
}
bool DocumentView::hasAnchor(const QString &anchor) const
{
    for (auto block = view_->document()->begin(); block.isValid(); block = block.next())
        for (auto run = block.begin(); !run.atEnd(); ++run)
            if (run.fragment().charFormat().anchorNames().contains(anchor)) return true;
    return false;
}
void DocumentView::jumpTo(const QString &anchor)
{
    ++layoutRevision_;
    if (hasAnchor(anchor)) {
        if (!jump_.isEmpty()) {
            jump_.clear(); watcher_->paused = false;
            coordinator_->request(window_->firstToken()); // Supersede an unloaded target through the existing generation guard.
        }
        view_->scrollToAnchor(anchor); view_->setFocus(); return;
    }
    if (!coordinator_ || lazyFailed_) return;
    if (anchor.size() > 4096) { status_->setText(QStringLiteral("Link target exceeds the navigation budget.")); return; }
    jump_ = anchor;
    watcher_->paused = true;
    coordinator_->request(anchor);
}
void DocumentView::updateContents()
{
    if (!contents_) return;
    auto *menu = contents_->menu();
    menu->clear();
    QList<DocumentSection> sections = navigation_;
    for (const auto &portion : portions_) sections.append(portion.content->sections);
    QSet<QString> seen;
    for (const auto &section : sections) {
        if (seen.contains(section.anchor)) continue;
        seen.insert(section.anchor);
        auto title = section.title;
        title.replace(QStringLiteral("&"), QStringLiteral("&&"));
        auto *action = menu->addAction(title);
        connect(action, &QAction::triggered, this, [this, anchor = section.anchor] { jumpTo(anchor); });
    }
}
void DocumentView::removePortion(int index)
{
    auto portion = portions_.takeAt(index);
    QTextCursor cursor(view_->document());
    cursor.setPosition(portion.frame->firstPosition() - 1);
    cursor.setPosition(portion.frame->lastPosition() + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    for (auto image = portion.content->images.cbegin(); image != portion.content->images.cend(); ++image)
        view_->document()->addResource(QTextDocument::ImageResource,
            QUrl(QStringLiteral("dzurkel-portion:%1:%2").arg(portion.slot).arg(image.key())), QVariant());
}
void DocumentView::addPortion(std::shared_ptr<const FormattedDocument> content, int direction)
{
    int slot = 0;
    while (std::any_of(portions_.cbegin(), portions_.cend(), [slot](const PortionFrame &p) { return p.slot == slot; })) ++slot;
    QString html = content->html;
    for (auto image = content->images.cbegin(); image != content->images.cend(); ++image) {
        const auto id = QStringLiteral("dzurkel-portion:%1:%2").arg(slot).arg(image.key());
        html.replace(QStringLiteral("src=\"%1\"").arg(image.key()), QStringLiteral("src=\"%1\"").arg(id));
        view_->document()->addResource(QTextDocument::ImageResource, QUrl(id), image.value());
    }
    QTextCursor cursor(view_->document());
    cursor.setPosition(direction < 0 ? 0 : view_->document()->characterCount() - 1);
    auto *frame = cursor.insertFrame(QTextFrameFormat());
    cursor = frame->firstCursorPosition();
    cursor.insertHtml(html);
    applyColors(true, frame);
    applyTypography(frame);
    PortionFrame added{slot, frame, std::move(content)};
    if (direction < 0) portions_.prepend(added);
    else portions_.append(added);
}
void DocumentView::compactFormats(QTextCursor &anchor)
{
    // Qt retains interned formats after deletion. Clear through its public API
    // and reimport only resident sanitized portions, keeping the widget/layout
    // engine and bounded resources. No private Qt format/cache manipulation.
    struct Position { int portion; int offset; };
    const auto locate = [this](int position) {
        for (int i = 0; i < portions_.size(); ++i) {
            const auto *frame = portions_[i].frame.data();
            if (position <= frame->lastPosition()) return Position{i, qMax(0, position - frame->firstPosition())};
        }
        return Position{int(portions_.size()) - 1, portions_.last().frame->lastPosition() - portions_.last().frame->firstPosition()};
    };
    const auto reading = locate(anchor.position());
    const auto selection = view_->textCursor();
    const auto start = locate(selection.anchor()), end = locate(selection.position());
    QList<std::shared_ptr<const FormattedDocument>> retained;
    for (const auto &portion : portions_) retained.append(portion.content);
    const auto font = view_->document()->defaultFont();
    portions_.clear();
    view_->document()->clear();
    view_->document()->setDefaultFont(font);
    for (const auto &content : retained) addPortion(content, 1);
    const auto restore = [this](Position position) {
        const auto *frame = portions_[position.portion].frame.data();
        return qMin(frame->lastPosition(), frame->firstPosition() + position.offset);
    };
    anchor = QTextCursor(view_->document());
    anchor.setPosition(restore(reading));
    QTextCursor restored(view_->document());
    restored.setPosition(restore(start));
    restored.setPosition(restore(end), QTextCursor::KeepAnchor);
    view_->setTextCursor(restored);
}
void DocumentView::acceptPortion(const LazyPortion &portion, const LazyPortion &evicted, int direction)
{
    if (!direction && portion.content.value("kind") == QJsonValue(QStringLiteral("navigation-error"))) {
        status_->setText(portion.content.value("message").toString().left(4096));
        jump_.clear(); watcher_->paused = false;
        return;
    }
    std::shared_ptr<const FormattedDocument> content;
    if (!decodeDocumentPortion(portion.content, content)) {
        coordinator_->stop();
        lazyFailed_ = true;
        status_->setText(QStringLiteral("Invalid document content."));
        jump_.clear(); watcher_->paused = false;
        return;
    }
    watcher_->paused = true;
    const int margin = qRound(view_->document()->documentMargin());
    QTextCursor anchor = view_->cursorForPosition(QPoint(margin, margin + view_->fontMetrics().height() / 2));
    const int top = view_->cursorRect(anchor).top();
    const int horizontal = view_->horizontalScrollBar()->value();
    if (!direction) {
        while (!portions_.isEmpty()) removePortion(0);
        window_->reset(portion);
    }
    addPortion(content, direction);
    if (!evicted.token.isEmpty()) removePortion(direction > 0 ? 0 : portions_.size() - 1);
    if (view_->document()->allFormats().size() > 4096) compactFormats(anchor);
    view_->document()->documentLayout()->documentSize();
    if (direction) view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() + view_->cursorRect(anchor).top() - top);
    else {
        view_->verticalScrollBar()->setValue(0);
        if (!content->targetAnchor.isEmpty()) view_->scrollToAnchor(content->targetAnchor);
        jump_.clear();
    }
    view_->horizontalScrollBar()->setValue(horizontal);
    status_->setText(content->notice);
    updateContents();
    const auto revision = ++layoutRevision_;
    if (direction) {
        const int applied = view_->verticalScrollBar()->value();
        // QTextEdit may finish a fractional-pixel layout/scrollbar adjustment
        // after the edit. Correct the same live cursor once Qt settles, unless
        // another edit or user scroll has superseded this anchor.
        QTimer::singleShot(0, this, [this, revision, anchor, top, applied] {
            if (revision != layoutRevision_ || view_->verticalScrollBar()->value() != applied || !jump_.isEmpty()) return;
            QScopedValueRollback<bool> updating(watcher_->paused, true);
            view_->document()->documentLayout()->documentSize();
            view_->verticalScrollBar()->setValue(applied + view_->cursorRect(anchor).top() - top);
        });
    }
    watcher_->paused = false;
}
QWidget *DocumentView::toolbarWidget() const { return contents_; }
void DocumentView::applyZoom(qreal factor)
{
    ++layoutRevision_;
    factor = std::clamp(factor, .5, maximumZoom());
    auto *scroll = view_->verticalScrollBar();
    const auto ratio = scroll->maximum() > 0 ? qreal(scroll->value()) / scroll->maximum() : 0;
    factor_ = factor;
    auto font = baseFont_;
    font.setPointSizeF(baseFont_.pointSizeF() * factor_);
    view_->document()->setDefaultFont(font);
    applyTypography();
    // Force layout before restoring the approximate reading position.
    view_->document()->documentLayout()->documentSize();
    scroll->setValue(qRound(ratio * scroll->maximum()));
    emit zoomChanged();
}
void DocumentView::zoomBy(int steps) { applyZoom(steppedZoom(steps)); }
void DocumentView::fit() { applyZoom(1.0); }
QString DocumentView::zoomHint() const { return QStringLiteral("Text reflows to the window. Fit restores the default text size; zoom ranges from 50% to 300%."); }
void DocumentView::applyTypography(QTextFrame *portion)
{
    QTextCursor edit(view_->document());
    edit.beginEditBlock();
    const qreal size = baseFont_.pointSizeF() * factor_;
    const auto first = portion ? portion->firstCursorPosition().block() : view_->document()->begin();
    const int end = portion ? portion->lastPosition() : view_->document()->characterCount();
    for (auto block = first; block.isValid() && block.position() <= end; block = block.next()) {
        auto format = block.blockFormat();
        const int heading = format.headingLevel();
        bool title = false, subtitle = false;
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            for (const auto &name : fragment.fragment().charFormat().anchorNames()) {
                title |= name.startsWith(QLatin1String("dzurkel-title-"));
                subtitle |= name.startsWith(QLatin1String("dzurkel-subtitle-"));
            }
        }
        const qreal scale = title ? 2.0 : subtitle ? 1.2 : heading == 1 ? 1.6 : heading == 2 ? 1.35 : heading == 3 ? 1.18 : heading ? 1.08 : 1.0;
        format.setTopMargin(title ? 0 : heading ? size * 1.1 : 0);
        format.setBottomMargin(title ? size * .8 : heading ? size * .4 : block.textList() ? size * .2 : size * .6);
        format.setLineHeight(115, QTextBlockFormat::ProportionalHeight);
        edit.setPosition(block.position());
        edit.setBlockFormat(format);
        struct Run { int position; int length; QTextCharFormat format; };
        QList<Run> runs;
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const auto run = fragment.fragment();
            runs.append({run.position(), run.length(), run.charFormat()});
        }
        for (auto &run : runs) {
            // Qt's HTML heading size adjustment takes precedence over explicit
            // point sizes. Remove it, preserving every other run property.
            run.format.clearProperty(QTextFormat::FontSizeAdjustment);
            run.format.clearProperty(QTextFormat::FontPixelSize);
            run.format.setFontPointSize(size * scale);
            if (heading || title) run.format.setFontWeight(QFont::Bold);
            edit.setPosition(run.position);
            edit.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
            edit.setCharFormat(run.format);
        }
    }
    edit.endEditBlock();
}
void DocumentView::applyColors(bool rememberSource, QTextFrame *portion)
{
    const auto background = luminance(palette().color(QPalette::Base));
    QTextCursor edit(view_->document());
    edit.beginEditBlock();
    const auto first = portion ? portion->firstCursorPosition().block() : view_->document()->begin();
    const int end = portion ? portion->lastPosition() : view_->document()->characterCount();
    for (auto block = first; block.isValid() && block.position() <= end; block = block.next()) {
        // Snapshot positions first: merging formats can merge/split fragments.
        struct Run { int start; int length; QColor source; };
        QList<Run> runs;
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const auto run = fragment.fragment();
            const auto format = run.charFormat();
            if (format.hasProperty(SourceColor)) runs.append({run.position(), run.length(), format.property(SourceColor).value<QColor>()});
            else if (rememberSource && format.hasProperty(QTextFormat::ForegroundBrush)) runs.append({run.position(), run.length(), format.foreground().color()});
        }
        for (const auto &run : runs) {
            if (!run.source.isValid()) continue;
            const auto source = luminance(run.source);
            const auto contrast = (std::max(source, background) + .05) / (std::min(source, background) + .05);
            QTextCharFormat format;
            format.setProperty(SourceColor, run.source);
            format.setForeground(contrast >= 4.5 ? run.source : palette().color(QPalette::Text));
            edit.setPosition(run.start);
            edit.setPosition(run.start + run.length, QTextCursor::KeepAnchor);
            edit.mergeCharFormat(format);
        }
    }
    edit.endEditBlock();
}
void DocumentView::changeEvent(QEvent *event)
{
    ZoomPreview::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) applyColors();
}
}

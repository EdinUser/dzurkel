#include "DocumentView.h"
#include <QMenu>
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
void normalizeMarkdownTables(QTextDocument *document)
{
    QList<QTextFrame *> frames{document->rootFrame()};
    QTextCursor edit(document);
    edit.beginEditBlock();
    while (!frames.isEmpty()) {
        auto *frame = frames.takeLast();
        frames.append(frame->childFrames());
        auto *table = qobject_cast<QTextTable *>(frame);
        if (!table) continue;
        auto format = table->format();
        format.setCellPadding(4);
        format.setCellSpacing(0);
        table->setFormat(format);
        for (int row = 0; row < table->rows(); ++row) for (int column = 0; column < table->columns(); ++column) {
            auto cell = table->cellAt(row, column);
            auto cellFormat = cell.format().toTableCellFormat();
            cellFormat.setVerticalAlignment(QTextCharFormat::AlignTop);
            cell.setFormat(cellFormat);
            // Qt's Markdown importer can leave an empty leading block in the
            // first header cell. Remove only that synthetic empty paragraph.
            auto first = cell.firstCursorPosition();
            if (first.block().text().isEmpty() && first.block().next().position() <= cell.lastCursorPosition().position()) {
                first.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
                first.removeSelectedText();
            }
            if (!row) {
                auto header = cell.firstCursorPosition();
                header.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                QTextCharFormat bold;
                bold.setFontWeight(QFont::Bold);
                header.mergeCharFormat(bold);
            }
        }
    }
    edit.endEditBlock();
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
        if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) view_->scrollToAnchor(url.fragment());
    });
    // Direct QTextDocument imports leave the browser cursor at the end. Qt
    // reveals that cursor on focus, so reset it before presenting the preview.
    view_->moveCursor(QTextCursor::Start);
    view_->verticalScrollBar()->setValue(0);
    view_->horizontalScrollBar()->setValue(0);
    layout->addWidget(view_);
    setFocusProxy(view_);
}
QWidget *DocumentView::toolbarWidget() const { return contents_; }
void DocumentView::applyZoom(qreal factor)
{
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
void DocumentView::applyTypography()
{
    QTextCursor edit(view_->document());
    edit.beginEditBlock();
    const qreal size = baseFont_.pointSizeF() * factor_;
    for (auto block = view_->document()->begin(); block.isValid(); block = block.next()) {
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
void DocumentView::applyColors(bool rememberSource)
{
    const auto background = luminance(palette().color(QPalette::Base));
    QTextCursor edit(view_->document());
    edit.beginEditBlock();
    for (auto block = view_->document()->begin(); block.isValid(); block = block.next()) {
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

#include "TextPreviewProvider.h"
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "preview/lazy/LazyTextWindow.h"
#include "ui/ZoomPreview.h"
#include "preview/readers/TextDecoder.h"
#include "preview/readers/TextCodec.h"
#include "preview/text/TextHighlighting.h"

namespace dzurkel {
namespace {
QSettings textSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral("dzurkel"), QStringLiteral("dzurkel"));
}
class TextView : public ZoomPreview {
public:
    TextView(const PreviewData &data, QWidget *parent) : ZoomPreview(parent), editor_(new QPlainTextEdit(this))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        editor_->setObjectName(QStringLiteral("textPreview"));
        editor_->setReadOnly(true);
        editor_->setUndoRedoEnabled(false);
        wrap_ = new QToolButton(this);
        wrap_->setObjectName(QStringLiteral("wrapLines"));
        wrap_->setText(QStringLiteral("Wrap lines"));
        wrap_->setToolTip(QStringLiteral("Soft wrap long lines to the preview width"));
        wrap_->setAccessibleName(wrap_->text());
        wrap_->setCheckable(true);
        wrap_->setFixedHeight(30);
        auto settings = textSettings();
        settings.setFallbacksEnabled(false);
        wrap_->setChecked(settings.value(QStringLiteral("previews/wrapLines"), true).toBool());
        editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        editor_->setLineWrapMode(wrap_->isChecked() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        connect(wrap_, &QToolButton::toggled, this, [this](bool enabled) {
            editor_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
            auto settings = textSettings();
            settings.setFallbacksEnabled(false);
            settings.setValue(QStringLiteral("previews/wrapLines"), enabled);
        });
        baseFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (baseFont_.pointSizeF() <= 0) baseFont_.setPointSizeF(11);
        editor_->setFont(baseFont_);
        toolbar_ = wrap_;
        editor_->setPlainText(data.text);
        QString fileName = data.resource.url.fileName();
        if (fileName.isEmpty()) fileName = QFileInfo(data.resource.path).fileName();
        highlightText(editor_, fileName, data.mimeType);
        layout->addWidget(editor_);
        if (data.lazy) {
            auto *status = new QLabel(this);
            status->setObjectName(QStringLiteral("textLoadingStatus"));
            status->setTextFormat(Qt::PlainText);
            status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            status->setFixedHeight(status->fontMetrics().height() + 4);
            layout->addWidget(status);
            auto *lazy = new LazyTextWindow(editor_, status, data.lazy,
                LazyPortion{QStringLiteral("0"), data.lazyPrevious, data.lazyNext,
                    QJsonObject{{"text", data.text}}, 0});
            toolbar_ = new QWidget(this);
            auto *tools = new QHBoxLayout(toolbar_);
            tools->setContentsMargins(0, 0, 0, 0);
            tools->setSpacing(6);
            tools->addWidget(wrap_);
            auto *beginning = new QToolButton(toolbar_);
            beginning->setObjectName(QStringLiteral("textBeginning"));
            beginning->setText(QStringLiteral("Beginning"));
            beginning->setToolTip(QStringLiteral("Show the beginning of the file (Ctrl+Home)"));
            beginning->setAccessibleName(beginning->text());
            auto *end = new QToolButton(toolbar_);
            end->setObjectName(QStringLiteral("textEnd"));
            end->setText(QStringLiteral("End"));
            end->setToolTip(QStringLiteral("Show the final portion of the file (Ctrl+End)"));
            end->setAccessibleName(end->text());
            tools->addWidget(beginning); tools->addWidget(end);
            connect(beginning, &QToolButton::clicked, lazy, &LazyTextWindow::showBeginning);
            connect(end, &QToolButton::clicked, lazy, &LazyTextWindow::showEnd);
        }
        setFocusProxy(editor_);
    }
    void zoomBy(int steps) override { apply(steppedZoom(steps)); }
    void fit() override { apply(1.0); }
    qreal zoomFactor() const override { return factor_; }
    bool isFit() const override { return factor_ == 1.0; }
    qreal maximumZoom() const override { return 3.0; }
    qreal minimumZoom() const override { return .5; }
    QWidget *toolbarWidget() const override { return toolbar_; }
    QString zoomHint() const override { return QStringLiteral("Text size only; Fit restores the default font size. Whitespace and line wrapping are unchanged."); }
private:
    void apply(qreal factor)
    {
        const auto line = editor_->verticalScrollBar()->value();
        factor_ = std::clamp(factor, .5, maximumZoom());
        auto font = baseFont_;
        font.setPointSizeF(baseFont_.pointSizeF() * factor_);
        editor_->setFont(font);
        editor_->verticalScrollBar()->setValue(line);
        emit zoomChanged();
    }
    QPlainTextEdit *editor_;
    QToolButton *wrap_;
    QWidget *toolbar_;
    QFont baseFont_;
    qreal factor_ = 1.0;
};
}
bool TextPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.inherits(QStringLiteral("text/plain"))
        || mime.inherits(QStringLiteral("application/json"))
        || mime.inherits(QStringLiteral("application/xml"))
        || mime.name().endsWith(QStringLiteral("+json"))
        || mime.name().endsWith(QStringLiteral("+xml"));
}
PreviewData TextPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                    const QSize &, const Cancellation &cancel) const
{
    return loadFromDevice(*file, std::move(data), cancel);
}
PreviewData TextPreviewProvider::loadFromDevice(QIODevice &device, PreviewData data, const Cancellation &cancel) const
{
    auto decoded = decodePreviewText(device, ByteLimit, cancel);
    if (decoded.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!decoded.error.isEmpty()) { data.message = decoded.error; return data; }
    data.truncated = decoded.truncated;
    data.detail = decoded.encoding;
    QString text = std::move(decoded.text);
    // QPlainTextEdit treats these as paragraph breaks too. Normalize them
    // before applying the line budget, including standalone classic-Mac CR.
    TextCodec::normalizeLines(text);
    QString bounded;
    bounded.reserve(text.size());
    int lines = 1;
    int column = 0;
    for (auto character : text) {
        if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
        if (character == QChar('\n')) {
            if (++lines > LineLimit) { data.truncated = true; break; }
            column = 0;
            bounded += character;
        } else if (++column <= LineLengthLimit) {
            if ((character.category() == QChar::Other_Control) && character != QChar('\r') && character != QChar('\t')
                && character != QChar('\f')) character = QChar::ReplacementCharacter;
            bounded += character;
        } else if (column == LineLengthLimit + 1) {
            bounded += QStringLiteral(" … [line shortened]");
            data.truncated = true;
        }
    }
    data.text = std::move(bounded);
    data.textLineCount = data.text.isEmpty() ? 0
        : data.text.count(QChar('\n')) + (data.text.endsWith(QChar('\n')) ? 0 : 1);
    data.kind = PreviewKind::Text;
    data.message.clear();
    if (data.truncated) data.message = QStringLiteral("Partial preview — limited to 2 MiB, 20,000 lines, and 16,384 characters per line.");
    return data;
}
QWidget *TextPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new TextView(data, parent);
}
} // namespace dzurkel

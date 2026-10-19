#include "TextPreviewProvider.h"
#include <QFile>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include "ui/ZoomPreview.h"
#include "preview/readers/TextDecoder.h"

namespace dzurkel {
namespace {
class TextView : public ZoomPreview {
public:
    TextView(const QString &text, QWidget *parent) : ZoomPreview(parent), editor_(new QPlainTextEdit(this))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        editor_->setObjectName(QStringLiteral("textPreview"));
        editor_->setReadOnly(true);
        editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
        baseFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (baseFont_.pointSizeF() <= 0) baseFont_.setPointSizeF(11);
        editor_->setFont(baseFont_);
        editor_->setPlainText(text);
        layout->addWidget(editor_);
        setFocusProxy(editor_);
    }
    void zoomBy(int steps) override { apply(steppedZoom(steps)); }
    void fit() override { apply(1.0); }
    qreal zoomFactor() const override { return factor_; }
    bool isFit() const override { return factor_ == 1.0; }
    qreal maximumZoom() const override { return 3.0; }
    qreal minimumZoom() const override { return .5; }
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
    auto decoded = decodePreviewText(*file, ByteLimit, cancel);
    if (decoded.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!decoded.error.isEmpty()) { data.message = decoded.error; return data; }
    data.truncated = decoded.truncated;
    data.detail = decoded.encoding;
    QString text = std::move(decoded.text);
    // QPlainTextEdit treats these as paragraph breaks too. Normalize them
    // before applying the line budget, including standalone classic-Mac CR.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QChar::LineSeparator, QChar('\n'));
    text.replace(QChar::ParagraphSeparator, QChar('\n'));
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
    return new TextView(data.text, parent);
}
} // namespace dzurkel

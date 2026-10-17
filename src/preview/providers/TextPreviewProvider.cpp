#include "TextPreviewProvider.h"
#include <QFile>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include "preview/readers/TextDecoder.h"

namespace dzurkel {
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
    auto *view = new QPlainTextEdit(parent);
    view->setObjectName(QStringLiteral("textPreview"));
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(data.text);
    return view;
}
} // namespace dzurkel

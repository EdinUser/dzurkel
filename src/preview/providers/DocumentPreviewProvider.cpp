#include "DocumentPreviewProvider.h"
#include "preview/document/DocumentView.h"
#include "preview/readers/DocumentReader.h"
#include "preview/readers/TextDecoder.h"
#include "TextPreviewProvider.h"
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace dzurkel {
QString DocumentPreviewProvider::format(const QMimeType &mime, const QString &path, const QByteArray &signature)
{
    if (signature.startsWith("{\\rtf") || mime.name() == QLatin1String("application/rtf") || mime.name() == QLatin1String("text/rtf")) return QStringLiteral("rtf");
    if (mime.name() == QLatin1String("application/vnd.openxmlformats-officedocument.wordprocessingml.document")) return QStringLiteral("docx");
    if (mime.name() == QLatin1String("application/epub+zip")) return QStringLiteral("epub");
    if (mime.name() == QLatin1String("application/x-fictionbook+xml")) return QStringLiteral("fb2");
    if (mime.name() == QLatin1String("application/x-mobipocket-ebook")) return QStringLiteral("mobi");
    if (mime.name() == QLatin1String("application/msword")) return QStringLiteral("doc");
    if (mime.name() == QLatin1String("text/markdown") || mime.name() == QLatin1String("text/x-markdown")) return QStringLiteral("markdown");
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) return QStringLiteral("markdown");
    return QStringList{"docx", "rtf", "epub", "doc", "fb2", "sfb", "mobi"}.contains(suffix) ? suffix : QString();
}
bool DocumentPreviewProvider::supports(const QMimeType &mime) const { return !format(mime, {}, {}).isEmpty(); }
PreviewData DocumentPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data, const QSize &, const Cancellation &cancel) const
{
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    if (data.readerId == QLatin1String("doc") && QStandardPaths::findExecutable(QStringLiteral("antiword")).isEmpty()) {
        data.missingLibrary = QStringLiteral("antiword");
        data.message = QStringLiteral("Legacy DOC preview needs antiword. Check Settings for details!");
        return data;
    }
    DocumentResult result;
    if (data.readerId == QLatin1String("markdown")) {
        auto decoded = decodePreviewText(*file, TextPreviewProvider::ByteLimit, cancel);
        if (decoded.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
        if (!decoded.error.isEmpty()) { data.message = decoded.error; return data; }
        auto document = std::make_shared<FormattedDocument>();
        bool truncated = decoded.truncated || decoded.text.size() > DocumentReader::CharacterLimit;
        auto text = decoded.text.left(DocumentReader::CharacterLimit);
        if (text.size() < decoded.text.size() && !text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QChar('\r'), QChar('\n'));
        int lines = 0;
        int pipes = 0, maximumPipes = 0;
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
            if (text.at(i) == QChar('|')) ++pipes;
            if (text.at(i) == QChar('\n')) {
                maximumPipes = std::max(maximumPipes, pipes); pipes = 0;
                if (++lines == 5000) { text.truncate(i); truncated = true; break; }
            }
        }
        maximumPipes = std::max(maximumPipes, pipes);
        // Conservative preflight: avoid a wide Markdown header expanding
        // thousands of short rows into an enormous dense Qt text table.
        if (maximumPipes > 256 || (maximumPipes && qint64(maximumPipes + 1) * (lines + 1) > 100000)) {
            file->seek(0);
            auto fallback = TextPreviewProvider().load(std::move(file), std::move(data), {}, cancel);
            if (fallback.kind == PreviewKind::Text) fallback.message = QStringLiteral("Markdown shown as plain text; table layout exceeds preview limits. ") + fallback.message;
            return fallback;
        }
        document->partial = truncated;
        document->plainText = text;
        document->markdown = std::move(text);
        if (document->partial) document->notice = QStringLiteral("Partial preview — showing the beginning of this document.");
        result.document = document;
    } else result = DocumentReader::read(*file, data.readerId, cancel);
    if (result.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!result.error.isEmpty()) { data.message = result.error; return data; }
    data.kind = PreviewKind::Document;
    data.document = result.document;
    data.truncated = result.document->partial;
    data.message = result.document->notice;
    data.detail = QStringLiteral("Content preview · Formatting simplified");
    return data;
}
QWidget *DocumentPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const { return new DocumentView(data.document, parent); }
}

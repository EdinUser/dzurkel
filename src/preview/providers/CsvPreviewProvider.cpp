#include "CsvPreviewProvider.h"
#include "TextPreviewProvider.h"
#include "preview/readers/CsvReader.h"
#include "preview/readers/TextDecoder.h"
#include "preview/table/SpreadsheetView.h"
#include <QFile>
#include <QFileInfo>

namespace dzurkel {
bool CsvPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.name() == QStringLiteral("text/csv") || mime.name() == QStringLiteral("text/tab-separated-values")
        || mime.name() == QStringLiteral("application/csv") || mime.name() == QStringLiteral("text/x-csv");
}
PreviewData CsvPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                   const QSize &target, const Cancellation &cancel) const
{
    auto decoded = decodePreviewText(*file, CsvReader::ByteLimit, cancel);
    if (decoded.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!decoded.error.isEmpty()) { data.message = decoded.error; return data; }
    const bool tsv = QFileInfo(data.resource.path).suffix().compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0
        || data.mimeType == QStringLiteral("text/tab-separated-values");
    auto parsed = CsvReader::read(decoded.text, decoded.truncated, cancel, tsv ? QChar('\t') : QChar());
    if (parsed.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!parsed.error.isEmpty() || (parsed.document->partial && !parsed.document->sheets.first().rowCount)) {
        if (!file->seek(0)) { data.message = QStringLiteral("CSV text fallback could not be read."); return data; }
        const auto reason = parsed.error.isEmpty() ? QStringLiteral("No complete CSV record fits the preview limits.") : parsed.error;
        data = TextPreviewProvider().load(std::move(file), std::move(data), target, cancel);
        if (data.kind == PreviewKind::Text) {
            data.readerId = QStringLiteral("text");
            data.message = reason + QStringLiteral(" Showing text instead.") + (data.message.isEmpty() ? QString() : QChar(' ') + data.message);
        }
        return data;
    }
    const auto &sheet = parsed.document->sheets.first();
    const auto separator = parsed.delimiter == QChar('\t') ? QStringLiteral("Tab")
        : parsed.delimiter == QChar(';') ? QStringLiteral("Semicolon") : QStringLiteral("Comma");
    data.kind = PreviewKind::Table;
    data.table = parsed.document;
    data.truncated = parsed.document->partial;
    data.message = parsed.document->notice;
    data.detail = QStringLiteral("%1 · %2-separated · %3 rows × %4 columns · Values shown as text")
        .arg(decoded.encoding, separator).arg(sheet.rowCount).arg(sheet.columnCount);
    return data;
}
QWidget *CsvPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new SpreadsheetView(data.table, parent);
}
}

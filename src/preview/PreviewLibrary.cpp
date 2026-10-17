#include "PreviewLibrary.h"
#include "providers/CsvPreviewProvider.h"
#include "providers/XlsxPreviewProvider.h"
#include "readers/XlsxReader.h"
#include "providers/ImagePreviewProvider.h"
#include "providers/PdfPreviewProvider.h"
#include "providers/TextPreviewProvider.h"
#include "readers/CsvReader.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>

namespace dzurkel {
QList<ReaderCapability> PreviewLibrary::readers()
{
    QStringList formats;
    for (const auto &format : QImageReader::supportedImageFormats()) formats.append(QString::fromLatin1(format).toUpper());
    formats.removeDuplicates();
    std::sort(formats.begin(), formats.end());
    return {
        {QStringLiteral("image"), QStringLiteral("Images"), QStringLiteral("image-x-generic"), QStringLiteral("imageInfo"),
         QStringLiteral("Available image readers: %1. Actual support depends on the file and installed Qt plugins. Images fit without enlargement; GIFs play with bounded frames and Pause/Play. Other animations show a static frame.").arg(formats.join(QStringLiteral(", "))),
         SourceAccess::CompleteSnapshot, 64 * 1024 * 1024},
        {QStringLiteral("pdf"), QStringLiteral("PDF"), QStringLiteral("application-pdf"), QStringLiteral("pdfInfo"),
         QStringLiteral("Read-only PDF previews with scrolling and zoom. Password-protected, damaged, or oversized documents show file information instead."),
         SourceAccess::CompleteSnapshot, 64 * 1024 * 1024},
        {QStringLiteral("csv"), QStringLiteral("CSV / TSV tables"), QStringLiteral("x-office-spreadsheet"), QStringLiteral("csvInfo"),
         QStringLiteral("Read-only tables for CSV and TSV, using comma, semicolon or tab separators. Values remain literal text; no formulas are evaluated. Complete records are shown within byte, row, column and cell limits. Malformed CSV falls back to text."),
         SourceAccess::PrefixSnapshot, CsvReader::ByteLimit + 1},
        {QStringLiteral("xlsx"), QStringLiteral("Excel XLSX tables"), QStringLiteral("x-office-spreadsheet"), QStringLiteral("xlsxInfo"),
         QStringLiteral("Simple read-only tables and sheet tabs for XLSX. Shows saved values, recognized dates/times and markers for unavailable formula results; never calculates formulas. Saved results may be stale. Other formatting, charts and macros are ignored. Bounded loading with partial-preview notices; legacy XLS is not supported."),
         SourceAccess::CompleteSnapshot, XlsxReader::ByteLimit},
        {QStringLiteral("text"), QStringLiteral("Plain text"), QStringLiteral("text-x-generic"), QStringLiteral("textInfo"),
         QStringLiteral("Read-only text and source files. Large files are truncated with a notice; binary files show file information instead."),
         SourceAccess::PrefixSnapshot, TextPreviewProvider::ByteLimit + 1}
    };
}
std::optional<ReaderCapability> PreviewLibrary::capability(const QString &id)
{
    for (const auto &reader : readers()) if (reader.id == id) return reader;
    return {};
}
QString PreviewLibrary::select(QFile &file, const QMimeType &mime, const QString &originalPath)
{
    if (PdfPreviewProvider().supports(mime) || file.peek(1024).startsWith("%PDF-")) return QStringLiteral("pdf");
    bool image = false;
    {
        QImageReader reader(&file);
        image = reader.canRead();
    }
    file.seek(0);
    if (image || ImagePreviewProvider().supports(mime)) return QStringLiteral("image");
    const auto suffix = QFileInfo(originalPath).suffix().toLower();
    if (XlsxPreviewProvider().supports(mime) || suffix == QStringLiteral("xlsx")) return QStringLiteral("xlsx");
    if (CsvPreviewProvider().supports(mime) || suffix == QStringLiteral("csv") || suffix == QStringLiteral("tsv"))
        return QStringLiteral("csv");
    if (TextPreviewProvider().supports(mime)) return QStringLiteral("text");
    return {};
}
std::unique_ptr<PreviewProvider> PreviewLibrary::createReader(const QString &id)
{
    if (id == QStringLiteral("image")) return std::make_unique<ImagePreviewProvider>();
    if (id == QStringLiteral("pdf")) return std::make_unique<PdfPreviewProvider>();
    if (id == QStringLiteral("csv")) return std::make_unique<CsvPreviewProvider>();
    if (id == QStringLiteral("xlsx")) return std::make_unique<XlsxPreviewProvider>();
    if (id == QStringLiteral("text")) return std::make_unique<TextPreviewProvider>();
    return {};
}
}

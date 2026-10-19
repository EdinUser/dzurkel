#include "PreviewLibrary.h"
#include "providers/CsvPreviewProvider.h"
#include "providers/XlsxPreviewProvider.h"
#include "readers/XlsxReader.h"
#include "providers/ImagePreviewProvider.h"
#include "providers/PdfPreviewProvider.h"
#include "providers/TextPreviewProvider.h"
#include "providers/AudioPreviewProvider.h"
#include "providers/VideoPreviewProvider.h"
#include "providers/DocumentPreviewProvider.h"
#include "readers/DocumentReader.h"
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
        {QStringLiteral("video"), QStringLiteral("Video"), QStringLiteral("video-x-generic"), QStringLiteral("videoInfo"),
         QStringLiteral("Read-only streaming video previews, fitted to the window. MP4, MKV, WebM, MOV, AVI, MPEG, OGV and WMV depend on the installed Qt media backend and codecs. Autoplay uses the same session mute/volume choice as music; play/pause (P), seeking, volume and mute. Space/Escape close the preview. Audio-only containers use the audio view. Slow sources show loading, seeking or buffering feedback; leaving the preview cancels playback. A bounded source cache avoids a whole-file snapshot; underlying mounts may cache additional data."),
         SourceAccess::Streaming, 0},
        {QStringLiteral("audio"), QStringLiteral("Audio"), QStringLiteral("audio-x-generic"), QStringLiteral("audioInfo"),
         QStringLiteral("Read-only streaming previews for MP3, FLAC, Ogg/Vorbis, Opus, WAV, AAC, M4A and audio-only MP4. Playback depends on the installed Qt media backend and codecs. Play/pause, seeking, volume and mute; Space closes the preview. Unsupported or damaged files show an error. No codecs are installed and no whole-file copy is made."),
         SourceAccess::Streaming, 0},
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
        {QStringLiteral("docx"), QStringLiteral("Word DOCX"), QStringLiteral("x-office-document"), QStringLiteral("docxInfo"),
         QStringLiteral("Quick content previews with basic headings, emphasis, lists, tables and images. Unsupported formatting contributes readable text where possible; exact page layout is omitted. Bounded loading; no editing or external content."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("rtf"), QStringLiteral("Rich text RTF"), QStringLiteral("x-office-document"), QStringLiteral("rtfInfo"),
         QStringLiteral("Quick content previews with basic emphasis and readable text. Complex formatting and tables are simplified; embedded objects are placeholders. Bounded loading; no editing."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("epub"), QStringLiteral("EPUB books"), QStringLiteral("application-epub+zip"), QStringLiteral("epubInfo"),
         QStringLiteral("Quick previews of the beginning of DRM-free EPUB books in reading order. Basic formatting, images and a compact Contents menu. Publisher layout is simplified; scripts, remote content and embedded fonts are ignored. No library or reading-progress management."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("doc"), QStringLiteral("Legacy Word DOC"), QStringLiteral("x-office-document"), QStringLiteral("docInfo"),
         QStringLiteral("Bounded text-only previews using the optional antiword extractor. Formatting is omitted. If antiword is unavailable, file information explains the limitation. No office suite or package installation."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("fb2"), QStringLiteral("FictionBook FB2"), QStringLiteral("application-epub+zip"), QStringLiteral("fb2Info"),
         QStringLiteral("Bounded previews of FictionBook XML with headings, emphasis and readable text. Images use placeholders; complex layouts and links are simplified. No editing or external resources."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("sfb"), QStringLiteral("Chitanka SFB"), QStringLiteral("text-x-generic"), QStringLiteral("sfbInfo"),
         QStringLiteral("Bounded UTF-8 previews with basic Chitanka titles, chapter headings and emphasis. Unrecognized formatting remains readable text."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("mobi"), QStringLiteral("Mobipocket MOBI"), QStringLiteral("application-epub+zip"), QStringLiteral("mobiInfo"),
         QStringLiteral("Bounded previews of unencrypted MOBI 6 books using PalmDOC compression or uncompressed text. Basic formatting; images use placeholders. HUFF/CDIC, KF8 and encrypted variants show an explanation. No external converter is required."),
         SourceAccess::CompleteSnapshot, DocumentReader::ByteLimit},
        {QStringLiteral("markdown"), QStringLiteral("Markdown"), QStringLiteral("text-markdown"), QStringLiteral("markdownInfo"),
         QStringLiteral("Read-only formatted previews for MD and Markdown: headings, lists, emphasis, quotes, code and tables. Embedded HTML and external resources are disabled. Bounded content with partial-preview notices."),
         SourceAccess::PrefixSnapshot, TextPreviewProvider::ByteLimit + 1},
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
    const auto document = DocumentPreviewProvider::format(mime, originalPath, file.peek(16));
    if (!document.isEmpty()) return document;
    if (VideoPreviewProvider::accepts(mime, originalPath)) return QStringLiteral("video");
    if (AudioPreviewProvider::accepts(mime, originalPath)) return QStringLiteral("audio");
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
    if (id == QStringLiteral("audio")) return std::make_unique<AudioPreviewProvider>();
    if (id == QStringLiteral("video")) return std::make_unique<VideoPreviewProvider>();
    if (id == QStringLiteral("docx") || id == QStringLiteral("rtf") || id == QStringLiteral("epub") || id == QStringLiteral("doc") || id == QStringLiteral("markdown")
        || id == QStringLiteral("fb2") || id == QStringLiteral("sfb") || id == QStringLiteral("mobi"))
        return std::make_unique<DocumentPreviewProvider>();
    return {};
}
}

#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/readers/DocumentReader.h"
#include "preview/document/DocumentView.h"
#include "preview/document/DocumentRecords.h"
#include "preview/lazy/LazyCoordinator.h"
#include <QWheelEvent>
#include <QJsonArray>
#include "controller/PreviewController.h"
#include "preview/StructuredDecodeJob.h"
#include "resources/ResourcePreparation.h"
#include "resources/ResourceAccessPolicy.h"
#include <QBuffer>
#include <QDBusConnection>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QLabel>
#include <QMimeDatabase>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextImageFormat>
#include <QTextBlock>
#include <QTextList>
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QToolButton>
#include <QTest>
#include <QSignalSpy>
#include <QColorSpace>
#include <QScopeGuard>
#include <QFileInfo>
#include <zip.h>
#include <QtEndian>
#include <signal.h>

using namespace dzurkel;
namespace {
const QByteArray wordNs = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
const QByteArray relNs = "http://schemas.openxmlformats.org/package/2006/relationships";
const QByteArray officeNs = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
using Parts = QMap<QString, QByteArray>;
QByteArray wordXml(const QByteArray &body)
{
    return "<w:document xmlns:w=\"" + wordNs + "\" xmlns:r=\"" + officeNs + "\"><w:body>" + body + "</w:body></w:document>";
}
Parts docx(const QByteArray &body)
{
    return {{"_rels/.rels", "<Relationships xmlns=\"" + relNs + "\"><Relationship Id=\"main\" Type=\"" + officeNs + "officeDocument\" Target=\"word/content.xml\"/></Relationships>"},
        {"word/content.xml", wordXml(body)}};
}
Parts epub(const QByteArray &body, const QByteArray &extraManifest = {}, const QByteArray &extraSpine = {})
{
    return {{"mimetype", "application/epub+zip"},
        {"META-INF/container.xml", "<container><rootfiles><rootfile media-type=\"application/oebps-package+xml\" full-path=\"OPS/book.opf\"/></rootfiles></container>"},
        {"OPS/book.opf", "<package version=\"3.0\"><metadata><title>Example book</title><creator>Example author</creator></metadata><manifest><item id=\"one\" href=\"z.xhtml\" media-type=\"application/xhtml+xml\"/>" + extraManifest + "</manifest><spine><itemref idref=\"one\"/>" + extraSpine + "</spine></package>"},
        {"OPS/z.xhtml", "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>" + body + "</body></html>"}};
}
QByteArray mobi(const QList<QByteArray> &records, int compression = 1, int flags = 0, int textLength = -1)
{
    QByteArray data(78 + (records.size() + 1) * 8 + 2, '\0');
    data.replace(60, 8, "BOOKMOBI");
    qToBigEndian<quint16>(records.size() + 1, data.data() + 76);
    QByteArray header(248, '\0');
    qToBigEndian<quint16>(compression, header.data());
    if (textLength < 0) { textLength = 0; for (const auto &record : records) textLength += record.size(); }
    qToBigEndian<quint32>(textLength, header.data() + 4);
    qToBigEndian<quint16>(records.size(), header.data() + 8);
    qToBigEndian<quint16>(4096, header.data() + 10);
    header.replace(16, 4, "MOBI");
    qToBigEndian<quint32>(232, header.data() + 20);
    qToBigEndian<quint32>(65001, header.data() + 28);
    qToBigEndian<quint32>(6, header.data() + 36);
    qToBigEndian<quint16>(flags, header.data() + 242);
    qToBigEndian<quint32>(data.size(), data.data() + 78);
    data += header;
    for (qsizetype i = 0; i < records.size(); ++i) {
        qToBigEndian<quint32>(data.size(), data.data() + 78 + (i + 1) * 8);
        data += records.at(i);
    }
    return data;
}
}
class DocumentFuse : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
public slots:
    QString mountUrl(const QString &) { return path; }
};
class DocumentTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void selectsFormatsWithoutOverridingStrongSignatures();
    void wordPreservesContentAndSimplifiesUnsupportedFormatting();
    void epubUsesSpineOrderAndSanitizesContent();
    void epubSupportsOldDoctypeWithoutResolvingExternalResources();
    void fb2PreservesStructureAndRejectsUnsafeXml();
    void sfbPreservesUnknownFormattingAsText();
    void mobiDecodesRecordsAndSanitizesMarkup();
    void mobiRejectsDamagedEncryptedAndUnsupportedRecords();
    void rtfPreservesUnicodeAndSkipsDestinations();
    void rtfUsesFontCodepagesAndTextTableFallback();
    void imagesAreDecodedWithinBoundsAndUnknownObjectsKeepText();
    void missingLegacyExtractorExplainsFallback();
    void malformedAndEncryptedInputsFailCleanly();
    void resourceLimitsAndCancellation();
    void viewIsReadOnlyAndBlocksResourceLoading();
    void typographyListsAndThemeSafeColors();
    void markdownUsesTheReadingViewAndBounds();
    void markdownStartsAtTop_data();
    void markdownStartsAtTop();
    void contentsSharesNavigationRowAndFollowsPreviewLifetime_data();
    void contentsSharesNavigationRowAndFollowsPreviewLifetime();
    void readingGeometryDoesNotReplaceOtherPreviewSizes();
    void docFooterKeepsWarningsSeparate();
    void localAndRemoteControllerUseDocument_data();
    void localAndRemoteControllerUseDocument();
    void optionalPrivateSamples();
    void allFormatsUseSharedHelper_data();
    void allFormatsUseSharedHelper();
    void packagesSeekAcrossSparseGap_data();
    void packagesSeekAcrossSparseGap();
    void sequentialPrefixesRetainBeginning_data();
    void sequentialPrefixesRetainBeginning();
    void mobiSkipsLargeUnusedRecords();
    void helperPreservesEmbeddedImage();
    void legacyExtractorReadsValidatedDescriptor();
    void helperRejectsIncompleteDocument();
    void helperRejectsIncompleteDocument_data();
    void cancellationStopsExtractorDescendants();
    void markdownFallbackSurvivesHelperTransfer();
    void epubPortionsContinueAndReplaySemanticBlocks();
    void epubDefersChaptersAndKeepsImagesAndLinks();
    void epubWindowEvictsAndNavigatesWithStableGeometry();
    void epubFormatsOnlyInsertedPortion();
    void wordPortionsStreamAndReplayWithNumbering();
    void wordWindowEvictsWithStableSelectionAndGeometry();
    void remainingDocumentPortionsContinueAndReplay_data();
    void remainingDocumentPortionsContinueAndReplay();
    void documentWindowsContinueAndEvict_data();
    void documentWindowsContinueAndEvict();
    void legacyPortionsBackpressureAndReplay();
    void markdownKeepsStructuresAndForwardReferences();
    void documentContinuationKeepsEncodingAndLateErrors();
private:
    QString write(const QString &name, const QByteArray &bytes);
    QString archive(const QString &suffix, const Parts &parts);
    DocumentResult read(const QString &path, const QString &format);
    FileResource prepare(const QString &path);
    PreviewData loadPrepared(const FileResource &resource, const QString &program = {});
    QTemporaryDir directory_;
    DocumentFuse fuse_;
    int serial_ = 0;
};
void DocumentTest::initTestCase()
{
    qRegisterMetaType<PreviewData>();
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath("config").toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void DocumentTest::cleanup()
{
    QTRY_COMPARE(StructuredDecodeJob::runningHelpers(), 0);
    QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0);
}
FileResource DocumentTest::prepare(const QString &path)
{
    FileResource resource;
    QString error;
    if (!parseFileResource(path, resource, error)) { QTest::qFail(qPrintable(error), __FILE__, __LINE__); return {}; }
    ResourcePreparation preparation;
    QSignalSpy ready(&preparation, &ResourcePreparation::ready);
    preparation.start(resource);
    if (!ready.wait(5000)) { QTest::qFail("Preparation did not complete", __FILE__, __LINE__); return {}; }
    return qvariant_cast<FileResource>(ready.first().first());
}
PreviewData DocumentTest::loadPrepared(const FileResource &resource, const QString &program)
{
    const auto candidate = PreviewRegistry::load(resource, {}, std::make_shared<std::atomic_bool>(false));
    if (candidate.kind != PreviewKind::Document || candidate.document) { QTest::qFail("Expected an unloaded document candidate", __FILE__, __LINE__); return {}; }
    StructuredDecodeJob job(candidate, nullptr, program);
    QSignalSpy ready(&job, &StructuredDecodeJob::completed);
    if (!ready.wait(10000)) { QTest::qFail("Document helper did not complete", __FILE__, __LINE__); return {}; }
    return qvariant_cast<PreviewData>(ready.first().first());
}
QString DocumentTest::write(const QString &name, const QByteArray &bytes)
{
    const auto path = directory_.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
QString DocumentTest::archive(const QString &suffix, const Parts &parts)
{
    const auto path = directory_.filePath(QStringLiteral("fixture%1.%2").arg(++serial_).arg(suffix));
    int error;
    auto *zip = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!zip) return {};
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        auto *source = zip_source_buffer(zip, it.value().constData(), size_t(it.value().size()), 0);
        if (!source || zip_file_add(zip, it.key().toUtf8().constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
            if (source) zip_source_free(source);
            zip_discard(zip); return {};
        }
    }
    if (zip_close(zip)) { zip_discard(zip); return {}; }
    return path;
}
DocumentResult DocumentTest::read(const QString &path, const QString &format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("Test file open failed"), false};
    return DocumentReader::read(file, format, std::make_shared<std::atomic_bool>(false));
}
void DocumentTest::selectsFormatsWithoutOverridingStrongSignatures()
{
    for (const auto &suffix : {"docx", "epub", "rtf", "doc", "fb2", "sfb", "mobi"}) {
        const auto path = write(QStringLiteral("content.") + QLatin1String(suffix), "unknown");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/octet-stream"));
        QCOMPARE(PreviewLibrary::select(file, mime, path), QLatin1String(suffix));
        QCOMPARE(file.pos(), 0);
        QVERIFY(PreviewLibrary::createReader(QLatin1String(suffix)));
        QCOMPARE(PreviewLibrary::capability(QLatin1String(suffix))->access, SourceAccess::SeekableSource);
        QCOMPARE(PreviewLibrary::capability(QLatin1String(suffix))->sourceLimit, 0);
    }
    QFile file(write("misleading.docx", "%PDF-1.7\n")); QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(file, {}, file.fileName()), QStringLiteral("pdf"));
    QFile rtf(write("disguised.doc", "{\\rtf1 sample}")); QVERIFY(rtf.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(rtf, {}, rtf.fileName()), QStringLiteral("rtf"));
}
void DocumentTest::fb2PreservesStructureAndRejectsUnsafeXml()
{
    const QByteArray xml = "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description><title-info><book-title>Example</book-title><author><first-name>Ada</first-name><last-name>Example</last-name></author></title-info></description>"
        "<body><title><p>Book</p><p>title</p></title><section><title><p>Chapter</p></title><p>Before <emphasis>italic</emphasis> <strong>bold</strong> <unknown>fallback</unknown></p>"
        "<section><title><p>Subsection</p></title><p>Кирилица &amp; text</p><image href=\"file:///missing\"/></section></section></body>"
        "<body name=\"notes\"><section><p>Note text</p></section></body><binary>c2VjcmV0</binary></FictionBook>";
    auto result = read(write("example.fb2", xml), "fb2");
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->title, QStringLiteral("Example"));
    QCOMPARE(result.document->author, QStringLiteral("Ada Example"));
    QVERIFY(result.document->html.contains("<i>italic</i>"));
    QVERIFY(result.document->html.contains("<h1>Book title</h1>"));
    QVERIFY(result.document->html.contains("<h2>Subsection</h2>"));
    QVERIFY(result.document->plainText.contains(QStringLiteral("Кирилица & text")));
    QVERIFY(result.document->plainText.contains("fallback"));
    QVERIFY(result.document->plainText.contains("Before italic bold fallback"));
    QVERIFY(result.document->plainText.contains("Note text"));
    QVERIFY(!result.document->html.contains("file:///"));
    QVERIFY(!result.document->plainText.contains("c2VjcmV0"));
    QCOMPARE(result.document->sections.size(), 3);
    QVERIFY(!read(write("bad.fb2", "<!DOCTYPE FictionBook [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><FictionBook><body>&x;</body></FictionBook>"), "fb2").document);
    QVERIFY(!read(write("wrong.fb2", "<html><body>Not FictionBook</body></html>"), "fb2").document);
    QVERIFY(!read(write("truncated.fb2", xml.left(xml.size() / 2)), "fb2").document);
    result = read(write("long.fb2", "<FictionBook><body><p>" + QByteArray(DocumentReader::CharacterLimit + 10, 'x') + "</p></body></FictionBook>"), "fb2");
    QVERIFY(result.document); QVERIFY(result.document->partial);
}
void DocumentTest::sfbPreservesUnknownFormattingAsText()
{
    const auto result = read(write("example.sfb", QByteArray::fromHex("efbbbf") + "|\tExample author\n|\tExample title\nD>\n\tDedication\nD$\n>\tChapter one\n>\tSubtitle\n\tТекст с _emphasis_ и __bold__.\n>>\tChapter two\nZ>\tUnknown content <script>literal</script>\n"), "sfb");
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->title, QStringLiteral("Example title"));
    QCOMPARE(result.document->author, QStringLiteral("Example author"));
    QVERIFY(result.document->html.contains("<i>emphasis</i>"));
    QVERIFY(result.document->html.contains("<b>bold</b>"));
    QVERIFY(result.document->html.contains("<h1>Chapter one Subtitle</h1>"));
    QVERIFY(result.document->plainText.contains("Dedication"));
    QVERIFY(!result.document->plainText.contains("D>"));
    QVERIFY(result.document->html.contains("&lt;script&gt;literal&lt;/script&gt;"));
    QVERIFY(result.document->plainText.contains("Z>\tUnknown content"));
    QVERIFY(result.document->plainText.contains(QStringLiteral("Текст")));
    QVERIFY(!read(write("binary.sfb", QByteArray("bad\0binary", 10)), "sfb").document);
}
void DocumentTest::mobiDecodesRecordsAndSanitizesMarkup()
{
    const auto source = QByteArray("<html><head><title>Ignore</title><style>secret</style></head><body><h1>Chapter</h1><p>Кирилица &amp; &#x41; <b>bold</b></p><script>secret</script><img src='file:///missing'><table><tr><td>Cell</td></tr></table><blockquote><blockquote>Quote</blockquote></blockquote></body></html>");
    auto result = read(write("example.mobi", mobi({source.left(80), source.mid(80)})), "mobi");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->plainText.contains(QStringLiteral("Кирилица & A bold")));
    QVERIFY(result.document->html.contains("<h1>Chapter</h1>"));
    QVERIFY(result.document->html.contains("<b>bold</b>"));
    QVERIFY(result.document->plainText.contains("Cell"));
    QVERIFY(!result.document->plainText.contains("secret"));
    QVERIFY(!result.document->html.contains("file:///"));
    QVERIFY(!result.document->html.contains("<table"));
    QVERIFY(!result.document->html.contains("<blockquote"));
    QVERIFY(result.document->plainText.contains("Quote"));
    // PalmDOC: literal runs, overlapping backreferences, and packed spaces.
    QByteArray compressed("<p>abc"); compressed += char(0x80); compressed += char(0x1b);
    compressed += char(0xc1); compressed += char(1); compressed += char(0xe9); compressed += "</p>";
    auto compressedFile = mobi({compressed}, 2, 0, 19);
    const int header = qFromBigEndian<quint32>(compressedFile.constData() + 78);
    qToBigEndian<quint32>(1252, compressedFile.data() + header + 28);
    result = read(write("compressed.mobi", compressedFile), "mobi");
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->plainText.trimmed(), QStringLiteral("abcabcabc Aé"));
    // Flags 1 and 2: one overlap byte and a backwards-sized trailing entry.
    result = read(write("trailers.mobi", mobi({QByteArray("<p>Readable</p>\0\x81", 17)}, 1, 3, 15)), "mobi");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->plainText.contains("Readable"));
    QList<QByteArray> large;
    for (int i = 0; i < 520; ++i) large.append(QByteArray(4096, 'a'));
    result = read(write("large.mobi", mobi(large)), "mobi");
    QVERIFY(result.document); QVERIFY(result.document->partial);
}
void DocumentTest::mobiRejectsDamagedEncryptedAndUnsupportedRecords()
{
    const auto good = mobi({"<p>Text</p>"});
    const int header = qFromBigEndian<quint32>(good.constData() + 78);
    auto changed = good; qToBigEndian<quint16>(2, changed.data() + header + 12);
    QVERIFY(read(write("encrypted.mobi", changed), "mobi").error.contains("Encrypted"));
    changed = good; qToBigEndian<quint32>(8, changed.data() + header + 36);
    QVERIFY(read(write("kf8.mobi", changed), "mobi").error.contains("variant"));
    QVERIFY(read(write("huff.mobi", mobi({"anything"}, 17480)), "mobi").error.contains("variant"));
    changed = good; qToBigEndian<quint32>(0xffffffff, changed.data() + 86);
    QVERIFY(!read(write("offset.mobi", changed), "mobi").document);
    QVERIFY(!read(write("reference.mobi", mobi({QByteArray::fromHex("8008")}, 2)), "mobi").document);
    QVERIFY(!read(write("literal.mobi", mobi({QByteArray::fromHex("0861")}, 2)), "mobi").document);
    QVERIFY(!read(write("trailer.mobi", mobi({"bad"}, 1, 2)), "mobi").document);
    QVERIFY(!read(write("truncated.mobi", good.left(90)), "mobi").document);
}
void DocumentTest::wordPreservesContentAndSimplifiesUnsupportedFormatting()
{
    auto parts = docx("<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Example document</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Bold &amp; readable</w:t></w:r>"
        "<w:unknown><w:r><w:t>Fallback text</w:t></w:r></w:unknown>"
        "<w:del><w:r><w:delText>Deleted secret</w:delText></w:r></w:del>"
        "<w:ins><w:r><w:t>Inserted</w:t></w:r></w:ins>"
        "<w:r><w:rPr><w:vanish/></w:rPr><w:t>Hidden secret</w:t></w:r>"
        "<w:r><w:instrText>HYPERLINK file:///secret</w:instrText></w:r>"
        "<w:r><w:footnoteReference w:id=\"1\"/></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>List item</w:t></w:r></w:p>"
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell value</w:t></w:r></w:p></w:tc></w:tr></w:tbl>");
    parts.insert("word/_rels/content.xml.rels", "<Relationships xmlns=\"" + relNs + "\"><Relationship Id=\"notes\" Type=\"" + officeNs + "footnotes\" Target=\"notes.xml\"/></Relationships>");
    parts.insert("word/notes.xml", "<w:footnotes xmlns:w=\"" + wordNs + "\"><w:footnote w:id=\"1\"><w:p><w:r><w:t>Note text</w:t></w:r></w:p></w:footnote></w:footnotes>");
    const auto result = read(archive("docx", parts), "docx");
    QVERIFY2(result.document, qPrintable(result.error));
    const auto d = result.document;
    for (const auto &text : {"Example document", "Bold & readable", "Fallback text", "Inserted", "List item", "Cell value", "Note text"}) QVERIFY(d->plainText.contains(QLatin1String(text)));
    QVERIFY(!d->plainText.contains("secret"));
    QVERIFY(!d->plainText.contains("HYPERLINK"));
    QVERIFY(d->html.contains("<b>Bold &amp; readable</b>"));
    QVERIFY(d->html.contains("<table"));
    QVERIFY(d->html.contains("<h1>"));
    QVERIFY(d->notice.contains("Revisions"));
    DocumentView view(d);
    QVERIFY(view.findChild<QTextBrowser *>()->toPlainText().contains("Cell value"));
}
void DocumentTest::epubUsesSpineOrderAndSanitizesContent()
{
    auto parts = epub("<h1>First chapter</h1><p style=\"font-size:9000px;background:url(file:///secret)\">Hello <b>world</b><custom>Fallback</custom></p>"
        "<script>executable secret</script><style>hidden stylesheet</style><img src=\"https://example.invalid/image.png\"/>"
        "<a href=\"file:///secret\">Visible link</a><a href=\"a.xhtml#part\">Next section</a>",
        "<item id=\"two\" href=\"a.xhtml\" media-type=\"application/xhtml+xml\"/>", "<itemref idref=\"two\"/>");
    parts.insert("OPS/a.xhtml", "<html><body><h1 id=\"part\">Second chapter</h1><p>More content</p></body></html>");
    auto result = read(archive("epub", parts), "epub");
    QVERIFY2(result.document, qPrintable(result.error));
    auto d = result.document;
    QCOMPARE(d->title, QStringLiteral("Example book"));
    QCOMPARE(d->author, QStringLiteral("Example author"));
    QVERIFY(d->plainText.indexOf("First chapter") < d->plainText.indexOf("Second chapter"));
    QVERIFY(d->plainText.contains("Fallback"));
    QVERIFY(d->plainText.contains("Visible link"));
    QVERIFY(!d->html.contains("secret"));
    QVERIFY(!d->html.contains("9000"));
    QVERIFY(!d->html.contains("https:"));
    QVERIFY(d->html.contains("href=\"#a"));
    QCOMPARE(d->sections.size(), 2);
}
void DocumentTest::epubSupportsOldDoctypeWithoutResolvingExternalResources()
{
    auto parts = epub({});
    parts["OPS/z.xhtml"] = "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"https://example.invalid/xhtml.dtd\"><html><body><p>Old&nbsp;book&mdash;text</p></body></html>";
    const auto result = read(archive("epub", parts), "epub");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->plainText.contains(QString::fromUtf8("Old book—text")));
    parts["META-INF/encryption.xml"] = "<encryption><EncryptedData><EncryptionMethod Algorithm=\"http://www.idpf.org/2008/embedding\"/></EncryptedData></encryption>";
    QVERIFY(read(archive("epub", parts), "epub").document);
}
void DocumentTest::rtfPreservesUnicodeAndSkipsDestinations()
{
    const auto path = write("unicode.rtf", "{\\rtf1\\ansi\\ansicpg1251{\\fonttbl{\\f0 Arial;}}\\uc1 "
        "\\u1055?\\u1088?\\u1080?\\u1074?\\u1077?\\u1090? \\b bold\\b0 {\\i italic} "
        "{\\*\\unknown secret}{\\field{\\*\\fldinst HYPERLINK file:///secret}{\\fldrslt visible link}}"
        "{\\pict 00ff}\\par escaped \\{value\\} \\u1040\\'c0}");
    const auto result = read(path, "rtf");
    QVERIFY2(result.document, qPrintable(result.error));
    auto d = result.document;
    QVERIFY(d->plainText.contains(QString::fromUtf8("Привет")));
    QVERIFY(d->plainText.contains("escaped {value}"));
    QVERIFY(d->plainText.contains("visible link"));
    QVERIFY(!d->plainText.contains("secret"));
    QVERIFY(!d->plainText.contains("Arial"));
    QVERIFY(d->html.contains("<b>bold</b>"));
    QVERIFY(d->html.contains("<i>italic</i>"));
    QCOMPARE(d->plainText.count(QChar(0x410)), 1);
    const auto emoji = read(write("emoji.rtf", "{\\rtf1 \\b \\u-10179?\\u-8704?}"), "rtf");
    QVERIFY(emoji.document);
    QTextDocument rendered;
    rendered.setHtml(emoji.document->html);
    QVERIFY(rendered.toPlainText().contains(QString::fromUtf8("😀")));
}
void DocumentTest::rtfUsesFontCodepagesAndTextTableFallback()
{
    auto result = read(write("codepage.rtf", "{\\rtf1\\ansi{\\fonttbl{\\f0\\fcharset204 Arial;}}\\f0 \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2\\cell Value\\row Next}"), "rtf");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->plainText.contains(QString::fromUtf8("Привет\tValue\nNext")));
    result = read(write("binary.rtf", "{\\rtf1 Text{\\pict\\bin4 {}\\x}Tail}"), "rtf");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->plainText.contains("Tail"));
}
void DocumentTest::imagesAreDecodedWithinBoundsAndUnknownObjectsKeepText()
{
    QByteArray bytes;
    QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly));
    QImage image(30, 20, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(&buffer, "PNG"));
    auto parts = docx("<w:p><w:r><w:drawing><blip xmlns=\"http://schemas.openxmlformats.org/drawingml/2006/main\" r:embed=\"picture\"/></w:drawing></w:r></w:p>"
        "<w:p><w:r><w:t>Illustration</w:t></w:r><oMath><t>Readable equation</t></oMath><chart/></w:p>");
    parts.insert("word/_rels/content.xml.rels", "<Relationships><Relationship Id=\"picture\" Type=\"x/image\" Target=\"media/image.png\"/></Relationships>");
    parts.insert("word/media/image.png", bytes);
    auto result = read(archive("docx", parts), "docx");
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->images.size(), 1);
    QVERIFY(result.document->plainText.contains("Readable equation"));
    QVERIFY(result.document->plainText.contains("[Chart]"));
    DocumentView view(result.document);
    auto *text = view.findChild<QTextBrowser *>();
    auto loaded = text->document()->resource(QTextDocument::ImageResource, QUrl("dzurkel-image:0"));
    QVERIFY(loaded.canConvert<QImage>());
    QCOMPARE(loaded.value<QImage>().size(), QSize(30, 20));
    parts = epub("<p>Text</p><img src=\"image.png\"/><math><mi>x</mi><mo>+</mo><mn>1</mn></math>");
    parts.insert("OPS/image.png", bytes);
    result = read(archive("epub", parts), "epub");
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->images.size(), 1);
    QVERIFY(result.document->plainText.contains("x+1"));
    parts["OPS/image.png"] = QByteArray(9 * 1024 * 1024, 'x');
    result = read(archive("epub", parts), "epub");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->images.isEmpty());
    QVERIFY(result.document->plainText.contains("[Image]"));
}
void DocumentTest::missingLegacyExtractorExplainsFallback()
{
    const auto saved = qgetenv("PATH");
    qputenv("PATH", directory_.path().toUtf8());
    auto result = read(write("legacy.doc", QByteArray::fromHex("d0cf11e0a1b11ae1")), "doc");
    qputenv("PATH", saved);
    QVERIFY(!result.document);
    QVERIFY(result.error.contains("antiword"));
}
void DocumentTest::malformedAndEncryptedInputsFailCleanly()
{
    for (const auto &bytes : {QByteArray("{\\rtf1 incomplete"), QByteArray("{\\rtf1 \\bin999 no}"), QByteArray("{\\rtf1 \\uc999 broken}")}) {
        auto result = read(write("broken.rtf", bytes), "rtf");
        QVERIFY(!result.document); QVERIFY(!result.error.isEmpty());
    }
    auto parts = epub("<p>text</p>");
    parts["META-INF/encryption.xml"] = "<encryption><EncryptedData><EncryptionMethod Algorithm=\"aes\"/></EncryptedData></encryption>";
    QVERIFY(!read(archive("epub", parts), "epub").document);
    parts = epub({});
    parts["OPS/z.xhtml"] = "<!DOCTYPE html [<!ENTITY secret SYSTEM 'file:///etc/passwd'>]><html><body>&secret;</body></html>";
    QVERIFY(!read(archive("epub", parts), "epub").document);
    auto word = docx("<w:p><w:r><w:t>content</w:t></w:r></w:p>");
    word["_rels/.rels"] = "<Relationships><Relationship Id=\"main\" Type=\"x/officeDocument\" Target=\"../../escape.xml\"/></Relationships>";
    QVERIFY(!read(archive("docx", word), "docx").document);
    QVERIFY(!read(write("binary.doc", QByteArray::fromHex("d0cf11e0a1b11ae1") + "broken"), "doc").document);
}
void DocumentTest::resourceLimitsAndCancellation()
{
    auto result = read(write("long.rtf", "{\\rtf1 " + QByteArray(DocumentReader::CharacterLimit + 1000, 'x') + "}"), "rtf");
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->partial);
    QCOMPARE(result.document->plainText.size(), DocumentReader::CharacterLimit);
    QVERIFY(!result.document->notice.isEmpty());
    result = read(write("deep.rtf", "{\\rtf1 " + QByteArray(80, '{') + "x" + QByteArray(81, '}')), "rtf");
    QVERIFY(!result.document);
    QFile file(write("cancelled.rtf", "{\\rtf1 example}")); QVERIFY(file.open(QIODevice::ReadOnly));
    result = DocumentReader::read(file, "rtf", std::make_shared<std::atomic_bool>(true));
    QVERIFY(result.cancelled); QVERIFY(!result.document);
    result = read(write("large.rtf", QByteArray(DocumentReader::InputPrefixLimit + 1, 'x')), "rtf");
    QVERIFY(!result.document);
    auto parts = epub("<p>early</p>");
    parts.insert("ignored.bin", QByteArray(65 * 1024 * 1024, 'x'));
    QVERIFY(read(archive("epub", parts), "epub").document);
}
void DocumentTest::viewIsReadOnlyAndBlocksResourceLoading()
{
    auto d = std::make_shared<FormattedDocument>();
    d->html = "<h1>Project notes</h1><p>A quick look at the content of this document.</p>"
        "<h2>What matters</h2><p>Readable structure, <b>emphasis</b> and <i>simple formatting</i>.</p>"
        "<ul><li>Headings and paragraphs</li><li>Lists and tables</li><li>Plain text when formatting is unsupported</li></ul>"
        "<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\"><tr><th>Format</th><th>Preview</th></tr><tr><td>DOCX / RTF / EPUB</td><td>Readable content</td></tr><tr><td>Legacy DOC</td><td>Plain text</td></tr></table>";
    DocumentView view(d);
    view.resize(650, 500); view.show();
    auto *text = view.findChild<QTextBrowser *>(); QVERIFY(text);
    QVERIFY(text->isReadOnly()); QVERIFY(!text->openLinks()); QVERIFY(!text->openExternalLinks());
    QVERIFY(!text->document()->resource(QTextDocument::ImageResource, QUrl("file:///etc/passwd")).isValid());
    const auto original = text->toPlainText();
    QTest::keyClicks(text, "replacement"); QCOMPARE(text->toPlainText(), original);
    const auto size = text->document()->defaultFont().pointSizeF();
    view.zoomBy(2); QVERIFY(text->document()->defaultFont().pointSizeF() > size);
    view.fit(); QCOMPARE(text->document()->defaultFont().pointSizeF(), size);
    QVERIFY(!view.findChild<QToolButton *>(QStringLiteral("documentContents")));
    QVERIFY(view.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/document-preview.png")));
}
void DocumentTest::localAndRemoteControllerUseDocument_data()
{
    QTest::addColumn<bool>("remote");
    QTest::newRow("local") << false;
    QTest::newRow("fake-SMB") << true;
}
void DocumentTest::typographyListsAndThemeSafeColors()
{
    auto parts = docx("<w:p><w:pPr><w:pStyle w:val=\"Title\"/></w:pPr><w:r><w:t>Document title</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Section heading</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>Body paragraph</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:color w:val=\"A00000\"/></w:rPr><w:t>Source red</w:t></w:r></w:p>"
        "<w:p/><w:p/><w:p/><w:p><w:r><w:t>After empty paragraphs</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>First bullet with a long enough sentence to wrap naturally across the narrow preview window.</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>Nested bullet</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>Last bullet</w:t></w:r></w:p>");
    const auto result = read(archive("docx", parts), "docx");
    QVERIFY2(result.document, qPrintable(result.error));
    DocumentView view(result.document);
    auto *text = view.findChild<QTextBrowser *>();
    auto block = [&](const QString &value) { return text->document()->find(value).block(); };
    auto pointSize = [&](const QString &value) { return text->document()->find(value).charFormat().fontPointSize(); };
    QVERIFY(pointSize("Document title") > pointSize("Section heading"));
    QVERIFY(pointSize("Section heading") > pointSize("Body paragraph"));
    QVERIFY(block("Section heading").blockFormat().topMargin() > block("Body paragraph").blockFormat().topMargin());
    const auto first = block("First bullet");
    const auto nested = block("Nested bullet");
    QVERIFY(first.textList()); QVERIFY(nested.textList());
    QVERIFY(nested.textList()->format().indent() > first.textList()->format().indent());
    QCOMPARE(first.textList(), block("Last bullet").textList());
    QVERIFY(!first.text().startsWith(QString::fromUtf8("•")));
    QPalette light = view.palette(); light.setColor(QPalette::Base, Qt::white); light.setColor(QPalette::Text, Qt::black);
    view.setPalette(light);
    QCOMPARE(text->document()->find("Source red").charFormat().foreground().color(), QColor("#A00000"));
    QPalette dark = light; dark.setColor(QPalette::Base, QColor("#121212")); dark.setColor(QPalette::Text, Qt::white);
    view.setPalette(dark);
    QCOMPARE(text->document()->find("Source red").charFormat().foreground().color(), QColor(Qt::white));
    view.setPalette(light);
    QCOMPARE(text->document()->find("Source red").charFormat().foreground().color(), QColor("#A00000"));
    view.resize(620, 760); view.show();
    view.grab();
    text->document()->documentLayout()->documentSize();
    QVERIFY(block("Document title").layout()->lineAt(0).height() > block("Section heading").layout()->lineAt(0).height());
    const auto geometry = view.size();
    const auto size = pointSize("Document title");
    view.zoomBy(2);
    QVERIFY(pointSize("Document title") > size);
    QCOMPARE(view.size(), geometry);
    QVERIFY(view.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/document-refined.png")));
}
void DocumentTest::markdownUsesTheReadingViewAndBounds()
{
    const auto path = write("preview.md", "# Markdown title\r\n\r\n## Section\r\n\r\nA **bold** paragraph.\r\n\r\n- First item\r\n- Second item\r\n\r\n```cpp\r\n  int value = 42;\r\n```\r\n\r\n| Name | Value |\r\n| --- | --- |\r\n| Example | 42 |\r\n\r\n[External](https://example.invalid/)\r\n\r\n![Blocked](file:///etc/passwd)\r\n");
    FileResource resource; QString error;
    QVERIFY(resolveFileResource(path, resource, error));
    auto data = PreviewRegistry::load(resource, QSize(600, 700), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.readerId, QStringLiteral("markdown"));
    QCOMPARE(data.kind, PreviewKind::Document);
    QVERIFY(!data.truncated); // CRLF normalization is not truncation.
    DocumentView view(data.document);
    auto *text = view.findChild<QTextBrowser *>();
    QVERIFY(text->toPlainText().contains("bold"));
    QVERIFY(!text->toPlainText().contains("**bold**"));
    QVERIFY(text->toPlainText().contains("  int value = 42;"));
    QVERIFY(text->document()->find("First item").block().textList());
    QVERIFY(text->document()->find("Example").currentTable());
    auto header = text->document()->find("Name");
    QCOMPARE(header.currentTable()->cellAt(header).firstCursorPosition().block().text(), QStringLiteral("Name"));
    QCOMPARE(header.charFormat().fontWeight(), int(QFont::Bold));
    QVERIFY(view.findChild<QToolButton *>(QStringLiteral("documentContents")));
    QVERIFY(!text->document()->resource(QTextDocument::ImageResource, QUrl("file:///etc/passwd")).isValid());
    QVERIFY(!text->openExternalLinks());
    view.resize(620, 760); view.show();
    QVERIFY(view.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/markdown-preview.png")));
    const auto longPath = write("long.markdown", "# Bounded\n" + QByteArray(DocumentReader::CharacterLimit + 500, 'x'));
    QVERIFY(resolveFileResource(longPath, resource, error));
    data = PreviewRegistry::load(resource, QSize(600, 700), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Document); QVERIFY(data.truncated);
    QVERIFY(data.document->markdown.size() <= DocumentReader::CharacterLimit);
    const auto wide = write("wide.md", QByteArray(300, '|'));
    QVERIFY(resolveFileResource(wide, resource, error));
    data = PreviewRegistry::load(resource, QSize(600, 700), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Text);
    QVERIFY(data.message.contains("plain text"));
}
void DocumentTest::markdownStartsAtTop_data()
{
    QTest::addColumn<bool>("headings");
    QTest::newRow("with-contents") << true;
    QTest::newRow("without-contents") << false;
}
void DocumentTest::markdownStartsAtTop()
{
    QFETCH(bool, headings);
    QByteArray markdown;
    for (int section = 0; section < 3; ++section) {
        if (headings) markdown += "## Section " + QByteArray::number(section) + "\n\n";
        for (int paragraph = 0; paragraph < 30; ++paragraph)
            markdown += "A paragraph with **emphasis** and `code` for a long preview.\n\n";
    }
    FileResource resource; QString error;
    QVERIFY(resolveFileResource(write("scroll.md", markdown), resource, error));
    auto data = PreviewRegistry::load(resource, QSize(700, 850), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Document);
    PreviewWindow window;
    window.resize(700, 850);
    for (int opening = 0; opening < 2; ++opening) {
        window.setContent(PreviewRegistry::createWidget(data, nullptr));
        window.show();
        window.activateWindow();
        auto *text = window.findChild<QTextBrowser *>(QStringLiteral("documentText"));
        QVERIFY(text);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(text->hasFocus());
        QVERIFY(text->verticalScrollBar()->maximum() > 0);
        QCOMPARE(text->verticalScrollBar()->value(), 0);
        QCOMPARE(text->horizontalScrollBar()->value(), 0);
        QCOMPARE(text->textCursor().position(), 0);
        text->verticalScrollBar()->setValue(text->verticalScrollBar()->maximum());
        QVERIFY(text->verticalScrollBar()->value() > 0);
    }
}
void DocumentTest::contentsSharesNavigationRowAndFollowsPreviewLifetime_data()
{
    QTest::addColumn<bool>("markdown");
    QTest::newRow("markdown") << true;
    QTest::newRow("formatted-document") << false;
}
void DocumentTest::contentsSharesNavigationRowAndFollowsPreviewLifetime()
{
    QFETCH(bool, markdown);
    auto document = std::make_shared<FormattedDocument>();
    if (markdown) {
        document->markdown = "# Start\n\n";
        for (int paragraph = 0; paragraph < 80; ++paragraph)
            document->markdown += "A paragraph separating the two headings.\n\n";
        document->markdown += "## Last section\n\nLast paragraph.\n";
    } else {
        document->html = "<h1><a name=\"start\">Start</a></h1>";
        for (int paragraph = 0; paragraph < 80; ++paragraph)
            document->html += "<p>A paragraph separating the two headings.</p>";
        document->html += "<h2><a name=\"last\">Last section</a></h2><p>Last paragraph.</p>";
        document->sections = {{"Start", "start"}, {"Last section", "last"}};
    }
    PreviewWindow window;
    window.resize(700, 850);
    window.setNavigation(true, true, QStringLiteral("9 / 10"));
    window.setContent(new DocumentView(document));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QPointer<QToolButton> contents = window.findChild<QToolButton *>(QStringLiteral("documentContents"));
    auto *next = window.findChild<QPushButton *>(QStringLiteral("nextFile"));
    auto *text = window.findChild<QTextBrowser *>(QStringLiteral("documentText"));
    QVERIFY(contents); QVERIFY(next); QVERIFY(text);
    QVERIFY(contents->isVisible());
    const auto contentsPosition = contents->mapTo(&window, contents->rect().center());
    const auto nextPosition = next->mapTo(&window, next->rect().center());
    QVERIFY2(qAbs(contentsPosition.y() - nextPosition.y()) <= 2,
        "Contents must share the navigation row rather than consume another header row");
    QVERIFY(contentsPosition.x() > nextPosition.x());
    QVERIFY(text->mapTo(&window, QPoint()).y() > contentsPosition.y());
    QCOMPARE(contents->menu()->actions().size(), 2);
    contents->menu()->actions().last()->trigger();
    QTRY_VERIFY(text->verticalScrollBar()->value() > 0);
    const auto sectionScroll = text->verticalScrollBar()->value();
    QCoreApplication::processEvents();
    QCOMPARE(text->verticalScrollBar()->value(), sectionScroll);
    contents->menu()->actions().first()->trigger();
    QTRY_VERIFY(text->verticalScrollBar()->value() < sectionScroll);
    window.setContent(new QLabel(QStringLiteral("Another preview")));
    QVERIFY(contents.isNull());
    QVERIFY(!window.findChild<QToolButton *>(QStringLiteral("documentContents")));
    window.setContent(new DocumentView(document));
    QTRY_VERIFY(window.findChild<QToolButton *>(QStringLiteral("documentContents"))->isVisible());
    document = std::make_shared<FormattedDocument>();
    document->html = "<p>A short document without sections.</p>";
    window.setContent(new DocumentView(document));
    QVERIFY(!window.findChild<QToolButton *>(QStringLiteral("documentContents")));
}
void DocumentTest::readingGeometryDoesNotReplaceOtherPreviewSizes()
{
    PreviewWindow window;
    window.preparePresentation();
    const auto generalDefault = window.size();
    auto document = std::make_shared<FormattedDocument>(); document->html = "<p>Reading preview</p>";
    PreviewData data; data.kind = PreviewKind::Document; data.document = document; data.readerId = "docx";
    const auto show = [&](const PreviewData &next) {
        window.beginPreviewUpdate();
        FileResource resource;
        resource.path = QStringLiteral("/tmp/synthetic-%1").arg(int(next.kind));
        resource.url = QUrl::fromLocalFile(resource.path);
        window.setResource(resource);
        if (next.kind == PreviewKind::Document || next.kind == PreviewKind::Text || next.kind == PreviewKind::Image)
            window.setContent(PreviewRegistry::createWidget(next, nullptr));
        else window.setContent(new QLabel(QStringLiteral("Synthetic preview")));
        window.setPreviewData(next);
        window.endPreviewUpdate();
        if (!window.isVisible()) window.show();
    };
    show(data);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.height() > window.width());
    window.resize(560, 660);
    QTRY_COMPARE(window.size(), QSize(560, 660));
    const auto readingSize = window.size();
    auto *zoom = window.findChild<DocumentView *>(); QVERIFY(zoom);
    zoom->zoomBy(3); QCOMPARE(window.size(), readingSize);
    PreviewData table; table.kind = PreviewKind::Table; show(table);
    QTRY_COMPARE(window.size(), generalDefault);
    window.resize(740, 520); QTRY_COMPARE(window.size(), QSize(740, 520));
    const auto tableSize = window.size();
    show(data); QTRY_COMPARE(window.size(), readingSize);
    PreviewData pdf; pdf.kind = PreviewKind::Pdf; show(pdf); QTRY_COMPARE(window.size(), generalDefault);
    show(table); QTRY_COMPARE(window.size(), tableSize);
    PreviewData text; text.kind = PreviewKind::Text; text.text = "Plain text keeps its whitespace.";
    show(text); QTRY_COMPARE(window.size(), readingSize);
    auto *plain = window.findChild<QPlainTextEdit *>(); QVERIFY(plain);
    const auto fontSize = plain->font().pointSizeF();
    QTest::keyClick(plain, Qt::Key_Plus); QVERIFY(plain->font().pointSizeF() > fontSize); QCOMPARE(window.size(), readingSize);
    PreviewData audio; audio.kind = PreviewKind::Audio; show(audio);
    QTRY_VERIFY(window.width() <= 560);
    show(data); QTRY_COMPARE(window.size(), readingSize);
    for (const auto &size : {QSize(40, 24), QSize(2000, 1400)}) {
        PreviewData image; image.kind = PreviewKind::Image; image.imageSize = size;
        image.image = QImage(size, QImage::Format_RGB32); image.image.fill(Qt::blue);
        show(image);
        QTRY_VERIFY(window.width() <= window.screen()->availableGeometry().width());
        if (size.width() < 400) QTRY_VERIFY2(window.width() <= 450, qPrintable(QStringLiteral("Small image size %1x%2; minimum %3x%4").arg(window.width()).arg(window.height()).arg(window.minimumWidth()).arg(window.minimumHeight())));
        else QTRY_VERIFY(window.width() > window.height());
        show(data); QTRY_COMPARE(window.size(), readingSize);
    }
    window.hide();
    window.preparePresentation(); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(100); // Complete the compositor's new mapping before switching profiles.
    QTRY_COMPARE(window.size(), readingSize);
    show(table); QTRY_COMPARE(window.size(), tableSize);
}
void DocumentTest::docFooterKeepsWarningsSeparate()
{
    PreviewWindow window;
    window.setContent(new QLabel("Text"));
    PreviewData data; data.kind = PreviewKind::Document; data.readerId = "doc";
    data.message = QStringLiteral("Text-only preview; document formatting omitted.");
    window.setPreviewData(data);
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("previewSummary"))->text(), QStringLiteral("DOC · Text-only preview"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("previewStatus"));
    QVERIFY(status->isHidden());
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->toolTip().contains("formatting omitted"));
    data.message += QStringLiteral(" Partial preview — showing the beginning of this document.");
    window.setPreviewData(data);
    QCOMPARE(status->text(), QStringLiteral("Partial preview"));
    QVERIFY(!status->isHidden());
}
void DocumentTest::localAndRemoteControllerUseDocument()
{
    QFETCH(bool, remote);
    const auto path = archive("docx", docx("<w:p><w:r><w:t>Readable content</w:t></w:r></w:p>"));
    fuse_.path = path;
    PreviewController controller;
    QString error;
    QVERIFY2(controller.show(remote ? QStringLiteral("smb://server/share/test.docx") : path, error), qPrintable(error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *view = window->findChild<DocumentView *>(); QVERIFY(view);
    auto *text = view->findChild<QTextBrowser *>(); QVERIFY(text);
    QVERIFY(text->toPlainText().contains("Readable content"));
    if (!remote) QVERIFY(window->grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/document-window.png")));
    QTest::keyClick(text, Qt::Key_Plus); QCOMPARE(view->zoomFactor(), 1.1);
    QTest::keyClick(text, Qt::Key_Space);
    QTRY_VERIFY(!controller.isVisible());
    QTRY_VERIFY(!window->findChild<QTextBrowser *>());
}
void DocumentTest::allFormatsUseSharedHelper_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<QString>("base");
    for (const auto &format : {"docx", "epub", "rtf", "fb2", "sfb", "mobi", "markdown"})
        for (const auto &base : {QString(), QStringLiteral("smb://server/share/"), QStringLiteral("sftp://user@server:2222/")})
            QTest::newRow(qPrintable(QString::fromLatin1(format) + QChar('-') + (base.isEmpty() ? "local" : base.left(base.indexOf(':')))))
                << QString::fromLatin1(format) << base;
}
void DocumentTest::allFormatsUseSharedHelper()
{
    QFETCH(QString, format);
    QFETCH(QString, base);
    QString path;
    if (format == "docx") path = archive(format, docx("<w:p><w:r><w:t>Readable beginning</w:t></w:r></w:p>"));
    else if (format == "epub") path = archive(format, epub("<h1>Chapter</h1><p>Readable beginning</p>"));
    else if (format == "rtf") path = write("shared.rtf", "{\\rtf1 Readable beginning}");
    else if (format == "fb2") path = write("shared.fb2", "<FictionBook><body><section><title><p>Chapter</p></title><p>Readable beginning</p></section></body></FictionBook>");
    else if (format == "sfb") path = write("shared.sfb", ">\tChapter\nReadable beginning\n");
    else if (format == "mobi") path = write("shared.mobi", mobi({"<h1>Chapter</h1><p>Readable beginning</p>"}));
    else path = write("shared.md", "# Chapter\n\nReadable beginning\n");
    QString error;
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith("sftp:"), error));
    fuse_.path = path;
    const auto url = base.isEmpty() ? path : base + QFileInfo(path).fileName();
    const auto resource = prepare(url);
    QVERIFY(resource.seekableSource);
    QVERIFY(!resource.backingStore);
    QCOMPARE(resource.backingPath, path);
    const auto data = loadPrepared(resource);
    QCOMPARE(data.kind, PreviewKind::Document);
    QVERIFY(data.document->plainText.contains("Readable beginning"));
    QCOMPARE(data.resource.url, resource.url);
    PreviewController controller;
    QVERIFY(controller.show(url, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *text = controller.window()->findChild<QTextBrowser *>();
    QVERIFY(text);
    QVERIFY(text->toPlainText().contains("Readable beginning"));
    QCOMPARE(controller.fileUrl(), resource.url.toString(QUrl::FullyEncoded));
}
void DocumentTest::packagesSeekAcrossSparseGap_data()
{
    QTest::addColumn<QString>("format");
    QTest::newRow("docx") << QStringLiteral("docx");
    QTest::newRow("epub") << QStringLiteral("epub");
}
void DocumentTest::packagesSeekAcrossSparseGap()
{
    QFETCH(QString, format);
    const auto path = archive(format, format == "docx" ? docx("<w:p><w:r><w:t>Early content</w:t></w:r></w:p>") : epub("<p>Early content</p>"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    const auto bytes = file.readAll();
    const auto end = bytes.size() - 22;
    QCOMPARE(bytes.mid(end, 4), QByteArray::fromHex("504b0506"));
    const auto offset = qFromLittleEndian<quint32>(bytes.constData() + end + 16);
    auto directory = bytes.mid(offset);
    constexpr quint32 gap = 256 * 1024 * 1024;
    qToLittleEndian<quint32>(offset + gap, directory.data() + directory.size() - 22 + 16);
    QVERIFY(file.resize(offset));
    QVERIFY(file.seek(offset + gap));
    QCOMPARE(file.write(directory), directory.size());
    file.close();
    const auto resource = prepare(path);
    QVERIFY(!resource.backingStore);
    const auto data = loadPrepared(resource);
    QCOMPARE(data.kind, PreviewKind::Document);
    QVERIFY(data.document->plainText.contains("Early content"));
    QVERIFY(QFile::remove(path));
    QCOMPARE(loadPrepared(resource).kind, PreviewKind::Information);
}
void DocumentTest::sequentialPrefixesRetainBeginning_data()
{
    QTest::addColumn<QString>("format");
    for (const auto &format : {"rtf", "fb2", "sfb", "markdown"}) QTest::newRow(format) << QString::fromLatin1(format);
}
void DocumentTest::sequentialPrefixesRetainBeginning()
{
    QFETCH(QString, format);
    QByteArray bytes;
    if (format == "rtf") bytes = "{\\rtf1 Early content {\\pict\\bin16778240 " + QByteArray(16778240, '\0') + "}}";
    else if (format == "fb2") bytes = "<FictionBook><body><p>Early content</p></body><binary>" + QByteArray(DocumentReader::InputPrefixLimit + 1024, 'Q') + "</binary></FictionBook>";
    else if (format == "sfb") bytes = "Early content\n" + QByteArray(DocumentReader::InputPrefixLimit + 1024, 'x');
    else bytes = "Early content\n\n" + QByteArray(2 * 1024 * 1024 + 1024, 'x');
    const auto path = write(format == "markdown" ? "prefix.md" : "prefix." + format, bytes);
    const auto result = loadPrepared(prepare(path));
    QCOMPARE(result.kind, PreviewKind::Document);
    QVERIFY(result.document->partial);
    QVERIFY(result.document->plainText.contains("Early content"));
    QVERIFY(result.document->notice.contains("Partial"));
}
void DocumentTest::mobiSkipsLargeUnusedRecords()
{
    auto bytes = mobi({"<p>Early content</p>", "unused"});
    const auto header = qFromBigEndian<quint32>(bytes.constData() + 78);
    qToBigEndian<quint16>(1, bytes.data() + header + 8);
    qToBigEndian<quint32>(20, bytes.data() + header + 4);
    const auto path = write("large-unused.mobi", bytes);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(256 * 1024 * 1024));
    file.close();
    const auto data = loadPrepared(prepare(path));
    QCOMPARE(data.kind, PreviewKind::Document);
    QVERIFY(data.document->plainText.contains("Early content"));
}
void DocumentTest::helperPreservesEmbeddedImage()
{
    QImage image(30, 20, QImage::Format_RGBA64);
    image.fill(QColor(30, 110, 200, 128));
    image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    auto parts = epub("<h1>Chapter</h1><p>Early content</p><img src=\"image.png\"/>");
    parts.insert("OPS/image.png", bytes);
    const auto data = loadPrepared(prepare(archive("epub", parts)));
    QCOMPARE(data.kind, PreviewKind::Document);
    QCOMPARE(data.document->images.size(), 1);
    const auto actual = data.document->images.value("dzurkel-image:0");
    QCOMPARE(actual.size(), image.size());
    QCOMPARE(actual.format(), image.format());
    QCOMPARE(actual.pixelColor(0, 0), image.pixelColor(0, 0));
    QCOMPARE(actual.colorSpace(), image.colorSpace());
    QVERIFY(!data.document->sections.isEmpty());
    QVERIFY(data.document->html.contains("dzurkel-image:0"));
}
void DocumentTest::legacyExtractorReadsValidatedDescriptor()
{
    const auto program = directory_.filePath("antiword");
    QFile script(program);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/usr/bin/python3\nimport os,sys\nassert sys.argv[-1] == '/proc/self/fd/0'\nwith open(sys.argv[-1], 'rb') as f:\n assert f.read(8) == bytes.fromhex('d0cf11e0a1b11ae1')\n assert os.fstat(f.fileno()).st_size > 16000000\nprint('Validated descriptor text')\n");
    script.close();
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto saved = qgetenv("PATH");
    const auto restore = qScopeGuard([saved] { qputenv("PATH", saved); });
    qputenv("PATH", directory_.path().toUtf8());
    const auto path = write("large.doc", QByteArray::fromHex("d0cf11e0a1b11ae1"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(256 * 1024 * 1024));
    file.close();
    const auto data = loadPrepared(prepare(path));
    QCOMPARE(data.kind, PreviewKind::Document);
    QVERIFY(data.document->plainText.contains("Validated descriptor text"));
    QVERIFY(data.message.contains("Text-only"));
    QVERIFY(QFile::remove(program));
    const auto missing = loadPrepared(prepare(path));
    QCOMPARE(missing.kind, PreviewKind::Information);
    QCOMPARE(missing.missingLibrary, QStringLiteral("antiword"));
}
void DocumentTest::cancellationStopsExtractorDescendants()
{
    const auto program = directory_.filePath("antiword");
    QFile script(program);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/usr/bin/python3\nimport os,sys,subprocess,time\np=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])\nwith open(os.environ['DZURKEL_TEST_CHILD_PID'],'w') as f: f.write(str(p.pid))\ntime.sleep(30)\n");
    script.close();
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto saved = qgetenv("PATH");
    const auto pidFile = directory_.filePath("extractor-child.pid");
    const auto restore = qScopeGuard([saved] { qputenv("PATH", saved); qunsetenv("DZURKEL_TEST_CHILD_PID"); });
    qputenv("PATH", directory_.path().toUtf8());
    qputenv("DZURKEL_TEST_CHILD_PID", pidFile.toUtf8());
    const auto resource = prepare(write("cancel-extractor.doc", QByteArray::fromHex("d0cf11e0a1b11ae1")));
    const auto candidate = PreviewRegistry::load(resource, {}, std::make_shared<std::atomic_bool>(false));
    StructuredDecodeJob job(candidate);
    QTRY_VERIFY(QFileInfo::exists(pidFile));
    QFile pidInput(pidFile);
    QVERIFY(pidInput.open(QIODevice::ReadOnly));
    const auto pid = pidInput.readAll().toLongLong();
    QVERIFY(pid > 0);
    QElapsedTimer elapsed;
    elapsed.start();
    job.stop();
    QVERIFY(elapsed.elapsed() < 100);
    const auto stopped = [pid] {
        QFile status(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!status.open(QIODevice::ReadOnly)) return true;
        const auto bytes = status.readAll();
        return bytes.mid(bytes.lastIndexOf(')') + 2, 1) == "Z";
    };
    QTRY_VERIFY(stopped());
}
void DocumentTest::markdownFallbackSurvivesHelperTransfer()
{
    const auto path = write("wide-markdown.md", QByteArray("a|").repeated(300) + "\n\nReadable beginning\n");
    const auto data = loadPrepared(prepare(path));
    QCOMPARE(data.kind, PreviewKind::Text);
    QVERIFY(data.text.contains("Readable beginning"));
    QVERIFY(data.message.contains("Markdown shown as plain text"));
}
void DocumentTest::helperRejectsIncompleteDocument_data()
{
    QTest::addColumn<QByteArray>("output");
    const QByteArray begin = "{\"event\":\"document\",\"partial\":false,\"text\":false}\n";
    QTest::newRow("incomplete") << begin + "{\"event\":\"field\",\"name\":\"plainText\",\"value\":\"Incomplete\"}\n";
    QTest::newRow("unknown-field") << begin + "{\"event\":\"field\",\"name\":\"unbounded\",\"value\":\"x\"}\n";
    QTest::newRow("wrong-payload") << QByteArray("{\"event\":\"begin\",\"partial\":false,\"notice\":\"\"}\n");
    QTest::newRow("image-budget") << begin + "{\"event\":\"image\",\"id\":\"dzurkel-image:0\",\"width\":5000,\"height\":5000,\"format\":5,\"bytes\":100000000,\"profileBytes\":0}\n";
    QTest::newRow("missing-image-data") << begin + "{\"event\":\"image\",\"id\":\"dzurkel-image:0\",\"width\":1,\"height\":1,\"format\":5,\"bytes\":4,\"profileBytes\":0}\n{\"event\":\"image-end\"}\n";
    QTest::newRow("external-dependency") << QByteArray("{\"event\":\"error\",\"message\":\"x\",\"missingLibrary\":\"unknown\"}\n");
}
void DocumentTest::helperRejectsIncompleteDocument()
{
    QFETCH(QByteArray, output);
    const auto program = directory_.filePath("incomplete.py");
    QFile script(program);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/usr/bin/python3\nimport sys\nsys.stdout.buffer.write(bytes.fromhex('" + output.toHex() + "'))\nsys.stdout.flush()\n");
    script.close();
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto resource = prepare(write("incomplete.rtf", "{\\rtf1 Early}"));
    const auto result = loadPrepared(resource, program);
    QCOMPARE(result.kind, PreviewKind::Information);
    QVERIFY(!result.document);
}

void DocumentTest::epubPortionsContinueAndReplaySemanticBlocks()
{
    QByteArray body("<div id=\"whole\"><h1>Long chapter</h1>");
    for (int i = 0; i < 1800; ++i)
        body += "<p id=\"p" + QByteArray::number(i) + "\">Paragraph " + QByteArray::number(i) + " <b>" + QByteArray(180, 'x') + "</b> café 世界😀</p>";
    body += "</div>";
    const auto path = archive("epub", epub(body));
    EpubPortionReader reader(path);
    QString token = QStringLiteral("0"), previous;
    QString text;
    int portions = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        std::shared_ptr<const FormattedDocument> document;
        QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), document));
        QVERIFY(document->plainText.size() < 20000);
        QVERIFY(!document->partial);
        text += document->plainText;
        previous = reply.value("previous").toString();
        token = reply.value("next").toString();
        ++portions;
    } while (!token.isEmpty());
    QVERIFY(text.size() > DocumentReader::CharacterLimit);
    QCOMPARE(text.count(QStringLiteral("Paragraph ")), 1800);
    QVERIFY(text.contains(QString::fromUtf8("café 世界😀")));
    QVERIFY(portions > 20);
    for (int count = 0; count < 3; ++count) {
        const auto reply = reader.read(previous);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        previous = reply.value("previous").toString();
    }
    const auto target = QStringLiteral("a") + QString::fromLatin1(QByteArray("OPS/z.xhtml").toHex()) + '-' + QString::fromLatin1(QByteArray("p1500").toHex());
    auto reply = reader.read(target);
    QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
    std::shared_ptr<const FormattedDocument> document;
    QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), document));
    QVERIFY(document->plainText.contains(QStringLiteral("Paragraph 1500 ")));
    QCOMPARE(document->targetAnchor, target);
    QVERIFY(QFile::remove(path));
    QVERIFY(!reader.read(reply.value("previous").toString()).contains("error"));
    auto many = epub("<p>0</p>");
    for (int i = 1; i < 140; ++i) {
        const auto number = QByteArray::number(i);
        many["OPS/book.opf"].replace("</manifest>", "<item id=\"c" + number + "\" href=\"c" + number + ".xhtml\" media-type=\"application/xhtml+xml\"/></manifest>");
        many["OPS/book.opf"].replace("</spine>", "<itemref idref=\"c" + number + "\"/></spine>");
        many[QStringLiteral("OPS/c%1.xhtml").arg(i)] = "<html><body><p>" + number + "</p></body></html>";
    }
    EpubPortionReader chapters(archive("epub", many));
    token = QStringLiteral("0");
    for (int i = 0; i < 140; ++i) {
        reply = chapters.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), document));
        QCOMPARE(document->plainText.trimmed(), QString::number(i));
        token = reply.value("next").toString();
    }
    QVERIFY(token.isEmpty());
}
void DocumentTest::epubDefersChaptersAndKeepsImagesAndLinks()
{
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(&buffer, "PNG"));
    auto parts = epub("<h1>First</h1><p>Hello <b>bold</b> <a href=\"second.xhtml#destination\">Next</a></p><img src=\"red.png\"/>",
        "<item id=\"two\" href=\"second.xhtml\" media-type=\"application/xhtml+xml\"/>", "<itemref idref=\"two\"/>");
    parts["OPS/red.png"] = png;
    const auto longId = QString(QChar(0x754c)).repeated(200);
    parts["OPS/second.xhtml"] = "<html><body><h1 id=\"destination\">Second</h1><p id=\"" + longId.toUtf8() + "\">Target</p><img src=\"red.png\"/></body></html>";
    FileResource resource;
    resource.path = archive("epub", parts);
    LazyCoordinator coordinator(resource, QStringLiteral("epub"));
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready), failed(&coordinator, &LazyCoordinator::failed);
    coordinator.request();
    QTRY_COMPARE(ready.size(), 1);
    auto portion = qvariant_cast<LazyPortion>(ready.last().first());
    std::shared_ptr<const FormattedDocument> document;
    QVERIFY(decodeDocumentPortion(portion.content, document));
    QCOMPARE(document->navigation.size(), 2);
    QCOMPARE(document->images.size(), 1);
    QCOMPARE(document->images.cbegin().value().pixelColor(0, 0), QColor(Qt::red));
    QVERIFY(document->html.contains(QStringLiteral("<b>bold</b>")));
    QVERIFY(!document->plainText.contains(QStringLiteral("Second")));
    coordinator.request(portion.next);
    QTRY_COMPARE(ready.size(), 2);
    portion = qvariant_cast<LazyPortion>(ready.last().first());
    QVERIFY(decodeDocumentPortion(portion.content, document));
    QVERIFY(document->plainText.contains(QStringLiteral("Second")));
    QCOMPARE(document->images.cbegin().value().pixelColor(0, 0), QColor(Qt::red));
    const auto longAnchor = QStringLiteral("a") + QString::fromLatin1(QByteArray("OPS/second.xhtml").toHex()) + '-' + QString::fromLatin1(longId.toUtf8().toHex());
    QVERIFY(longAnchor.size() > 1024);
    coordinator.request(longAnchor);
    QTRY_COMPARE(ready.size(), 3);
    portion = qvariant_cast<LazyPortion>(ready.last().first());
    QVERIFY(decodeDocumentPortion(portion.content, document));
    QCOMPARE(document->targetAnchor, longAnchor);
    QVERIFY(failed.isEmpty());
    QVERIFY(coordinator.cachedBytes() <= coordinator.cacheBudget());
    coordinator.stop();
    parts["OPS/second.xhtml"] = "<html><broken";
    EpubPortionReader excessive(archive("epub", epub("<p>" + QByteArray(DocumentReader::CharacterLimit + 1, 'x') + "</p>")));
    QVERIFY(excessive.read(QStringLiteral("0")).value("error").toString().contains(QStringLiteral("per-portion")));
    EpubPortionReader malformed(archive("epub", parts));
    const auto first = malformed.read(QStringLiteral("0"));
    QVERIFY(!first.contains("error"));
    QVERIFY(malformed.read(first.value("next").toString()).contains("error"));
    EpubPortionReader mutableReader(resource.path);
    auto reply = mutableReader.read(QStringLiteral("0"));
    QVERIFY(!reply.contains("error"));
    QFile file(resource.path);
    QVERIFY(file.open(QIODevice::Append));
    file.write("changed"); file.close();
    QVERIFY(mutableReader.read(reply.value("next").toString()).value("error").toString().contains(QStringLiteral("changed")));
}
void DocumentTest::remainingDocumentPortionsContinueAndReplay_data()
{
    QTest::addColumn<QString>("format");
    for (const auto *format : {"rtf", "fb2", "sfb", "mobi", "markdown"}) QTest::newRow(format) << QString::fromLatin1(format);
}
void DocumentTest::remainingDocumentPortionsContinueAndReplay()
{
    QFETCH(QString, format);
    QByteArray content;
    for (int i = 0; i < 1600; ++i) {
        const auto text = "Paragraph " + QByteArray::number(i) + " " + QByteArray(200, 'x');
        if (format == "rtf") content += text + "\\par ";
        else if (format == "fb2") content += "<p><strong>" + text + "</strong></p>";
        else if (format == "sfb") content += "__" + text + "__\n";
        else if (format == "mobi") content += "<p><b>" + text + "</b></p>";
        else content += "**" + text + "**\n\n";
    }
    if (format == "rtf") content = "{\\rtf1\\ansi\\b " + content + "}";
    if (format == "fb2") content = "<FictionBook><body><section>" + content + "</section></body></FictionBook>";
    if (format == "mobi") {
        QList<QByteArray> records;
        for (qsizetype i = 0; i < content.size(); i += 4096) records.append(content.mid(i, 4096));
        content = mobi(records);
    }
    const auto path = write("progressive." + (format == "markdown" ? QStringLiteral("md") : format), content);
    RichDocumentPortionReader reader(path, format);
    QString token = QStringLiteral("0"), replayToken, reconstructed;
    QJsonObject saved;
    int count = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        std::shared_ptr<const FormattedDocument> document;
        QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), document));
        QVERIFY(document->plainText.size() < 40000);
        reconstructed += document->plainText;
        QTextDocument rendered; rendered.setHtml(document->html);
        const auto formatted = rendered.find(QStringLiteral("Paragraph"));
        if (!formatted.isNull()) QVERIFY(formatted.charFormat().fontWeight() >= QFont::Bold);
        if (++count == 3) { saved = reply; replayToken = token; }
        token = reply.value("next").toString();
    } while (!token.isEmpty() && count < 150);
    QVERIFY(count > 20); QVERIFY(count < 150);
    QVERIFY(reconstructed.size() > DocumentReader::CharacterLimit);
    for (int i = 0; i < 1600; ++i) QVERIFY2(reconstructed.contains(QStringLiteral("Paragraph %1 ").arg(i)), qPrintable(QString::number(i)));
    QCOMPARE(reader.read(replayToken), saved);
    RichDocumentPortionReader unlinked(path, format);
    const auto first = unlinked.read(QStringLiteral("0"));
    QVERIFY(!first.contains("error")); QVERIFY(QFile::remove(path));
    QVERIFY(!unlinked.read(first.value("next").toString()).contains("error"));
}
void DocumentTest::documentWindowsContinueAndEvict_data()
{
    remainingDocumentPortionsContinueAndReplay_data();
}
void DocumentTest::documentWindowsContinueAndEvict()
{
    QFETCH(QString, format);
    QByteArray content;
    for (int i = 0; i < 1200; ++i) {
        const auto text = "Visible " + QByteArray::number(i) + " " + QByteArray(200, 'x');
        if (format == "rtf") content += text + "\\par ";
        else if (format == "fb2") content += "<p>" + text + "</p>";
        else if (format == "sfb") content += text + "\n";
        else if (format == "mobi") content += "<p>" + text + "</p>";
        else content += text + "\n\n";
    }
    if (format == "rtf") content = "{\\rtf1 " + content + "}";
    if (format == "fb2") content = "<FictionBook><body>" + content + "</body></FictionBook>";
    if (format == "mobi") {
        QList<QByteArray> records;
        for (qsizetype i = 0; i < content.size(); i += 4096) records.append(content.mid(i, 4096));
        content = mobi(records);
    }
    PreviewController controller; QString error;
    QVERIFY(controller.show(write("window." + (format == "markdown" ? QStringLiteral("md") : format), content), error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window)); QTest::qWait(200);
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText")); QVERIFY(browser);
    const auto geometry = window->geometry();
    for (int direction : {1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1}) {
        browser->verticalScrollBar()->setValue(direction > 0 ? browser->verticalScrollBar()->maximum() : 0);
        const auto old = browser->toPlainText();
        auto block = browser->cursorForPosition(QPoint(30, 30)).block();
        while (block.isValid() && block.text().size() < 5) block = block.next();
        QVERIFY(block.isValid());
        auto anchor = QTextCursor(block);
        anchor.setPosition(block.position() + 1);
        anchor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 3);
        const auto selected = anchor.selectedText(); browser->setTextCursor(anchor);
        QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120 * direction), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(browser->viewport(), &wheel);
        QTRY_VERIFY(browser->toPlainText() != old);
        QCOMPARE(browser->textCursor().selectedText(), selected);
        QVERIFY(browser->document()->rootFrame()->childFrames().size() <= 3);
        QVERIFY(browser->document()->characterCount() < 60000);
        QCOMPARE(window->geometry(), geometry);
    }
    controller.hide();
}
void DocumentTest::legacyPortionsBackpressureAndReplay()
{
    const auto oldPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
    const auto bin = directory_.filePath("lazy-bin"); QVERIFY(QDir().mkpath(bin));
    const auto program = bin + QStringLiteral("/antiword");
    const auto script = QByteArray("#!/usr/bin/python3\nimport os,sys\nassert open(sys.argv[-1], 'rb').read(8).hex() == 'd0cf11e0a1b11ae1'\nfor i in range(18000):\n sys.stdout.write('Legacy %d '%i + 'x'*100 + '\\n')\n");
    QFile executable(program); QVERIFY(executable.open(QIODevice::WriteOnly)); executable.write(script); executable.close();
    QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    qputenv("PATH", bin.toUtf8() + ':' + oldPath);
    const auto path = write("lazy.doc", QByteArray::fromHex("d0cf11e0a1b11ae1"));
    RichDocumentPortionReader reader(path, QStringLiteral("doc"));
    QString token = QStringLiteral("0"), replayToken, all;
    QJsonObject saved;
    for (int i = 0; !token.isEmpty() && i < 400; ++i) {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        std::shared_ptr<const FormattedDocument> document; QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), document));
        all += document->plainText;
        if (i == 2) { saved = reply; replayToken = token; }
        token = reply.value("next").toString();
        if (i == 6) QTest::qWait(50); // Let the extractor fill its bounded kernel pipe.
    }
    QVERIFY(token.isEmpty()); QVERIFY(all.size() > 1024 * 1024);
    QVERIFY(all.contains(QStringLiteral("Legacy 17999 ")));
    QCOMPARE(reader.read(replayToken), saved);
    PreviewController controller; QString error;
    QVERIFY(controller.show(path, error)); QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window)); QTest::qWait(200);
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText")); QVERIFY(browser);
    const auto geometry = window->geometry();
    for (int i = 0; i < 8; ++i) {
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        const auto old = browser->toPlainText();
        QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(browser->viewport(), &wheel); QTRY_VERIFY(browser->toPlainText() != old);
        QVERIFY(browser->document()->rootFrame()->childFrames().size() <= 3);
        QVERIFY(browser->document()->characterCount() < 40000); QCOMPARE(window->geometry(), geometry);
    }
    controller.hide(); QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0);
}
void DocumentTest::documentContinuationKeepsEncodingAndLateErrors()
{
    const auto text = QString(4095, QChar('a')) + QStringLiteral("€ Кирилица 日本語 ").repeated(20000);
    for (const auto &format : {QStringLiteral("rtf"), QStringLiteral("mobi")}) {
        QByteArray bytes;
        if (format == "rtf") bytes = "{\\rtf1\\ansi\\ansicpg65001\\b " + text.toUtf8() + "}";
        else {
            const auto html = "<p><b>" + text.toUtf8() + "</b></p>";
            QList<QByteArray> records;
            for (qsizetype i = 0; i < html.size(); i += 4096) records.append(html.mid(i, 4096));
            bytes = mobi(records);
        }
        RichDocumentPortionReader reader(write("unicode." + format, bytes), format);
        QString token = QStringLiteral("0"), all;
        do {
            const auto reply = reader.read(token); QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
            std::shared_ptr<const FormattedDocument> portion; QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), portion));
            if (!portion->plainText.contains(QStringLiteral("[Empty section]"))) all += portion->plainText;
            token = reply.value("next").toString();
        } while (!token.isEmpty());
        all.remove(QChar('\n'));
        QCOMPARE(all, text);
    }
    RichDocumentPortionReader unsafe(write("dtd.fb2", "<!DOCTYPE FictionBook [<!ENTITY bad 'bad'>]><FictionBook><body><p>&bad;</p></body></FictionBook>"), QStringLiteral("fb2"));
    QVERIFY(unsafe.read(QStringLiteral("0")).contains("error"));
    QByteArray content("<FictionBook><body>");
    for (int i = 0; i < 100; ++i) content += "<p>Useful " + QByteArray::number(i) + " " + QByteArray(200, 'x') + "</p>";
    content += "<broken";
    PreviewController controller; QString error;
    QVERIFY(controller.show(write("late.fb2", content), error)); QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText")); QVERIFY(browser);
    auto *status = window->findChild<QLabel *>(QStringLiteral("documentLoadingStatus")); QVERIFY(status);
    for (int i = 0; i < 4 && !status->text().contains(QStringLiteral("unsupported")); ++i) {
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(browser->viewport(), &wheel);
        QTest::qWait(100);
    }
    QTRY_VERIFY(status->text().contains(QStringLiteral("unsupported")));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("Useful")));
    QCOMPARE(controller.previewKind(), QStringLiteral("document")); controller.hide();
}

void DocumentTest::markdownKeepsStructuresAndForwardReferences()
{
    QByteArray source("# Beginning\n\n[Forward][target]\n\n");
    for (int i = 0; i < 40; ++i) source += "```cpp\n" + QByteArray(300, 'x') + "\n\n# still code\n```\n\n1. First\n2. Second\n\n    nested paragraph\n\n| A | B |\n|---|---|\n| cell | value |\n\n";
    source += "# Next\n\n[target]: https://example.test/forward \"title\"\n\nEnd\n";
    RichDocumentPortionReader reader(write("structures.md", source), QStringLiteral("markdown"));
    QString token = QStringLiteral("0"); QString all;
    int tables = 0, lists = 0, code = 0; bool reference = false;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        std::shared_ptr<const FormattedDocument> portion; QVERIFY(decodeDocumentPortion(reply.value("content").toObject(), portion));
        QTextDocument document; document.setHtml(portion->html);
        for (auto *frame : document.rootFrame()->childFrames()) tables += qobject_cast<QTextTable *>(frame) != nullptr;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            if (block.text() == QStringLiteral("First")) { QVERIFY(block.textList()); ++lists; }
            if (block.text() == QStringLiteral("# still code")) { QCOMPARE(block.blockFormat().headingLevel(), 0); ++code; }
        }
        auto link = document.find(QStringLiteral("Forward"));
        if (!link.isNull()) { QCOMPARE(link.charFormat().anchorHref(), QStringLiteral("https://example.test/forward")); reference = true; }
        all += portion->plainText;
        token = reply.value("next").toString();
    } while (!token.isEmpty());
    QCOMPARE(tables, 40); QCOMPARE(lists, 40); QCOMPARE(code, 40); QVERIFY(reference); QVERIFY(all.contains(QStringLiteral("End")));
}

void DocumentTest::wordPortionsStreamAndReplayWithNumbering()
{
    QByteArray body;
    for (int i = 0; i < 1800; ++i) {
        const auto text = QStringLiteral("Item %1 ").arg(i) + QString(200, QChar('x'));
        body += "<w:p pad=\"" + QByteArray(6000, 'a') + "\"><w:pPr><w:pStyle w:val=\"List\"/></w:pPr><w:r><w:t>"
            + text.toUtf8() + "</w:t></w:r></w:p>";
    }
    auto parts = docx(body);
    parts["word/_rels/content.xml.rels"] = "<Relationships><Relationship Id=\"s\" Type=\"" + officeNs
        + "styles\" Target=\"styles.xml\"/><Relationship Id=\"n\" Type=\"" + officeNs + "numbering\" Target=\"numbering.xml\"/></Relationships>";
    parts["word/styles.xml"] = "<w:styles xmlns:w=\"" + wordNs + "\"><w:style w:styleId=\"List\"><w:pPr><w:numPr><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:rPr><w:b/></w:rPr></w:style></w:styles>";
    parts["word/numbering.xml"] = "<w:numbering xmlns:w=\"" + wordNs + "\"><w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:start w:val=\"7\"/><w:numFmt w:val=\"decimal\"/></w:lvl></w:abstractNum><w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num></w:numbering>";
    QVERIFY(parts["word/content.xml"].size() > 8 * 1024 * 1024);
    const auto path = archive("docx", parts);
    RichDocumentPortionReader reader(path, QStringLiteral("docx"));
    QString token = QStringLiteral("0"), reconstructed;
    QJsonObject saved;
    QString savedToken;
    int items = 0, loads = 0;
    do {
        const auto response = reader.read(token);
        QVERIFY2(!response.contains("error"), qPrintable(response.value("error").toString()));
        std::shared_ptr<const FormattedDocument> document;
        QVERIFY(decodeDocumentPortion(response.value("content").toObject(), document));
        QVERIFY(document->plainText.size() < 20000);
        if (!document->plainText.contains(QStringLiteral("[Empty section]"))) {
            QVERIFY(document->html.contains(QStringLiteral("start=\"%1\"").arg(items + 7)));
            QVERIFY(document->html.contains(QStringLiteral("<b>")));
            items += document->plainText.count(QStringLiteral("Item "));
            reconstructed += document->plainText.trimmed() + QChar('\n');
        }
        if (++loads == 3) { saved = response; savedToken = token; }
        token = response.value("next").toString();
    } while (!token.isEmpty() && loads < 100);
    QCOMPARE(items, 1800);
    QVERIFY(reconstructed.size() > DocumentReader::CharacterLimit);
    QVERIFY(loads > 30);
    const auto replay = reader.read(savedToken);
    QCOMPARE(replay, saved);
    auto broken = parts;
    broken["word/content.xml"].chop(20);
    RichDocumentPortionReader damaged(archive("docx", broken), QStringLiteral("docx"));
    token = QStringLiteral("0"); loads = 0;
    while (!token.isEmpty() && ++loads < 100) {
        const auto reply = damaged.read(token);
        if (reply.contains("error")) break;
        token = reply.value("next").toString();
    }
    QVERIFY(loads > 2); QVERIFY(loads < 100); QVERIFY(!token.isEmpty());
    QByteArray costly;
    for (int i = 0; i < 48; ++i) costly += "<w:p cost=\"" + QByteArray(100000, 'x')
        + "\"><w:r><w:t>Small visible text " + QByteArray::number(i) + "</w:t></w:r></w:p>";
    RichDocumentPortionReader budgeted(archive("docx", docx(costly)), QStringLiteral("docx"));
    const auto bounded = budgeted.read(QStringLiteral("0"));
    QVERIFY(!bounded.contains("error"));
    const auto boundary = bounded.value("next").toString();
    QVERIFY(!boundary.isEmpty()); QVERIFY(boundary.toInt() < 48);
    QVERIFY(!budgeted.read(boundary).contains("error"));
    QCOMPARE(budgeted.read(QStringLiteral("0")), bounded);
    RichDocumentPortionReader changed(path, QStringLiteral("docx"));
    const auto initial = changed.read(QStringLiteral("0"));
    QVERIFY(!initial.contains("error"));
    QFile file(path); QVERIFY(file.open(QIODevice::Append)); file.write("changed"); file.close();
    QVERIFY(changed.read(initial.value("next").toString()).value("error").toString().contains(QStringLiteral("changed")));
}
void DocumentTest::wordWindowEvictsWithStableSelectionAndGeometry()
{
    QByteArray body;
    for (int i = 0; i < 1500; ++i)
        body += "<w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Paragraph " + QByteArray::number(i) + " " + QByteArray(200, 'x') + "</w:t></w:r></w:p>";
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(archive("docx", docx(body)), error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(200);
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText"));
    QVERIFY(browser);
    const auto geometry = window->geometry();
    for (int load = 0; load < 14; ++load) {
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        const auto old = browser->toPlainText();
        auto anchor = browser->cursorForPosition(QPoint(30, 30));
        anchor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 3);
        const auto selected = anchor.selectedText();
        const auto block = anchor.block().text();
        const auto top = browser->cursorRect(anchor).top();
        const auto offset = anchor.positionInBlock();
        browser->setTextCursor(anchor);
        QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(browser->viewport(), &wheel);
        QTRY_VERIFY(browser->toPlainText() != old);
        auto restored = browser->document()->find(block); QVERIFY(!restored.isNull());
        restored.setPosition(restored.selectionStart() + offset);
        QTRY_COMPARE(browser->cursorRect(restored).top(), top);
        QCOMPARE(browser->textCursor().selectedText(), selected);
        QVERIFY(browser->document()->rootFrame()->childFrames().size() <= 3);
        QVERIFY(browser->document()->characterCount() < 40000);
        QCOMPARE(window->geometry(), geometry);
    }
    for (int load = 0; load < 5; ++load) {
        browser->verticalScrollBar()->setValue(0);
        const auto old = browser->toPlainText();
        QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(browser->viewport(), &wheel);
        QTRY_VERIFY(browser->toPlainText() != old);
        QCOMPARE(window->geometry(), geometry);
    }
    controller.hide();
}

void DocumentTest::epubFormatsOnlyInsertedPortion()
{
    QByteArray body("<h1>Resident heading</h1><p>Resident color</p>");
    for (int i = 0; i < 180; ++i)
        body += "<h2>Heading " + QByteArray::number(i) + "</h2><p>Body "
            + QByteArray::number(i) + " <b>" + QByteArray(180, 'x') + "</b></p>";
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(archive("epub", epub(body)), error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText"));
    auto *view = window->findChild<DocumentView *>();
    QVERIFY(browser); QVERIFY(view);
    auto palette = view->palette();
    palette.setColor(QPalette::Base, QColor("#121212"));
    palette.setColor(QPalette::Text, Qt::white);
    view->setPalette(palette);
    view->zoomBy(2);
    auto *document = browser->document();
    document->documentLayout()->documentSize();
    const auto oldText = browser->toPlainText();
    const auto resident = document->find(QStringLiteral("Resident heading"));
    const auto residentFormat = resident.charFormat();
    const auto bodySize = document->find(QStringLiteral("Body 0")).charFormat().fontPointSize();
    QPointer<QTextFrame> frame = document->rootFrame()->childFrames().first();
    // QTextDocument reports text and format changes through the same public
    // signal. A continuation must not reformat the resident heading at the top.
    QSignalSpy changes(document, &QTextDocument::contentsChange);
    browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
    QWheelEvent wheel(QPointF(30, 30), browser->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(browser->viewport(), &wheel);
    QTRY_VERIFY(browser->toPlainText() != oldText);
    QCOMPARE(document->rootFrame()->childFrames().size(), 2);
    QVERIFY(frame);
    QCOMPARE(document->find(QStringLiteral("Resident heading")).charFormat(), residentFormat);
    QVERIFY(!changes.isEmpty());
    for (const auto &change : changes) QVERIFY(change.at(0).toInt() > resident.selectionEnd());
    auto *added = document->rootFrame()->childFrames().last();
    bool heading = false, bodyFormatted = false;
    for (auto block = added->firstCursorPosition().block(); block.isValid() && block.position() <= added->lastPosition(); block = block.next()) {
        auto cursor = QTextCursor(block);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (block.text().startsWith(QStringLiteral("Heading "))) {
            QVERIFY(cursor.charFormat().fontPointSize() > bodySize);
            heading = true;
        } else if (block.text().startsWith(QStringLiteral("Body "))) {
            QCOMPARE(cursor.charFormat().fontPointSize(), bodySize);
            bodyFormatted = true;
        }
    }
    QVERIFY(heading); QVERIFY(bodyFormatted);
    // Explicit theme/zoom changes still update all resident portions.
    palette.setColor(QPalette::Base, Qt::white); palette.setColor(QPalette::Text, Qt::black);
    view->setPalette(palette);
    view->fit();
    QVERIFY(document->find(QStringLiteral("Resident heading")).charFormat().fontPointSize() < residentFormat.fontPointSize());
    controller.hide();
}

void DocumentTest::epubWindowEvictsAndNavigatesWithStableGeometry()
{
    QByteArray body("<h1>First chapter</h1>");
    for (int i = 0; i < 2000; ++i) {
        const auto number = QByteArray::number(i);
        body += "<p id=\"p" + number + "\">Paragraph " + number + " <b>" + QByteArray(180, 'x') + "</b>";
        for (int run = 0; run < 5; ++run) body += "<span id=\"r" + number + '-' + QByteArray::number(run) + "\">x</span>";
        body += "</p>";
        if (i % 40 == 0) body += "<table><tr><th>Header " + number + "</th></tr><tr><td>Cell " + number + "</td></tr></table><ul><li>List item " + number + "</li></ul><p>Image " + number + "<img src=\"red.png\"/></p>";
    }
    auto parts = epub(body, "<item id=\"two\" href=\"next.xhtml\" media-type=\"application/xhtml+xml\"/>", "<itemref idref=\"two\"/>");
    parts["OPS/next.xhtml"] = "<html><body><h1>Second chapter</h1><p>Loaded by navigation</p></body></html>";
    QByteArray png;
    QBuffer imageBuffer(&png);
    QVERIFY(imageBuffer.open(QIODevice::WriteOnly));
    QImage image(16, 16, QImage::Format_ARGB32); image.fill(Qt::red);
    QVERIFY(image.save(&imageBuffer, "PNG"));
    parts["OPS/red.png"] = png;
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(archive("epub", parts), error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("document"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(200);
    auto *browser = window->findChild<QTextBrowser *>(QStringLiteral("documentText"));
    QVERIFY(browser);
    auto *status = window->findChild<QLabel *>(QStringLiteral("documentLoadingStatus"));
    QVERIFY(status);
    const auto geometry = window->geometry();
    const auto wheel = [](QTextBrowser *view, int direction) {
        QWheelEvent event(QPointF(30, 30), view->viewport()->mapToGlobal(QPoint(30, 30)), {}, QPoint(0, -120 * direction),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &event);
    };
    bool compacted = false;
    int previousFormats = 0;
    for (int load = 0; load < 24; ++load) {
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        const auto old = browser->toPlainText();
        QTextCursor anchor = browser->cursorForPosition(QPoint(30, 30));
        anchor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 3);
        const auto top = browser->cursorRect(anchor).top();
        const auto block = anchor.block().text();
        const int offset = anchor.positionInBlock();
        const auto selected = anchor.selectedText();
        browser->setTextCursor(anchor);
        wheel(browser, 1);
        QTRY_VERIFY2(browser->toPlainText() != old, qPrintable(status->text()));
        auto restored = browser->document()->find(block);
        QVERIFY(!restored.isNull());
        restored.setPosition(restored.selectionStart() + offset);
        QTRY_COMPARE(browser->cursorRect(restored).top(), top);
        QCOMPARE(browser->textCursor().selectedText(), selected);
        const int formats = int(browser->document()->allFormats().size());
        compacted |= formats < previousFormats;
        previousFormats = formats;
        QVERIFY(formats < 10000);
        QVERIFY(browser->document()->rootFrame()->childFrames().size() <= 3);
        QVERIFY(browser->document()->characterCount() < 40000);
        bool bold = false, imageFound = false, tableFound = false;
        QList<QTextFrame *> frames{browser->document()->rootFrame()};
        while (!frames.isEmpty()) {
            auto *frame = frames.takeLast(); frames.append(frame->childFrames());
            tableFound |= qobject_cast<QTextTable *>(frame) != nullptr;
        }
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
            for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
                const auto format = fragment.fragment().charFormat();
                bold |= format.fontWeight() >= QFont::Bold;
                if (format.isImageFormat()) {
                    const auto resource = browser->document()->resource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name()));
                    QVERIFY(resource.canConvert<QImage>());
                    QCOMPARE(qvariant_cast<QImage>(resource).pixelColor(0, 0), QColor(Qt::red));
                    imageFound = true;
                }
            }
        QVERIFY(bold); QVERIFY(tableFound); QVERIFY(imageFound);
        QCOMPARE(window->geometry(), geometry);
    }
    QVERIFY(compacted);
    QVERIFY(!browser->toPlainText().contains(QStringLiteral("First chapter")));
    for (int load = 0; load < 30 && !browser->toPlainText().contains(QStringLiteral("First chapter")); ++load) {
        browser->verticalScrollBar()->setValue(0);
        const auto old = browser->toPlainText();
        wheel(browser, -1);
        QTRY_VERIFY2(browser->toPlainText() != old, qPrintable(status->text()));
    }
    QVERIFY(browser->toPlainText().contains(QStringLiteral("First chapter")));
    auto *contents = window->findChild<QToolButton *>(QStringLiteral("documentContents"));
    QVERIFY(contents);
    QVERIFY(contents->menu()->actions().size() >= 2);
    const auto broken = QStringLiteral("a") + QString::fromLatin1(QByteArray("OPS/next.xhtml").toHex()) + '-' + QString::fromLatin1(QByteArray("missing").toHex());
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl(QChar('#') + broken))));
    QTRY_VERIFY(status->text().contains(QStringLiteral("could not be located")));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("First chapter")));
    contents->menu()->actions().at(1)->trigger();
    QTRY_VERIFY2(browser->toPlainText().contains(QStringLiteral("Loaded by navigation")), qPrintable(status->text()));
    QVERIFY(!browser->toPlainText().contains(QStringLiteral("First chapter")));
    QCOMPARE(window->geometry(), geometry);
    contents->menu()->actions().first()->trigger();
    QTRY_VERIFY(browser->toPlainText().contains(QStringLiteral("First chapter")));
    QCOMPARE(window->geometry(), geometry);
    controller.hide();
}
void DocumentTest::optionalPrivateSamples()
{
    const auto paths = qEnvironmentVariable("DZURKEL_DOCUMENT_SAMPLES");
    if (paths.isEmpty()) QSKIP("Set DZURKEL_DOCUMENT_SAMPLES to private sample directories for local validation.");
    int total = 0, partial = 0;
    QElapsedTimer clock; clock.start();
    for (const auto &path : paths.split(QChar(':'))) {
        QDirIterator files(path, {"*.docx", "*.doc", "*.epub", "*.rtf", "*.fb2", "*.sfb", "*.mobi"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto name = files.next();
            const auto format = QFileInfo(name).suffix().toLower();
            QFile source(name); QVERIFY(source.open(QIODevice::ReadOnly));
            const auto mime = QMimeDatabase().mimeTypeForFileNameAndData(name, &source);
            source.seek(0);
            QCOMPARE(PreviewLibrary::select(source, mime, name), format);
            const auto result = read(name, format);
            // Do not print names, metadata or document content into test logs.
            QVERIFY2(result.document, qPrintable(result.error));
            QVERIFY(!result.document->plainText.trimmed().isEmpty());
            partial += result.document->partial;
            DocumentView view(result.document);
            view.resize(700, 500);
            QVERIFY(!view.findChild<QTextBrowser *>()->toPlainText().trimmed().isEmpty());
            if (qEnvironmentVariableIsSet("DZURKEL_DOCUMENT_SCREENSHOTS") && QStringList{"fb2", "sfb", "mobi"}.contains(format)) {
                view.resize(700, 900); view.show();
                QVERIFY(QTest::qWaitForWindowExposed(&view));
                QVERIFY(view.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/ebook-%1.png").arg(format)));
            }
            ++total;
        }
    }
    QVERIFY(total > 0);
    qInfo("Private sample validation: %d files, %d partial previews, %lld ms", total, partial, qlonglong(clock.elapsed()));
}
QTEST_MAIN(DocumentTest)
#include "DocumentTest.moc"

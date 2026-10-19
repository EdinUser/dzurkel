#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/readers/DocumentReader.h"
#include "preview/document/DocumentView.h"
#include "controller/PreviewController.h"
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
#include <zip.h>
#include <QtEndian>

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
private:
    QString write(const QString &name, const QByteArray &bytes);
    QString archive(const QString &suffix, const Parts &parts);
    DocumentResult read(const QString &path, const QString &format);
    QTemporaryDir directory_;
    DocumentFuse fuse_;
    int serial_ = 0;
};
void DocumentTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath("config").toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
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
        QCOMPARE(PreviewLibrary::capability(QLatin1String(suffix))->sourceLimit, DocumentReader::ByteLimit);
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
    result = read(write("large.rtf", QByteArray(DocumentReader::ByteLimit + 1, 'x')), "rtf");
    QVERIFY(!result.document);
    auto parts = epub("<p>early</p>");
    parts.insert("ignored.bin", QByteArray(65 * 1024 * 1024, 'x'));
    QVERIFY(!read(archive("epub", parts), "epub").document);
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
    const auto generalSize = window.size();
    show(data); QTRY_COMPARE(window.size(), readingSize);
    PreviewData pdf; pdf.kind = PreviewKind::Pdf; show(pdf); QTRY_COMPARE(window.size(), generalSize);
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
    show(table); QTRY_COMPARE(window.size(), generalSize);
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

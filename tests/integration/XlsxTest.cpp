#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/readers/XlsxReader.h"
#include "controller/PreviewController.h"
#include "preview/table/SpreadsheetView.h"
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <zip.h>
#include <thread>

using namespace dzurkel;
namespace {
const QByteArray ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const QByteArray relns = "http://schemas.openxmlformats.org/package/2006/relationships";
const QByteArray reltype = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
QByteArray sheet(const QByteArray &body) {
    return "<worksheet xmlns=\"" + ns + "\"><dimension ref=\"A1:XFD1048576\"/><sheetData>" + body + "</sheetData></worksheet>";
}
QMap<QString, QByteArray> package(const QByteArray &body = "<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>00123</t></is></c></row>") {
    return {
        {QStringLiteral("_rels/.rels"), "<Relationships xmlns=\"" + relns + "\"><Relationship Id=\"main\" Type=\"" + reltype + "officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"},
        {QStringLiteral("xl/workbook.xml"), "<workbook xmlns=\"" + ns + "\" xmlns:r=\"" + reltype.chopped(1) + "\"><sheets><sheet name=\"First\" sheetId=\"1\" r:id=\"r1\"/></sheets></workbook>"},
        {QStringLiteral("xl/_rels/workbook.xml.rels"), "<Relationships xmlns=\"" + relns + "\"><Relationship Id=\"r1\" Type=\"" + reltype + "worksheet\" Target=\"worksheets/data.xml\"/></Relationships>"},
        {QStringLiteral("xl/worksheets/data.xml"), sheet(body)}
    };
}
}
class XlsxFuse : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
public slots:
    QString mountUrl(const QString &) { return path; }
};
class XlsxTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void storedValuesAndMissingFormulaCaches();
    void formatsDatesAndTimes_data();
    void formatsDatesAndTimes();
    void multipleSheetsAndStrictNamespaces();
    void actualCellsDetermineExtentAndPartialLimits();
    void partialAndMissingResultsAreVisible();
    void rejectsMalformedAndOversized_data();
    void rejectsMalformedAndOversized();
    void cancellationStopsLoading();
    void snapshotAndRemoteUseSharedGrid_data();
    void snapshotAndRemoteUseSharedGrid();
    void inspectUserSample();
private:
    QString write(const QMap<QString, QByteArray> &parts);
    XlsxResult read(const QString &path, const Cancellation &cancel = std::make_shared<std::atomic_bool>(false));
    QTemporaryDir directory_;
    XlsxFuse fuse_;
    int serial_ = 0;
};
void XlsxTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
QString XlsxTest::write(const QMap<QString, QByteArray> &parts)
{
    const auto path = directory_.filePath(QStringLiteral("sample-%1.xlsx").arg(++serial_));
    auto *zip = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    if (!zip) return {};
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        auto *source = zip_source_buffer(zip, it.value().constData(), zip_uint64_t(it.value().size()), 0);
        if (!source || zip_file_add(zip, it.key().toUtf8().constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
            if (source) zip_source_free(source);
            zip_discard(zip);
            return {};
        }
    }
    if (zip_close(zip) != 0) { zip_discard(zip); return {}; }
    return path;
}
XlsxResult XlsxTest::read(const QString &path, const Cancellation &cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QTest::qFail("Cannot open fixture", __FILE__, __LINE__); return {}; }
    return XlsxReader::read(file, cancel);
}
void XlsxTest::storedValuesAndMissingFormulaCaches()
{
    auto parts = package("<row r=\"1\">"
        "<c r=\"A1\" t=\"s\"><v>0</v></c>"
        "<c r=\"B1\"><v>12345678901234567890</v></c>"
        "<c r=\"C1\" t=\"b\"><v>1</v></c>"
        "<c r=\"D1\"><f>UNKNOWN()</f><v>0</v></c>"
        "<c r=\"E1\"><f>1+2</f></c>"
        "<c r=\"F1\"><f>1+2</f><v/></c>"
        "<c r=\"G1\" t=\"str\"><f>UNKNOWN()</f><v>saved</v></c>"
        "<c r=\"H1\" t=\"e\"><v>#N/A</v></c>"
        "<c r=\"I1\" t=\"inlineStr\"><is><r><t>line1</t></r><r><t xml:space=\"preserve\"> &amp; line2</t></r></is></c>"
        "<c r=\"J1\" t=\"d\"><v>2026-10-17</v></c>"
        "<c r=\"K1\" s=\"100\"><v>45000</v></c>"
        "<c r=\"L1\" t=\"str\"><f>EMPTY()</f><v/></c>"
        "<c r=\"M1\" t=\"b\"><f>UNKNOWN()</f></c>"
        "</row>");
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
        "<Relationship Id=\"strings\" Type=\"" + reltype + "sharedStrings\" Target=\"strings.xml\"/></Relationships>");
    parts[QStringLiteral("xl/strings.xml")] = "<sst xmlns=\"" + ns + "\"><si><t>00123</t></si></sst>";
    const auto result = read(write(parts));
    QVERIFY2(result.document, qPrintable(result.error));
    const auto &s = result.document->sheets.first();
    QCOMPARE(s.rowCount, 1);
    QCOMPARE(s.columnCount, 13);
    QCOMPARE(s.value(0, 0).toString(), QStringLiteral("00123"));
    QCOMPARE(s.value(0, 1).toString(), QStringLiteral("12345678901234567890"));
    QCOMPARE(s.value(0, 2).toString(), QStringLiteral("TRUE"));
    QCOMPARE(s.value(0, 3).toString(), QStringLiteral("0"));
    QCOMPARE(s.value(0, 4).toString(), QStringLiteral("[Result unavailable]"));
    QCOMPARE(s.value(0, 5).toString(), QStringLiteral("[Result unavailable]"));
    QCOMPARE(s.value(0, 6).toString(), QStringLiteral("saved"));
    QCOMPARE(s.value(0, 7).toString(), QStringLiteral("#N/A"));
    QCOMPARE(s.value(0, 8).toString(), QStringLiteral("line1 & line2"));
    QCOMPARE(s.value(0, 9).toString(), QStringLiteral("2026-10-17"));
    QCOMPARE(s.value(0, 10).toString(), QStringLiteral("45000"));
    QVERIFY(!s.value(0, 11).isValid()); // A cached empty string is legitimate.
    QCOMPARE(s.value(0, 12).toString(), QStringLiteral("[Result unavailable]"));
    QVERIFY(result.document->notice.contains(QStringLiteral("3 formula result(s) unavailable")));
    QVERIFY(!result.document->partial);
}
void XlsxTest::formatsDatesAndTimes_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<QByteArray>("code");
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<bool>("date1904");
    QTest::addColumn<QString>("expected");
    QTest::newRow("1900-first-day") << 14 << QByteArray() << QByteArray("1") << false << QStringLiteral("1900-01-01");
    QTest::newRow("1900-before-leap-bug") << 14 << QByteArray() << QByteArray("59") << false << QStringLiteral("1900-02-28");
    QTest::newRow("1900-leap-bug") << 14 << QByteArray() << QByteArray("60") << false << QStringLiteral("1900-02-29 (Excel)");
    QTest::newRow("1900-after-leap-bug") << 14 << QByteArray() << QByteArray("61") << false << QStringLiteral("1900-03-01");
    QTest::newRow("1904-epoch") << 14 << QByteArray() << QByteArray("0") << true << QStringLiteral("1904-01-01");
    QTest::newRow("date-time") << 22 << QByteArray() << QByteArray("61.5") << false << QStringLiteral("1900-03-01 12:00:00");
    QTest::newRow("time") << 21 << QByteArray() << QByteArray("0.5") << false << QStringLiteral("12:00:00");
    QTest::newRow("time-fraction") << 21 << QByteArray() << QByteArray("0.5000013888888889") << false << QStringLiteral("12:00:00.120");
    QTest::newRow("date-before-midnight") << 14 << QByteArray() << QByteArray("61.9999999999") << false << QStringLiteral("1900-03-01");
    QTest::newRow("date-time-midnight-rounding") << 22 << QByteArray() << QByteArray("61.9999999999") << false << QStringLiteral("1900-03-02 00:00:00");
    QTest::newRow("duration") << 46 << QByteArray() << QByteArray("1.5") << false << QStringLiteral("36:00:00");
    QTest::newRow("custom-date") << 164 << QByteArray("yyyy-mm-dd") << QByteArray("61") << false << QStringLiteral("1900-03-01");
    QTest::newRow("custom-locale") << 164 << QByteArray("[$-409]d-mmm-yyyy") << QByteArray("61") << false << QStringLiteral("1900-03-01");
    QTest::newRow("custom-time") << 164 << QByteArray("h:mm AM/PM") << QByteArray("0.75") << false << QStringLiteral("18:00:00");
    QTest::newRow("custom-duration") << 164 << QByteArray("[hh]:mm:ss") << QByteArray("2.5") << false << QStringLiteral("60:00:00");
    QTest::newRow("quoted-date-literal") << 164 << QByteArray("0 &quot;days&quot;") << QByteArray("61") << false << QStringLiteral("61");
    QTest::newRow("escaped-date-literal") << 164 << QByteArray("0\\d") << QByteArray("61") << false << QStringLiteral("61");
    QTest::newRow("conditional-format") << 164 << QByteArray("[&gt;100]yyyy;0") << QByteArray("61") << false << QStringLiteral("61");
    QTest::newRow("plain-long-identifier") << 0 << QByteArray() << QByteArray("12345678901234567890") << false << QStringLiteral("12345678901234567890");
    QTest::newRow("negative-date") << 14 << QByteArray() << QByteArray("-1") << false << QStringLiteral("-1");
    QTest::newRow("nonfinite-date") << 14 << QByteArray() << QByteArray("NaN") << false << QStringLiteral("NaN");
    QTest::newRow("huge-date") << 14 << QByteArray() << QByteArray("1e100") << false << QStringLiteral("1e100");
}
void XlsxTest::formatsDatesAndTimes()
{
    QFETCH(int, format);
    QFETCH(QByteArray, code);
    QFETCH(QByteArray, value);
    QFETCH(bool, date1904);
    QFETCH(QString, expected);
    auto parts = package("<row><c s=\"1\"><v>" + value + "</v></c><c s=\"1\"><f>UNKNOWN()</f><v>" + value + "</v></c></row>");
    if (date1904) parts[QStringLiteral("xl/workbook.xml")].replace("<sheets>", "<workbookPr date1904=\"true\"/><sheets>");
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
        "<Relationship Id=\"styles\" Type=\"" + reltype + "styles\" Target=\"styles.xml\"/></Relationships>");
    parts[QStringLiteral("xl/styles.xml")] = "<styleSheet xmlns=\"" + ns + "\">"
        + (code.isEmpty() ? QByteArray() : "<numFmts><numFmt numFmtId=\"" + QByteArray::number(format) + "\" formatCode=\"" + code + "\"/></numFmts>")
        + "<cellXfs><xf numFmtId=\"0\"/><xf numFmtId=\"" + QByteArray::number(format) + "\"/></cellXfs></styleSheet>";
    // The same formatting path must work for Strict and transitional packages.
    for (bool strict : {false, true}) {
        if (strict) for (auto &bytes : parts) {
            bytes.replace(ns, "http://purl.oclc.org/ooxml/spreadsheetml/main");
            bytes.replace(reltype.chopped(1), "http://purl.oclc.org/ooxml/officeDocument/relationships");
        }
        const auto result = read(write(parts));
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->sheets.first().value(0, 0).toString(), expected);
        QCOMPARE(result.document->sheets.first().value(0, 1).toString(), expected);
    }
}
void XlsxTest::multipleSheetsAndStrictNamespaces()
{
    auto parts = package();
    parts[QStringLiteral("xl/workbook.xml")].replace("</sheets>", "<sheet name=\"Second\" sheetId=\"2\" r:id=\"r2\"/></sheets>");
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
        "<Relationship Id=\"r2\" Type=\"" + reltype + "worksheet\" Target=\"/xl/worksheets/second.xml\"/>"
        "<Relationship Id=\"remote\" Type=\"" + reltype + "externalLink\" Target=\"https://invalid.example/never-fetch\" TargetMode=\"External\"/></Relationships>");
    parts[QStringLiteral("xl/worksheets/second.xml")] = sheet("<row><c t=\"inlineStr\"><is><t>second</t></is></c><c r=\"C1\"><v>22</v></c></row>");
    for (auto &bytes : parts) {
        bytes.replace(ns, "http://purl.oclc.org/ooxml/spreadsheetml/main");
        bytes.replace(reltype.chopped(1), "http://purl.oclc.org/ooxml/officeDocument/relationships");
    }
    const auto result = read(write(parts));
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->sheets.size(), 2);
    QCOMPARE(result.document->sheets.last().value(0, 0).toString(), QStringLiteral("second"));
    SpreadsheetView view(result.document);
    auto *tabs = view.findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 2);
    tabs->setCurrentIndex(1);
    auto *grid = qobject_cast<QTableView *>(tabs->currentWidget());
    QCOMPARE(grid->model()->data(grid->model()->index(0, 0)).toString(), QStringLiteral("second"));
    QVERIFY(!(grid->model()->flags(grid->model()->index(0, 0)) & Qt::ItemIsEditable));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(write(parts), error));
    QTRY_VERIFY(!controller.loading());
    const auto *summary = controller.window()->findChild<QLabel *>(QStringLiteral("previewSummary"));
    QVERIFY(summary);
    QVERIFY(summary->text().contains(QStringLiteral("1 × 1")));
    controller.window()->findChild<QTabWidget *>()->setCurrentIndex(1);
    QVERIFY(summary->text().contains(QStringLiteral("1 × 3")));
}
void XlsxTest::actualCellsDetermineExtentAndPartialLimits()
{
    auto result = read(write(package()));
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->sheets.first().rowCount, 1);
    QCOMPARE(result.document->sheets.first().columnCount, 1);
    QVERIFY(!result.document->partial);
    result = read(write(package("<row r=\"1\"><c r=\"A1\"><v>1</v></c><c r=\"IW1\"><v>2</v></c></row><row r=\"1048576\"><c r=\"A1048576\"><v>3</v></c></row>")));
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->partial);
    QCOMPARE(result.document->sheets.first().cells.size(), 1);
    QCOMPARE(result.document->sheets.first().rowCount, 1);
    QVERIFY(!result.document->notice.isEmpty());
    QByteArray row = "<row r=\"1\">";
    for (int i = 0; i <= XlsxReader::CellLimit; ++i) row += "<c r=\"A1\"><v>1</v></c>";
    row += "</row>";
    result = read(write(package(row)));
    QVERIFY2(result.document, qPrintable(result.error));
    QVERIFY(result.document->partial);
}
void XlsxTest::partialAndMissingResultsAreVisible()
{
    const auto path = write(package("<row><c r=\"A1\"><f>UNKNOWN()</f></c><c r=\"IW1\"><v>1</v></c></row>"));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    auto *status = controller.window()->findChild<QLabel *>(QStringLiteral("previewStatus"));
    QVERIFY(status);
    QVERIFY(status->isVisible());
    QVERIFY(status->text().contains(QStringLiteral("Partial")));
    QVERIFY(status->toolTip().contains(QStringLiteral("1 formula result(s) unavailable")));
    auto *grid = controller.window()->findChild<QTableView *>();
    QVERIFY(grid);
    QCOMPARE(grid->model()->data(grid->model()->index(0, 0)).toString(), QStringLiteral("[Result unavailable]"));
    QVERIFY(grid->window()->grab().save(QStringLiteral("xlsx-partial-results.png")));
}
void XlsxTest::rejectsMalformedAndOversized_data()
{
    QTest::addColumn<QString>("problem");
    for (const auto &p : {"xml", "dtd", "shared-index", "traversal", "external-sheet", "field", "part", "archive", "entries", "missing", "styles-xml", "styles-format", "styles-field", "date-system"})
        QTest::newRow(p) << QString::fromLatin1(p);
}
void XlsxTest::rejectsMalformedAndOversized()
{
    QFETCH(QString, problem);
    auto parts = package();
    const auto data = QStringLiteral("xl/worksheets/data.xml");
    if (problem == QLatin1String("xml")) parts[data] = "<worksheet>";
    if (problem == QLatin1String("dtd")) parts[data].prepend("<!DOCTYPE worksheet [<!ENTITY e 'bad'>]>");
    if (problem == QLatin1String("shared-index")) parts[data] = sheet("<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c></row>");
    if (problem == QLatin1String("traversal")) parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("worksheets/data.xml", "../../outside.xml");
    if (problem == QLatin1String("external-sheet")) parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("Target=\"worksheets/data.xml\"", "TargetMode=\"External\" Target=\"https://invalid.example/file.xml\"");
    if (problem == QLatin1String("field")) parts[data] = sheet("<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>" + QByteArray(XlsxReader::FieldLimit + 1, 'a') + "</t></is></c></row>");
    if (problem == QLatin1String("part")) parts[data] = QByteArray(XlsxReader::PartLimit + 1, 'a');
    if (problem == QLatin1String("archive")) parts[QStringLiteral("ignored.bin")] = QByteArray(XlsxReader::ExpandedLimit + 1, 'a');
    if (problem == QLatin1String("entries")) for (int i = 0; i < XlsxReader::EntryLimit; ++i) parts.insert(QString::number(i), {});
    if (problem == QLatin1String("missing")) parts.remove(data);
    if (problem.startsWith(QLatin1String("styles-"))) {
        parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
            "<Relationship Id=\"styles\" Type=\"" + reltype + "styles\" Target=\"styles.xml\"/></Relationships>");
        parts[QStringLiteral("xl/styles.xml")] = problem == QLatin1String("styles-xml") ? QByteArray("<styleSheet>")
            : "<styleSheet xmlns=\"" + ns + "\"><numFmts><numFmt numFmtId=\""
                + (problem == QLatin1String("styles-format") ? QByteArray("-1") : QByteArray("164"))
                + "\" formatCode=\"" + (problem == QLatin1String("styles-field") ? QByteArray(XlsxReader::FieldLimit + 1, 'y') : QByteArray("yyyy"))
                + "\"/></numFmts></styleSheet>";
    }
    if (problem == QLatin1String("date-system")) parts[QStringLiteral("xl/workbook.xml")].replace("<sheets>", "<workbookPr date1904=\"invalid\"/><sheets>");
    const auto result = read(write(parts));
    QVERIFY(!result.document);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.cancelled);
}
void XlsxTest::cancellationStopsLoading()
{
    const auto path = write(package());
    auto cancel = std::make_shared<std::atomic_bool>(true);
    auto result = read(path, cancel);
    QVERIFY(result.cancelled);
    QVERIFY(!result.document);
    // Cancel during decompression/XML processing, not only before dispatch.
    QByteArray rows;
    for (int i = 1; i < 10000; ++i) rows += "<row r=\"" + QByteArray::number(i) + "\"><c r=\"A" + QByteArray::number(i) + "\" t=\"inlineStr\"><is><t>" + QByteArray(500, 'a') + "</t></is></c></row>";
    const auto large = write(package(rows));
    cancel->store(false);
    std::jthread thread([cancel] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); cancel->store(true); });
    QElapsedTimer timer;
    timer.start();
    result = read(large, cancel);
    QVERIFY(result.cancelled);
    QVERIFY(timer.elapsed() < 1000);
}
void XlsxTest::snapshotAndRemoteUseSharedGrid_data()
{
    QTest::addColumn<bool>("remote");
    QTest::newRow("local") << false;
    QTest::newRow("smb") << true;
}
void XlsxTest::snapshotAndRemoteUseSharedGrid()
{
    QFETCH(bool, remote);
    const auto path = write(package());
    fuse_.path = path;
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(remote ? QStringLiteral("smb://server/share/book.xlsx") : path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *view = window->findChild<QTableView *>();
    QVERIFY(view);
    QCOMPARE(view->model()->data(view->model()->index(0, 0)).toString(), QStringLiteral("00123"));
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
}
void XlsxTest::inspectUserSample()
{
    const auto path = qEnvironmentVariable("DZURKEL_XLSX_SAMPLE");
    if (path.isEmpty()) QSKIP("Optional private sample path not supplied");
    QElapsedTimer timer;
    timer.start();
    const auto result = read(path);
    QVERIFY2(result.document, qPrintable(result.error));
    qInfo() << "Sample parse milliseconds:" << timer.elapsed() << "sheets:" << result.document->sheets.size();
    for (const auto &s : result.document->sheets)
        qInfo() << "Rows:" << s.rowCount << "columns:" << s.columnCount << "stored cells:" << s.cells.size();
    QVERIFY(!result.document->partial);
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(window->findChild<QTableView *>());
    // Private data remains local; screenshot is an ignored build artifact.
    QVERIFY(window->grab().save(QStringLiteral("xlsx-sample.png")));
    controller.hide();
}
QTEST_MAIN(XlsxTest)
#include "XlsxTest.moc"

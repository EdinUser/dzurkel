#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/readers/XlsxReader.h"
#include "controller/PreviewController.h"
#include "resources/ResourcePreparation.h"
#include "resources/ResourceAccessPolicy.h"
#include "preview/xlsx/XlsxDecodeJob.h"
#include "preview/table/SpreadsheetView.h"
#include "preview/lazy/LazyCoordinator.h"
#include "preview/table/TableRowBatch.h"
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QWheelEvent>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QScreen>
#include <QScrollBar>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QtEndian>
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
    void cleanup();
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
    void tablesKeepGeometryIndependentOfReadingAndPdf_data();
    void tablesKeepGeometryIndependentOfReadingAndPdf();
    void tableWidthFollowsColumns_data();
    void tableWidthFollowsColumns();
    void directSourceSeeksAcrossLargeSparseArchive();
    void ignoresUnusedExpandedParts();
    void boundsConsumedPartsAndArchiveMetadata();
    void helperPreservesCellsAndLargeReplies();
    void helperRejectsMalformedResults_data();
    void helperRejectsMalformedResults();
    void helperCancellationDeadlineAndConcurrency();
    void progressiveRowsPassFormerLimitsAndReplay();
    void progressiveHelperRetainsFormattingAndChunkedReplies();
    void progressiveSheetsEvictAndKeepGeometry();
    void progressiveErrorsAndMutation();
    void inspectUserSample();
private:
    QString write(const QMap<QString, QByteArray> &parts);
    XlsxResult read(const QString &path, const Cancellation &cancel = std::make_shared<std::atomic_bool>(false));
    FileResource prepare(const QString &path);
    PreviewData decode(const FileResource &resource);
    QString script(const QByteArray &body);
    QTemporaryDir directory_;
    XlsxFuse fuse_;
    int serial_ = 0;
};
void XlsxTest::initTestCase()
{
    qRegisterMetaType<PreviewData>();
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void XlsxTest::cleanup()
{
    QTRY_COMPARE(XlsxDecodeJob::runningHelpers(), 0);
    QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0);
}
FileResource XlsxTest::prepare(const QString &path)
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
PreviewData XlsxTest::decode(const FileResource &resource)
{
    auto candidate = PreviewRegistry::load(resource, {}, std::make_shared<std::atomic_bool>(false));
    if (candidate.kind != PreviewKind::Table || candidate.table) {
        QTest::qFail("Expected an unloaded XLSX candidate", __FILE__, __LINE__); return {};
    }
    XlsxDecodeJob job(candidate);
    QSignalSpy ready(&job, &XlsxDecodeJob::completed);
    if (!ready.wait(10000)) { QTest::qFail("XLSX helper did not complete", __FILE__, __LINE__); return {}; }
    return qvariant_cast<PreviewData>(ready.first().first());
}
QString XlsxTest::script(const QByteArray &body)
{
    const auto path = directory_.filePath(QStringLiteral("helper-%1.py").arg(++serial_));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write("#!/usr/bin/python3\nimport sys, time\n" + body);
    file.close();
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return {};
    return path;
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
        const auto path = write(parts);
        const auto result = read(path);
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->sheets.first().value(0, 0).toString(), expected);
        QCOMPARE(result.document->sheets.first().value(0, 1).toString(), expected);
        XlsxPortionReader progressive(path);
        const auto reply = progressive.read(QStringLiteral("0"));
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        QCOMPARE(batch.sheet->value(0, 0).toString(), expected);
        QCOMPARE(batch.sheet->value(0, 1).toString(), expected);
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
    QVERIFY(summary->text().contains(QStringLiteral("Rows 1–1 · 1 columns")));
    controller.window()->findChild<QTabWidget *>()->setCurrentIndex(1);
    QTRY_VERIFY(summary->text().contains(QStringLiteral("Rows 1–1 · 3 columns")));
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
    for (const auto &p : {"xml", "dtd", "shared-index", "traversal", "external-sheet", "field", "part", "entries", "missing", "styles-xml", "styles-format", "styles-field", "date-system"})
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
    QTest::addColumn<QString>("base");
    QTest::newRow("local") << QString();
    QTest::newRow("smb") << QStringLiteral("smb://server/share/book.xlsx");
    QTest::newRow("sftp") << QStringLiteral("sftp://user@server:2222/book.xlsx");
}
void XlsxTest::snapshotAndRemoteUseSharedGrid()
{
    QFETCH(QString, base);
    QString policyError;
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError));
    const auto path = write(package());
    fuse_.path = path;
    PreviewController controller;
    QString error;
    const auto url = base.isEmpty() ? path : base;
    const auto resource = prepare(url);
    QVERIFY(resource.seekableSource);
    QVERIFY(!resource.backingStore);
    QCOMPARE(resource.backingPath, path);
    QVERIFY(controller.show(url, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    QCOMPARE(controller.fileUrl(), resource.url.toString(QUrl::FullyEncoded));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *view = window->findChild<QTableView *>();
    QVERIFY(view);
    QCOMPARE(view->model()->data(view->model()->index(0, 0)).toString(), QStringLiteral("00123"));
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
}
void XlsxTest::tablesKeepGeometryIndependentOfReadingAndPdf_data()
{
    QTest::addColumn<bool>("excel");
    QTest::newRow("csv") << false;
    QTest::newRow("excel") << true;
}
void XlsxTest::tablesKeepGeometryIndependentOfReadingAndPdf()
{
    QFETCH(bool, excel);
    const auto text = directory_.filePath(QStringLiteral("reading.txt"));
    const auto csv = directory_.filePath(QStringLiteral("wide-table.csv"));
    for (const auto &path : {text, csv}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(path == csv ? "Name,Value\nFirst,42\n" : "A reading preview.\n");
    }
    const auto pdf = directory_.filePath(QStringLiteral("portrait.pdf"));
    {
        QPdfWriter writer(pdf);
        QPainter painter(&writer);
        painter.drawText(100, 100, QStringLiteral("Portrait PDF"));
    }
    const auto table = excel ? write(package()) : csv;
    PreviewController controller;
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QString error;
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_VERIFY(window->height() > window->width());
    QVERIFY(controller.show(pdf, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_VERIFY(window->height() > window->width());
    QVERIFY(controller.show(table, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    const auto area = window->screen()->availableGeometry().size();
    QTRY_VERIFY(window->width() >= 400 && window->width() <= 560);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(100); // Settle the compositor's table geometry transaction.
    const auto manual = QSize(760, 540).boundedTo(area - QSize(32, 32));
    window->resize(manual);
    QTRY_COMPARE(window->size(), manual);
    for (const auto &other : {text, pdf}) {
        QVERIFY(controller.show(other, error));
        QTRY_VERIFY(!controller.loading());
        QTRY_VERIFY2(window->height() > window->width(), qPrintable(QStringLiteral("%1, %2x%3, %4")
            .arg(controller.previewKind()).arg(window->width()).arg(window->height())
            .arg(window->findChild<QLabel *>(QStringLiteral("previewStatus"))->text())));
        QVERIFY(controller.show(table, error));
        QTRY_VERIFY(!controller.loading());
        QTRY_COMPARE(window->size(), manual);
    }
    controller.hide();
    QVERIFY(controller.show(table, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), manual);
}
void XlsxTest::tableWidthFollowsColumns_data()
{
    QTest::addColumn<bool>("excel");
    QTest::addColumn<int>("columns");
    for (bool excel : {false, true}) for (int columns : {2, 6, 40})
        QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(excel ? "xlsx" : "csv").arg(columns))) << excel << columns;
}
void XlsxTest::tableWidthFollowsColumns()
{
    QFETCH(bool, excel);
    QFETCH(int, columns);
    QString path;
    if (excel) {
        QByteArray row("<row r=\"1\">");
        for (int column = 0; column < columns; ++column) {
            QString name;
            for (int value = column + 1; value; value = (value - 1) / 26)
                name.prepend(QChar('A' + (value - 1) % 26));
            row += "<c r=\"" + name.toUtf8() + "1\" t=\"inlineStr\"><is><t>Value</t></is></c>";
        }
        path = write(package(row + "</row>"));
    } else {
        path = directory_.filePath(QStringLiteral("columns-%1.csv").arg(columns));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("Value,").repeated(columns - 1) + "Value\n");
    }
    PreviewController controller;
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *grid = window->findChild<QTableView *>();
    QVERIFY(grid);
    QCOMPARE(grid->model()->columnCount(), columns);
    const int limit = window->screen()->availableGeometry().width() * 85 / 100;
    QTRY_VERIFY(window->width() <= limit);
    if (columns == 2) {
        QTRY_VERIFY(window->width() >= 400 && window->width() <= 560);
        QTRY_COMPARE(grid->horizontalScrollBar()->maximum(), 0);
    } else {
        QTRY_VERIFY(window->width() >= qMin(columns * 110, limit));
        if (columns == 40) QTRY_VERIFY(grid->horizontalScrollBar()->maximum() > 0);
    }
    if (columns == 6) {
        QTest::qWait(100);
        window->resize(400, 480);
        QTRY_COMPARE(window->size(), QSize(400, 480));
        const auto next = directory_.filePath(excel ? QStringLiteral("next-table.xlsx") : QStringLiteral("next-table.csv"));
        QFile::remove(next);
        QVERIFY(QFile::copy(path, next));
        QVERIFY(controller.show(next, error));
        QTRY_VERIFY(!controller.loading());
        QTRY_VERIFY(window->width() >= qMin(columns * 110, limit));
        QTRY_COMPARE(window->height(), 480);
    }
}
void XlsxTest::directSourceSeeksAcrossLargeSparseArchive()
{
    const auto path = write(package());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    const auto bytes = file.readAll();
    const int end = bytes.size() - 22; // Generated ZIP has no comment or ZIP64 footer.
    QCOMPARE(bytes.mid(end, 4), QByteArray::fromHex("504b0506"));
    const auto offset = qFromLittleEndian<quint32>(bytes.constData() + end + 16);
    auto directory = bytes.mid(offset);
    constexpr quint32 gap = 256 * 1024 * 1024;
    qToLittleEndian<quint32>(offset + gap, directory.data() + directory.size() - 22 + 16);
    QVERIFY(file.resize(offset));
    QVERIFY(file.seek(offset + gap));
    QCOMPARE(file.write(directory), directory.size());
    file.close();
    QVERIFY(QFileInfo(path).size() > gap);
    const auto prepared = prepare(path);
    QVERIFY(prepared.seekableSource);
    QCOMPARE(prepared.backingPath, path);
    QVERIFY(!prepared.backingStore);
    const auto data = decode(prepared);
    QCOMPARE(data.kind, PreviewKind::Table);
    QCOMPARE(data.table->sheets.first().value(0, 0).toString(), QStringLiteral("00123"));
    QCOMPARE(data.resource.path, path);
    QVERIFY(QFile::remove(path));
    const auto vanished = decode(prepared);
    QCOMPARE(vanished.kind, PreviewKind::Information);
    QVERIFY(!vanished.message.isEmpty());
}
void XlsxTest::ignoresUnusedExpandedParts()
{
    auto parts = package();
    parts[QStringLiteral("unused-media.bin")] = QByteArray(XlsxReader::ExpandedLimit + 1, 'a');
    const auto result = read(write(parts));
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->sheets.first().value(0, 0).toString(), QStringLiteral("00123"));
}
void XlsxTest::boundsConsumedPartsAndArchiveMetadata()
{
    auto metadataParts = package();
    const auto longName = QStringLiteral("unused/") + QString(5000, QChar('n'));
    for (int i = 0; i < 900; ++i) metadataParts.insert(longName + QString::number(i), {});
    auto result = read(write(metadataParts));
    QVERIFY(!result.document);
    QVERIFY(result.error.contains(QStringLiteral("resource limits")));

    auto parts = package();
    QByteArray sheets, rels;
    const auto contents = sheet(QByteArray(15 * 1024 * 1024, ' '));
    parts.remove(QStringLiteral("xl/worksheets/data.xml"));
    for (int i = 1; i <= 5; ++i) {
        const auto id = QByteArray::number(i);
        sheets += "<sheet name=\"Sheet" + id + "\" sheetId=\"" + id + "\" r:id=\"r" + id + "\"/>";
        rels += "<Relationship Id=\"r" + id + "\" Type=\"" + reltype + "worksheet\" Target=\"worksheets/" + id + ".xml\"/>";
        parts.insert(QStringLiteral("xl/worksheets/%1.xml").arg(i), contents);
    }
    parts[QStringLiteral("xl/workbook.xml")] = "<workbook xmlns=\"" + ns + "\" xmlns:r=\"" + reltype.chopped(1) + "\"><sheets>" + sheets + "</sheets></workbook>";
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")] = "<Relationships xmlns=\"" + relns + "\">" + rels + "</Relationships>";
    result = read(write(parts));
    QVERIFY(!result.document);
    QVERIFY(result.error.contains(QStringLiteral("resource limits")));
}
void XlsxTest::helperPreservesCellsAndLargeReplies()
{
    const QString value = QString(255, QChar(0x03b2)) + QChar('\n');
    const auto encoded = value.toUtf8();
    QByteArray rows;
    for (int row = 1; row <= 600; ++row) {
        rows += "<row r=\"" + QByteArray::number(row) + "\">";
        for (int column = 0; column < 10; ++column)
            rows += "<c r=\"" + QByteArray(1, char('A' + column)) + QByteArray::number(row)
                + "\" t=\"inlineStr\"><is><t>" + encoded + "</t></is></c>";
        rows += "</row>";
    }
    const auto prepared = prepare(write(package(rows)));
    const auto data = decode(prepared);
    QCOMPARE(data.kind, PreviewKind::Table);
    QVERIFY(!data.truncated);
    QCOMPARE(data.table->sheets.size(), 1);
    const auto &sheet = data.table->sheets.first();
    QCOMPARE(sheet.rowCount, 600);
    QCOMPARE(sheet.columnCount, 10);
    QCOMPARE(sheet.cells.size(), 6000);
    QCOMPARE(sheet.value(0, 0).toString(), value);
    QCOMPARE(sheet.value(599, 9).toString(), value);
}
void XlsxTest::helperRejectsMalformedResults_data()
{
    QTest::addColumn<QByteArray>("output");
    const QByteArray start = "{\"event\":\"begin\",\"partial\":false,\"notice\":\"\"}\n";
    const QByteArray sheet = "{\"event\":\"sheet\",\"name\":\"A\",\"rows\":1,\"columns\":1}\n";
    const QByteArray cell = "{\"event\":\"cell\",\"row\":0,\"column\":0,\"value\":\"literal\"}\n";
    QTest::newRow("json") << QByteArray("bad\n");
    QTest::newRow("record-budget") << QByteArray(XlsxRecordLimit + 1, 'x');
    QTest::newRow("truncated") << start + sheet + cell;
    QTest::newRow("no-sheets") << start + "{\"event\":\"end\",\"detail\":\"\"}\n";
    QTest::newRow("duplicate-cell") << start + sheet + cell + cell;
    QTest::newRow("cell-outside-grid") << start + sheet + "{\"event\":\"cell\",\"row\":2,\"column\":0,\"value\":\"x\"}\n";
    QTest::newRow("grid-budget") << start + "{\"event\":\"sheet\",\"name\":\"A\",\"rows\":10001,\"columns\":256}\n";
    QTest::newRow("field-budget") << start + sheet + "{\"event\":\"cell\",\"row\":0,\"column\":0,\"value\":\""
        + QByteArray(XlsxReader::FieldLimit + 1, 'x') + "\"}\n";
}
void XlsxTest::helperRejectsMalformedResults()
{
    QFETCH(QByteArray, output);
    const auto program = script("sys.stdout.buffer.write(bytes.fromhex('" + output.toHex() + "'))\nsys.stdout.flush()\n");
    XlsxDecodeJob job({}, nullptr, program);
    QSignalSpy done(&job, &XlsxDecodeJob::completed);
    QVERIFY(done.wait(5000));
    const auto result = qvariant_cast<PreviewData>(done.first().first());
    QCOMPARE(result.kind, PreviewKind::Information);
    QVERIFY(!result.table);
    QVERIFY(!result.message.isEmpty());
}
void XlsxTest::helperCancellationDeadlineAndConcurrency()
{
    const auto program = script("time.sleep(10)\n");
    auto first = std::make_unique<XlsxDecodeJob>(PreviewData{}, nullptr, program);
    XlsxDecodeJob second({}, nullptr, program, 150);
    XlsxDecodeJob third({}, nullptr, program);
    QSignalSpy secondDone(&second, &XlsxDecodeJob::completed);
    QSignalSpy thirdDone(&third, &XlsxDecodeJob::completed);
    QCOMPARE(XlsxDecodeJob::runningHelpers(), 2);
    QVERIFY(thirdDone.wait(5000));
    QCOMPARE(qvariant_cast<PreviewData>(thirdDone.first().first()).kind, PreviewKind::Information);
    QElapsedTimer clock;
    clock.start();
    first.reset();
    QVERIFY(clock.elapsed() < 100);
    QTRY_COMPARE(secondDone.size(), 1);
    QVERIFY(qvariant_cast<PreviewData>(secondDone.first().first()).message.contains(QStringLiteral("timed out")));
}

void XlsxTest::progressiveRowsPassFormerLimitsAndReplay()
{
    QByteArray rows;
    const QByteArray field(160, 'x');
    for (int row = 1; row <= 11000; ++row) {
        rows += "<row r=\"" + QByteArray::number(row) + "\">";
        for (int column = 0; column < 10; ++column)
            rows += "<c t=\"inlineStr\"><is><t>" + QByteArray::number(row) + ':' + field + "</t></is></c>";
        rows += "</row>";
    }
    QVERIFY(rows.size() > XlsxReader::PartLimit);
    XlsxPortionReader reader(write(package(rows)));
    QString token = QStringLiteral("0"), previous;
    qint64 count = 0, lastStart = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(QStringLiteral("Row %1, %2: %3").arg(count).arg(token, reply.value("error").toString())));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        QCOMPARE(batch.firstRow, count);
        QVERIFY(batch.sheet->rowCount > 0 && batch.sheet->rowCount <= 64);
        for (int row = 0; row < batch.sheet->rowCount; ++row)
            QCOMPARE(batch.sheet->value(row, 9).toString(), QString::number(count + row + 1) + ':' + QString::fromLatin1(field));
        lastStart = count;
        count += batch.sheet->rowCount;
        previous = reply.value("previous").toString();
        token = reply.value("next").toString();
    } while (!token.isEmpty());
    QCOMPARE(count, 11000);
    const auto reply = reader.read(previous); // Deliberate cache miss: compressed-entry replay.
    QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
    TableRowBatch batch;
    QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
    QCOMPARE(batch.firstRow + batch.sheet->rowCount, lastStart);
    QVERIFY(!reader.read(QStringLiteral("f:0:1")).value("error").toString().isEmpty());
}
void XlsxTest::progressiveHelperRetainsFormattingAndChunkedReplies()
{
    const QString field(16384, QChar(0x754c));
    QByteArray rows;
    for (int row = 1; row <= 3; ++row) {
        rows += "<row r=\"" + QByteArray::number(row) + "\">";
        for (int column = 0; column < 4; ++column) rows += "<c t=\"s\"><v>0</v></c>";
        rows += "<c s=\"0\"><v>60</v></c><c><f>UNKNOWN()</f></c><c t=\"b\"><v>1</v></c></row>";
    }
    auto parts = package(rows);
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
        "<Relationship Id=\"s\" Type=\"" + reltype + "sharedStrings\" Target=\"strings.xml\"/>"
        "<Relationship Id=\"f\" Type=\"" + reltype + "styles\" Target=\"styles.xml\"/></Relationships>");
    parts[QStringLiteral("xl/strings.xml")] = "<sst xmlns=\"" + ns + "\"><si><r><t>" + field.toUtf8() + "</t></r></si></sst>";
    parts[QStringLiteral("xl/styles.xml")] = "<styleSheet xmlns=\"" + ns + "\"><cellXfs><xf numFmtId=\"14\"/></cellXfs></styleSheet>";
    FileResource resource;
    resource.path = write(parts);
    LazyCoordinator coordinator(resource, QStringLiteral("xlsx"));
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready), errors(&coordinator, &LazyCoordinator::failed);
    coordinator.request();
    QTRY_COMPARE(ready.size(), 1);
    auto portion = qvariant_cast<LazyPortion>(ready.last().first());
    QVERIFY(QFile::remove(resource.path)); // Persistent validated descriptor, no path reopen.
    for (int row = 0; row < 3; ++row) {
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(portion.content, batch));
        QCOMPARE(batch.firstRow, row);
        QCOMPARE(batch.sheet->rowCount, 1);
        QCOMPARE(batch.sheet->value(0, 3).toString(), field);
        QCOMPARE(batch.sheet->value(0, 4).toString(), QStringLiteral("1900-02-29 (Excel)"));
        QCOMPARE(batch.sheet->value(0, 5).toString(), QStringLiteral("[Result unavailable]"));
        QCOMPARE(batch.sheet->value(0, 6).toString(), QStringLiteral("TRUE"));
        QVERIFY(portion.content.value("notice").toString().contains(QStringLiteral("formula")));
        if (row < 2) {
            coordinator.request(portion.next);
            QTRY_COMPARE(ready.size(), row + 2);
            portion = qvariant_cast<LazyPortion>(ready.last().first());
        }
    }
    QVERIFY(portion.next.isEmpty());
    QVERIFY(errors.isEmpty());
    QVERIFY(coordinator.cachedBytes() <= coordinator.cacheBudget());
    coordinator.stop();
}
void XlsxTest::progressiveSheetsEvictAndKeepGeometry()
{
    QByteArray rows;
    for (int row = 1; row <= 1500; ++row)
        rows += "<row r=\"" + QByteArray::number(row) + "\"><c><v>" + QByteArray::number(row) + "</v></c></row>";
    auto parts = package(rows);
    parts[QStringLiteral("xl/workbook.xml")].replace("</sheets>", "<sheet name=\"Second\" sheetId=\"2\" r:id=\"r2\"/></sheets>");
    parts[QStringLiteral("xl/_rels/workbook.xml.rels")].replace("</Relationships>",
        "<Relationship Id=\"r2\" Type=\"" + reltype + "worksheet\" Target=\"worksheets/second.xml\"/></Relationships>");
    parts[QStringLiteral("xl/worksheets/second.xml")] = sheet("<row><c t=\"inlineStr\"><is><t>Second sheet</t></is></c></row>");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(write(parts), error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    QVERIFY(QTest::qWaitForWindowExposed(const_cast<PreviewWindow *>(controller.window())));
    QTest::qWait(200);
    auto *tabs = controller.window()->findChild<QTabWidget *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    auto *grid = qobject_cast<QTableView *>(tabs->widget(0));
    auto *other = qobject_cast<QTableView *>(tabs->widget(1));
    QAbstractItemModelTester tester(grid->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester otherTester(other->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(other->model()->rowCount(), 0);
    const auto geometry = controller.window()->geometry();
    const auto wheel = [](QTableView *view, int direction) {
        QWheelEvent event(QPointF(20, 20), view->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, -120 * direction),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &event);
    };
    for (int load = 0; load < 10; ++load) {
        grid->verticalScrollBar()->setValue(grid->verticalScrollBar()->maximum());
        const auto first = grid->model()->headerData(0, Qt::Vertical);
        const auto count = grid->model()->rowCount();
        const QPersistentModelIndex anchor(grid->model()->index(qMax(0, grid->rowAt(0)), 0));
        const auto value = anchor.data();
        const auto top = grid->visualRect(anchor).top();
        wheel(grid, 1);
        QTRY_VERIFY(grid->model()->headerData(0, Qt::Vertical) != first || grid->model()->rowCount() != count);
        QVERIFY(anchor.isValid());
        QCOMPARE(anchor.data(), value);
        QCOMPARE(grid->visualRect(anchor).top(), top);
        QVERIFY(grid->model()->rowCount() <= 192);
        QCOMPARE(controller.window()->geometry(), geometry);
    }
    QVERIFY(grid->model()->headerData(0, Qt::Vertical).toInt() > 1);
    for (int load = 0; load < 12 && grid->model()->headerData(0, Qt::Vertical).toInt() > 1; ++load) {
        grid->verticalScrollBar()->setValue(0);
        const auto first = grid->model()->headerData(0, Qt::Vertical);
        wheel(grid, -1);
        QTRY_VERIFY(grid->model()->headerData(0, Qt::Vertical) != first);
    }
    QCOMPARE(grid->model()->headerData(0, Qt::Vertical).toInt(), 1);
    tabs->setCurrentIndex(1);
    QTRY_COMPARE(other->model()->rowCount(), 1);
    QCOMPARE(other->model()->index(0, 0).data().toString(), QStringLiteral("Second sheet"));
    QCOMPARE(grid->model()->rowCount(), 0);
    tabs->setCurrentIndex(0);
    tabs->setCurrentIndex(1);
    tabs->setCurrentIndex(0); // Replace pending sheet targets; stale replies cannot populate another sheet.
    QTRY_VERIFY(grid->model()->rowCount() > 0);
    QCOMPARE(grid->model()->index(0, 0).data().toString(), QStringLiteral("1"));
    QCOMPARE(other->model()->rowCount(), 0);
    QCOMPARE(controller.window()->geometry(), geometry);
    controller.hide();
}
void XlsxTest::progressiveErrorsAndMutation()
{
    QByteArray rows;
    for (int row = 1; row <= 64; ++row) rows += "<row><c><v>1</v></c></row>";
    XlsxPortionReader emptyTail(write(package(rows + "<row r=\"1000000\"><c r=\"A1000000\"/></row>")));
    const auto complete = emptyTail.read(QStringLiteral("0"));
    QVERIFY(!complete.contains("error"));
    QCOMPARE(complete.value("content").toObject().value("rows").toArray().size(), 64);
    QVERIFY(complete.value("next").toString().isEmpty());
    XlsxPortionReader sparse(write(package(rows + "<row r=\"200\"><c><v>last</v></c></row>")));
    QString next = QStringLiteral("0");
    int total = 0;
    do {
        const auto reply = sparse.read(next);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        QCOMPARE(batch.firstRow, total);
        total += batch.sheet->rowCount;
        next = reply.value("next").toString();
        if (next.isEmpty()) QCOMPARE(batch.sheet->value(batch.sheet->rowCount - 1, 0).toString(), QStringLiteral("last"));
    } while (!next.isEmpty());
    QCOMPARE(total, 200);
    auto parts = package(rows);
    parts[QStringLiteral("xl/worksheets/data.xml")].replace("</sheetData>", "<row><c t=\"s\"><v>999</v></c></row></sheetData>");
    XlsxPortionReader reader(write(parts));
    auto reply = reader.read(QStringLiteral("0"));
    QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
    QVERIFY(!reply.value("next").toString().isEmpty());
    QVERIFY(reader.read(reply.value("next").toString()).contains("error"));
    const auto path = write(package(rows + rows));
    XlsxPortionReader mutableReader(path);
    reply = mutableReader.read(QStringLiteral("0"));
    QVERIFY(!reply.contains("error"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    file.write("changed");
    file.close();
    QVERIFY(mutableReader.read(reply.value("next").toString()).value("error").toString().contains(QStringLiteral("changed")));
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

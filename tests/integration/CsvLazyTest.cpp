#include "preview/lazy/CsvPortionReader.h"
#include "preview/lazy/LazyCoordinator.h"
#include "preview/readers/CsvRecordParser.h"
#include "preview/table/TableRowBatch.h"
#include "controller/PreviewController.h"
#include <QAbstractItemModelTester>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStringEncoder>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

using namespace dzurkel;
class CsvLazyTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void existingParserRetainsStateAcrossEveryBoundary();
    void rowsRemainReversibleAcrossEncodings_data();
    void rowsRemainReversibleAcrossEncodings();
    void passesFormerByteAndRowCutoffs();
    void wideRowsUseSharedChunkedReplies();
    void malformedAndOversizedRecordsKeepFallbackAndErrors();
    void tableEvictsWithStableSelectionScrollAndGeometry();
    void malformedMultipartReplies_data();
    void malformedMultipartReplies();
private:
    QString write(const QString &name, const QByteArray &bytes);
    void wheel(QTableView *grid, int direction);
    QTemporaryDir directory_;
};
QString CsvLazyTest::write(const QString &name, const QByteArray &bytes)
{
    QFile file(directory_.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}
void CsvLazyTest::wheel(QTableView *grid, int direction)
{
    QWheelEvent event(QPointF(20, 20), grid->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, -120 * direction),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(grid->viewport(), &event);
}
void CsvLazyTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
}
void CsvLazyTest::cleanup() { QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0); }
void CsvLazyTest::existingParserRetainsStateAcrossEveryBoundary()
{
    const QString text = QStringLiteral("a,b,c\r\n\"one\r\ntwo\",\"escaped \"\"quote\"\"\",\r\nlast,,record");
    const QList<QStringList> expected{{"a", "b", "c"}, {"one\ntwo", "escaped \"quote\"", ""}, {"last", "", "record"}};
    for (qsizetype split = 0; split <= text.size(); ++split) {
        CsvRecordParser parser(QChar(','), std::make_shared<std::atomic_bool>(false));
        QList<QStringList> rows;
        auto sink = [&](QStringList row) { rows.append(row); return true; };
        const auto first = parser.feed(QStringView(text).left(split), false, sink);
        const auto second = parser.feed(QStringView(text).mid(split), true, sink);
        QVERIFY(first.error.isEmpty());
        QVERIFY(second.error.isEmpty());
        QCOMPARE(rows, expected);
    }
}
void CsvLazyTest::rowsRemainReversibleAcrossEncodings_data()
{
    QTest::addColumn<int>("encoding");
    QTest::addColumn<QChar>("delimiter");
    for (const auto encoding : {QStringConverter::Utf8, QStringConverter::Utf16LE, QStringConverter::Utf16BE,
                                QStringConverter::Utf32LE, QStringConverter::Utf32BE, QStringConverter::Latin1})
        for (const auto delimiter : {QChar(','), QChar(';'), QChar('\t')})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(QStringConverter::nameForEncoding(encoding)).arg(delimiter.unicode()))) << int(encoding) << delimiter;
}
void CsvLazyTest::rowsRemainReversibleAcrossEncodings()
{
    QFETCH(int, encoding);
    QFETCH(QChar, delimiter);
    const auto special = encoding == int(QStringConverter::Latin1) ? QString::fromUtf8("café") : QString::fromUtf8("café 世界😀");
    const auto field = special + QStringLiteral("\nquoted \"value\"");
    auto escaped = field;
    escaped.replace(QChar('\n'), QStringLiteral("\r\n"));
    escaped.replace(QChar('"'), QStringLiteral("\"\""));
    QString text;
    for (int row = 0; row < 1300; ++row)
        text += QString::number(row) + delimiter + QChar('"') + escaped + QChar('"') + delimiter + QStringLiteral("00123\r\n");
    QStringEncoder encoder(QStringConverter::Encoding(encoding), QStringConverter::Flag::WriteBom);
    CsvPortionReader reader(write(QStringLiteral("encoding.csv"), encoder(text)), delimiter == '\t');
    QString token = QStringLiteral("0"), backwards;
    qint64 row = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        QCOMPARE(batch.firstRow, row);
        QVERIFY(batch.sheet->rowCount > 0 && batch.sheet->rowCount <= 64);
        for (int index = 0; index < batch.sheet->rowCount; ++index) {
            QCOMPARE(batch.sheet->value(index, 0).toString(), QString::number(row + index));
            QCOMPARE(batch.sheet->value(index, 1).toString(), field);
            QCOMPARE(batch.sheet->value(index, 2).toString(), QStringLiteral("00123"));
        }
        backwards = reply.value("previous").toString();
        row += batch.sheet->rowCount;
        token = reply.value("next").toString();
    } while (!token.isEmpty());
    QCOMPARE(row, 1300);
    qint64 previousEnd = -1;
    while (!backwards.isEmpty()) {
        const auto reply = reader.read(backwards);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        if (previousEnd >= 0) QCOMPARE(batch.firstRow + batch.sheet->rowCount, previousEnd);
        for (int index = 0; index < batch.sheet->rowCount; ++index) {
            QCOMPARE(batch.sheet->value(index, 0).toString(), QString::number(batch.firstRow + index));
            QCOMPARE(batch.sheet->value(index, 1).toString(), field);
        }
        previousEnd = batch.firstRow;
        backwards = reply.value("previous").toString();
    }
    QCOMPARE(previousEnd, 0);
}
void CsvLazyTest::passesFormerByteAndRowCutoffs()
{
    QByteArray bytes;
    for (int row = 0; row < 18000; ++row) bytes += QByteArray::number(row) + ',' + QByteArray(240, 'x') + '\n';
    QVERIFY(bytes.size() > CsvReader::ByteLimit);
    CsvPortionReader reader(write(QStringLiteral("large.csv"), bytes));
    QString token = QStringLiteral("0");
    qint64 rows = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        TableRowBatch batch;
        QVERIFY(decodeTableRowBatch(reply.value("content").toObject(), batch));
        QCOMPARE(batch.firstRow, rows);
        rows += batch.sheet->rowCount;
        token = reply.value("next").toString();
    } while (!token.isEmpty());
    QCOMPARE(rows, 18000);
}
void CsvLazyTest::wideRowsUseSharedChunkedReplies()
{
    const QString field(16384, QChar(0x754c));
    const auto text = QStringList{field, field, field, field}.join(',') + QChar('\n') + QStringLiteral("next,row\n");
    FileResource resource;
    resource.path = write(QStringLiteral("wide.csv"), text.toUtf8());
    LazyCoordinator coordinator(resource, QStringLiteral("csv"));
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready), errors(&coordinator, &LazyCoordinator::failed);
    coordinator.request();
    QTRY_COMPARE(ready.size(), 1);
    auto portion = qvariant_cast<LazyPortion>(ready.last().first());
    TableRowBatch batch;
    QVERIFY(decodeTableRowBatch(portion.content, batch));
    QCOMPARE(batch.sheet->rowCount, 1);
    QCOMPARE(batch.sheet->value(0, 3).toString(), field);
    QVERIFY(portion.cost > LazyCoordinator::RecordLimit);
    QVERIFY(coordinator.cachedBytes() <= coordinator.cacheBudget());
    QVERIFY(QFile::remove(resource.path));
    coordinator.request(portion.next);
    QTRY_COMPARE(ready.size(), 2);
    portion = qvariant_cast<LazyPortion>(ready.last().first());
    QVERIFY(decodeTableRowBatch(portion.content, batch));
    QCOMPARE(batch.firstRow, 1);
    QCOMPARE(batch.sheet->value(0, 0).toString(), QStringLiteral("next"));
    coordinator.request(portion.previous);
    QTRY_COMPARE(ready.size(), 3);
    QVERIFY(decodeTableRowBatch(qvariant_cast<LazyPortion>(ready.last().first()).content, batch));
    QCOMPARE(batch.firstRow, 0);
    QCOMPARE(batch.sheet->value(0, 3).toString(), field);
    QCOMPARE(errors.size(), 0);
}
void CsvLazyTest::malformedAndOversizedRecordsKeepFallbackAndErrors()
{
    CsvPortionReader malformed(write(QStringLiteral("malformed.csv"), "\"unclosed"));
    const auto fallback = malformed.read(QStringLiteral("0")).value("content").toObject();
    QCOMPARE(fallback.value("text").toString(), QStringLiteral("\"unclosed"));
    QVERIFY(fallback.value("notice").toString().contains(QStringLiteral("Showing text instead")));
    CsvPortionReader oversized(write(QStringLiteral("oversized.csv"), QByteArray(20000, 'a') + ",b\n"));
    auto reply = oversized.read(QStringLiteral("0"));
    QVERIFY(reply.value("content").toObject().value("text").isString());
    QVERIFY(!reply.value("next").toString().isEmpty());
    CsvPortionReader later(write(QStringLiteral("later.csv"), QByteArray("a,b\n").repeated(64) + "\"unclosed"));
    reply = later.read(QStringLiteral("0"));
    QVERIFY(reply.value("content").toObject().value("rows").isArray());
    QVERIFY(later.read(reply.value("next").toString()).contains("error"));
}
void CsvLazyTest::tableEvictsWithStableSelectionScrollAndGeometry()
{
    QByteArray bytes;
    for (int row = 0; row < 3000; ++row) {
        for (int column = 0; column < 40; ++column) {
            if (column) bytes += ',';
            bytes += "r" + QByteArray::number(row) + "c" + QByteArray::number(column);
        }
        bytes += '\n';
    }
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(write(QStringLiteral("scroll.csv"), bytes), error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    QVERIFY(QTest::qWaitForWindowExposed(const_cast<PreviewWindow *>(controller.window())));
    QTest::qWait(200);
    QPointer<QTableView> grid = controller.window()->findChild<QTableView *>(QStringLiteral("spreadsheetGrid"));
    QVERIFY(grid);
    QAbstractItemModelTester tester(grid->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(grid->model()->rowCount() <= 64);
    grid->setColumnWidth(0, 137);
    grid->horizontalScrollBar()->setValue(60);
    const auto geometry = controller.window()->geometry();
    for (int load = 0; load < 14; ++load) {
        grid->verticalScrollBar()->setValue(grid->verticalScrollBar()->maximum());
        const auto oldFirst = grid->model()->headerData(0, Qt::Vertical).toLongLong();
        const int oldCount = grid->model()->rowCount();
        const QPersistentModelIndex anchor(grid->model()->index(qMax(0, grid->rowAt(0)), 0));
        grid->selectionModel()->select(anchor, QItemSelectionModel::ClearAndSelect);
        const auto value = anchor.data();
        const int top = grid->visualRect(anchor).top();
        const int horizontal = grid->horizontalScrollBar()->value();
        wheel(grid, 1);
        QTRY_VERIFY(!grid || grid->model()->headerData(0, Qt::Vertical).toLongLong() != oldFirst || grid->model()->rowCount() != oldCount);
        QVERIFY(grid);
        QVERIFY(anchor.isValid());
        QCOMPARE(anchor.data(), value);
        QVERIFY(grid->selectionModel()->isSelected(anchor));
        QCOMPARE(grid->visualRect(anchor).top(), top);
        QCOMPARE(grid->horizontalScrollBar()->value(), horizontal);
        QCOMPARE(grid->columnWidth(0), 137);
        QVERIFY(grid->model()->rowCount() <= 3 * 64);
        QCOMPARE(controller.window()->geometry(), geometry);
    }
    QVERIFY(grid->model()->headerData(0, Qt::Vertical).toLongLong() > 1);
    for (int load = 0; load < 40 && grid->model()->headerData(0, Qt::Vertical).toLongLong() > 1; ++load) {
        const auto first = grid->model()->headerData(0, Qt::Vertical).toLongLong();
        grid->verticalScrollBar()->setValue(0);
        wheel(grid, -1);
        QTRY_VERIFY(!grid || grid->model()->headerData(0, Qt::Vertical).toLongLong() < first);
        QVERIFY(grid);
        QCOMPARE(controller.window()->geometry(), geometry);
    }
    QCOMPARE(grid->model()->headerData(0, Qt::Vertical).toLongLong(), 1);
    QCOMPARE(grid->model()->index(0, 0).data().toString(), QStringLiteral("r0c0"));
    controller.hide();
}
void CsvLazyTest::malformedMultipartReplies_data()
{
    QTest::addColumn<QByteArray>("extra");
    QTest::newRow("over-budget") << QByteArray("r.update(payloadBytes=20000000); print(json.dumps(r),flush=True)");
    QTest::newRow("wrong-offset") << QByteArray("r.update(payloadBytes=10); print(json.dumps(r),flush=True); r.pop('payloadBytes'); r.update(offset=5,data='eA=='); print(json.dumps(r),flush=True)");
    QTest::newRow("truncated") << QByteArray("r.update(payloadBytes=10); print(json.dumps(r),flush=True); sys.exit(0)");
    QTest::newRow("invalid-base64") << QByteArray("r.update(payloadBytes=10); print(json.dumps(r),flush=True); r.pop('payloadBytes'); r.update(offset=0,data='!'); print(json.dumps(r),flush=True)");
    QTest::newRow("oversized-unterminated-record") << QByteArray("sys.stdout.write('x'*131072); sys.stdout.flush()");
}
void CsvLazyTest::malformedMultipartReplies()
{
    QFETCH(QByteArray, extra);
    const auto program = write(QStringLiteral("broken-helper"), "#!/usr/bin/python3\nimport sys,json\nr=json.loads(sys.stdin.readline())\n" + extra + "\nsys.stdin.read()\n");
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    LazyCoordinator coordinator({}, QStringLiteral("csv"), program, 1000);
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready), errors(&coordinator, &LazyCoordinator::failed);
    coordinator.request();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(ready.size(), 0);
    if (QByteArray(QTest::currentDataTag()) == QByteArrayLiteral("oversized-unterminated-record"))
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("budget")));
}
QTEST_MAIN(CsvLazyTest)
#include "CsvLazyTest.moc"

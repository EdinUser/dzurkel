#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/readers/CsvReader.h"
#include "preview/readers/TextDecoder.h"
#include "preview/table/SpreadsheetView.h"
#include "controller/PreviewController.h"
#include "resources/ResourceAccessPolicy.h"
#include <QDBusConnection>
#include <QFile>
#include <QLabel>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDialog>
#include <QLabel>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

using namespace dzurkel;
class CsvFuse : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
public slots:
    QString mountUrl(const QString &) { return path; }
};
class LibraryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void catalogDrivesSelectionAndBudgets();
    void quotedRecordsAndLiteralValues();
    void detectsSeparators_data();
    void detectsSeparators();
    void preservesEncoding_data();
    void preservesEncoding();
    void completeRecordsSurviveLimits();
    void malformedCsvFallsBackAndBinaryDoesNotRender();
    void modelIsSparseReadOnlyAndSheetAware();
    void localAndRemoteCsvUseTable_data();
    void localAndRemoteCsvUseTable();
    void tableKeysNavigateContentAndDismiss();
    void compactChromeKeepsDetailsAccessible();
private:
    QString write(const QString &name, const QByteArray &bytes);
    PreviewData load(const QString &path);
    QTemporaryDir directory_;
    CsvFuse fuse_;
};
QString LibraryTest::write(const QString &name, const QByteArray &bytes)
{
    const auto path = directory_.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
PreviewData LibraryTest::load(const QString &path)
{
    FileResource resource;
    QString error;
    if (!resolveFileResource(path, resource, error)) { QTest::qFail(qPrintable(error), __FILE__, __LINE__); return {}; }
    return PreviewRegistry::load(resource, QSize(640, 480), std::make_shared<std::atomic_bool>(false));
}
void LibraryTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void LibraryTest::catalogDrivesSelectionAndBudgets()
{
    const auto readers = PreviewLibrary::readers();
    QCOMPARE(readers.size(), 5);
    QSet<QString> ids;
    for (const auto &entry : readers) {
        QVERIFY(!ids.contains(entry.id));
        ids.insert(entry.id);
        QVERIFY(!entry.description.isEmpty());
        QVERIFY(entry.sourceLimit > 0);
        QVERIFY(PreviewLibrary::createReader(entry.id));
    }
    QVERIFY(PreviewLibrary::createReader(QStringLiteral("xlsx")));
    QVERIFY(PreviewLibrary::capability(QStringLiteral("xlsx")));
    const auto csv = PreviewLibrary::capability(QStringLiteral("csv"));
    QVERIFY(csv);
    QCOMPARE(csv->access, SourceAccess::PrefixSnapshot);
    QCOMPARE(csv->sourceLimit, CsvReader::ByteLimit + 1);
    const auto path = write(QStringLiteral("extension.csv"), "a,b\n1,2\n");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(file, QMimeDatabase().mimeTypeForName(QStringLiteral("text/plain")), path), QStringLiteral("csv"));
    QCOMPARE(file.pos(), 0);
    const auto pdf = write(QStringLiteral("misleading.csv"), "%PDF-1.7\ninvalid fixture");
    QFile document(pdf);
    QVERIFY(document.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(document, QMimeDatabase().mimeTypeForName(QStringLiteral("text/csv")), pdf), QStringLiteral("pdf"));
}
void LibraryTest::quotedRecordsAndLiteralValues()
{
    const auto path = write(QStringLiteral("quoted.csv"), "Name,Code,Literal,Note,Empty\r\n\"A, B\",00123,=1+2,\"line1\r\nline2 \"\"quoted\"\"\",\r\nlast,row\r\n");
    const auto data = load(path);
    QCOMPARE(data.kind, PreviewKind::Table);
    QCOMPARE(data.readerId, QStringLiteral("csv"));
    QVERIFY(data.table);
    const auto &sheet = data.table->sheets.first();
    QCOMPARE(sheet.rowCount, 3);
    QCOMPARE(sheet.columnCount, 5);
    QCOMPARE(sheet.value(1, 0).toString(), QStringLiteral("A, B"));
    QCOMPARE(sheet.value(1, 1).toString(), QStringLiteral("00123"));
    QCOMPARE(sheet.value(1, 2).toString(), QStringLiteral("=1+2"));
    QCOMPARE(sheet.value(1, 3).toString(), QStringLiteral("line1\nline2 \"quoted\""));
    QVERIFY(!sheet.value(1, 4).isValid());
    QVERIFY(!sheet.value(2, 4).isValid());
    QVERIFY(!data.truncated);
}
void LibraryTest::detectsSeparators_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("extension");
    QTest::newRow("comma") << QByteArray("Name,Value\nA,1\n") << QStringLiteral("csv");
    QTest::newRow("semicolon-decimal-comma") << QByteArray("Name;Value\nA;1,23\n") << QStringLiteral("csv");
    QTest::newRow("tab") << QByteArray("Name\tValue\nA\t1\n") << QStringLiteral("tsv");
    QTest::newRow("quoted-semicolon") << QByteArray("\"Name\";\"Value\"\n\"A;B\";1\n") << QStringLiteral("csv");
}
void LibraryTest::detectsSeparators()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, extension);
    const auto data = load(write(QStringLiteral("separator.") + extension, bytes));
    QCOMPARE(data.kind, PreviewKind::Table);
    QCOMPARE(data.table->sheets.first().columnCount, 2);
    QCOMPARE(data.table->sheets.first().rowCount, 2);
}
void LibraryTest::preservesEncoding_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("expected");
    QTest::newRow("UTF-8-BOM") << QByteArray::fromHex("efbbbf") + QString::fromUtf8("пример,001\n").toUtf8() << QString::fromUtf8("пример");
    QTest::newRow("UTF-16LE") << QByteArray::fromHex("fffe41002c003000300031000a00") << QStringLiteral("A");
    QTest::newRow("Latin-1") << QByteArray::fromHex("636166e92c3030310a") << QString::fromUtf8("café");
}
void LibraryTest::preservesEncoding()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    const auto data = load(write(QStringLiteral("encoding.csv"), bytes));
    QCOMPARE(data.kind, PreviewKind::Table);
    QCOMPARE(data.table->sheets.first().value(0, 0).toString(), expected);
    QCOMPARE(data.table->sheets.first().value(0, 1).toString(), QStringLiteral("001"));
}
void LibraryTest::completeRecordsSurviveLimits()
{
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    auto result = CsvReader::read(QStringLiteral("a,b\n1,\"incomplete"), true, cancel, QChar(','));
    QVERIFY(result.document->partial);
    QCOMPARE(result.document->sheets.first().rowCount, 1);
    QVERIFY(result.error.isEmpty());
    result = CsvReader::read(QStringLiteral("a,b\n1,2"), true, cancel, QChar(','));
    QCOMPARE(result.document->sheets.first().rowCount, 1); // Unquoted truncated record is incomplete too.
    result = CsvReader::read(QStringLiteral("x\n").repeated(CsvReader::RowLimit + 1), false, cancel);
    QCOMPARE(result.document->sheets.first().rowCount, CsvReader::RowLimit);
    QVERIFY(result.document->partial);
    result = CsvReader::read(QStringLiteral("ok\n") + QStringLiteral("x,").repeated(CsvReader::ColumnLimit), false, cancel, QChar(','));
    QCOMPARE(result.document->sheets.first().rowCount, 1);
    QVERIFY(result.document->partial);
    result = CsvReader::read(QStringLiteral("ok\n") + QString(CsvReader::FieldLimit + 1, QChar('x')), false, cancel);
    QCOMPARE(result.document->sheets.first().rowCount, 1);
    QVERIFY(result.document->partial);
    const auto row = QStringLiteral("x,").repeated(199) + QStringLiteral("x\n");
    result = CsvReader::read(row.repeated(501), false, cancel, QChar(','));
    QCOMPARE(result.document->sheets.first().rowCount, 500);
    QCOMPARE(result.document->sheets.first().cells.size(), CsvReader::CellLimit);
    QVERIFY(result.document->partial);
    cancel->store(true);
    QVERIFY(CsvReader::read(QStringLiteral("a,b\n"), false, cancel).cancelled);
    QByteArray large("a,b\n");
    large += QByteArray(CsvReader::ByteLimit, 'x');
    const auto bounded = load(write(QStringLiteral("large.csv"), large));
    QCOMPARE(bounded.kind, PreviewKind::Table);
    QVERIFY(bounded.truncated);
    QCOMPARE(bounded.table->sheets.first().rowCount, 1);
}
void LibraryTest::malformedCsvFallsBackAndBinaryDoesNotRender()
{
    for (const auto &bytes : {QByteArray("a,b\n\"unterminated"), QByteArray("a,b\n\"quoted\"wrong,2\n"), QByteArray("a,b\nunquoted\"quote,2\n")}) {
        const auto data = load(write(QStringLiteral("malformed.csv"), bytes));
        QCOMPARE(data.kind, PreviewKind::Text);
        QVERIFY(data.message.contains(QStringLiteral("Showing text instead")));
    }
    QCOMPARE(load(write(QStringLiteral("binary.csv"), QByteArray("a,b\n\0bad", 9))).kind, PreviewKind::Information);
    const auto empty = load(write(QStringLiteral("empty.csv"), {}));
    QCOMPARE(empty.kind, PreviewKind::Table);
    QCOMPARE(empty.table->sheets.first().rowCount, 0);
}
void LibraryTest::modelIsSparseReadOnlyAndSheetAware()
{
    auto document = std::make_shared<TabularDocument>();
    document->sheets.append(TabularSheet{QStringLiteral("First"), 10000, 256, {}});
    document->sheets.first().cells.insert(TabularSheet::key(9999, 255), QStringLiteral("00123"));
    document->sheets.first().cells.insert(TabularSheet::key(0, 0), QStringLiteral("<img src='file:///untrusted'>"));
    document->sheets.append(TabularSheet{QStringLiteral("Second"), 1, 1, {}});
    SpreadsheetView widget(document);
    widget.resize(800, 500);
    widget.show();
    auto *tabs = widget.findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 2);
    auto *view = qobject_cast<QTableView *>(tabs->widget(0));
    QVERIFY(view);
    auto *model = view->model();
    QCOMPARE(model->rowCount(), 10000);
    QCOMPARE(model->columnCount(), 256);
    QCOMPARE(document->sheets.first().cells.size(), 2);
    QCOMPARE(model->data(model->index(9999, 255)).toString(), QStringLiteral("00123"));
    QCOMPARE(model->headerData(26, Qt::Horizontal).toString(), QStringLiteral("AA"));
    QVERIFY(!(model->flags(model->index(0, 0)) & Qt::ItemIsEditable));
    QVERIFY(model->data(model->index(0, 0), Qt::ToolTipRole).toString().contains(QStringLiteral("&lt;img")));
    QVERIFY(!model->setData(model->index(0, 0), QStringLiteral("changed")));
    tabs->setCurrentIndex(1);
    QCOMPARE(widget.focusProxy(), tabs->widget(1));
}
void LibraryTest::localAndRemoteCsvUseTable_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::newRow("local") << QString();
    QTest::newRow("SMB") << QStringLiteral("smb://user@server/share/data.csv");
    QTest::newRow("SFTP") << QStringLiteral("sftp://user@server:2222/share/data.csv");
}
void LibraryTest::localAndRemoteCsvUseTable()
{
    QFETCH(QString, scheme);
    const auto path = write(QStringLiteral("remote.csv"), "Name,Value\nExample,001\n");
    fuse_.path = path;
    QString error;
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(true, error));
    PreviewController controller;
    QVERIFY(controller.show(scheme.isEmpty() ? path : scheme, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    if (!scheme.isEmpty()) QCOMPARE(controller.fileUrl(), scheme);
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *view = window->findChild<QTableView *>();
    QVERIFY(view);
    QCOMPARE(view->model()->data(view->model()->index(1, 1)).toString(), QStringLiteral("001"));
    QVERIFY(window->grab().save(QStringLiteral("library-csv-%1.png").arg(QTest::currentDataTag())));
    controller.hide();
    QTRY_VERIFY(!window->findChild<QTableView *>());
}
void LibraryTest::compactChromeKeepsDetailsAccessible()
{
    const auto path = write(QStringLiteral("table <sample>.csv"), "Name,Value\nExample,123\n");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(window->windowTitle().contains(QStringLiteral("table <sample>.csv")));
    QVERIFY(!window->findChild<QLabel *>(QStringLiteral("filePath")));
    QVERIFY(!window->findChild<QLabel *>(QStringLiteral("fileName")));
    auto *details = window->findChild<QPushButton *>(QStringLiteral("fileInfoButton"));
    QVERIFY(details);
    QVERIFY(details->toolTip().contains(QStringLiteral("UTF-8")));
    QVERIFY(details->toolTip().contains(QStringLiteral("Comma-separated")));
    QVERIFY(details->toolTip().contains(QStringLiteral("&lt;sample&gt;")));
    QVERIFY(details->accessibleDescription().contains(path));
    const auto *position = window->findChild<QLabel *>(QStringLiteral("navigationStatus"));
    const auto *next = window->findChild<QPushButton *>(QStringLiteral("nextFile"));
    QVERIFY2(position->geometry().right() < next->geometry().left(),
        qPrintable(QStringLiteral("Navigation counter %1,%2 overlaps Next at %3")
            .arg(position->x()).arg(position->width()).arg(next->x())));
    const auto *summary = window->findChild<QLabel *>(QStringLiteral("previewSummary"));
    QVERIFY(summary);
    QVERIFY(summary->text().contains(QStringLiteral("2 × 2")));
    QVERIFY(!summary->text().contains(QStringLiteral("UTF")));
    QVERIFY(!summary->text().contains(QStringLiteral("separated")));
    QVERIFY(!window->findChild<QLabel *>(QStringLiteral("previewStatus"))->isVisible());
    QVERIFY(!window->findChild<QWidget *>(QStringLiteral("zoomStatus"))->isVisible());
    auto *grid = window->findChild<QTableView *>();
    QVERIFY(grid);
    QCOMPARE(grid->model()->data(grid->model()->index(1, 1)).toString(), QStringLiteral("123"));
    QVERIFY(window->grab().save(QStringLiteral("ui-refinement-table.png")));
    details->setFocus();
    QTest::keyClick(details, Qt::Key_Space);
    auto *dialog = window->findChild<QDialog *>(QStringLiteral("fileInformationDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("fileInformationText"))->text().contains(QStringLiteral("UTF-8")));
    QVERIFY(dialog->grab().save(QStringLiteral("ui-refinement-information.png")));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->isVisible());
    QVERIFY(controller.isVisible());
}

void LibraryTest::tableKeysNavigateContentAndDismiss()
{
    QTemporaryDir directory;
    const auto csv = directory.filePath(QStringLiteral("1.csv"));
    const auto text = directory.filePath(QStringLiteral("2.txt"));
    { QFile file(csv); QVERIFY(file.open(QFile::WriteOnly)); file.write("a,b\n1,2\n"); }
    { QFile file(text); QVERIFY(file.open(QFile::WriteOnly)); file.write("text\n"); }
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(csv, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *view = window->findChild<QTableView *>();
    QVERIFY(view);
    view->setCurrentIndex(view->model()->index(0, 0));
    QTest::keyClick(view, Qt::Key_Down);
    QCOMPARE(view->currentIndex().row(), 1);
    QCOMPARE(controller.previewKind(), QStringLiteral("table"));
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
    QVERIFY(controller.show(csv, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_VERIFY(window->findChild<QPushButton *>(QStringLiteral("nextFile"))->isEnabled());
    QTest::keyClick(window->findChild<QTableView *>(), Qt::Key_Right, Qt::AltModifier);
    QTRY_COMPARE(controller.fileUrl(), QUrl::fromLocalFile(text).toString(QUrl::FullyEncoded));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
}
QTEST_MAIN(LibraryTest)
#include "LibraryTest.moc"

#include "preview/lazy/LazyCoordinator.h"
#include "preview/lazy/TextPortionReader.h"
#include "controller/PreviewController.h"
#include "ui/ZoomPreview.h"
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QElapsedTimer>
#include <QWheelEvent>
#include <QLabel>
#include <QToolButton>
#include <QSyntaxHighlighter>

using namespace dzurkel;
class LazyTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void preservesAllCharactersAcrossEvictedPortions_data();
    void preservesAllCharactersAcrossEvictedPortions();
    void rejectsChangedAndBinarySources();
    void cacheAndRequestsStayBounded();
    void rejectsBrokenProtocol_data();
    void rejectsBrokenProtocol();
    void replacesPendingAndCancelsWithoutWaiting();
    void slidingTextKeepsAnchorAndGeometry_data();
    void slidingTextKeepsAnchorAndGeometry();
    void replacementAndDismissalStopSessions();
    void laterErrorKeepsVisibleContent();
    void endPortionsKeepEncoding_data();
    void endPortionsKeepEncoding();
    void endSkipsEmptyFinalIntervals_data();
    void endSkipsEmptyFinalIntervals();
    void jumpsToEndOfSparseSqlWithoutScanningMiddle();
    void endErrorKeepsUsefulText();
private:
    QString write(const QString &name, const QByteArray &bytes);
    QTemporaryDir directory_;
};
QString LazyTest::write(const QString &name, const QByteArray &bytes)
{
    QFile file(directory_.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}
void LazyTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
}
void LazyTest::cleanup() { QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0); }
void LazyTest::preservesAllCharactersAcrossEvictedPortions_data()
{
    QTest::addColumn<int>("encoding");
    for (auto encoding : {QStringConverter::Utf8, QStringConverter::Utf16LE, QStringConverter::Utf16BE,
                          QStringConverter::Utf32LE, QStringConverter::Utf32BE, QStringConverter::Latin1})
        QTest::newRow(QStringConverter::nameForEncoding(encoding)) << int(encoding);
}
void LazyTest::preservesAllCharactersAcrossEvictedPortions()
{
    QFETCH(int, encoding);
    QString original;
    for (int i = 0; i < 2000; ++i)
        original += QStringLiteral("%1 café %2\r\n").arg(i).arg(encoding == int(QStringConverter::Latin1) ? QStringLiteral("line") : QString::fromUtf8("世界😀"));
    original += QString(20000, QChar('z')); // Continues a long unbroken line.
    original += QChar(0xfeff); // Interior BOMs remain content, even at boundaries.
    auto converter = QStringEncoder(QStringConverter::Encoding(encoding), QStringConverter::Flag::WriteBom);
    const QByteArray bytes = converter(original);
    const auto path = write(QStringLiteral("encoding.txt"), bytes);
    TextPortionReader reader(path);
    QString assembled;
    QString token = QStringLiteral("0");
    int portions = 0;
    do {
        const auto reply = reader.read(token);
        QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
        const auto text = reply.value("content").toObject().value("text").toString();
        QVERIFY(text.size() <= TextPortionReader::PortionBytes + 1024);
        assembled += text;
        const auto previous = reply.value("previous").toString();
        if (!previous.isEmpty()) {
            const auto backwards = reader.read(previous);
            QVERIFY(!backwards.contains("error"));
            QCOMPARE(backwards.value("next").toString(), token);
        }
        token = reply.value("next").toString();
        ++portions;
    } while (!token.isEmpty());
    QVERIFY(portions > 5);
    original.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (encoding == int(QStringConverter::Latin1)) original.replace(QChar(0xfeff), QChar('?'));
    QCOMPARE(assembled, original);
    // No index or source copy is needed to revisit the beginning.
    QVERIFY(reader.read(QStringLiteral("0")).value("previous").toString().isEmpty());
    QVERIFY(reader.read(QStringLiteral("-1")).contains("error"));
}
void LazyTest::rejectsChangedAndBinarySources()
{
    const auto path = write(QStringLiteral("changed.txt"), "hello\n");
    TextPortionReader reader(path);
    QVERIFY(!reader.read(QStringLiteral("0")).contains("error"));
    QVERIFY(!write(QStringLiteral("changed.txt"), "changed and longer\n").isEmpty());
    QVERIFY(reader.read(QStringLiteral("0")).contains("error"));
    TextPortionReader binary(write(QStringLiteral("binary.txt"), QByteArray("a\0b", 3)));
    QVERIFY(binary.read(QStringLiteral("0")).contains("error"));
}
void LazyTest::cacheAndRequestsStayBounded()
{
    FileResource resource;
    resource.path = write(QStringLiteral("large.txt"), QByteArray(3 * 1024 * 1024, 'a'));
    LazyCoordinator coordinator(resource, QStringLiteral("text"));
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready);
    QSignalSpy errors(&coordinator, &LazyCoordinator::failed);
    for (int i = 0; i < 40; ++i) {
        coordinator.request(QString::number(i));
        coordinator.request(QString::number(i)); // Deduplicate foreground work.
        QTRY_COMPARE(ready.size(), i + 1);
        QCOMPARE(qvariant_cast<LazyPortion>(ready.last().first()).token, QString::number(i));
        QVERIFY(coordinator.cachedBytes() <= LazyCoordinator::CacheLimit);
        QVERIFY(coordinator.cachedPortions() <= LazyCoordinator::CacheEntries);
    }
    coordinator.request(QStringLiteral("700")); // Beyond the former 2 MiB cutoff.
    QTRY_COMPARE(ready.size(), 41);
    QCOMPARE(qvariant_cast<LazyPortion>(ready.last().first()).token, QStringLiteral("700"));
    // Keep the open descriptor usable after unlink, including cache misses.
    QVERIFY(QFile::remove(resource.path));
    coordinator.request(QStringLiteral("0"));
    QTRY_COMPARE(ready.size(), 42);
    QCOMPARE(qvariant_cast<LazyPortion>(ready.last().first()).content.value("text").toString(), QString(4096, 'a'));
    QCOMPARE(errors.size(), 0);
}
void LazyTest::rejectsBrokenProtocol_data()
{
    QTest::addColumn<QByteArray>("script");
    QTest::newRow("wrong-id") << QByteArray("#!/usr/bin/python3\nimport sys,json\nr=json.loads(sys.stdin.readline()); r.update(id=99,previous='',next='',content={})\nprint(json.dumps(r),flush=True)\nsys.stdin.read()\n");
    QTest::newRow("oversized") << QByteArray("#!/usr/bin/python3\nimport sys\nsys.stdin.readline()\nprint('x'*140000,flush=True)\nsys.stdin.read()\n");
    QTest::newRow("cycle") << QByteArray("#!/usr/bin/python3\nimport sys,json\nr=json.loads(sys.stdin.readline()); r.update(previous='',next=r['token'],content={})\nprint(json.dumps(r),flush=True)\nsys.stdin.read()\n");
    QTest::newRow("truncated") << QByteArray("#!/bin/sh\nread request\nprintf '{'\n");
    QTest::newRow("timeout") << QByteArray("#!/usr/bin/python3\nimport sys,time\nsys.stdin.readline()\ntime.sleep(10)\n");
}
void LazyTest::rejectsBrokenProtocol()
{
    QFETCH(QByteArray, script);
    const auto program = write(QStringLiteral("broken-helper"), script);
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    LazyCoordinator coordinator({}, QStringLiteral("text"), program, 150);
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready);
    QSignalSpy errors(&coordinator, &LazyCoordinator::failed);
    coordinator.request();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!coordinator.loading());
}
void LazyTest::replacesPendingAndCancelsWithoutWaiting()
{
    const auto program = write(QStringLiteral("slow-helper"), QByteArray(
        "#!/usr/bin/python3\nimport sys,json,time\nfor line in sys.stdin:\n r=json.loads(line); time.sleep(.1); r.update(previous='',next='',content={'text':r['token']}); print(json.dumps(r),flush=True)\n"));
    QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    LazyCoordinator coordinator({}, QStringLiteral("text"), program);
    QSignalSpy ready(&coordinator, &LazyCoordinator::ready);
    coordinator.request(QStringLiteral("0"));
    QTest::qWait(30);
    coordinator.request(QStringLiteral("1"));
    coordinator.request(QStringLiteral("2"));
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(qvariant_cast<LazyPortion>(ready.first().first()).token, QStringLiteral("2"));
    coordinator.request(QStringLiteral("3"));
    QElapsedTimer timer;
    timer.start();
    coordinator.stop();
    QVERIFY(timer.elapsed() < 100);
    QTest::qWait(150);
    QCOMPARE(ready.size(), 1);
}
void LazyTest::slidingTextKeepsAnchorAndGeometry_data()
{
    QTest::addColumn<bool>("wrap");
    QTest::newRow("wrapped") << true;
    QTest::newRow("unwrapped") << false;
}
void LazyTest::slidingTextKeepsAnchorAndGeometry()
{
    QFETCH(bool, wrap);
    QByteArray bytes;
    for (int i = 0; i < 50000; ++i) bytes += QByteArray::number(i).rightJustified(6, '0') + QByteArray(" content with words and whitespace   ").repeated(3) + '\n';
    const auto path = write(QStringLiteral("scrolling.txt"), bytes);
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    QVERIFY(QTest::qWaitForWindowExposed(const_cast<PreviewWindow *>(controller.window())));
    QTest::qWait(200); // Settle native map/configure before measuring geometry/anchors.
    QPointer<QPlainTextEdit> editor = controller.window()->findChild<QPlainTextEdit *>(QStringLiteral("textPreview"));
    QVERIFY(editor);
    editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    QVERIFY(editor->toPlainText().size() < 6000);
    const auto geometry = controller.window()->geometry();
    for (int i = 0; i < 12; ++i) {
        editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
        const auto before = editor->toPlainText();
        auto anchor = editor->cursorForPosition(QPoint(4, 0));
        anchor.select(QTextCursor::LineUnderCursor);
        const auto anchorText = anchor.selectedText();
        const int top = editor->cursorRect(anchor).top();
        QWheelEvent wheel(QPointF(20, 20), editor->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(editor->viewport(), &wheel);
        QTRY_VERIFY(!editor || editor->toPlainText() != before);
        QVERIFY2(editor, qPrintable(QStringLiteral("Text view disappeared: visible=%1 generation=%2 kind=%3 url=%4")
            .arg(controller.isVisible()).arg(controller.generation()).arg(controller.previewKind(), controller.fileUrl())));
        QVERIFY(editor->toPlainText().size() < 3 * 6000);
        QCOMPARE(anchor.selectedText(), anchorText);
        QVERIFY2(qAbs(editor->cursorRect(anchor).top() - top) <= editor->fontMetrics().lineSpacing() + 2,
            qPrintable(QStringLiteral("Visible anchor jumped during load/eviction: %1 -> %2 (load %3)").arg(top).arg(editor->cursorRect(anchor).top()).arg(i)));
        QCOMPARE(controller.window()->geometry(), geometry);
    }
    QVERIFY(!editor->toPlainText().startsWith(QStringLiteral("000000")));
    for (int i = 0; i < 12 && !editor->toPlainText().startsWith(QStringLiteral("000000")); ++i) {
        const auto before = editor->toPlainText();
        editor->verticalScrollBar()->setValue(0);
        QWheelEvent wheel(QPointF(20, 20), editor->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(editor->viewport(), &wheel);
        QTRY_VERIFY(!editor || editor->toPlainText() != before);
        QVERIFY2(editor, qPrintable(QStringLiteral("Text view disappeared: visible=%1 generation=%2 kind=%3 url=%4")
            .arg(controller.isVisible()).arg(controller.generation()).arg(controller.previewKind(), controller.fileUrl())));
        QCOMPARE(controller.window()->geometry(), geometry);
    }
    QVERIFY(editor->toPlainText().startsWith(QStringLiteral("000000")));
    controller.hide();
}
void LazyTest::replacementAndDismissalStopSessions()
{
    const auto first = write(QStringLiteral("first.txt"), QByteArray(100000, 'a'));
    const auto second = write(QStringLiteral("second.txt"), "second\n");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(first, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.show(second, error));
    QTRY_VERIFY(!controller.loading());
    auto *editor = controller.window()->findChild<QPlainTextEdit *>(QStringLiteral("textPreview"));
    QVERIFY(editor);
    QCOMPARE(editor->toPlainText(), QStringLiteral("second\n"));
    controller.hide();
    QTRY_COMPARE(LazyCoordinator::runningHelpers(), 0);
}
void LazyTest::laterErrorKeepsVisibleContent()
{
    const auto path = write(QStringLiteral("mutating.txt"), QByteArray(100000, 'a'));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(QTest::qWaitForWindowExposed(const_cast<PreviewWindow *>(controller.window())));
    QTest::qWait(200);
    auto *editor = controller.window()->findChild<QPlainTextEdit *>(QStringLiteral("textPreview"));
    auto *status = controller.window()->findChild<QLabel *>(QStringLiteral("textLoadingStatus"));
    QVERIFY(editor);
    QVERIFY(status);
    const auto before = editor->toPlainText();
    const auto geometry = controller.window()->geometry();
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Append));
    QCOMPARE(source.write("changed"), 7);
    source.close();
    editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
    QWheelEvent wheel(QPointF(20, 20), editor->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(editor->viewport(), &wheel);
    QTRY_VERIFY(status->text().contains(QStringLiteral("source changed")));
    QVERIFY(editor->toPlainText().startsWith(before));
    QCOMPARE(controller.window()->geometry(), geometry);
    controller.hide();
}
void LazyTest::endPortionsKeepEncoding_data()
{
    preservesAllCharactersAcrossEvictedPortions_data();
}
void LazyTest::endPortionsKeepEncoding()
{
    QFETCH(int, encoding);
    QString original;
    for (int i = 0; i < 2000; ++i)
        original += QStringLiteral("%1 café %2\r\n").arg(i).arg(encoding == int(QStringConverter::Latin1) ? QStringLiteral("line") : QString::fromUtf8("世界😀"));
    original += QStringLiteral("COMMIT;\r\n-- Export complete.");
    auto converter = QStringEncoder(QStringConverter::Encoding(encoding), QStringConverter::Flag::WriteBom);
    TextPortionReader reader(write(QStringLiteral("end-encoding.txt"), converter(original)));
    const auto reply = reader.read(QStringLiteral("end"));
    QVERIFY2(!reply.contains("error"), qPrintable(reply.value("error").toString()));
    const auto text = reply.value("content").toObject().value("text").toString();
    original.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QVERIFY(!text.isEmpty()); QVERIFY(original.endsWith(text));
    QVERIFY(text.endsWith(QStringLiteral("COMMIT;\n-- Export complete.")));
    QVERIFY(text.size() <= TextPortionReader::PortionBytes + 1024);
    QVERIFY(reply.value("next").toString().isEmpty());
    const auto previous = reply.value("previous").toString(); QVERIFY(!previous.isEmpty());
    const auto before = reader.read(previous); QVERIFY(!before.contains("error"));
    const auto numericEnd = reader.read(before.value("next").toString());
    QCOMPARE(numericEnd.value("content"), reply.value("content"));
}
void LazyTest::endSkipsEmptyFinalIntervals_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("short") << QByteArray("COMMIT;");
    QTest::newRow("exact-interval") << QByteArray(4096, 'a');
    QTest::newRow("line-lookahead") << (QByteArray(4096, 'a') + '\n');
    QTest::newRow("short-final-line") << (QByteArray(8192, 'a') + "\nCOMMIT;\n");
}
void LazyTest::endSkipsEmptyFinalIntervals()
{
    QFETCH(QByteArray, bytes);
    TextPortionReader reader(write(QStringLiteral("end-small.txt"), bytes));
    const auto reply = reader.read(QStringLiteral("end"));
    QVERIFY(!reply.contains("error"));
    const auto text = reply.value("content").toObject().value("text").toString();
    QVERIFY(bytes.endsWith(text.toUtf8()));
    QCOMPARE(text.isEmpty(), bytes.isEmpty());
    QVERIFY(reply.value("next").toString().isEmpty());
}
void LazyTest::jumpsToEndOfSparseSqlWithoutScanningMiddle()
{
    const auto path = directory_.filePath(QStringLiteral("large-export.sql"));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray beginning;
    while (beginning.size() < 65536) beginning += "-- Export beginning\nSELECT 1;\n";
    QCOMPARE(file.write(beginning), beginning.size());
    constexpr qint64 size = 3LL * 1024 * 1024 * 1024 + 123;
    QVERIFY(file.resize(size));
    QByteArray ending;
    while (ending.size() < 65536) ending += "-- Final statements\nSELECT 2;\n";
    ending += "COMMIT;\n-- Export complete.";
    QVERIFY(file.seek(size - ending.size()));
    QCOMPARE(file.write(ending), ending.size()); file.close();
    // The sparse middle contains binary zeroes. A tail read must seek past it,
    // neither scan it for language/encoding nor copy it into a temporary file.
    PreviewController controller; QString error;
    QVERIFY(controller.show(path, error)); QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window)); QTest::qWait(200);
    auto *editor = window->findChild<QPlainTextEdit *>(QStringLiteral("textPreview")); QVERIFY(editor);
    auto *end = window->findChild<QToolButton *>(QStringLiteral("textEnd")); QVERIFY(end);
    auto *status = window->findChild<QLabel *>(QStringLiteral("textLoadingStatus")); QVERIFY(status);
    auto *start = window->findChild<QToolButton *>(QStringLiteral("textBeginning")); QVERIFY(start);
    QPointer<QSyntaxHighlighter> highlighter = editor->document()->findChild<QSyntaxHighlighter *>();
    auto *document = editor->document();
    const auto geometry = window->geometry();
    const auto first = editor->toPlainText();
    auto *zoom = window->findChild<ZoomPreview *>(); QVERIFY(zoom); zoom->zoomBy(1);
    const auto pointSize = editor->font().pointSizeF();
    editor->setFocus();
    QTest::keyClick(editor, Qt::Key_End, Qt::ControlModifier);
    QTRY_VERIFY(editor->toPlainText().endsWith(QStringLiteral("COMMIT;\n-- Export complete.")));
    QCOMPARE(status->text(), QStringLiteral("End of file"));
    QCOMPARE(editor->verticalScrollBar()->value(), editor->verticalScrollBar()->maximum());
    QCOMPARE(editor->document(), document);
    QCOMPARE(document->findChild<QSyntaxHighlighter *>(), highlighter.data());
    QCOMPARE(editor->font().pointSizeF(), pointSize);
    QCOMPARE(window->geometry(), geometry);
    QVERIFY(editor->toPlainText().size() < 6000);
    QTest::keyClick(editor, Qt::Key_Home, Qt::ControlModifier);
    QTRY_COMPARE(editor->toPlainText(), first);
    QTest::mouseClick(end, Qt::LeftButton);
    QTRY_VERIFY(editor->toPlainText().endsWith(QStringLiteral("-- Export complete.")));
    const auto final = editor->toPlainText();
    editor->verticalScrollBar()->setValue(0);
    QWheelEvent wheel(QPointF(20, 20), editor->viewport()->mapToGlobal(QPoint(20, 20)), {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(editor->viewport(), &wheel);
    QTRY_VERIFY(editor->toPlainText().size() > final.size());
    QCOMPARE(editor->toPlainText().count(QStringLiteral("-- Export complete.")), 1);
    QVERIFY(editor->toPlainText().size() < 3 * 6000);
    QTest::mouseClick(start, Qt::LeftButton); QTRY_COMPARE(editor->toPlainText(), first);
    // The beginning request supersedes an end request without publishing stale text.
    QTest::mouseClick(end, Qt::LeftButton);
    QTest::keyClick(editor, Qt::Key_Home, Qt::ControlModifier);
    QTest::qWait(100); QCOMPARE(editor->toPlainText(), first);
    QCOMPARE(window->geometry(), geometry);
    QCOMPARE(document->findChild<QSyntaxHighlighter *>(), highlighter.data());
    controller.hide();
}
void LazyTest::endErrorKeepsUsefulText()
{
    const auto path = write(QStringLiteral("damaged-end.sql"), QByteArray(65536, 'a') + QByteArray(1, char(0xff)));
    PreviewController controller; QString error;
    QVERIFY(controller.show(path, error)); QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *editor = window->findChild<QPlainTextEdit *>(QStringLiteral("textPreview")); QVERIFY(editor);
    auto *status = window->findChild<QLabel *>(QStringLiteral("textLoadingStatus")); QVERIFY(status);
    const auto useful = editor->toPlainText();
    QTest::keyClick(editor, Qt::Key_End, Qt::ControlModifier);
    QTRY_VERIFY(status->text().contains(QStringLiteral("encoding")));
    QCOMPARE(editor->toPlainText(), useful);
    QCOMPARE(controller.previewKind(), QStringLiteral("text")); controller.hide();
}
QTEST_MAIN(LazyTest)
#include "LazyTest.moc"

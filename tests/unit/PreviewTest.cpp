#include "controller/PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "preview/providers/TextPreviewProvider.h"
#include <QFile>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <sys/stat.h>
#include <unistd.h>

using namespace dzurkel;

class PreviewTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void decodesImages_data();
    void decodesImages();
    void honorsJpegOrientation();
    void rejectsOversizedAndDamagedImages();
    void decodesText_data();
    void decodesText();
    void boundsTextAndRejectsBinary();
    void loadsMultipagePdfAndReleasesIt();
    void handlesInvalidAndProtectedPdf();
    void describesDirectoriesLinksAndFifos();
    void rejectsFileReplacementWithFifo();
    void newestRequestWinsAndQueueIsBounded();
    void dismissalPreventsResurrection();
    void textAndPdfChildrenDoNotConsumeDismissal();
    void repeatedlyReplacesAndReleasesProviders();
private:
    QString write(const QString &name, const QByteArray &bytes);
    QString makePdf();
    PreviewData load(const QString &path, const QSize &size = QSize(320, 240));
    QTemporaryDir directory_;
};

void PreviewTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    QImageReader::setAllocationLimit(ImagePreviewProvider::AllocationLimitMiB);
}
QString PreviewTest::write(const QString &name, const QByteArray &bytes)
{
    const QString path = directory_.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
PreviewData PreviewTest::load(const QString &path, const QSize &size)
{
    FileResource resource;
    QString error;
    if (!resolveFileResource(path, resource, error)) {
        PreviewData result;
        result.message = error;
        return result;
    }
    return PreviewRegistry::load(resource, size, std::make_shared<std::atomic_bool>(false));
}
QString PreviewTest::makePdf()
{
    const QString path = directory_.filePath(QStringLiteral("multipage.pdf"));
    QPdfWriter writer(path);
    writer.setResolution(96);
    QPainter painter(&writer);
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 18));
    painter.drawText(100, 100, QStringLiteral("First preview page"));
    writer.newPage();
    painter.drawText(100, 100, QStringLiteral("Second preview page"));
    painter.end();
    return path;
}
void PreviewTest::decodesImages_data()
{
    QTest::addColumn<QByteArray>("format");
    QTest::newRow("PNG") << QByteArray("PNG");
    QTest::newRow("JPEG") << QByteArray("JPEG");
    QTest::newRow("BMP") << QByteArray("BMP");
}
void PreviewTest::decodesImages()
{
    QFETCH(QByteArray, format);
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::blue);
    // Deliberately misleading extension: actual decoding capability must win.
    const QString path = directory_.filePath(QString::fromLatin1(format) + QStringLiteral(".txt"));
    QVERIFY(image.save(path, format.constData()));
    const auto data = load(path, QSize(100, 100));
    QCOMPARE(data.kind, PreviewKind::Image);
    QVERIFY(data.message.isEmpty());
    QCOMPARE(data.image.size(), QSize(100, 50));
    const auto color = data.image.pixelColor(50, 25);
    QVERIFY(color.red() <= 2 && color.green() <= 2 && color.blue() >= 253);
}
void PreviewTest::honorsJpegOrientation()
{
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString path = directory_.filePath(QStringLiteral("oriented.jpg"));
    QVERIFY(image.save(path, "JPEG"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray bytes = file.readAll();
    file.close();
    const auto exif = QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
    bytes.insert(2, exif);
    QVERIFY(!write(QStringLiteral("oriented.jpg"), bytes).isEmpty());
    const auto data = load(path, QSize(500, 500));
    QCOMPARE(data.kind, PreviewKind::Image);
    QCOMPARE(data.image.size(), QSize(60, 120));
}
void PreviewTest::rejectsOversizedAndDamagedImages()
{
    QImage tiny(1, 1, QImage::Format_RGB32);
    tiny.fill(Qt::red);
    const QString path = directory_.filePath(QStringLiteral("oversized.png"));
    QVERIFY(tiny.save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray bytes = file.readAll();
    file.close();
    qToBigEndian<quint32>(100000, reinterpret_cast<uchar *>(bytes.data() + 16));
    qToBigEndian<quint32>(100000, reinterpret_cast<uchar *>(bytes.data() + 20));
    quint32 crc = 0xffffffff;
    for (int i = 12; i < 29; ++i) {
        crc ^= uchar(bytes.at(i));
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    qToBigEndian<quint32>(crc ^ 0xffffffff, reinterpret_cast<uchar *>(bytes.data() + 29));
    QVERIFY(!write(QStringLiteral("oversized.png"), bytes).isEmpty());
    const auto oversized = load(path);
    QCOMPARE(oversized.kind, PreviewKind::Information);
    QVERIFY2(oversized.message.contains(QStringLiteral("budget")), qPrintable(oversized.message));
    const auto damaged = load(write(QStringLiteral("damaged.png"), bytes.left(40)));
    QCOMPARE(damaged.kind, PreviewKind::Information);
    QVERIFY(!damaged.message.isEmpty());
}
void PreviewTest::decodesText_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("expected");
    QTest::newRow("UTF-8") << QString::fromUtf8("  Пример 😀\n\tline\n").toUtf8() << QString::fromUtf8("  Пример 😀\n\tline\n");
    QTest::newRow("line endings") << QByteArray("A\r\nB\rC\n") << QStringLiteral("A\nB\nC\n");
    QTest::newRow("UTF-32LE") << QByteArray::fromHex("fffe0000410000000a00000011040000") << QString::fromUtf8("A\nБ");
    QTest::newRow("UTF-16LE") << QByteArray::fromHex("fffe41000a001104") << QString::fromUtf8("A\nБ");
    QTest::newRow("Latin-1") << QByteArray::fromHex("636166e90a") << QString::fromUtf8("café\n");
    QTest::newRow("plain XML") << QByteArray("<script>alert('text only')</script>\n") << QStringLiteral("<script>alert('text only')</script>\n");
}
void PreviewTest::decodesText()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    const auto data = load(write(QStringLiteral("text.txt"), bytes));
    QCOMPARE(data.kind, PreviewKind::Text);
    QCOMPARE(data.text, expected);
    std::unique_ptr<QWidget> widget(PreviewRegistry::createWidget(data, nullptr));
    auto *view = qobject_cast<QPlainTextEdit *>(widget.get());
    QVERIFY(view);
    QVERIFY(view->isReadOnly());
    QCOMPARE(view->lineWrapMode(), QPlainTextEdit::NoWrap);
    QCOMPARE(view->toPlainText(), expected);
}
void PreviewTest::boundsTextAndRejectsBinary()
{
    QByteArray large;
    while (large.size() <= TextPreviewProvider::ByteLimit) large += "  line\n";
    const auto truncated = load(write(QStringLiteral("large.log"), large));
    QCOMPARE(truncated.kind, PreviewKind::Text);
    QVERIFY(truncated.truncated);
    QVERIFY(truncated.text.count('\n') < TextPreviewProvider::LineLimit);
    QVERIFY(truncated.message.contains(QStringLiteral("Partial preview")));
    const auto separators = load(write(QStringLiteral("separators.txt"), QString(30000, QChar::ParagraphSeparator).toUtf8()));
    QCOMPARE(separators.kind, PreviewKind::Text);
    QVERIFY(separators.truncated);
    QVERIFY(separators.text.count('\n') < TextPreviewProvider::LineLimit);
    const auto longLine = load(write(QStringLiteral("long.txt"), QByteArray(100000, 'x')));
    QCOMPARE(longLine.kind, PreviewKind::Text);
    QVERIFY(longLine.truncated);
    QVERIFY(longLine.text.size() < TextPreviewProvider::LineLengthLimit + 100);
    QCOMPARE(load(write(QStringLiteral("binary.txt"), QByteArray("abc\0def", 7))).kind, PreviewKind::Information);
    const auto unsupported = load(write(QStringLiteral("unknown.bin"), QByteArray::fromHex("000102030405ff")));
    QCOMPARE(unsupported.kind, PreviewKind::Information);
    QVERIFY(!unsupported.mimeType.isEmpty());
    QVERIFY(unsupported.modified.isValid());
}
void PreviewTest::loadsMultipagePdfAndReleasesIt()
{
    const QString path = makePdf();
    for (int i = 0; i < 5; ++i) {
        auto data = load(path);
        QCOMPARE(data.kind, PreviewKind::Pdf);
        QVERIFY(data.pdf);
        QCOMPARE(data.pdf->pageCount(), 2);
        QVERIFY(!data.pdf->render(0, QSize(160, 200)).isNull());
        QPointer<QPdfDocument> guard(data.pdf.get());
        auto *widget = PreviewRegistry::createWidget(data, nullptr);
        auto *view = widget->findChild<QPdfView *>();
        QVERIFY(view);
        QCOMPARE(view->pageMode(), QPdfView::PageMode::MultiPage);
        QCOMPARE(view->zoomMode(), QPdfView::ZoomMode::FitToWidth);
        QCOMPARE(view->pageNavigator()->currentPage(), 0);
        widget->resize(400, 400);
        widget->show();
        QCoreApplication::processEvents();
        delete widget;
        data.pdf.reset();
        QVERIFY(guard.isNull());
    }
}
void PreviewTest::handlesInvalidAndProtectedPdf()
{
    const auto damaged = load(write(QStringLiteral("damaged.pdf"), QByteArray("%PDF-1.7\nnot a PDF")));
    QCOMPARE(damaged.kind, PreviewKind::Information);
    QVERIFY(damaged.message.contains(QStringLiteral("PDF")));
    const auto protectedPdf = load(QStringLiteral(DZURKEL_FIXTURES) + QStringLiteral("/password-protected.pdf"));
    QCOMPARE(protectedPdf.kind, PreviewKind::Information);
    QVERIFY2(protectedPdf.message.contains(QStringLiteral("Password-protected")), qPrintable(protectedPdf.message));
    QPdfDocument decrypted(nullptr);
    decrypted.setPassword(QStringLiteral("secret"));
    QCOMPARE(decrypted.load(QStringLiteral(DZURKEL_FIXTURES) + QStringLiteral("/password-protected.pdf")), QPdfDocument::Error::None);
    QCOMPARE(decrypted.pageCount(), 1);
    const QString large = write(QStringLiteral("large.pdf"), "%PDF-1.7\n");
    QFile sparse(large);
    QVERIFY(sparse.open(QIODevice::WriteOnly | QIODevice::Append));
    QVERIFY(sparse.resize(65 * 1024 * 1024));
    sparse.close();
    const auto tooLarge = load(large);
    QCOMPARE(tooLarge.kind, PreviewKind::Information);
    QVERIFY(tooLarge.message.contains(QStringLiteral("64 MiB")));
}
void PreviewTest::describesDirectoriesLinksAndFifos()
{
    const QString regular = write(QStringLiteral("target.txt"), "content");
    const QString validLink = directory_.filePath(QStringLiteral("valid-link"));
    const QString brokenLink = directory_.filePath(QStringLiteral("broken-link"));
    const QString cycle = directory_.filePath(QStringLiteral("cycle"));
    const QString fifo = directory_.filePath(QStringLiteral("named-pipe"));
    QCOMPARE(::symlink(QFile::encodeName(regular), QFile::encodeName(validLink)), 0);
    QCOMPARE(::symlink("missing", QFile::encodeName(brokenLink)), 0);
    QCOMPARE(::symlink("cycle", QFile::encodeName(cycle)), 0);
    QCOMPARE(::mkfifo(QFile::encodeName(fifo), 0600), 0);
    for (const auto &path : {directory_.path(), validLink, brokenLink, cycle, fifo}) {
        FileResource resource;
        QString error;
        QVERIFY2(resolveFileResource(path, resource, error), qPrintable(error));
        QVERIFY(resource.informationOnly);
        const auto data = load(path);
        QCOMPARE(data.kind, PreviewKind::Information);
        QVERIFY(!data.message.isEmpty());
    }
}
void PreviewTest::rejectsFileReplacementWithFifo()
{
    const QString path = write(QStringLiteral("replaced.txt"), "content");
    FileResource resource;
    QString error;
    QVERIFY(resolveFileResource(path, resource, error));
    QVERIFY(QFile::remove(path));
    QCOMPARE(::mkfifo(QFile::encodeName(path), 0600), 0);
    const auto data = PreviewRegistry::load(resource, QSize(100, 100), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Information);
    QVERIFY(data.message.contains(QStringLiteral("not a regular file")));
}
void PreviewTest::newestRequestWinsAndQueueIsBounded()
{
    const QString first = write(QStringLiteral("first.txt"), "first");
    const QString last = write(QStringLiteral("last.txt"), "last");
    QSemaphore entered, release;
    std::atomic_int calls = 0;
    PreviewController controller(false, [&](const FileResource &resource, const QSize &, const Cancellation &) {
        if (++calls == 1) { entered.release(); release.tryAcquire(1, 3000); }
        PreviewData data;
        data.resource = resource;
        data.kind = PreviewKind::Text;
        data.text = resource.path;
        return data;
    });
    QString error;
    QVERIFY(controller.show(first, error));
    QVERIFY(entered.tryAcquire(1, 2000));
    for (int i = 0; i < 30; ++i) QVERIFY(controller.show(last, error));
    QCOMPARE(calls.load(), 1);
    release.release();
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(calls.load(), 2);
    auto *view = controller.window()->findChild<QPlainTextEdit *>();
    QVERIFY(view);
    QCOMPARE(view->toPlainText(), last);
    controller.hide();
}
void PreviewTest::dismissalPreventsResurrection()
{
    const QString path = write(QStringLiteral("slow.txt"), "slow");
    QSemaphore entered, release;
    PreviewController controller(false, [&](const FileResource &, const QSize &, const Cancellation &) {
        entered.release();
        release.tryAcquire(1, 3000);
        PreviewData data;
        data.kind = PreviewKind::Text;
        data.text = QStringLiteral("obsolete");
        return data;
    });
    QString error;
    QVERIFY(controller.show(path, error));
    QVERIFY(entered.tryAcquire(1, 2000));
    controller.hide();
    release.release();
    QTest::qWait(100);
    QVERIFY(!controller.isVisible());
    QVERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("none"));
    QVERIFY(!controller.window()->findChild<QPlainTextEdit *>());
}
void PreviewTest::textAndPdfChildrenDoNotConsumeDismissal()
{
    const QString text = write(QStringLiteral("editor.txt"), "line\nline\n");
    const QString pdf = makePdf();
    PreviewController controller;
    QString error;
    for (const auto &path : {text, pdf}) {
        QVERIFY(controller.show(path, error));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
        auto *target = path == text
            ? static_cast<QWidget *>(controller.window()->findChild<QPlainTextEdit *>())
            : static_cast<QWidget *>(controller.window()->findChild<QPdfView *>());
        QVERIFY(target);
        target->window()->activateWindow();
        QTRY_VERIFY(controller.window()->isActiveWindow());
        target->setFocus();
        QTest::keyClick(target, Qt::Key_Space);
        QVERIFY(!controller.isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void PreviewTest::repeatedlyReplacesAndReleasesProviders()
{
    QImage image(400, 240, QImage::Format_RGB32);
    image.fill(QColor("#e7edf4"));
    {
        QPainter painter(&image);
        painter.fillRect(25, 25, 160, 190, QColor("#3067b1"));
        painter.setBrush(QColor("#e6a52a"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(215, 45, 150, 150);
    }
    const QString picture = directory_.filePath(QStringLiteral("sample.png"));
    QVERIFY(image.save(picture));
    const QString text = write(QStringLiteral("sample.json"), "{\n  \"name\": \"Dzurkel\",\n  \"slogan\": \"Peek-a-Dzurkel!\"\n}\n");
    const QString pdf = makePdf();
    const QString binary = write(QStringLiteral("sample.bin"), QByteArray::fromHex("000102ff000000"));
    const QList<QPair<QString, QString>> samples = {{picture, "image"}, {text, "text"}, {pdf, "pdf"}, {binary, "information"}};
    PreviewController controller;
    QString error;
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (const auto &sample : samples) {
            QVERIFY(controller.show(sample.first, error));
            QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
            QCOMPARE(controller.previewKind(), sample.second);
            auto *status = controller.window()->findChild<QLabel *>(QStringLiteral("previewStatus"));
            QVERIFY(status);
            QVERIFY(!status->text().contains(QStringLiteral("unavailable")));
            const QString name = sample.second == QStringLiteral("information") ? QStringLiteral("fileInfoPreview") : sample.second + QStringLiteral("Preview");
            QPointer<QWidget> content = controller.window()->findChild<QWidget *>(name);
            QVERIFY(content);
            QPointer<QPdfDocument> document;
            if (auto *view = content->findChild<QPdfView *>()) document = view->document();
            if (cycle == 0) content->window()->grab().save(QStringLiteral("preview-%1.png").arg(sample.second));
            controller.hide();
            QTRY_VERIFY(content.isNull());
            QTRY_VERIFY(document.isNull());
        }
    }
}

QTEST_MAIN(PreviewTest)
#include "PreviewTest.moc"

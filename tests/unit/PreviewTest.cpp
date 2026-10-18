#include "controller/PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "preview/providers/TextPreviewProvider.h"
#include "ui/ZoomPreview.h"
#include "desktop/DesktopIntegration.h"
#include <QFile>
#include <QEvent>
#include <QElapsedTimer>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QScreen>
#include <QWindow>
#include <QtEndian>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

using namespace dzurkel;

class FirstShowSize : public QObject {
public:
    QSize size;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show && size.isEmpty())
            size = qobject_cast<QWidget *>(watched)->size();
        return false;
    }
};

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
    void immediateReopenIgnoresDismissedWorker();
    void shutdownCancelsCooperativeWorker();
    void textAndPdfChildrenDoNotConsumeDismissal();
    void repeatedlyReplacesAndReleasesProviders();
    void retainedPdfRuntimeStillReleasesEachDocument();
    void informationFallbackTemporarilyCompactsWindow();
    void imageWindowsFitContentAndPreserveReadingSize();
    void mappedImageAndTableTransitionsRestoreReadingSize();
    void informationFallbackRestoresMaximizedPreview();
    void coldInformationThenReopenedPdfKeepsContentAndSize();
    void coldInformationMapsAtCompactSize_data();
    void coldInformationMapsAtCompactSize();
    void hideBeforeFirstMappingPreventsResurrection();
    void newestRequestBeforeFirstMappingWins();
    void visiblePreviewReactivatesOnExplicitShow();
private:
    QString write(const QString &name, const QByteArray &bytes);
    QString makePdf();
    PreviewData load(const QString &path, const QSize &size = QSize(320, 240));
    QTemporaryDir directory_;
};

void PreviewTest::visiblePreviewReactivatesOnExplicitShow()
{
    if (!qEnvironmentVariableIsSet("DZURKEL_TEST_NATIVE_FOCUS")
        || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        QSKIP("Opt-in focus check on the real KDE Wayland desktop");
    const QString first = write(QStringLiteral("focus-first.txt"), "First preview\n");
    const QString second = write(QStringLiteral("focus-second.txt"), "Second preview\n");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(first, error));
    QTRY_VERIFY(!controller.loading());
    auto *preview = const_cast<PreviewWindow *>(controller.window());
    QVERIFY(QTest::qWaitForWindowExposed(preview));
    auto *surface = preview->windowHandle();
    const auto nativeId = preview->winId();
    QSignalSpy dismissed(preview, &PreviewWindow::dismissed);
    QWidget caller;
    caller.setWindowTitle(QStringLiteral("Dzurkel native focus test caller"));
    caller.resize(320, 180);
    caller.show();
    QVERIFY(QTest::qWaitForWindowExposed(&caller));
    const auto bus = QDBusConnection::sessionBus();
    const auto call = [&bus](const QString &path, const QString &method, const QVariantList &arguments) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), path,
            path == QStringLiteral("/Scripting") ? QStringLiteral("org.kde.kwin.Scripting")
                                                : QStringLiteral("org.kde.kwin.Script"), method);
        message.setArguments(arguments);
        return bus.call(message, QDBus::Block, 2000);
    };
    const auto activateCaller = [&]() {
        // Arrange focus only on this test's caller. The preview must gain focus
        // through its real activation request, never through this KWin script.
        const QJsonObject target{{QStringLiteral("pid"), QCoreApplication::applicationPid()},
                                 {QStringLiteral("title"), caller.windowTitle()}};
        QTemporaryFile script;
        if (!script.open()) return false;
        const QByteArray source = "const p = " + QJsonDocument(target).toJson(QJsonDocument::Compact)
            + "; workspace.stackingOrder.forEach(function(w) { if (w.pid === p.pid && "
              "w.caption.indexOf(p.title) === 0) workspace.activeWindow = w; });";
        if (script.write(source) != source.size() || !script.flush()) return false;
        const QString plugin = QStringLiteral("dzurkel-native-focus-test-%1").arg(QCoreApplication::applicationPid());
        const QDBusReply<int> loaded = call(QStringLiteral("/Scripting"), QStringLiteral("loadScript"),
                                            {script.fileName(), plugin});
        bool ran = false;
        if (loaded.isValid() && loaded.value() >= 0)
            ran = call(QStringLiteral("/Scripting/Script%1").arg(loaded.value()),
                       QStringLiteral("run"), {}).type() != QDBusMessage::ErrorMessage;
        call(QStringLiteral("/Scripting"), QStringLiteral("unloadScript"), {plugin});
        return ran;
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt == 2) {
            preview->showMinimized();
            QTRY_VERIFY(preview->isMinimized());
        }
        QVERIFY(activateCaller());
        QTRY_VERIFY(caller.isActiveWindow());
        QTRY_VERIFY(!preview->isActiveWindow());
        QVERIFY(preview->isVisible());
        const int dismissalsBeforeShow = dismissed.count();
        auto token = settingsActivationToken(caller.windowHandle());
        QTRY_VERIFY(token.isFinished());
        QVERIFY(!token.result().isEmpty());
        QVERIFY(controller.show(attempt == 0 ? first : second, error, nullptr, token.result()));
        QTRY_VERIFY(preview->isActiveWindow());
        QTRY_VERIFY(!controller.loading());
        QVERIFY(!preview->isMinimized());
        QCOMPARE(preview->windowHandle(), surface);
        QCOMPARE(preview->winId(), nativeId);
        QCOMPARE(dismissed.count(), dismissalsBeforeShow);
    }
    // A navigation request may finish after the user switches away. Updating
    // its content must not behave like another explicit launcher invocation.
    emit preview->navigateRequested(-1);
    QVERIFY(activateCaller());
    QTRY_VERIFY(caller.isActiveWindow());
    QTRY_COMPARE(controller.fileUrl(), QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded));
    QTRY_VERIFY(!controller.loading());
    QTest::qWait(200);
    QVERIFY(caller.isActiveWindow());
    QVERIFY(!preview->isActiveWindow());
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
}

void PreviewTest::coldInformationMapsAtCompactSize_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("archive") << QStringLiteral("first-map.zip");
    QTest::newRow("office-document") << QStringLiteral("first-map.odt");
}
void PreviewTest::coldInformationMapsAtCompactSize()
{
    QFETCH(QString, name);
    const QString path = write(name, QByteArray::fromHex("504b030400010203ff"));
    PreviewController controller;
    auto *window = controller.window()->findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->window();
    FirstShowSize firstShow;
    window->installEventFilter(&firstShow);
    QString error;
    QVERIFY(controller.show(path, error));
    QVERIFY(controller.isVisible()); // The requested preview is cancellable before mapping.
    QVERIFY(!window->isVisible());
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("information"));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(firstShow.size.width() >= 380);
    QVERIFY(firstShow.size.width() <= 560);
    QVERIFY(firstShow.size.height() >= 220);
    QVERIFY(firstShow.size.height() < 500);
    QTest::qWait(100);
    QCOMPARE(window->size(), firstShow.size);
    QVERIFY(window->grab().save(QStringLiteral("preview-first-map-%1.png").arg(name)));
    controller.hide();
    QVERIFY(controller.show(makePdf(), error));
    const QSize area = window->screen()->availableGeometry().size();
    QVERIFY(window->width() >= area.width() * 80 / 100);
    QVERIFY(window->height() >= area.height() * 80 / 100);
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("pdf"));
}
void PreviewTest::hideBeforeFirstMappingPreventsResurrection()
{
    const QString path = write(QStringLiteral("cancel-first-map.zip"), QByteArray::fromHex("504b030400010203ff"));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QVERIFY(controller.isVisible());
    QVERIFY(controller.toggle(path, error));
    QVERIFY(!controller.isVisible());
    QVERIFY(!controller.loading());
    QTest::qWait(150);
    QVERIFY(!controller.window()->isVisible());
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.window()->isVisible());
    QCOMPARE(controller.previewKind(), QStringLiteral("information"));
}
void PreviewTest::newestRequestBeforeFirstMappingWins()
{
    const QString unsupported = write(QStringLiteral("obsolete-first-map.zip"), QByteArray::fromHex("504b030400010203ff"));
    const QString text = write(QStringLiteral("current-first-map.txt"), "The latest request wins.\n");
    PreviewController controller;
    auto *window = controller.window()->findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->window();
    FirstShowSize firstShow;
    window->installEventFilter(&firstShow);
    QString error;
    QVERIFY(controller.show(unsupported, error));
    QVERIFY(controller.show(text, error));
    QVERIFY(!window->isVisible());
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    QCOMPARE(window->windowTitle(), QStringLiteral("current-first-map.txt"));
    const QSize area = window->screen()->availableGeometry().size();
    QVERIFY(firstShow.size.width() >= area.width() * 80 / 100);
    QVERIFY(firstShow.size.height() >= area.height() * 80 / 100);
}

void PreviewTest::coldInformationThenReopenedPdfKeepsContentAndSize()
{
    const QString unsupported = write(QStringLiteral("cold-information.zip"), QByteArray::fromHex("504b030400010203ff"));
    const QString pdf = makePdf();
    PreviewController controller;
    QString error;
    for (int cycle = 0; cycle < 4; ++cycle) {
        QVERIFY(controller.show(unsupported, error));
        QTRY_VERIFY(!controller.loading());
        auto *window = controller.window()->findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->window();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // Allow a late compositor configure to arrive after provider completion.
        QTest::qWait(100);
        auto *info = window->findChild<QWidget *>(QStringLiteral("fileInfoPreview"));
        QVERIFY(info);
        QTRY_VERIFY(info->isVisible());
        QTRY_VERIFY(info->height() >= 150);
        QTRY_VERIFY(window->height() >= 220);
        QVERIFY(window->width() <= 560);
        QVERIFY(window->height() < 500);
        if (cycle == 3) QVERIFY(window->grab().save(QStringLiteral("preview-cold-information.png")));
        controller.hide();
        // Match user dismissal: allow content teardown before another CLI show.
        QTRY_VERIFY(!window->findChild<QWidget *>(QStringLiteral("fileInfoPreview")));
        QVERIFY(controller.show(pdf, error));
        // Placement must use the large size before the async provider finishes.
        // Qt cannot repair the compositor's global position afterward on Wayland.
        const QSize area = window->screen()->availableGeometry().size();
        QVERIFY(window->width() >= area.width() * 80 / 100);
        QVERIFY(window->height() >= area.height() * 80 / 100);
        QTRY_VERIFY(!controller.loading());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(100);
        QCOMPARE(controller.previewKind(), QStringLiteral("pdf"));
        QTRY_VERIFY(window->width() >= area.width() * 80 / 100);
        QTRY_VERIFY(window->height() >= area.height() * 80 / 100);
        QVERIFY(window->findChild<QPdfView *>()->isVisible());
        if (cycle == 3) QVERIFY(window->grab().save(QStringLiteral("preview-reopened-pdf.png")));
        controller.hide();
        QTRY_VERIFY(!window->findChild<QPdfView *>());
    }
}

void PreviewTest::informationFallbackTemporarilyCompactsWindow()
{
    const QString unsupported = write(QStringLiteral("compact.zip"), QByteArray::fromHex("00010203ff"));
    const QString text = write(QStringLiteral("compact-return.txt"), "A readable preview\n");
    const QString imagePath = directory_.filePath(QStringLiteral("compact-return.png"));
    QImage image(1600, 900, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));
    std::atomic<int> targetWidth = 0;
    PreviewController controller(false, [&targetWidth](const FileResource &resource, const QSize &target, const Cancellation &cancel) {
        targetWidth.store(target.width());
        return PreviewRegistry::load(resource, target, cancel);
    });
    QString error;
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = controller.window()->findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->window();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    // Resize only after initial placement, as a user would; a pending initial
    // compositor configure can otherwise overwrite this synthetic resize.
    // Mutter may auto-maximize an initially large window. A manual border drag
    // restores it first; QWidget::resize alone does not change that state.
    window->showNormal();
    QTRY_VERIFY(!window->isMaximized());
    QTest::qWait(100);
    window->resize(740, 700);
    QCoreApplication::processEvents();
    const QSize previewSize = window->size();
    QVERIFY(controller.show(unsupported, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("information"));
    QTRY_VERIFY(window->width() <= 560);
    QTRY_VERIFY(window->height() < 500);
    QVERIFY(window->grab().save(QStringLiteral("preview-information-compact.png")));
    controller.hide();
    QVERIFY(controller.show(unsupported, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_VERIFY(window->width() <= 560);
    // Compact window resizing must not replace the remembered preview size.
    window->resize(500, 400);
    QVERIFY(controller.show(imagePath, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("image"));
    QVERIFY(window->size() != previewSize); // Images now choose their natural size.
    QVERIFY(targetWidth.load() > 600);
    QVERIFY(controller.show(unsupported, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), previewSize);
    QVERIFY(controller.show(unsupported, error));
    window->showMinimized();
    QTRY_VERIFY(!controller.loading());
    QVERIFY(window->isMinimized());
}

void PreviewTest::mappedImageAndTableTransitionsRestoreReadingSize()
{
    const auto text = write(QStringLiteral("mapped-reading.txt"), "A document starts at its reading size.\n");
    const auto table = write(QStringLiteral("mapped-reading.csv"), "Name,Value\nFirst,42\n");
    const auto imagePath = directory_.filePath(QStringLiteral("mapped-small.png"));
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));
    PreviewController controller;
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QSignalSpy dismissed(window, &PreviewWindow::dismissed);
    QString error;
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->showNormal();
    QTRY_VERIFY(!window->isMaximized());
    QTest::qWait(100);
    const QSize target = QSize(740, 600).boundedTo(window->screen()->availableGeometry().size() - QSize(32, 32));
    window->resize(target);
    QTRY_COMPARE(window->size(), target);
    const auto readingSize = window->size();
    QVERIFY(readingSize.width() > 400);
    const auto nativeId = window->winId();
    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY(controller.show(imagePath, error));
        QTRY_VERIFY(!controller.loading());
        QTRY_COMPARE(window->size(), QSize(400, 400));
        QVERIFY(controller.show(table, error));
        QTRY_VERIFY(!controller.loading());
        QCOMPARE(controller.previewKind(), QStringLiteral("table"));
        QTRY_COMPARE(window->size(), readingSize);
        QCOMPARE(window->winId(), nativeId);
        QCOMPARE(dismissed.count(), 0);
    }
}

void PreviewTest::imageWindowsFitContentAndPreserveReadingSize()
{
    const auto text = write(QStringLiteral("reading-size.txt"), "A document keeps its reading size.\n");
    const auto info = write(QStringLiteral("image-return.bin"), QByteArray::fromHex("000102ff"));
    const QSize area = QGuiApplication::primaryScreen()->availableGeometry().size();
    const QSize mediumSize(qMin(800, area.width() - 120), qMin(600, area.height() - 160));
    QStringList images;
    for (const auto &size : {QSize(40, 30), mediumSize, QSize(4000, 2000)}) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor("#3067b1"));
        QPainter painter(&image);
        painter.fillRect(image.rect().adjusted(size.width() / 8, size.height() / 8,
            -size.width() / 8, -size.height() / 8), QColor("#e6a52a"));
        painter.end();
        const auto path = directory_.filePath(QStringLiteral("natural-%1.png").arg(size.width()));
        QVERIFY(image.save(path));
        images.append(path);
    }
    PreviewController controller;
    auto *window = const_cast<PreviewWindow *>(controller.window());
    QSignalSpy dismissed(window, &PreviewWindow::dismissed);
    FirstShowSize firstShow;
    window->installEventFilter(&firstShow);
    QString error;
    // Fast cold images must map at their natural size, without an 85% flash.
    QVERIFY(controller.show(images.first(), error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(window->size(), QSize(400, 400));
    QCOMPARE(firstShow.size, QSize(400, 400));
    auto *zoom = window->findChild<ZoomPreview *>();
    QVERIFY(zoom);
    QCOMPARE(zoom->zoomFactor(), 1.0);
    QCOMPARE(zoom->findChild<QWidget *>(QStringLiteral("imageCanvas"))->size(), QSize(40, 30));
    QVERIFY(window->grab().save(QStringLiteral("ui-refinement-small-image.png")));
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QTest::qWait(100);
    window->resize(QSize(740, 700).boundedTo(area - QSize(32, 32)));
    QCoreApplication::processEvents();
    const auto readingSize = window->size();
    QVERIFY(controller.show(images.at(1), error));
    QTRY_VERIFY(!controller.loading());
    // Allow the asynchronous compositor geometry transaction to settle before
    // simulating a manual resize on the same mapped surface.
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(100);
    zoom = window->findChild<ZoomPreview *>();
    QTRY_COMPARE(zoom->zoomFactor(), 1.0);
    QCOMPARE(zoom->findChild<QWidget *>(QStringLiteral("imageCanvas"))->size(), mediumSize);
    QVERIFY(window->width() >= mediumSize.width());
    QVERIFY(window->width() <= mediumSize.width() + 32);
    QVERIFY(window->height() >= mediumSize.height());
    QVERIFY(window->height() <= mediumSize.height() + 130);
    QVERIFY(window->grab().save(QStringLiteral("ui-refinement-image.png")));
    window->resize(500, 500);
    QTest::qWait(100);
    QCOMPARE(window->size(), QSize(500, 500));
    zoom->zoomBy(1);
    QCOMPARE(window->size(), QSize(500, 500));
    QVERIFY(controller.show(images.last(), error));
    QTRY_VERIFY(!controller.loading());
    zoom = window->findChild<ZoomPreview *>();
    QVERIFY(zoom->zoomFactor() < 1.0);
    QVERIFY(window->width() < area.width());
    QVERIFY(window->height() < area.height());
    QVERIFY(window->grab().save(QStringLiteral("ui-refinement-large-image.png")));
    // Image and compact-information transitions must both preserve the document size.
    QVERIFY(controller.show(info, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.show(images.first(), error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), QSize(400, 400));
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), readingSize);
    QCOMPARE(dismissed.count(), 0); // Automatic Wayland placement is not dismissal.
    controller.hide();
    QCOMPARE(dismissed.count(), 1);
    QVERIFY(controller.show(images.first(), error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), QSize(400, 400));
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    QTRY_COMPARE(window->size(), readingSize);
    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    QVERIFY(controller.show(images.at(1), error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(window->isMaximized());
}

void PreviewTest::informationFallbackRestoresMaximizedPreview()
{
    const QString text = write(QStringLiteral("maximized-return.txt"), "Text preview\n");
    const QString pdf = makePdf();
    const QString unsupported = write(QStringLiteral("maximized.bin"), QByteArray::fromHex("00010203ff"));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = controller.window()->findChild<QPushButton *>(QStringLiteral("fileInfoButton"))->window();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const QSize normalSize = window->size();
    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    // Wait for the compositor's maximize configure, not just Qt's local flag.
    QTRY_VERIFY(window->size() != normalSize);
    QVERIFY(controller.show(unsupported, error));
    QTRY_VERIFY(!controller.loading());
    // Wayland acknowledges maximize/restore requests asynchronously.
    QTRY_VERIFY(!window->isMaximized());
    QTRY_VERIFY(window->width() <= 560);
    QVERIFY(controller.show(pdf, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("pdf"));
    QTRY_VERIFY(window->isMaximized());
}

void PreviewTest::retainedPdfRuntimeStillReleasesEachDocument()
{
    const QString file = makePdf();
    PreviewController controller;
    for (int i = 0; i < 8; ++i) {
        QString error;
        QVERIFY2(controller.show(file, error), qPrintable(error));
        QTRY_VERIFY(!controller.loading());
        QCOMPARE(controller.previewKind(), QStringLiteral("pdf"));
        auto *view = controller.window()->findChild<QPdfView *>();
        QVERIFY(view);
        QPointer<QPdfDocument> previous = view->document();
        QVERIFY(previous);
        controller.hide();
        QTRY_VERIFY(previous.isNull());
        QVERIFY(!controller.window()->findChild<QPdfView *>());
    }
}

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
    QCOMPARE(data.image.size(), QSize(400, 200)); // Retain source detail for zoom.
    QCOMPARE(data.imageSize, QSize(400, 200));
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
    FileResource resource;
    QString error;
    QVERIFY(resolveFileResource(path, resource, error));
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    QSize metadataSize;
    const auto cancelled = PreviewRegistry::load(resource, QSize(500, 500), cancel, {},
        [&](const QSize &size) {
            metadataSize = size;
            // Cancellation here must stop allocation/decoding, proving that
            // oriented metadata arrives before the full image is read.
            cancel->store(true);
        });
    QCOMPARE(metadataSize, QSize(60, 120));
    QCOMPARE(cancelled.kind, PreviewKind::Cancelled);
    QVERIFY(cancelled.image.isNull());
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
    QTest::addColumn<int>("lines");
    QTest::newRow("UTF-8") << QString::fromUtf8("  Пример 😀\n\tline\n").toUtf8() << QString::fromUtf8("  Пример 😀\n\tline\n") << 2;
    QTest::newRow("line endings") << QByteArray("A\r\nB\rC\n") << QStringLiteral("A\nB\nC\n") << 3;
    QTest::newRow("UTF-32LE") << QByteArray::fromHex("fffe0000410000000a00000011040000") << QString::fromUtf8("A\nБ") << 2;
    QTest::newRow("UTF-16LE") << QByteArray::fromHex("fffe41000a001104") << QString::fromUtf8("A\nБ") << 2;
    QTest::newRow("Latin-1") << QByteArray::fromHex("636166e90a") << QString::fromUtf8("café\n") << 1;
    QTest::newRow("plain XML") << QByteArray("<script>alert('text only')</script>\n") << QStringLiteral("<script>alert('text only')</script>\n") << 1;
    QTest::newRow("blank line") << QByteArray("A\n\nB") << QStringLiteral("A\n\nB") << 3;
    QTest::newRow("no final newline") << QByteArray("Only one line") << QStringLiteral("Only one line") << 1;
}
void PreviewTest::decodesText()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    QFETCH(int, lines);
    const auto data = load(write(QStringLiteral("text.txt"), bytes));
    QCOMPARE(data.kind, PreviewKind::Text);
    QCOMPARE(data.text, expected);
    QCOMPARE(data.textLineCount, lines);
    std::unique_ptr<QWidget> widget(PreviewRegistry::createWidget(data, nullptr));
    auto *view = qobject_cast<QPlainTextEdit *>(widget.get());
    QVERIFY(view);
    QVERIFY(view->isReadOnly());
    QCOMPARE(view->lineWrapMode(), QPlainTextEdit::NoWrap);
    QCOMPARE(view->toPlainText(), expected);
    PreviewWindow window(data.resource);
    window.setContent(widget.release());
    window.setPreviewData(data);
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("previewSummary"))->text(),
        QStringLiteral("%1 · %2 %3").arg(data.detail).arg(lines)
            .arg(lines == 1 ? QStringLiteral("line") : QStringLiteral("lines")));
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
void PreviewTest::immediateReopenIgnoresDismissedWorker()
{
    const QString first = write(QStringLiteral("dismissed.txt"), "first");
    const QString last = write(QStringLiteral("reopened.txt"), "last");
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
    controller.hide();
    QVERIFY(controller.show(last, error));
    release.release();
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.isVisible());
    QCOMPARE(calls.load(), 2);
    auto *view = controller.window()->findChild<QPlainTextEdit *>();
    QVERIFY(view);
    QCOMPARE(view->toPlainText(), last);
}
void PreviewTest::shutdownCancelsCooperativeWorker()
{
    const QString path = write(QStringLiteral("shutdown.txt"), "shutdown");
    QSemaphore entered;
    std::atomic_bool cancelled = false;
    auto controller = std::make_unique<PreviewController>(false,
        [&](const FileResource &, const QSize &, const Cancellation &cancel) {
            entered.release();
            QElapsedTimer deadline;
            deadline.start();
            while (!cancel->load() && deadline.elapsed() < 2000)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cancelled.store(cancel->load());
            PreviewData data;
            data.kind = PreviewKind::Cancelled;
            return data;
        });
    QString error;
    QVERIFY(controller->show(path, error));
    QVERIFY(entered.tryAcquire(1, 2000));
    QElapsedTimer timer;
    timer.start();
    controller.reset();
    QVERIFY(cancelled.load());
    QVERIFY2(timer.elapsed() < 1500, "Cooperative worker did not finish promptly during shutdown");
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

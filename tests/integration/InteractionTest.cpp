#include "controller/PreviewController.h"
#include "resources/FolderNavigation.h"
#include "resources/ResourceAccessPolicy.h"
#include "preview/PreviewRegistry.h"
#include "ui/ZoomPreview.h"
#include <QDBusConnection>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfView>
#include <QPdfPageRenderer>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <sys/stat.h>
#include <unistd.h>

using namespace dzurkel;

class ListingFuse : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString root;
public slots:
    QString mountUrl(const QString &url) { return root + '/' + QUrl(url).fileName(); }
};

class InteractionTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void roundZoomSteps();
    void smallImagesFitWithoutEnlargement();
    void gifPlaybackPauseReplayAndReplacement();
    void gifBudgetsFallBackToFirstFrame();
    void imageZoomScrollFitAndReset();
    void imageZoomRetainsBoundedDetail();
    void pdfZoomKeepsDocumentAndNativeArrows();
    void naturalOrderExcludesLinksAndSupportsHiddenAnchor();
    void changingFolderSkipsRemovedFilesAndKeepsBoundaries();
    void listingBudgetNeverReturnsPartialOrder();
    void remoteNavigationPreservesOriginalIdentity_data();
    void remoteNavigationPreservesOriginalIdentity();
    void cancelledListingCannotReplaceNewLocation();
    void navigationButtonsAndProviderKeys();
private:
    static QString write(const QTemporaryDir &directory, const QString &name, const QByteArray &bytes = "hello\n");
    static FileResource resource(const QString &path);
    QTemporaryDir config_;
    ListingFuse fuse_;
};
QString InteractionTest::write(const QTemporaryDir &directory, const QString &name, const QByteArray &bytes)
{
    const auto path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
FileResource InteractionTest::resource(const QString &path)
{
    FileResource result;
    QString error;
    if (!parseFileResource(path, result, error)) QTest::qFail(qPrintable(error), __FILE__, __LINE__);
    return result;
}
void InteractionTest::initTestCase()
{
    QVERIFY(config_.isValid());
    qputenv("XDG_CONFIG_HOME", config_.path().toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void InteractionTest::roundZoomSteps()
{
    class ZoomProbe : public ZoomPreview {
    public:
        qreal scale = 1.21;
        void zoomBy(int steps) override { scale = steppedZoom(steps); }
        void fit() override {}
        qreal zoomFactor() const override { return scale; }
        bool isFit() const override { return false; }
    } zoom;
    zoom.zoomBy(-1); QCOMPARE(zoom.scale, 1.2);
    zoom.zoomBy(-1); QCOMPARE(zoom.scale, 1.1);
    zoom.zoomBy(-1); QCOMPARE(zoom.scale, 1.0);
    zoom.scale = 1.21;
    zoom.zoomBy(1); QCOMPARE(zoom.scale, 1.3);
    zoom.zoomBy(2); QCOMPARE(zoom.scale, 1.5);
    zoom.scale = 1.199999999999;
    zoom.zoomBy(1); QCOMPARE(zoom.scale, 1.3);
    zoom.scale = .06;
    zoom.zoomBy(-1); QCOMPARE(zoom.scale, .06);
    zoom.zoomBy(1); QCOMPARE(zoom.scale, .1);
    zoom.zoomBy(1000); QCOMPARE(zoom.scale, 8.0);
    zoom.zoomBy(-1000); QCOMPARE(zoom.scale, .1);
}
void InteractionTest::smallImagesFitWithoutEnlargement()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("small.png"));
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *zoom = window->findChild<ZoomPreview *>();
    auto *canvas = zoom->findChild<QWidget *>(QStringLiteral("imageCanvas"));
    QCOMPARE(zoom->zoomFactor(), 1.0);
    QCOMPARE(canvas->size(), QSize(40, 30));
    window->resize(1000, 800);
    QCoreApplication::processEvents();
    QCOMPARE(zoom->zoomFactor(), 1.0);
    QCOMPARE(canvas->size(), QSize(40, 30));
    zoom->zoomBy(1);
    QCOMPARE(zoom->zoomFactor(), 1.1);
    zoom->fit();
    QCOMPARE(zoom->zoomFactor(), 1.0);
}
void InteractionTest::gifPlaybackPauseReplayAndReplacement()
{
    const auto path = QStringLiteral(DZURKEL_FIXTURES "/animated.gif");
    const auto data = PreviewRegistry::load(resource(path), QSize(320, 240), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Image);
    QCOMPARE(data.animationFrames.size(), 3);
    QCOMPARE(data.animationDelays, QList<int>({80, 120, 160}));
    QCOMPARE(data.animationLoops, -1);
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *zoom = window->findChild<ZoomPreview *>();
    auto *playback = zoom->findChild<QPushButton *>(QStringLiteral("animationToggle"));
    auto *canvas = zoom->findChild<QWidget *>(QStringLiteral("imageCanvas"));
    QVERIFY(playback && canvas);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCOMPARE(zoom->zoomFactor(), 1.0);
    const auto color = [canvas] { return canvas->grab().toImage().pixelColor(canvas->width() / 2, canvas->height() / 2); };
    const auto first = color();
    QTRY_VERIFY(color() != first);
    QTest::mouseClick(playback, Qt::LeftButton);
    QCOMPARE(playback->text(), QStringLiteral("Play"));
    const auto paused = color();
    QTest::qWait(200);
    QCOMPARE(color(), paused);
    zoom->zoomBy(1);
    QCOMPARE(zoom->zoomFactor(), 1.1);
    QTest::mouseClick(playback, Qt::LeftButton);
    QTRY_VERIFY(color() != paused);
    QCOMPARE(zoom->zoomFactor(), 1.1); // Frames must not reset zoom.
    QPointer<QWidget> old = zoom;
    QVERIFY(controller.show(QStringLiteral(DZURKEL_FIXTURES "/animated-once.gif"), error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(old.isNull());
    playback = window->findChild<QPushButton *>(QStringLiteral("animationToggle"));
    QVERIFY(playback);
    QTRY_COMPARE(playback->text(), QStringLiteral("Replay"));
    QTest::mouseClick(playback, Qt::LeftButton);
    QCOMPARE(playback->text(), QStringLiteral("Pause"));
    controller.hide();
    QTRY_VERIFY(!window->findChild<QPushButton *>(QStringLiteral("animationToggle")));
}
void InteractionTest::gifBudgetsFallBackToFirstFrame()
{
    for (const auto &name : {"animation-frame-limit.gif", "animation-memory-limit.gif"}) {
        const auto data = PreviewRegistry::load(resource(QStringLiteral(DZURKEL_FIXTURES "/") + QString::fromLatin1(name)),
                                               QSize(320, 240), std::make_shared<std::atomic_bool>(false));
        QCOMPARE(data.kind, PreviewKind::Image);
        QVERIFY(!data.image.isNull());
        QVERIFY(data.animationFrames.isEmpty());
        QVERIFY2(data.message.contains(QStringLiteral("showing the first frame")), qPrintable(data.message));
        const auto expected = QByteArray(name).contains("memory") ? QStringLiteral("64 MiB") : QStringLiteral("300-frame");
        QVERIFY2(data.message.contains(expected), qPrintable(data.message));
    }
}
void InteractionTest::imageZoomScrollFitAndReset()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("image.png"));
    QImage image(1600, 1200, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *zoom = window->findChild<ZoomPreview *>();
    QVERIFY(zoom);
    QVERIFY(zoom->isFit());
    const qreal initial = zoom->zoomFactor();
    QTest::keyClick(zoom->focusProxy(), Qt::Key_Plus);
    QVERIFY(!zoom->isFit());
    QVERIFY(qAbs(zoom->zoomFactor() - (std::floor(initial * 10 + 1e-8) + 1) / 10.0) < .001);
    QTest::keyClick(zoom, Qt::Key_0);
    QVERIFY(zoom->isFit());
    auto *scroll = zoom->findChild<QScrollArea *>();
    QVERIFY(scroll);
    // Fractional wheel events accumulate instead of each causing a jump.
    for (int i = 0; i < 4; ++i) {
        QWheelEvent wheel(QPointF(20, 20), QPointF(20, 20), {}, QPoint(0, 30),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(scroll->viewport(), &wheel);
        if (i < 3) QVERIFY(zoom->isFit());
    }
    QVERIFY(!zoom->isFit());
    zoom->zoomBy(100);
    QCOMPARE(zoom->zoomFactor(), 8.0);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum() / 2);
    const int before = scroll->verticalScrollBar()->value();
    const auto factor = zoom->zoomFactor();
    QWheelEvent wheel(QPointF(20, 20), QPointF(20, 20), {}, QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(scroll->viewport(), &wheel);
    QVERIFY(scroll->verticalScrollBar()->value() > before);
    QCOMPARE(zoom->zoomFactor(), factor);
    zoom->zoomBy(-100);
    QCOMPARE(zoom->zoomFactor(), .1);
    QTest::mouseClick(window->findChild<QPushButton *>(QStringLiteral("zoomFit")), Qt::LeftButton);
    QVERIFY(zoom->isFit());
    window->resize(600, 500);
    QCoreApplication::processEvents();
    QVERIFY(zoom->zoomFactor() < initial);
    QVERIFY(window->grab().save(QStringLiteral("interactions-image.png")));
    QPointer<ZoomPreview> old = zoom;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(old.isNull());
    QVERIFY(window->findChild<ZoomPreview *>()->isFit());
    QTest::keyClick(window->findChild<ZoomPreview *>(), Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
}
void InteractionTest::imageZoomRetainsBoundedDetail()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("large.jpg"));
    QImage image(5000, 3000, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    QVERIFY(image.save(path));
    image = {};
    const auto data = PreviewRegistry::load(resource(path), QSize(320, 240), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Image);
    QCOMPARE(data.imageSize, QSize(5000, 3000));
    QVERIFY(data.image.width() <= 4096 && data.image.height() <= 4096);
    QVERIFY(data.image.width() > 320); // Zoom has useful detail beyond the first viewport.
    QVERIFY(data.image.sizeInBytes() <= 64 * 1024 * 1024);
}
void InteractionTest::pdfZoomKeepsDocumentAndNativeArrows()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("file1.pdf"));
    {
        QPdfWriter writer(path);
        writer.setResolution(96);
        QPainter painter(&writer);
        painter.setFont(QFont(QStringLiteral("Sans Serif"), 18));
        painter.drawText(100, 100, QStringLiteral("Zoom test"));
        painter.fillRect(40, 140, 100, 80, Qt::blue);
        writer.newPage();
        painter.drawText(100, 100, QStringLiteral("Second page"));
    }
    QVERIFY(!write(directory, QStringLiteral("file2.txt")).isEmpty());
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *zoom = window->findChild<ZoomPreview *>();
    auto *view = window->findChild<QPdfView *>();
    QVERIFY(zoom && view);
    auto *document = view->document();
    QTest::keyClick(view, Qt::Key_Plus);
    QCOMPARE(view->zoomMode(), QPdfView::ZoomMode::Custom);
    QCOMPARE(view->document(), document);
    QVERIFY(qAbs(zoom->zoomFactor() * 100 - window->findChild<QLabel *>(QStringLiteral("zoomStatus"))->text().remove('%').toInt()) < 1);
    zoom->zoomBy(100);
    QVERIFY(zoom->zoomFactor() <= zoom->maximumZoom() + .001);
    QVERIFY(zoom->maximumZoom() <= 8.0);
    QTest::keyClick(view, Qt::Key_Right);
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
    QSignalSpy rendered(view->findChild<QPdfPageRenderer *>(), &QPdfPageRenderer::pageRendered);
    QTest::keyClick(view, Qt::Key_0);
    QCOMPARE(view->zoomMode(), QPdfView::ZoomMode::FitToWidth);
    // Inspect the first page after fitting, independently of the current
    // document scroll position retained by zoom/fit.
    view->verticalScrollBar()->setValue(0);
    view->horizontalScrollBar()->setValue(0);
    QTRY_VERIFY(!rendered.isEmpty());
    const auto pageIsVisible = [view] {
        const auto image = view->viewport()->grab().toImage();
        for (int y = 8; y < image.height(); y += 8)
            for (int x = 8; x < image.width(); x += 8) {
                const auto color = image.pixelColor(x, y);
                if (color.blue() > 180 && color.red() < 80 && color.green() < 80) return true;
            }
        return false;
    };
    QTRY_VERIFY(pageIsVisible());
    QVERIFY(window->grab().save(QStringLiteral("interactions-pdf.png")));
    QTRY_VERIFY(window->findChild<QPushButton *>(QStringLiteral("nextFile"))->isEnabled());
    QTest::keyClick(view, Qt::Key_Right, Qt::AltModifier);
    QTRY_VERIFY(controller.fileUrl().endsWith(QStringLiteral("file2.txt")));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
}
void InteractionTest::naturalOrderExcludesLinksAndSupportsHiddenAnchor()
{
    QTemporaryDir directory;
    for (const auto &name : {"file10.txt", "file2.txt", "File1.txt", ".hidden.txt", "file3.zip"})
        QVERIFY(!write(directory, QString::fromLatin1(name)).isEmpty());
    QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("folder")));
    QCOMPARE(::symlink("file2.txt", QFile::encodeName(directory.filePath(QStringLiteral("link.txt"))).constData()), 0);
    QCOMPARE(::mkfifo(QFile::encodeName(directory.filePath(QStringLiteral("pipe"))).constData(), 0600), 0);
    FolderNavigation listing;
    QSignalSpy spy(&listing, &FolderNavigation::ready);
    listing.start(resource(directory.filePath(QStringLiteral("file2.txt"))));
    QVERIFY(spy.wait(5000));
    auto result = spy.takeFirst();
    QStringList names;
    for (const auto &url : result.at(0).toStringList()) names.append(QUrl(url).fileName());
    QCOMPARE(names, QStringList({"File1.txt", "file2.txt", "file3.zip", "file10.txt"}));
    QCOMPARE(result.at(1).toInt(), 1);
    QVERIFY(result.at(2).toString().isEmpty());
    listing.start(resource(directory.filePath(QStringLiteral(".hidden.txt"))));
    QVERIFY(spy.wait(5000));
    result = spy.takeFirst();
    QCOMPARE(result.at(0).toStringList().size(), 5);
    QVERIFY(result.at(1).toInt() >= 0);
    for (const auto &path : {directory.path(), directory.filePath(QStringLiteral("link.txt")), directory.filePath(QStringLiteral("pipe"))}) {
        listing.start(resource(path));
        QVERIFY(spy.wait(5000));
        result = spy.takeFirst();
        QVERIFY(result.at(0).toStringList().isEmpty());
        QVERIFY(!result.at(2).toString().isEmpty());
    }
}
void InteractionTest::changingFolderSkipsRemovedFilesAndKeepsBoundaries()
{
    QTemporaryDir directory;
    for (const auto &name : {"file1.txt", "file2.txt", "file3.txt"}) QVERIFY(!write(directory, QString::fromLatin1(name)).isEmpty());
    FolderNavigation listing;
    QSignalSpy spy(&listing, &FolderNavigation::ready);
    const auto current = resource(directory.filePath(QStringLiteral("file2.txt")));
    QVERIFY(QFile::remove(current.path));
    listing.start(current, 1);
    QVERIFY(spy.wait(5000));
    auto result = spy.takeFirst();
    QVERIFY(result.at(3).toString().endsWith(QStringLiteral("file3.txt")));
    QVERIFY(!result.at(2).toString().isEmpty());
    listing.start(resource(directory.filePath(QStringLiteral("file3.txt"))), 1);
    QVERIFY(spy.wait(5000));
    result = spy.takeFirst();
    QVERIFY(result.at(3).toString().isEmpty());
    QCOMPARE(result.at(1).toInt(), result.at(0).toStringList().size() - 1);
}
void InteractionTest::listingBudgetNeverReturnsPartialOrder()
{
    QTemporaryDir directory;
    for (int i = 0; i <= FolderNavigation::EntryLimit; ++i)
        QVERIFY(!write(directory, QStringLiteral("%1.txt").arg(i), {}).isEmpty());
    FolderNavigation listing;
    QSignalSpy spy(&listing, &FolderNavigation::ready);
    listing.start(resource(directory.filePath(QStringLiteral("0.txt"))));
    QVERIFY(spy.wait(10000));
    const auto result = spy.takeFirst();
    QVERIFY(result.at(0).toStringList().isEmpty());
    QVERIFY(result.at(2).toString().contains(QStringLiteral("10,000")));
}
void InteractionTest::remoteNavigationPreservesOriginalIdentity_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("SMB") << QStringLiteral("smb://user@server/share/");
    QTest::newRow("SFTP") << QStringLiteral("sftp://user@server:2222/share/");
}
void InteractionTest::remoteNavigationPreservesOriginalIdentity()
{
    QFETCH(QString, base);
    QTemporaryDir directory;
    const auto name = QString::fromUtf8("file2 пример #%.txt");
    QVERIFY(!write(directory, name).isEmpty());
    QVERIFY(!write(directory, QStringLiteral("file10.txt")).isEmpty());
    fuse_.root = directory.path();
    QString error;
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(true, error));
    QUrl original(base);
    original.setPath(original.path() + name);
    FolderNavigation listing;
    QSignalSpy spy(&listing, &FolderNavigation::ready);
    listing.start(resource(original.toString(QUrl::FullyEncoded)), 1);
    QVERIFY(spy.wait(5000));
    const auto result = spy.takeFirst();
    QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
    QCOMPARE(result.at(0).toStringList().size(), 2);
    QCOMPARE(result.at(3).toString(), base + QStringLiteral("file10.txt"));
    QCOMPARE(QUrl(result.at(0).toStringList().first()).fileName(), name);
}
void InteractionTest::cancelledListingCannotReplaceNewLocation()
{
    QTemporaryDir oldDirectory, newDirectory;
    const auto oldPath = write(oldDirectory, QStringLiteral("old.txt"));
    const auto newPath = write(newDirectory, QStringLiteral("new.txt"));
    FolderNavigation listing;
    QSignalSpy spy(&listing, &FolderNavigation::ready);
    listing.start(resource(oldPath));
    listing.cancel();
    QTest::qWait(50); // Reap the cancelled child before testing replacement.
    QVERIFY(spy.isEmpty());
    listing.start(resource(newPath));
    QVERIFY(spy.wait(5000));
    const auto result = spy.takeFirst();
    QCOMPARE(result.at(0).toStringList(), QStringList{QUrl::fromLocalFile(newPath).toString(QUrl::FullyEncoded)});
    QTest::qWait(50);
    QVERIFY(spy.isEmpty());
    FolderNavigation shortDeadline(nullptr, 1);
    QSignalSpy timeout(&shortDeadline, &FolderNavigation::ready);
    shortDeadline.start(resource(QStringLiteral("smb://offline.invalid/share/file.txt")));
    QVERIFY(timeout.wait(5000));
    QVERIFY(timeout.first().at(2).toString().contains(QStringLiteral("timed out")));
}
void InteractionTest::navigationButtonsAndProviderKeys()
{
    QTemporaryDir directory;
    const auto text = write(directory, QStringLiteral("file1.txt"));
    const auto unsupported = write(directory, QStringLiteral("file3.zip"), QByteArray::fromHex("504b030400010203ff"));
    const auto imagePath = directory.filePath(QStringLiteral("file2.png"));
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(text, error));
    QTRY_VERIFY(!controller.loading());
    auto *window = const_cast<PreviewWindow *>(controller.window());
    auto *previous = window->findChild<QPushButton *>(QStringLiteral("previousFile"));
    auto *next = window->findChild<QPushButton *>(QStringLiteral("nextFile"));
    QTRY_VERIFY(next->isEnabled());
    QVERIFY(!previous->isEnabled());
    auto *editor = window->findChild<QPlainTextEdit *>();
    QVERIFY(editor);
    QTest::keyClick(editor, Qt::Key_Right);
    QTest::keyClick(editor, Qt::Key_Down);
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(text).toString(QUrl::FullyEncoded));
    QTest::keyClick(editor, Qt::Key_Down, Qt::AltModifier);
    QTRY_COMPARE(controller.fileUrl(), QUrl::fromLocalFile(imagePath).toString(QUrl::FullyEncoded));
    QTRY_VERIFY(!controller.loading());
    QTRY_VERIFY(next->isEnabled());
    auto *zoom = window->findChild<ZoomPreview *>();
    QVERIFY(zoom);
    zoom->zoomBy(100);
    auto *scroll = zoom->findChild<QScrollArea *>();
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    scroll->verticalScrollBar()->setValue(0);
    // Scrolling also works when a zoom/navigation button retains keyboard focus.
    QTest::keyClick(window->findChild<QPushButton *>(QStringLiteral("zoomIn")), Qt::Key_Down);
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(imagePath).toString(QUrl::FullyEncoded));
    zoom->fit();
    QTest::keyClick(zoom->focusProxy(), Qt::Key_Down);
    QTRY_COMPARE(controller.fileUrl(), QUrl::fromLocalFile(unsupported).toString(QUrl::FullyEncoded));
    QTRY_VERIFY(!controller.loading());
    QTRY_VERIFY(previous->isEnabled());
    QVERIFY(!next->isEnabled());
    QCOMPARE(controller.previewKind(), QStringLiteral("information"));
    QTest::keyClick(window, Qt::Key_Up);
    QTRY_COMPARE(controller.fileUrl(), QUrl::fromLocalFile(imagePath).toString(QUrl::FullyEncoded));
    QTRY_VERIFY(!controller.loading());
    QVERIFY(window->findChild<ZoomPreview *>()->isFit());
    QVERIFY(window->grab().save(QStringLiteral("interactions-navigation.png")));
    controller.hide();
    QTest::qWait(50);
    QVERIFY(!controller.isVisible());
    QVERIFY(!next->isEnabled());
}
QTEST_MAIN(InteractionTest)
#include "InteractionTest.moc"

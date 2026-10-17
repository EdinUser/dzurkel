#include "resources/ResourcePreparation.h"
#include "controller/PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/providers/TextPreviewProvider.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <sys/stat.h>

using namespace dzurkel;

// This fake verifies our exact KIO-FUSE protocol without contacting a server,
// using credentials or modifying the user's actual session service.
class FakeFuse : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
    QString lastUrl;
    int delay = 0;
    bool reject = false;
public slots:
    QString mountUrl(const QString &url) {
        lastUrl = url;
        if (reject) {
            sendErrorReply(QStringLiteral("org.kde.KIOFuse.VFS.Error.CannotMount"), QStringLiteral("Access denied"));
            return {};
        }
        if (delay) {
            setDelayedReply(true);
            const auto request = message();
            const auto bus = connection();
            const auto result = path;
            QTimer::singleShot(delay, this, [bus, request, result] { bus.send(request.createReply(QVariantList{result})); });
            return {};
        }
        return path;
    }
};

class ResourceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void parsesRemoteIdentityWithoutFilesystemAccess();
    void snapshotsSupportedRemoteFormats();
    void boundsTextAndContentCopies();
    void describesSpecialResourcesWithoutReadingThem();
    void reportsAccessFailure();
    void missingLocalFileIsAnAsynchronousError();
    void slowShareDoesNotBlockDismissalOrReplacement();
    void slowShareTimesOut();
    void liveAuthenticatedSmbPreview();
private:
    FileResource prepare(const QString &url);
    QString write(const QString &name, const QByteArray &bytes);
    QTemporaryDir directory_;
    FakeFuse fuse_;
};

void ResourceTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    if (!qEnvironmentVariableIsEmpty("DZURKEL_LIVE_SMB_URL")) return;
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void ResourceTest::init()
{
    fuse_.lastUrl.clear();
    fuse_.reject = false;
    fuse_.delay = 0;
}
QString ResourceTest::write(const QString &name, const QByteArray &bytes)
{
    const auto path = directory_.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
FileResource ResourceTest::prepare(const QString &url)
{
    FileResource input, result;
    QString error;
    if (!parseFileResource(url, input, error)) { QTest::qFail(qPrintable(error), __FILE__, __LINE__); return {}; }
    ResourcePreparation preparation;
    bool ready = false;
    connect(&preparation, &ResourcePreparation::ready, this, [&](const FileResource &resource) {
        result = resource;
        ready = true;
    });
    preparation.start(input);
    QElapsedTimer timer;
    timer.start();
    while (!ready && timer.elapsed() < 5000) QTest::qWait(10);
    if (!ready) QTest::qFail("Resource preparation did not finish", __FILE__, __LINE__);
    return result;
}
void ResourceTest::parsesRemoteIdentityWithoutFilesystemAccess()
{
    FileResource resource;
    QString error;
    const QString url = QStringLiteral("smb://offline.invalid/share/%D0%BF%D1%80%D0%B8%D0%BC%D0%B5%D1%80%20%23%25.txt");
    QElapsedTimer timer;
    timer.start();
    QVERIFY(parseFileResource(url, resource, error));
    QVERIFY(timer.elapsed() < 100);
    QCOMPARE(resource.url.toString(QUrl::FullyEncoded), url);
    QVERIFY(resource.path.endsWith(QString::fromUtf8("пример #%.txt")));
    QVERIFY(fuse_.lastUrl.isEmpty());
    QVERIFY(parseFileResource(QStringLiteral("smb://user@server/share/file.txt"), resource, error));
    QCOMPARE(resource.url.userName(), QStringLiteral("user"));
    for (const auto &invalid : {"smb://", "smb://server/", "smb://user:password@server/share/file",
                               "smb://server/share/file?query", "smb://server/share/file#fragment",
                               "smb://server:123/share/file", "smb://server/share/file%00.txt"})
        QVERIFY2(!parseFileResource(QString::fromLatin1(invalid), resource, error), invalid);
}
void ResourceTest::snapshotsSupportedRemoteFormats()
{
    const auto imagePath = directory_.filePath(QStringLiteral("image.png"));
    QImage image(20, 30, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));
    const auto pdfPath = directory_.filePath(QStringLiteral("document.pdf"));
    {
        QPdfWriter writer(pdfPath);
        QPainter painter(&writer);
        painter.drawText(100, 100, QStringLiteral("SMB fixture"));
    }
    const auto textPath = write(QStringLiteral("original.txt"), "original snapshot\n");
    QVERIFY(!textPath.isEmpty());
    const QList<QPair<QString, PreviewKind>> fixtures = {{imagePath, PreviewKind::Image},
        {pdfPath, PreviewKind::Pdf}, {textPath, PreviewKind::Text}};
    for (const auto &fixture : fixtures) {
        fuse_.path = fixture.first;
        const auto url = QStringLiteral("smb://server/share/%1").arg(QFileInfo(fixture.first).fileName());
        auto resource = prepare(url);
        QCOMPARE(fuse_.lastUrl, url);
        QCOMPARE(resource.url.toString(), url);
        QVERIFY(!resource.informationOnly);
        QVERIFY(!resource.backingPath.isEmpty());
        QVERIFY(resource.backingPath != fixture.first);
        QCOMPARE(QFileInfo(resource.backingPath).suffix(), QFileInfo(fixture.first).suffix());
        const auto store = resource.backingStore->path();
        {
            auto data = PreviewRegistry::load(resource, QSize(100, 100), std::make_shared<std::atomic_bool>(false));
            QCOMPARE(data.kind, fixture.second);
            QCOMPARE(data.resource.url, QUrl(url));
        }
        resource = {};
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QFileInfo::exists(store));
    }
    fuse_.path = textPath;
    auto snapshot = prepare(QStringLiteral("smb://server/share/original.txt"));
    QVERIFY(!write(QStringLiteral("original.txt"), "changed source\n").isEmpty());
    const auto data = PreviewRegistry::load(snapshot, QSize(100, 100), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.text, QStringLiteral("original snapshot\n"));
}
void ResourceTest::boundsTextAndContentCopies()
{
    fuse_.path = write(QStringLiteral("large.txt"), QByteArray(TextPreviewProvider::ByteLimit + 1024, 'a'));
    auto resource = prepare(QStringLiteral("smb://server/share/large.txt"));
    QVERIFY(!resource.informationOnly);
    QCOMPARE(QFileInfo(resource.backingPath).size(), TextPreviewProvider::ByteLimit + 1);
    const auto data = PreviewRegistry::load(resource, QSize(100, 100), std::make_shared<std::atomic_bool>(false));
    QVERIFY(data.truncated);
    QVERIFY(data.message.contains(QStringLiteral("Partial preview")));
    fuse_.path = write(QStringLiteral("huge.pdf"), "%PDF-1.4\n");
    QFile huge(fuse_.path);
    QVERIFY(huge.open(QIODevice::ReadWrite));
    QVERIFY(huge.resize(64 * 1024 * 1024 + 1));
    huge.close();
    resource = prepare(QStringLiteral("smb://server/share/huge.pdf"));
    QVERIFY(resource.informationOnly);
    QVERIFY(resource.backingPath.isEmpty());
    QVERIFY(resource.metadata.value(QStringLiteral("message")).toString().contains(QStringLiteral("64 MiB")));
    QVERIFY(QDir(resource.backingStore->path()).entryList(QDir::Files).isEmpty());
}
void ResourceTest::describesSpecialResourcesWithoutReadingThem()
{
    const auto target = write(QStringLiteral("target.txt"), "not opened");
    const auto link = directory_.filePath(QStringLiteral("link.txt"));
    QVERIFY(QFile::link(target, link));
    const auto fifo = directory_.filePath(QStringLiteral("pipe"));
    QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    for (const auto &path : {directory_.path(), link, fifo}) {
        fuse_.path = path;
        const auto resource = prepare(QStringLiteral("smb://server/share/object"));
        QVERIFY(resource.informationOnly);
        QVERIFY(resource.backingPath.isEmpty());
        QVERIFY(!resource.metadata.value(QStringLiteral("message")).toString().isEmpty());
    }
}
void ResourceTest::reportsAccessFailure()
{
    fuse_.reject = true;
    const auto resource = prepare(QStringLiteral("smb://server/share/file.txt"));
    QVERIFY(resource.informationOnly);
    const auto message = resource.metadata.value(QStringLiteral("message")).toString();
    QVERIFY(message.contains(QStringLiteral("authenticate")));
    QVERIFY(!message.contains(QStringLiteral("password")));
}
void ResourceTest::missingLocalFileIsAnAsynchronousError()
{
    PreviewController controller;
    QString error;
    const auto path = directory_.filePath(QStringLiteral("missing.txt"));
    QVERIFY(controller.show(path, error));
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(controller.previewKind(), QStringLiteral("information"));
    bool explained = false;
    for (auto *label : controller.window()->findChildren<QLabel *>())
        explained |= label->text().contains(QStringLiteral("does not exist"));
    QVERIFY(explained);
}
void ResourceTest::slowShareDoesNotBlockDismissalOrReplacement()
{
    fuse_.delay = 1500;
    fuse_.path = write(QStringLiteral("late.txt"), "late remote result");
    PreviewController controller;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(controller.show(QStringLiteral("smb://server/share/late.txt"), error));
    QVERIFY(timer.elapsed() < 100);
    QTRY_VERIFY(!fuse_.lastUrl.isEmpty());
    QTRY_VERIFY(controller.window()->isVisible());
    QVERIFY(controller.loading());
    controller.hide();
    QVERIFY(!controller.isVisible());
    QVERIFY(!controller.loading());
    const auto local = write(QStringLiteral("replacement.txt"), "current local file");
    QVERIFY(controller.show(local, error));
    QTRY_COMPARE(controller.previewKind(), QStringLiteral("text"));
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(local).toString(QUrl::FullyEncoded));
    QTest::qWait(1600);
    QCOMPARE(controller.window()->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("current local file"));
    controller.hide();
    QTest::qWait(200);
    QVERIFY(!controller.isVisible());
}
void ResourceTest::slowShareTimesOut()
{
    fuse_.delay = 2000;
    fuse_.path = directory_.path();
    ResourcePreparation preparation(nullptr, 200);
    FileResource input, result;
    QString error;
    QVERIFY(parseFileResource(QStringLiteral("smb://server/share/slow.txt"), input, error));
    bool ready = false;
    connect(&preparation, &ResourcePreparation::ready, this, [&](const FileResource &resource) {
        result = resource;
        ready = true;
    });
    preparation.start(input);
    QTRY_VERIFY_WITH_TIMEOUT(ready, 1500);
    QVERIFY(result.informationOnly);
    QVERIFY(result.metadata.value(QStringLiteral("message")).toString().contains(QStringLiteral("timed out")));
}

void ResourceTest::liveAuthenticatedSmbPreview()
{
    const QString url = qEnvironmentVariable("DZURKEL_LIVE_SMB_URL");
    if (url.isEmpty()) QSKIP("Opt in with DZURKEL_LIVE_SMB_URL and run only this test on the desktop bus.");
    FileResource parsed;
    QString error;
    QVERIFY(parseFileResource(url, parsed, error));
    QCOMPARE(parsed.url.scheme(), QStringLiteral("smb"));
    PreviewController controller;
    QVERIFY(controller.show(url, error));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), ResourcePreparation::TimeoutMs + 3000);
    const auto kind = controller.previewKind();
    QVERIFY2(kind == QStringLiteral("image") || kind == QStringLiteral("pdf") || kind == QStringLiteral("text"),
        qPrintable(QStringLiteral("Expected decoded SMB content, got %1").arg(kind)));
    QCOMPARE(controller.fileUrl(), parsed.url.toString(QUrl::FullyEncoded));
    const auto *location = controller.window()->findChild<QLabel *>(QStringLiteral("filePath"));
    QVERIFY(location);
    QCOMPARE(location->text(), parsed.url.toDisplayString());
    QVERIFY(QTest::qWaitForWindowExposed(location->window()));
    controller.hide();
    QTRY_VERIFY(!controller.isVisible());
}

QTEST_MAIN(ResourceTest)
#include "ResourceTest.moc"

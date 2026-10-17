#include "resources/ResourcePreparation.h"
#include "resources/ResourceAccessPolicy.h"
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
#include <QProcess>
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
    void experimentalSftpRequiresOptIn();
    void parsesExperimentalSftpUrls();
    void snapshotsSupportedRemoteFormats_data();
    void snapshotsSupportedRemoteFormats();
    void boundsTextAndContentCopies_data();
    void boundsTextAndContentCopies();
    void describesSpecialResourcesWithoutReadingThem_data();
    void describesSpecialResourcesWithoutReadingThem();
    void reportsAccessFailure_data();
    void reportsAccessFailure();
    void missingLocalFileIsAnAsynchronousError();
    void slowShareDoesNotBlockDismissalOrReplacement_data();
    void slowShareDoesNotBlockDismissalOrReplacement();
    void slowShareTimesOut_data();
    void slowShareTimesOut();
    void liveAuthenticatedSmbPreview();
private:
    FileResource prepare(const QString &url);
    QString write(const QString &name, const QByteArray &bytes);
    QString remoteBase_;
    QTemporaryDir directory_;
    FakeFuse fuse_;
};

void ResourceTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    if (!qEnvironmentVariableIsEmpty("DZURKEL_LIVE_SMB_URL")) return;
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse_, QDBusConnection::ExportAllSlots));
}
void ResourceTest::init()
{
    fuse_.lastUrl.clear();
    fuse_.reject = false;
    fuse_.delay = 0;
    if (qEnvironmentVariableIsEmpty("DZURKEL_LIVE_SMB_URL")) {
        QString error;
        QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(false, error), qPrintable(error));
    }
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
void ResourceTest::experimentalSftpRequiresOptIn()
{
    FileResource resource;
    QString error;
    ResourceError failure = ResourceError::None;
    const auto url = QStringLiteral("sftp://user@offline.invalid:2222/file.txt");
    QVERIFY(!ResourceAccessPolicy::experimentalSftpEnabled());
    QVERIFY(!parseFileResource(url, resource, error, &failure));
    QCOMPARE(failure, ResourceError::UnsupportedScheme);
    QVERIFY(error.contains(QStringLiteral("experimental and disabled")));
    QVERIFY(resource.url.isEmpty());
    QVERIFY(fuse_.lastUrl.isEmpty());
    QProcess cli;
    cli.setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"));
    cli.setArguments({QStringLiteral("--show"), url});
    cli.start();
    QTRY_COMPARE(cli.state(), QProcess::NotRunning);
    QCOMPARE(cli.exitStatus(), QProcess::NormalExit);
    QCOMPARE(cli.exitCode(), 1);
    QVERIFY(cli.readAllStandardError().contains("experimental and disabled"));
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(true, error));
    QVERIFY(parseFileResource(url, resource, error));
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(false, error));
    QVERIFY(!parseFileResource(url, resource, error, &failure));
    QVERIFY(parseFileResource(QStringLiteral("smb://server/share/file.txt"), resource, error));
}
void ResourceTest::parsesExperimentalSftpUrls()
{
    QString error;
    QVERIFY(ResourceAccessPolicy::setExperimentalSftpEnabled(true, error));
    FileResource resource;
    const auto url = QStringLiteral("sftp://user@[::1]:2222/folder/%D0%BF%D1%80%D0%B8%D0%BC%D0%B5%D1%80%20%23%25.txt");
    QVERIFY2(parseFileResource(url, resource, error), qPrintable(error));
    QCOMPARE(resource.url.toString(QUrl::FullyEncoded), url);
    QCOMPARE(resource.url.port(), 2222);
    QVERIFY(resource.path.endsWith(QString::fromUtf8("пример #%.txt")));
    QVERIFY(parseFileResource(QStringLiteral("sftp://server/a/../file.txt"), resource, error));
    QCOMPARE(resource.url.path(), QStringLiteral("/a/../file.txt"));
    for (const auto &invalid : {"sftp://", "sftp://server", "sftp://user:secret@server/file",
                               "sftp://server/file?query", "sftp://server/file#fragment",
                               "sftp://server:0/file", "sftp://server:65536/file", "sftp://server/file%00.txt"})
        QVERIFY2(!parseFileResource(QString::fromLatin1(invalid), resource, error), invalid);
    for (const auto &unsupported : {"nfs://server/export/file", "ftp://server/file", "webdav://server/file"})
        QVERIFY(!parseFileResource(QString::fromLatin1(unsupported), resource, error));
    QVERIFY(fuse_.lastUrl.isEmpty());
}
void ResourceTest::snapshotsSupportedRemoteFormats_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::snapshotsSupportedRemoteFormats()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
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
        const auto url = (remoteBase_ + QStringLiteral("%1")).arg(QFileInfo(fixture.first).fileName());
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
    auto snapshot = prepare((remoteBase_ + QStringLiteral("original.txt")));
    QVERIFY(!write(QStringLiteral("original.txt"), "changed source\n").isEmpty());
    const auto data = PreviewRegistry::load(snapshot, QSize(100, 100), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.text, QStringLiteral("original snapshot\n"));
}
void ResourceTest::boundsTextAndContentCopies_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::boundsTextAndContentCopies()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
    fuse_.path = write(QStringLiteral("large.txt"), QByteArray(TextPreviewProvider::ByteLimit + 1024, 'a'));
    auto resource = prepare((remoteBase_ + QStringLiteral("large.txt")));
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
    resource = prepare((remoteBase_ + QStringLiteral("huge.pdf")));
    QVERIFY(resource.informationOnly);
    QVERIFY(resource.backingPath.isEmpty());
    QVERIFY(resource.metadata.value(QStringLiteral("message")).toString().contains(QStringLiteral("64 MiB")));
    QVERIFY(QDir(resource.backingStore->path()).entryList(QDir::Files).isEmpty());
}
void ResourceTest::describesSpecialResourcesWithoutReadingThem_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::describesSpecialResourcesWithoutReadingThem()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
    const auto target = write(QStringLiteral("target.txt"), "not opened");
    const auto link = directory_.filePath(QStringLiteral("link-%1.txt").arg(QUrl(base).scheme()));
    QVERIFY(QFile::link(target, link));
    const auto fifo = directory_.filePath(QStringLiteral("pipe-%1").arg(QUrl(base).scheme()));
    QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    for (const auto &path : {directory_.path(), link, fifo}) {
        fuse_.path = path;
        const auto resource = prepare((remoteBase_ + QStringLiteral("object")));
        QVERIFY(resource.informationOnly);
        QVERIFY(resource.backingPath.isEmpty());
        QVERIFY(!resource.metadata.value(QStringLiteral("message")).toString().isEmpty());
    }
}
void ResourceTest::reportsAccessFailure_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::reportsAccessFailure()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
    fuse_.reject = true;
    const auto resource = prepare((remoteBase_ + QStringLiteral("file.txt")));
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
void ResourceTest::slowShareDoesNotBlockDismissalOrReplacement_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::slowShareDoesNotBlockDismissalOrReplacement()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
    fuse_.delay = 1500;
    fuse_.path = write(QStringLiteral("late.txt"), "late remote result");
    PreviewController controller;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(controller.show((remoteBase_ + QStringLiteral("late.txt")), error));
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
void ResourceTest::slowShareTimesOut_data()
{
    QTest::addColumn<QString>("base");
    QTest::newRow("smb") << QStringLiteral("smb://server/share/");
    QTest::newRow("experimental-sftp") << QStringLiteral("sftp://user@server:2222/fixtures/");
}
void ResourceTest::slowShareTimesOut()
{
    QFETCH(QString, base);
    remoteBase_ = base;
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(base.startsWith(QStringLiteral("sftp:")), policyError), qPrintable(policyError));
    fuse_.delay = 2000;
    fuse_.path = directory_.path();
    ResourcePreparation preparation(nullptr, 200);
    FileResource input, result;
    QString error;
    QVERIFY(parseFileResource((remoteBase_ + QStringLiteral("slow.txt")), input, error));
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

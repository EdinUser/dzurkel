#include "resources/media/StreamingMediaSource.h"
#include <QElapsedTimer>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtConcurrentRun>
#include <sys/stat.h>

using namespace dzurkel;
class MediaFakeFuse : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
    QString lastUrl;
    int delayMs = 0;
public slots:
    QString mountUrl(const QString &url) {
        lastUrl = url;
        if (delayMs) {
            setDelayedReply(true);
            const auto request = message();
            const auto bus = connection();
            const auto result = path;
            QTimer::singleShot(delayMs, this, [bus, request, result] {
                bus.send(request.createReply(QVariantList{result}));
            });
        }
        return path;
    }
};
class MediaSourceTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup();
    void readsArbitraryRangesWithoutCopyingWholeSource();
    void cancellationWakesBlockedReadWithoutFreezingEventLoop();
    void stalledReadHasDeadline();
    void rejectsMissingAndSpecialFiles_data();
    void rejectsMissingAndSpecialFiles();
    void keepsOpenedIdentityAndDetectsTruncation();
    void repeatedReplacementReapsHelpers();
    void limitsHelpersDuringRapidReplacement();
    void resolvesSmbThroughExistingDesktopAdapter();
    void cancelsWhileRemoteResolutionIsPending();
    void emptyFileHasTrueEof();
    void missingHelperReportsFailure();
};
void MediaSourceTest::cleanup()
{
    QTRY_COMPARE_WITH_TIMEOUT(StreamingMediaSource::activeHelpers(), 0, 3000);
}
void MediaSourceTest::readsArbitraryRangesWithoutCopyingWholeSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const qint64 size = 512 * 1024 * 1024;
    QVERIFY(file.resize(size)); // Sparse fixture; no giant allocation or copy.
    QCOMPARE(file.write("begin", 5), 5);
    QVERIFY(file.seek(size - 4));
    QCOMPARE(file.write("tail", 4), 4);
    file.close();
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    QSignalSpy failed(&source, &StreamingMediaSource::failed);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), path);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 3000);
    QVERIFY2(failed.isEmpty(), qPrintable(source.errorString()));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(source.size(), size);
    QCOMPARE(source.bytesTransferred(), 0);
    auto future = QtConcurrent::run([&] {
        QByteArray result = source.read(5);
        if (!source.seek(size - 4)) return QByteArray();
        result += source.read(4);
        if (!source.atEnd() || !source.seek(0)) return QByteArray();
        return result + source.read(5);
    });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 3000);
    QCOMPARE(future.result(), QByteArray("begintailbegin"));
    QCOMPARE(source.bytesTransferred(), 14);
    QCOMPARE(source.readRequests(), 3);
    QCOMPARE(source.ownerThreadReads(), 0);
    QVERIFY(source.peakReadBytes() <= StreamingMediaSource::ChunkBytes);
    source.cancel();
}
void MediaSourceTest::cancellationWakesBlockedReadWithoutFreezingEventLoop()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("source")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("data", 4), 4);
    file.close();
    StreamingMediaSource source(nullptr, 10000);
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), file.fileName(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    auto future = QtConcurrent::run([&] { char bytes[4]; return source.read(bytes, 4); });
    QTRY_COMPARE_WITH_TIMEOUT(source.readRequests(), 1, 1000);
    int heartbeats = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start(5);
    QTest::qWait(60);
    QVERIFY(heartbeats >= 3);
    QVERIFY(!future.isFinished());
    QElapsedTimer elapsed;
    elapsed.start();
    source.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 500);
    QCOMPARE(future.result(), -1);
    QVERIFY(elapsed.elapsed() < 500);
}
void MediaSourceTest::stalledReadHasDeadline()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("source")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("data", 4), 4);
    file.close();
    StreamingMediaSource source(nullptr, 150);
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), file.fileName(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    QElapsedTimer elapsed;
    elapsed.start();
    auto future = QtConcurrent::run([&] { char bytes[4]; return source.read(bytes, 4); });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    QCOMPARE(future.result(), -1);
    QVERIFY(elapsed.elapsed() >= 100);
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(source.errorString().contains(QStringLiteral("timed out")));
    source.cancel();
}
void MediaSourceTest::rejectsMissingAndSpecialFiles_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto &kind : {"missing", "directory", "fifo", "symlink"})
        QTest::newRow(kind) << QString::fromLatin1(kind);
}
void MediaSourceTest::rejectsMissingAndSpecialFiles()
{
    QFETCH(QString, kind);
    QTemporaryDir directory;
    const QString path = kind == QStringLiteral("directory") ? directory.path() : directory.filePath(kind);
    if (kind == QStringLiteral("fifo")) QCOMPARE(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);
    if (kind == QStringLiteral("symlink")) QVERIFY(QFile::link(directory.path(), path));
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    QSignalSpy failed(&source, &StreamingMediaSource::failed);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), path);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!source.isOpen());
}
void MediaSourceTest::keepsOpenedIdentityAndDetectsTruncation()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("source"));
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original", 8), 8);
    original.close();
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), path);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    QVERIFY(original.rename(directory.filePath(QStringLiteral("old"))));
    QFile replacement(path);
    QVERIFY(replacement.open(QIODevice::WriteOnly));
    QCOMPARE(replacement.write("replaced", 8), 8);
    replacement.close();
    auto read = QtConcurrent::run([&] { return source.read(8); });
    QTRY_VERIFY_WITH_TIMEOUT(read.isFinished(), 1000);
    QCOMPARE(read.result(), QByteArray("original"));
    QVERIFY(original.open(QIODevice::WriteOnly)); // Truncate the opened inode.
    original.close();
    auto truncated = QtConcurrent::run([&] {
        if (!source.seek(0)) return qint64(0);
        char bytes[8];
        return source.read(bytes, 8);
    });
    QTRY_VERIFY_WITH_TIMEOUT(truncated.isFinished(), 1000);
    QCOMPARE(truncated.result(), -1);
    QVERIFY(source.errorString().contains(QStringLiteral("truncated")));
    source.cancel();
}
void MediaSourceTest::repeatedReplacementReapsHelpers()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("source")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("data", 4), 4);
    file.close();
    const auto descriptors = [] { return QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(); };
    const auto before = descriptors();
    for (int i = 0; i < 20; ++i) {
        {
            StreamingMediaSource source;
            QSignalSpy ready(&source, &StreamingMediaSource::ready);
            source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), file.fileName());
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
            auto future = QtConcurrent::run([&] { return source.read(4); });
            QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
            QCOMPARE(future.result(), QByteArray("data"));
            source.cancel();
        }
        QTRY_COMPARE_WITH_TIMEOUT(StreamingMediaSource::activeHelpers(), 0, 3000);
    }
    QVERIFY(descriptors() <= before + 1);
}
void MediaSourceTest::limitsHelpersDuringRapidReplacement()
{
    StreamingMediaSource first, second, third;
    first.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), QStringLiteral("/missing"));
    second.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), QStringLiteral("/missing"));
    QSignalSpy failed(&third, &StreamingMediaSource::failed);
    third.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), QStringLiteral("/missing"));
    QCOMPARE(StreamingMediaSource::activeHelpers(), 2);
    QCOMPARE(failed.size(), 1);
    first.cancel();
    second.cancel();
}
void MediaSourceTest::resolvesSmbThroughExistingDesktopAdapter()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("remote.mp3")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("remote", 6), 6);
    file.close();
    MediaFakeFuse fuse;
    fuse.path = file.fileName();
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse, QDBusConnection::ExportAllSlots));
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    QSignalSpy failed(&source, &StreamingMediaSource::failed);
    const auto url = QStringLiteral("smb://test.invalid/share/remote.mp3");
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), url);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 3000);
    QVERIFY2(failed.isEmpty(), qPrintable(source.errorString()));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(fuse.lastUrl, url);
    auto future = QtConcurrent::run([&] { return source.read(6); });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    QCOMPARE(future.result(), QByteArray("remote"));
    source.cancel();
    bus.unregisterObject(QStringLiteral("/org/kde/KIOFuse"));
    bus.unregisterService(QStringLiteral("org.kde.KIOFuse"));
}
void MediaSourceTest::cancelsWhileRemoteResolutionIsPending()
{
    MediaFakeFuse fuse;
    fuse.path = QStringLiteral("/missing");
    fuse.delayMs = 5000;
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KIOFuse")));
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KIOFuse"), &fuse, QDBusConnection::ExportAllSlots));
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), QStringLiteral("smb://test.invalid/share/pending.mp3"));
    QTRY_VERIFY_WITH_TIMEOUT(!fuse.lastUrl.isEmpty(), 1000);
    QElapsedTimer elapsed;
    elapsed.start();
    source.cancel();
    QVERIFY(elapsed.elapsed() < 100);
    QTRY_COMPARE_WITH_TIMEOUT(StreamingMediaSource::activeHelpers(), 0, 1000);
    QCOMPARE(ready.size(), 0);
    bus.unregisterObject(QStringLiteral("/org/kde/KIOFuse"));
    bus.unregisterService(QStringLiteral("org.kde.KIOFuse"));
}
void MediaSourceTest::emptyFileHasTrueEof()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("empty")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    StreamingMediaSource source;
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), file.fileName());
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    auto future = QtConcurrent::run([&] { char byte; return source.atEnd() && source.read(&byte, 1) == 0; });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    QVERIFY(future.result());
    QCOMPARE(source.readRequests(), 0);
    source.cancel();
}
void MediaSourceTest::missingHelperReportsFailure()
{
    StreamingMediaSource source;
    QSignalSpy failed(&source, &StreamingMediaSource::failed);
    source.start(QStringLiteral("/does-not-exist-dzurkel-helper"), QStringLiteral("/missing"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
    QVERIFY(!source.isOpen());
}
QTEST_GUILESS_MAIN(MediaSourceTest)
#include "MediaSourceTest.moc"

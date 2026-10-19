#include "controller/PreviewController.h"
#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/media/MediaBackend.h"
#include "resources/ResourcePreparation.h"
#include <QAudioOutput>
#include <QAudioDevice>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QMimeDatabase>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <algorithm>
#include <cmath>

using namespace dzurkel;
class VideoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void selectsVideoWithoutSnapshotOrInstalledCodecs();
    void sharesMuteAndVolumeAcrossAudioAndVideo();
    void silentVideoControlsSeekingAndDismissal();
    void audioOnlyMp4UsesAudioPresentation();
    void pausedVideoAndEndSeekDoNotRemainBuffering();
    void mp4IndexAtBeginningAndEnd_data();
    void mp4IndexAtBeginningAndEnd();
    void webmAndMkv();
    void replacementStopsVideoAndRejectsObsoleteFailure();
    void invalidVideoFallsBackWithoutPackageGuess();
    void localUserSample();
    void remoteUserSample();
private:
    QString write(const QString &name, const QByteArray &bytes);
    bool fixtures();
    void exercise(const QString &path);
    QTemporaryDir directory_;
    bool generated_ = false;
};
QString VideoTest::write(const QString &name, const QByteArray &bytes)
{
    QFile file(directory_.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}
void VideoTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
}
bool VideoTest::fixtures()
{
    if (generated_) return true;
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) return false;
    const auto run = [&](const QStringList &arguments) {
        QProcess process;
        process.start(ffmpeg, QStringList{"-hide_banner", "-loglevel", "error", "-y"} + arguments);
        if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); return false; }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    const QStringList input{"-f", "lavfi", "-i", "testsrc2=size=160x90:rate=12:duration=30", "-an", "-c:v", "mpeg4"};
    if (!run(input + QStringList{directory_.filePath("tail.mp4")})) return false;
    if (!run({"-i", directory_.filePath("tail.mp4"), "-c", "copy", "-movflags", "+faststart", directory_.filePath("front.mp4")})) return false;
    if (!run({"-i", directory_.filePath("tail.mp4"), "-c", "copy", directory_.filePath("video.mkv")})) return false;
    if (!run({"-f", "lavfi", "-i", "testsrc2=size=160x90:rate=12:duration=30", "-an", "-c:v", "libvpx", "-deadline", "realtime", directory_.filePath("video.webm")})) return false;
    if (!run({"-f", "lavfi", "-i", "sine=frequency=440:duration=30", "-c:a", "aac", directory_.filePath("audio.mp4")})) return false;
    generated_ = true;
    return true;
}
void VideoTest::cleanup() { QTRY_COMPARE_WITH_TIMEOUT(activeMediaHelpers(), 0, 3000); }
void VideoTest::selectsVideoWithoutSnapshotOrInstalledCodecs()
{
    const auto capability = PreviewLibrary::capability(QStringLiteral("video"));
    QVERIFY(capability);
    QCOMPARE(capability->access, SourceAccess::Streaming);
    QCOMPARE(capability->sourceLimit, 0);
    const auto path = write(QStringLiteral("large.mkv"), QByteArray::fromHex("1a45dfa3"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(512 * 1024 * 1024));
    const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/octet-stream"));
    for (const auto &suffix : {"mp4", "m4v", "mkv", "webm", "avi", "mov", "mpg", "mpeg", "ogv", "wmv"}) {
        QCOMPARE(PreviewLibrary::select(file, mime, QStringLiteral("sample.") + QString::fromLatin1(suffix)), QStringLiteral("video"));
        QCOMPARE(file.pos(), 0);
    }
    file.close();
    FileResource input;
    QString error;
    QVERIFY(parseFileResource(path, input, error));
    ResourcePreparation preparation;
    QSignalSpy ready(&preparation, &ResourcePreparation::ready);
    preparation.start(input);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    const auto resource = ready.first().first().value<FileResource>();
    QVERIFY(resource.streaming);
    QVERIFY(resource.backingPath.isEmpty());
    QVERIFY(!resource.backingStore);
    const auto data = PreviewRegistry::load(resource, QSize(640, 480), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Video);
    QCOMPARE(data.size, qint64(512 * 1024 * 1024));
    // Strong signatures continue to win over misleading video suffixes.
    const auto pdf = write(QStringLiteral("document.mp4"), "%PDF-1.7\nfixture");
    QFile document(pdf);
    QVERIFY(document.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(document, mime, pdf), QStringLiteral("pdf"));
}
void VideoTest::exercise(const QString &path)
{
    const QFileInfo before(path);
    const auto size = before.size();
    const auto modified = before.lastModified();
    PreviewController controller;
    connect(&controller, &PreviewController::stateChanged, &controller, [&] {
        for (const auto &name : {"audioPreview", "videoPreview"})
            if (auto *media = controller.window()->findChild<QWidget *>(QString::fromLatin1(name)))
                qobject_cast<MediaControls *>(media)->setMuted(true);
    });
    QString error;
    QVERIFY2(controller.show(path, error, nullptr, {}, false, true), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QVideoWidget *>(), 10000);
    auto *surface = controller.window()->findChild<QVideoWidget *>();
    QCOMPARE(surface->aspectRatioMode(), Qt::KeepAspectRatio);
    qint64 frames = 0, position = -1;
    QObject observer;
    connect(surface->videoSink(), &QVideoSink::videoFrameChanged, &observer, [&](const QVideoFrame &frame) {
        if (frame.isValid()) { ++frames; position = frame.startTime() / 1000; }
    });
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 2, 15000);
    QCOMPARE(controller.previewKind(), QStringLiteral("video"));
    QPointer<QMediaPlayer> player = controller.window()->findChild<QMediaPlayer *>();
    QVERIFY(player);
    QVERIFY(controller.window()->findChild<QAudioOutput *>()->isMuted());
    auto *seek = controller.window()->findChild<QSlider *>(QStringLiteral("videoSeek"));
    auto *play = controller.window()->findChild<QPushButton *>(QStringLiteral("videoPlay"));
    QVERIFY(seek && seek->isEnabled() && play);
    const auto target = std::min<qint64>(60000, player->duration() / 2);
    seek->setValue(qRound(target * 100000.0 / player->duration()));
    QCOMPARE(controller.window()->findChild<QLabel *>(QStringLiteral("videoStatus"))->text(), QStringLiteral("Seeking…"));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(position - target) < 3000, 15000);
    seek->setValue(qRound(2000 * 100000.0 / player->duration()));
    QTRY_VERIFY_WITH_TIMEOUT(position >= 0 && position < 5000, 15000);
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
    QSignalSpy navigation(&controller, &PreviewController::externalNavigationRequested);
    QTest::keyClick(seek, Qt::Key_Right);
    QCOMPARE(navigation.size(), 0);
    QTest::keyClick(play, Qt::Key_Right);
    QCOMPARE(navigation.size(), 1);
    const auto nativeScreenshot = qEnvironmentVariable("DZURKEL_VIDEO_NATIVE_SCREENSHOT");
    if (!nativeScreenshot.isEmpty() && !QFileInfo::exists(nativeScreenshot)) {
        // QWidget::grab omits the RHI video surface. This opt-in KDE capture
        // checks the actual compositor output, only while our window is active.
        QTRY_VERIFY_WITH_TIMEOUT(surface->window()->isActiveWindow(), 3000);
        QTest::qWait(300);
        player->pause();
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::qWait(100);
        QProcess capture;
        capture.start(QStringLiteral("spectacle"), {"--background", "--activewindow", "--nonotify", "--output", nativeScreenshot});
        QVERIFY(capture.waitForFinished(10000));
        QCOMPARE(capture.exitCode(), 0);
        QVERIFY(QFileInfo::exists(nativeScreenshot));
    }
    const auto screenshot = qEnvironmentVariable("DZURKEL_VIDEO_SCREENSHOT");
    if (!screenshot.isEmpty() && !QFileInfo::exists(screenshot))
        QVERIFY(surface->window()->grab().save(screenshot));
    QElapsedTimer stopping;
    stopping.start();
    QTest::keyClick(play, Qt::Key_Space);
    QVERIFY(stopping.elapsed() < 500);
    QVERIFY(!controller.isVisible());
    QVERIFY(player.isNull());
    QCOMPARE(QFileInfo(path).size(), size);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
}
void VideoTest::sharesMuteAndVolumeAcrossAudioAndVideo()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("Audio playback requires an output device.");
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    PreviewController controller;
    QString error;
    const auto open = [&](const QString &filename, const QString &kind) {
        if (!controller.show(directory_.filePath(filename), error)) return false;
        return QTest::qWaitFor([&] {
            auto *play = controller.window()->findChild<QPushButton *>(kind + QStringLiteral("Play"));
            return play && play->isEnabled();
        }, 5000);
    };
    // The first source has no audio track, so checking the audible default
    // cannot produce sound. Later unmute checks use zero volume.
    QVERIFY(open(QStringLiteral("tail.mp4"), QStringLiteral("video")));
    auto *output = controller.window()->findChild<QAudioOutput *>();
    QVERIFY(output && !output->isMuted());
    auto *volume = controller.window()->findChild<QSlider *>(QStringLiteral("videoVolume"));
    volume->setValue(23);
    QTest::mouseClick(controller.window()->findChild<QPushButton *>(QStringLiteral("videoMute")), Qt::LeftButton);
    QVERIFY(output->isMuted());
    controller.hide();

    QVERIFY(open(QStringLiteral("audio.mp4"), QStringLiteral("audio")));
    output = controller.window()->findChild<QAudioOutput *>();
    QVERIFY(output && output->isMuted());
    volume = controller.window()->findChild<QSlider *>(QStringLiteral("audioVolume"));
    QCOMPARE(volume->value(), 23);
    QVERIFY(qAbs(output->volume() - 0.23f) < 0.001f);
    volume->setValue(0);
    QTest::mouseClick(controller.window()->findChild<QPushButton *>(QStringLiteral("audioMute")), Qt::LeftButton);
    QVERIFY(!output->isMuted());
    controller.hide();

    QVERIFY(open(QStringLiteral("tail.mp4"), QStringLiteral("video")));
    output = controller.window()->findChild<QAudioOutput *>();
    QVERIFY(output && !output->isMuted());
    QCOMPARE(controller.window()->findChild<QSlider *>(QStringLiteral("videoVolume"))->value(), 0);
    QCOMPARE(output->volume(), 0.0f);
    // Leave subsequent personal-file tests muted as well.
    QTest::mouseClick(controller.window()->findChild<QPushButton *>(QStringLiteral("videoMute")), Qt::LeftButton);
    QVERIFY(output->isMuted());
    controller.hide();
}
void VideoTest::silentVideoControlsSeekingAndDismissal()
{
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg with MPEG4, AAC and VP8 encoders.");
    exercise(directory_.filePath("tail.mp4"));
}
void VideoTest::audioOnlyMp4UsesAudioPresentation()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("Audio-only playback requires an output device.");
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    PreviewController controller;
    connect(&controller, &PreviewController::stateChanged, &controller, [&] {
        if (auto *widget = controller.window()->findChild<QWidget *>(QStringLiteral("audioPreview")))
            qobject_cast<MediaControls *>(widget)->setMuted(true);
    });
    QString error;
    QVERIFY(controller.show(directory_.filePath("audio.mp4"), error));
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("audio"), 10000);
    QVERIFY(controller.window()->findChild<QWidget *>(QStringLiteral("audioPreview")));
    QVERIFY(controller.window()->findChild<QAudioOutput *>()->isMuted());
    controller.hide();
}
void VideoTest::pausedVideoAndEndSeekDoNotRemainBuffering()
{
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(directory_.filePath("tail.mp4"), error));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QSlider *>(QStringLiteral("videoSeek"))
        && controller.window()->findChild<QSlider *>(QStringLiteral("videoSeek"))->isEnabled(), 5000);
    auto *play = controller.window()->findChild<QPushButton *>(QStringLiteral("videoPlay"));
    auto *seek = controller.window()->findChild<QSlider *>(QStringLiteral("videoSeek"));
    auto *status = controller.window()->findChild<QLabel *>(QStringLiteral("videoStatus"));
    QTest::mouseClick(play, Qt::LeftButton);
    QTest::qWait(1700); // Longer than the stalled-frame feedback threshold.
    QCOMPARE(status->text(), QStringLiteral("Paused"));
    seek->setValue(seek->maximum());
    QTRY_VERIFY_WITH_TIMEOUT(status->text() != QStringLiteral("Seeking…"), 5000);
    QVERIFY(status->text() == QStringLiteral("Paused") || status->text() == QStringLiteral("Finished"));
    QTest::keyClick(play, Qt::Key_Escape);
    QVERIFY(!controller.isVisible());
    QVERIFY(!controller.window()->findChild<QMediaPlayer *>());
}
void VideoTest::mp4IndexAtBeginningAndEnd_data()
{
    QTest::addColumn<QString>("filename");
    QTest::newRow("front-index") << QStringLiteral("front.mp4");
    QTest::newRow("tail-index") << QStringLiteral("tail.mp4");
}
void VideoTest::mp4IndexAtBeginningAndEnd()
{
    QFETCH(QString, filename);
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    exercise(directory_.filePath(filename));
}
void VideoTest::webmAndMkv()
{
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    exercise(directory_.filePath("video.webm"));
    exercise(directory_.filePath("video.mkv"));
}
void VideoTest::replacementStopsVideoAndRejectsObsoleteFailure()
{
    if (!fixtures()) QSKIP("Video fixtures require ffmpeg.");
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(directory_.filePath("tail.mp4"), error));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>(), 5000);
    QPointer<QMediaPlayer> old = controller.window()->findChild<QMediaPlayer *>();
    auto *widget = controller.window()->findChild<QWidget *>(QStringLiteral("videoPreview"));
    QVERIFY(widget);
    QVERIFY(QMetaObject::invokeMethod(widget, "failed", Qt::QueuedConnection,
        Q_ARG(QString, QStringLiteral("Obsolete failure")), Q_ARG(QString, QString())));
    QVERIFY(controller.show(write(QStringLiteral("current.txt"), "current content"), error));
    QVERIFY(old.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("text"), 5000);
    QTest::qWait(200);
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    QVERIFY(!controller.window()->findChild<QMediaPlayer *>());
    controller.hide();
}
void VideoTest::invalidVideoFallsBackWithoutPackageGuess()
{
    PreviewController controller;
    QString error;
    QVERIFY(controller.show(write(QStringLiteral("broken.mkv"), "not a video"), error));
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("information"), 10000);
    QVERIFY(!controller.window()->findChild<QMediaPlayer *>());
    QString text;
    for (auto *label : controller.window()->findChildren<QLabel *>()) text += label->text();
    QVERIFY(text.contains(QStringLiteral("couldn't be previewed")));
    QVERIFY(!text.contains(QStringLiteral("dnf install")));
    controller.hide();
}
void VideoTest::localUserSample()
{
    const auto path = qEnvironmentVariable("DZURKEL_TEST_VIDEO_LOCAL");
    if (path.isEmpty()) QSKIP("Set DZURKEL_TEST_VIDEO_LOCAL for read-only personal-file coverage.");
    exercise(path);
}
void VideoTest::remoteUserSample()
{
    const auto path = qEnvironmentVariable("DZURKEL_TEST_VIDEO_REMOTE");
    if (path.isEmpty()) QSKIP("Set DZURKEL_TEST_VIDEO_REMOTE for read-only share coverage.");
    exercise(path);
}
QTEST_MAIN(VideoTest)
#include "VideoTest.moc"

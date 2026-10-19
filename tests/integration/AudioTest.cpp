#include "controller/PreviewController.h"
#include "preview/PreviewLibrary.h"
#include "preview/PreviewRegistry.h"
#include "preview/media/MediaBackend.h"
#include "resources/ResourcePreparation.h"
#include "resources/media/StreamingMediaSource.h"
#include <QAudioOutput>
#include <QAudioDevice>
#include <QDataStream>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace dzurkel;
class AudioTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void selectsAgreedFormatsWithoutRequiringTheirDecoders();
    void preparesLargeAudioWithoutSnapshot();
    void playbackControlsAndDismissalReleaseSource();
    void replacementStopsAudioBeforeNewPreviewLoads();
    void brokenMediaShowsInformationWithoutPackageGuess();
    void obsoleteMediaFailureCannotReplaceNewContent();
    void truncationDuringPlaybackStopsAndShowsInformation();
    void realMusicFiles();
    void generatedAudioFormats();
    void movingVideoDoesNotPlayAsAudio();
private:
    QString write(const QString &name, const QByteArray &bytes);
    void mute(PreviewController &controller);
    QTemporaryDir directory_;
    QString wave_;
};
QString AudioTest::write(const QString &name, const QByteArray &bytes)
{
    QFile file(directory_.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}
void AudioTest::mute(PreviewController &controller)
{
    // Commit signals run before the widget's queued startup, so test playback
    // cannot become audible, including when checking the user's real files.
    connect(&controller, &PreviewController::stateChanged, &controller, [&controller] {
        for (const auto &name : {"audioPreview", "videoPreview"})
            if (auto *media = controller.window()->findChild<QWidget *>(QString::fromLatin1(name)))
                qobject_cast<MediaControls *>(media)->setMuted(true);
    });
}
void AudioTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    wave_ = directory_.filePath(QStringLiteral("silence.wav"));
    QFile file(wave_);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 90 * 48000 * 2 * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(bytes + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
           << quint32(48000 * 4) << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    QVERIFY(file.resize(bytes + 44));
}
void AudioTest::cleanup()
{
    QTRY_COMPARE_WITH_TIMEOUT(activeMediaHelpers(), 0, 3000);
}
void AudioTest::selectsAgreedFormatsWithoutRequiringTheirDecoders()
{
    const auto capability = PreviewLibrary::capability(QStringLiteral("audio"));
    QVERIFY(capability);
    QCOMPARE(capability->access, SourceAccess::Streaming);
    QCOMPARE(capability->sourceLimit, 0);
    const auto invalid = write(QStringLiteral("wrong-data.bin"), "this is not audio");
    QFile file(invalid);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/octet-stream"));
    for (const auto &suffix : {"mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac"}) {
        QCOMPARE(PreviewLibrary::select(file, mime, QStringLiteral("sample.") + QString::fromLatin1(suffix)), QStringLiteral("audio"));
        QCOMPARE(file.pos(), 0);
    }
    // Strong document/image signatures retain priority over an audio suffix.
    const auto pdf = write(QStringLiteral("document.mp3"), "%PDF-1.7\ninvalid fixture");
    QFile document(pdf);
    QVERIFY(document.open(QIODevice::ReadOnly));
    QCOMPARE(PreviewLibrary::select(document, mime, pdf), QStringLiteral("pdf"));
}
void AudioTest::preparesLargeAudioWithoutSnapshot()
{
    const auto path = write(QStringLiteral("large.flac"), "fLaC");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(512 * 1024 * 1024));
    file.close();
    FileResource input;
    QString error;
    QVERIFY(parseFileResource(path, input, error));
    ResourcePreparation preparation;
    QSignalSpy ready(&preparation, &ResourcePreparation::ready);
    preparation.start(input);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    const auto resource = ready.first().first().value<FileResource>();
    QVERIFY2(!resource.informationOnly, qPrintable(resource.metadata.value(QStringLiteral("message")).toString()));
    QVERIFY(resource.streaming);
    QVERIFY(resource.backingPath.isEmpty());
    QVERIFY(!resource.backingStore);
    auto data = PreviewRegistry::load(resource, QSize(640, 480), std::make_shared<std::atomic_bool>(false));
    QCOMPARE(data.kind, PreviewKind::Audio);
    QCOMPARE(data.size, qint64(512 * 1024 * 1024));
}
void AudioTest::playbackControlsAndDismissalReleaseSource()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device; requires a desktop audio session.");
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(wave_, error, nullptr, {}, false, true));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QWidget *>(QStringLiteral("audioPreview")), 5000);
    auto *audio = controller.window()->findChild<QWidget *>(QStringLiteral("audioPreview"));
    QTRY_VERIFY_WITH_TIMEOUT(audio->findChild<QMediaPlayer *>(), 5000);
    QPointer<QMediaPlayer> player = audio->findChild<QMediaPlayer *>();
    QTRY_VERIFY_WITH_TIMEOUT(player && player->position() > 0, 5000);
    QCOMPARE(controller.previewKind(), QStringLiteral("audio"));
    QVERIFY(audio->findChild<QAudioOutput *>()->isMuted());
    auto *play = audio->findChild<QPushButton *>(QStringLiteral("audioPlay"));
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
    auto *seek = audio->findChild<QSlider *>(QStringLiteral("audioSeek"));
    QVERIFY(seek->isEnabled());
    QTest::mouseClick(seek, Qt::LeftButton, Qt::NoModifier, QPoint(seek->width() * 3 / 4, seek->height() / 2));
    QTRY_VERIFY(player->position() > 60000 && player->position() < 75000);
    QCOMPARE(controller.window()->focusWidget(), seek);
    seek->setValue(1000);
    QTRY_VERIFY(player->position() < 5000);
    QSignalSpy navigation(&controller, &PreviewController::externalNavigationRequested);
    QTest::keyClick(seek, Qt::Key_Right);
    QCOMPARE(navigation.size(), 0);
    auto *volume = audio->findChild<QSlider *>(QStringLiteral("audioVolume"));
    QTest::mouseClick(volume, Qt::LeftButton, Qt::NoModifier, QPoint(volume->width() / 4, volume->height() / 2));
    QVERIFY(volume->value() > 10 && volume->value() < 45);
    QCOMPARE(controller.window()->focusWidget(), volume);
    const int previousVolume = volume->value();
    QTest::keyClick(volume, Qt::Key_Right);
    QCOMPARE(volume->value(), previousVolume + 1);
    QCOMPARE(navigation.size(), 0);
    QTest::keyClick(play, Qt::Key_Right);
    QCOMPARE(navigation.size(), 1);
    for (auto *label : audio->findChildren<QLabel *>()) QCOMPARE(label->textFormat(), Qt::PlainText);
    QTest::keyClick(play, Qt::Key_Space);
    QVERIFY(!controller.isVisible());
    QVERIFY(player.isNull()); // Stop is synchronous, widget deletion is deferred.
}
void AudioTest::replacementStopsAudioBeforeNewPreviewLoads()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device.");
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(wave_, error, nullptr, {}, false, true));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>(), 5000);
    QPointer<QMediaPlayer> old = controller.window()->findChild<QMediaPlayer *>();
    QTRY_VERIFY_WITH_TIMEOUT(old && old->position() > 0, 5000);
    const auto text = write(QStringLiteral("next.txt"), "next preview");
    QVERIFY(controller.show(text, error, nullptr, {}, false, true));
    QVERIFY(old.isNull());
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("text"), 5000);
    controller.hide();
}
void AudioTest::brokenMediaShowsInformationWithoutPackageGuess()
{
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(write(QStringLiteral("broken.mp3"), "not an audio stream"), error, nullptr, {}, false, true));
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("information"), 10000);
    QString text;
    for (auto *label : controller.window()->findChildren<QLabel *>()) text += label->text();
    QVERIFY2(text.contains(QStringLiteral("couldn't be previewed")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("unsupported")));
    QVERIFY(!text.contains(QStringLiteral("dnf install")));
    QVERIFY(!text.contains(QStringLiteral("apt install")));
    QVERIFY(!controller.window()->findChild<QMediaPlayer *>());
    controller.hide();
}
void AudioTest::obsoleteMediaFailureCannotReplaceNewContent()
{
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(write(QStringLiteral("obsolete.mp3"), "invalid stream"), error, nullptr, {}, false, true));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
    const auto path = write(QStringLiteral("current.txt"), "current content");
    QVERIFY(controller.show(path, error, nullptr, {}, false, true));
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("text"), 5000);
    QTest::qWait(200);
    QCOMPARE(controller.previewKind(), QStringLiteral("text"));
    QVERIFY(!controller.window()->findChild<QMediaPlayer *>());
    controller.hide();
}
void AudioTest::truncationDuringPlaybackStopsAndShowsInformation()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device.");
    const auto path = directory_.filePath(QStringLiteral("truncated.wav"));
    QVERIFY(QFile::copy(wave_, path));
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(path, error, nullptr, {}, false, true));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>(), 5000);
    QPointer<QMediaPlayer> player = controller.window()->findChild<QMediaPlayer *>();
    QTRY_VERIFY_WITH_TIMEOUT(player && player->position() > 0, 5000);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.close();
    player->setPosition(60000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("information"), 10000);
    QVERIFY(player.isNull());
    QString text;
    for (auto *label : controller.window()->findChildren<QLabel *>()) text += label->text();
    QVERIFY2(text.contains(QStringLiteral("truncat"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("read"), Qt::CaseInsensitive), qPrintable(text));
    controller.hide();
}
void AudioTest::realMusicFiles()
{
    const auto root = qEnvironmentVariable("DZURKEL_TEST_MUSIC_ROOT");
    if (root.isEmpty()) QSKIP("Set DZURKEL_TEST_MUSIC_ROOT for read-only real music checks.");
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device.");
    QMap<QString, QString> files;
    QDirIterator iterator(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (QStringList{QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("m4a"),
            QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("wav")}.contains(suffix) && !files.contains(suffix))
            files.insert(suffix, path);
        if (suffix == QStringLiteral("mp3") && path.toLatin1() != path.toUtf8() && !files.contains(QStringLiteral("unicode")))
            files.insert(QStringLiteral("unicode"), path);
    }
    QVERIFY(!files.isEmpty());
    PreviewController controller;
    mute(controller);
    QString error;
    for (const auto &path : files) {
        const QFileInfo before(path);
        const auto size = before.size();
        const auto modified = before.lastModified();
        QVERIFY(controller.show(path, error, nullptr, {}, false, true));
        QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>(), 5000);
        QPointer<QMediaPlayer> player = controller.window()->findChild<QMediaPlayer *>();
        QTRY_VERIFY_WITH_TIMEOUT(player && player->position() > 0, 10000);
        QCOMPARE(controller.previewKind(), QStringLiteral("audio"));
        QVERIFY(controller.window()->findChild<QAudioOutput *>()->isMuted());
        QVERIFY(player->isSeekable());
        player->setPosition(std::min<qint64>(30000, player->duration() / 2));
        QTest::qWait(150);
        QCOMPARE(player->error(), QMediaPlayer::NoError);
        qInfo() << "Read-only playback/seek passed:" << path;
        const auto screenshot = qEnvironmentVariable("DZURKEL_AUDIO_SCREENSHOT");
        auto *audio = controller.window()->findChild<QWidget *>(QStringLiteral("audioPreview"));
        if (!screenshot.isEmpty() && !QFileInfo::exists(screenshot))
            QVERIFY(audio->window()->grab().save(screenshot));
        controller.hide();
        QTRY_VERIFY(!controller.window()->findChild<QMediaPlayer *>());
        QCOMPARE(QFileInfo(path).size(), size);
        QCOMPARE(QFileInfo(path).lastModified(), modified);
        QTRY_COMPARE_WITH_TIMEOUT(activeMediaHelpers(), 0, 3000);
    }
}
void AudioTest::generatedAudioFormats()
{
    const auto root = qEnvironmentVariable("DZURKEL_TEST_AUDIO_FIXTURES");
    if (root.isEmpty()) QSKIP("Set DZURKEL_TEST_AUDIO_FIXTURES to the generated media matrix directory.");
    if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device.");
    PreviewController controller;
    mute(controller);
    QString error;
    for (const auto &name : {"mp3.mp3", "flac.flac", "vorbis.ogg", "vorbis-oga.oga", "opus.opus",
                             "wav.wav", "m4a-aac.m4a", "m4a-alac.m4a", "aac.aac", "mp4-audio.mp4"}) {
        const auto path = QDir(root).filePath(QString::fromLatin1(name));
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        QVERIFY(controller.show(path, error, nullptr, {}, false, true));
        QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>(), 5000);
        QPointer<QMediaPlayer> player = controller.window()->findChild<QMediaPlayer *>();
        QTRY_VERIFY_WITH_TIMEOUT(player && player->position() > 0, 5000);
        QCOMPARE(controller.previewKind(), QStringLiteral("audio"));
        QVERIFY(player->isSeekable());
        player->setPosition(30000);
        QTRY_VERIFY_WITH_TIMEOUT(player && player->position() > 30100, 3000);
        QCOMPARE(player->error(), QMediaPlayer::NoError);
        qInfo() << "Integrated format playback/seek passed:" << name;
        controller.hide();
        QTRY_COMPARE_WITH_TIMEOUT(activeMediaHelpers(), 0, 3000);
    }
}
void AudioTest::movingVideoDoesNotPlayAsAudio()
{
    const auto root = qEnvironmentVariable("DZURKEL_TEST_AUDIO_FIXTURES");
    const auto path = QDir(root).filePath(QStringLiteral("video.mp4"));
    if (root.isEmpty() || !QFileInfo::exists(path)) QSKIP("Optional synthetic video fixture is unavailable.");
    PreviewController controller;
    mute(controller);
    QString error;
    QVERIFY(controller.show(path, error, nullptr, {}, false, true));
    QTRY_COMPARE_WITH_TIMEOUT(controller.previewKind(), QStringLiteral("video"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QMediaPlayer *>()
        && controller.window()->findChild<QMediaPlayer *>()->position() > 0, 5000);
    QVERIFY(controller.window()->findChild<QAudioOutput *>()->isMuted());
    controller.hide();
}
QTEST_MAIN(AudioTest)
#include "AudioTest.moc"

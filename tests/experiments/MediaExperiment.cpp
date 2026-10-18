#include "resources/media/StreamingMediaSource.h"
#include <QAudioOutput>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QTextStream>
#include <QTimer>
#include <memory>

using namespace dzurkel;
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dzurkel-media-experiment"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Uninstalled streaming experiment; muted unless --audible is supplied."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Local path or existing supported resource URL."));
    parser.addOption({QStringLiteral("audible"), QStringLiteral("Enable audible playback.")});
    parser.addOption({QStringLiteral("duration-ms"), QStringLiteral("Experiment duration after opening."), QStringLiteral("ms"), QStringLiteral("6000")});
    parser.addOption({QStringLiteral("seek-ms"), QStringLiteral("Seek after one second of playback."), QStringLiteral("ms"), QStringLiteral("30000")});
    parser.addOption({QStringLiteral("read-delay-ms"), QStringLiteral("Inject a delay in each helper read."), QStringLiteral("ms"), QStringLiteral("0")});
    parser.process(application);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);
    bool durationOk = false, seekOk = false, delayOk = false;
    const int duration = parser.value(QStringLiteral("duration-ms")).toInt(&durationOk);
    const qint64 seek = parser.value(QStringLiteral("seek-ms")).toLongLong(&seekOk);
    const int delay = parser.value(QStringLiteral("read-delay-ms")).toInt(&delayOk);
    if (!durationOk || duration < 1500 || duration > 60000 || !seekOk || seek < 0
        || !delayOk || delay < 0 || delay > 30000) parser.showHelp(2);
    StreamingMediaSource source;
    QAudioOutput audio;
    audio.setMuted(!parser.isSet(QStringLiteral("audible")));
    auto player = std::make_unique<QMediaPlayer>();
    player->setAudioOutput(&audio);
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 openedMs = -1, firstPositionMs = -1, stopMs = -1, lastPosition = 0;
    qint64 finalDuration = 0, positionAfterSeek = -1;
    qint64 bytesAtFirstPosition = -1, maximumHeartbeatGap = 0, previousHeartbeat = 0;
    int heartbeat = 0, exitCode = 0;
    int videoTracks = 0;
    bool finished = false, seekIssued = false, wasSeekable = false;
    QString failure;
    QTimer heartbeatTimer;
    heartbeatTimer.setInterval(10);
    QObject::connect(&heartbeatTimer, &QTimer::timeout, [&] {
        const auto current = elapsed.elapsed();
        maximumHeartbeatGap = qMax(maximumHeartbeatGap, current - previousHeartbeat);
        previousHeartbeat = current;
        ++heartbeat;
    });
    heartbeatTimer.start();
    const auto finish = [&](const QString &error) {
        if (finished) return;
        finished = true;
        failure = error;
        if (failure.isEmpty() && firstPositionMs < 0)
            failure = QStringLiteral("Playback did not advance before the experiment ended.");
        if (failure.isEmpty() && source.ownerThreadReads() != 0)
            failure = QStringLiteral("The backend performed a media read on the application thread.");
        exitCode = failure.isEmpty() && firstPositionMs >= 0 && source.ownerThreadReads() == 0 ? 0 : 1;
        finalDuration = player->duration();
        wasSeekable = player->isSeekable();
        videoTracks = player->videoTracks().size();
        audio.setMuted(true);
        QElapsedTimer stopping;
        stopping.start();
        source.cancel(); // Wake any decoder read before entering Qt teardown.
        player->setSource(QUrl());
        player.reset(); // The device must outlive all decoder access.
        stopMs = stopping.elapsed();
        // Let QProcess reap the helper before reporting lifecycle results.
        QTimer::singleShot(100, &application, [&] {
            const QJsonObject report{
                {QStringLiteral("error"), failure},
                {QStringLiteral("opened_ms"), openedMs},
                {QStringLiteral("first_position_ms"), firstPositionMs},
                {QStringLiteral("bytes_at_first_position"), bytesAtFirstPosition},
                {QStringLiteral("stop_ms"), stopMs},
                {QStringLiteral("source_bytes"), source.size()},
                {QStringLiteral("transferred_bytes"), source.bytesTransferred()},
                {QStringLiteral("read_requests"), source.readRequests()},
                {QStringLiteral("seek_requests"), source.seekRequests()},
                {QStringLiteral("peak_read_bytes"), source.peakReadBytes()},
                {QStringLiteral("owner_thread_reads"), source.ownerThreadReads()},
                {QStringLiteral("duration_ms"), finalDuration},
                {QStringLiteral("last_position_ms"), lastPosition},
                {QStringLiteral("position_after_seek_ms"), positionAfterSeek},
                {QStringLiteral("seek_issued"), seekIssued},
                {QStringLiteral("seekable"), wasSeekable},
                {QStringLiteral("video_tracks"), videoTracks},
                {QStringLiteral("gui_heartbeats"), heartbeat},
                {QStringLiteral("maximum_heartbeat_gap_ms"), maximumHeartbeatGap},
                {QStringLiteral("active_helpers_after_stop"), StreamingMediaSource::activeHelpers()}};
            QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
            application.exit(exitCode);
        });
    };
    QObject::connect(&source, &StreamingMediaSource::failed, &application, finish);
    QObject::connect(&source, &StreamingMediaSource::ready, &application, [&] {
        openedMs = elapsed.elapsed();
        player->setSourceDevice(&source);
        player->play();
        QTimer::singleShot(duration, &application, [&] { finish({}); });
    });
    QObject::connect(player.get(), &QMediaPlayer::positionChanged, &application, [&](qint64 position) {
        if (finished) return;
        lastPosition = position;
        if (seekIssued) positionAfterSeek = position;
        if (position > 0 && firstPositionMs < 0) {
            firstPositionMs = elapsed.elapsed();
            bytesAtFirstPosition = source.bytesTransferred();
            QTimer::singleShot(1000, &application, [&] {
                if (!finished && player->isSeekable() && seek < player->duration()) {
                    seekIssued = true;
                    player->setPosition(seek);
                }
            });
        }
    });
    QObject::connect(player.get(), &QMediaPlayer::errorOccurred, &application,
        [&](QMediaPlayer::Error, const QString &error) {
            // Avoid destroying the player in one of its own signal stacks.
            QTimer::singleShot(0, &application, [&, error] { finish(error); });
        });
    QTimer::singleShot(35000 + duration, &application, [&] { finish(QStringLiteral("Experiment timed out.")); });
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), parser.positionalArguments().first(), delay);
    return application.exec();
}

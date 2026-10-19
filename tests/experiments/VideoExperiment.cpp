#include "resources/media/StreamingMediaSource.h"
#include <QApplication>
#include <QAudioOutput>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTextStream>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

using namespace dzurkel;

// Uninstalled, muted probe. A sink callback proves decoded frame delivery, not
// compositor presentation; native visual acceptance remains a separate check.
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Read-only, muted streaming video probe."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Local path or supported resource URL."));
    parser.addOption({QStringLiteral("duration-ms"), QStringLiteral("Run time after source opens."), QStringLiteral("ms"), QStringLiteral("10000")});
    parser.addOption({QStringLiteral("read-delay-ms"), QStringLiteral("Delay each helper filesystem read (cache miss)."), QStringLiteral("ms"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("read-timeout-ms"), QStringLiteral("Deadline for each source read."), QStringLiteral("ms"), QStringLiteral("2000")});
    parser.addOption({QStringLiteral("cancel-loading"), QStringLiteral("Expect cancellation before any frame arrives.")});
    parser.addOption({QStringLiteral("hidden"), QStringLiteral("Decode to the widget sink without showing the window.")});
    parser.process(application);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);
    const auto integer = [&](const QString &name, int minimum, int maximum) {
        bool ok = false;
        const int value = parser.value(name).toInt(&ok);
        if (!ok || value < minimum || value > maximum) parser.showHelp(2);
        return value;
    };
    const int duration = integer(QStringLiteral("duration-ms"), 1000, 60000);
    const int delay = integer(QStringLiteral("read-delay-ms"), 0, 30000);
    const int timeout = integer(QStringLiteral("read-timeout-ms"), 1, 60000);
    const bool cancelLoading = parser.isSet(QStringLiteral("cancel-loading"));
    StreamingMediaSource source(nullptr, timeout);
    QAudioOutput audio;
    audio.setMuted(true);
    QVideoWidget view;
    view.setWindowTitle(QStringLiteral("Dzurkel streaming video experiment (muted)"));
    view.resize(800, 500);
    view.setAspectRatioMode(Qt::KeepAspectRatio);
    if (!parser.isSet(QStringLiteral("hidden"))) view.show();
    auto player = std::make_unique<QMediaPlayer>();
    player->setAudioOutput(&audio);
    player->setVideoOutput(&view);
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 opened = -1, firstFrame = -1, firstFrameBytes = -1;
    qint64 frames = 0, stopMs = -1, maxGap = 0, previousHeartbeat = 0;
    qint64 seekStarted = -1, seekTarget = -1;
    int width = 0, height = 0, heartbeats = 0, bufferingEvents = 0;
    bool finished = false, seekable = false, hadVideo = false, seekScheduled = false;
    int exitCode = 1;
    QString failure;
    QJsonArray seeks;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &application, [&] {
        const auto now = elapsed.elapsed();
        maxGap = std::max(maxGap, now - previousHeartbeat);
        previousHeartbeat = now;
        ++heartbeats;
    });
    heartbeat.start();
    std::function<void(QString)> finish;
    finish = [&](QString error) {
        if (finished) return;
        finished = true;
        heartbeat.stop();
        maxGap = std::max(maxGap, elapsed.elapsed() - previousHeartbeat);
        failure = std::move(error);
        seekable = player->isSeekable();
        hadVideo = player->hasVideo();
        if (failure.isEmpty()) {
            if (cancelLoading) {
                if (frames != 0 || source.readRequests() == 0)
                    failure = QStringLiteral("Cancellation did not occur during the initial decoder read.");
            } else if (frames < 2 || seeks.size() != 2) {
                failure = QStringLiteral("Expected advancing video frames and two completed seeks.");
            }
        }
        audio.setMuted(true);
        QElapsedTimer stopping;
        stopping.start();
        source.cancel();
        player->setSource(QUrl());
        player.reset();
        stopMs = stopping.elapsed();
        view.hide();
        if (failure.isEmpty() && (stopMs >= 500 || maxGap >= 500 || source.ownerThreadReads() != 0))
            failure = QStringLiteral("Responsiveness or source-thread acceptance failed.");
        // Include synchronous teardown in the heartbeat gap measurement above
        // through stop_ms. Reap asynchronously, with a bounded reporting wait.
        auto *reaper = new QTimer(&application);
        const auto reapingStarted = elapsed.elapsed();
        QObject::connect(reaper, &QTimer::timeout, &application, [&, reaper, reapingStarted] {
            if (StreamingMediaSource::activeHelpers() != 0 && elapsed.elapsed() - reapingStarted < 2000) return;
            reaper->stop();
            if (failure.isEmpty() && StreamingMediaSource::activeHelpers() != 0)
                failure = QStringLiteral("Media helper has not retired.");
            exitCode = failure.isEmpty() ? 0 : 1;
            const QJsonObject report{
                {QStringLiteral("error"), failure},
                {QStringLiteral("opened_ms"), opened},
                {QStringLiteral("first_frame_ms"), firstFrame},
                {QStringLiteral("bytes_at_first_frame"), firstFrameBytes},
                {QStringLiteral("frames"), frames},
                {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
                {QStringLiteral("has_video"), hadVideo}, {QStringLiteral("seekable"), seekable},
                {QStringLiteral("seeks"), seeks},
                {QStringLiteral("buffering_events"), bufferingEvents},
                {QStringLiteral("source_bytes"), source.size()},
                {QStringLiteral("transferred_bytes"), source.bytesTransferred()},
                {QStringLiteral("read_requests"), source.readRequests()},
                {QStringLiteral("seek_requests"), source.seekRequests()},
                {QStringLiteral("peak_read_bytes"), source.peakReadBytes()},
                {QStringLiteral("owner_thread_reads"), source.ownerThreadReads()},
                {QStringLiteral("gui_heartbeats"), heartbeats},
                {QStringLiteral("maximum_heartbeat_gap_ms"), maxGap},
                {QStringLiteral("stop_ms"), stopMs},
                {QStringLiteral("active_helpers_after_stop"), StreamingMediaSource::activeHelpers()},
                {QStringLiteral("read_delay_ms"), delay},
                {QStringLiteral("read_timeout_ms"), timeout},
                {QStringLiteral("cancel_loading"), cancelLoading}};
            QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
            application.exit(exitCode);
        });
        reaper->start(20);
    };
    const auto scheduleSeek = [&](qint64 target) {
        seekScheduled = true;
        QTimer::singleShot(1000, &application, [&, target] {
            if (finished) return;
            seekTarget = target;
            seekStarted = elapsed.elapsed();
            player->setPosition(target);
        });
    };
    QObject::connect(view.videoSink(), &QVideoSink::videoFrameChanged, &application, [&](const QVideoFrame &frame) {
        if (finished || !frame.isValid()) return;
        ++frames;
        width = frame.width();
        height = frame.height();
        if (firstFrame < 0) {
            firstFrame = elapsed.elapsed();
            firstFrameBytes = source.bytesTransferred();
        }
        const qint64 position = frame.startTime() / 1000;
        if (seekStarted >= 0 && frame.startTime() >= 0 && std::abs(position - seekTarget) < 3000) {
            seeks.append(QJsonObject{{QStringLiteral("target_ms"), seekTarget},
                {QStringLiteral("frame_ms"), position},
                {QStringLiteral("latency_ms"), elapsed.elapsed() - seekStarted}});
            seekStarted = -1;
            if (seeks.size() == 1) scheduleSeek(5000);
        }
        if (!cancelLoading && !seekScheduled && player->isSeekable() && player->duration() > 20000)
            scheduleSeek(std::min<qint64>(60000, player->duration() / 2));
    });
    QObject::connect(&source, &StreamingMediaSource::ready, &application, [&] {
        if (finished) return;
        opened = elapsed.elapsed();
        player->setSourceDevice(&source);
        player->setActiveSubtitleTrack(-1);
        player->play();
        QTimer::singleShot(duration, &application, [&] { finish({}); });
    });
    QObject::connect(&source, &StreamingMediaSource::failed, &application, [&](const QString &error) {
        QTimer::singleShot(0, &application, [&, error] { finish(error); });
    });
    QObject::connect(player.get(), &QMediaPlayer::errorOccurred, &application,
        [&](QMediaPlayer::Error, const QString &error) {
            QTimer::singleShot(0, &application, [&, error] { finish(error); });
        });
    QObject::connect(player.get(), &QMediaPlayer::mediaStatusChanged, &application, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::StalledMedia || status == QMediaPlayer::BufferingMedia) ++bufferingEvents;
    });
    QTimer::singleShot(35000 + duration, &application, [&] { finish(QStringLiteral("Experiment timed out.")); });
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), parser.positionalArguments().first(), delay);
    return application.exec();
}

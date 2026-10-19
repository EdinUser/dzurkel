#include "MediaSession.h"
#include "resources/media/StreamingMediaSource.h"
#include <QAudioDevice>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <cmath>

namespace dzurkel {
MediaSession::MediaSession(QObject *parent) : QObject(parent)
{
    watchdog_.setInterval(250);
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        if (stopped_) return;
        if (!ready_ && phase_.elapsed() > 60000)
            fail(QStringLiteral("Media loading timed out. Check the source connection and retry."));
        else if (seekTarget_ >= 0 && phase_.elapsed() > 45000)
            fail(QStringLiteral("Seeking timed out. Check the source connection and retry."));
        else if (ready_ && player_ && player_->playbackState() == QMediaPlayer::PlayingState
            && seekTarget_ < 0 && progress_.elapsed() > 30000)
            fail(QStringLiteral("Media playback stopped making progress. Check the source connection and retry."));
        else updateStatus();
    });
}
MediaSession::~MediaSession() { stop(); }
void MediaSession::start(const FileResource &resource, QVideoWidget *surface)
{
    if (stopped_ || source_) return;
    // Source waits tolerate a slow share; shutdown() still wakes decoder reads
    // immediately when closing or navigating away. Each timeout retires source.
    source_ = std::make_unique<StreamingMediaSource>(this, 15000);
    output_ = std::make_unique<QAudioOutput>(this);
    output_->setMuted(true); // Never emit sound before track classification.
    output_->setVolume(float(volume_) / 100);
    player_ = std::make_unique<QMediaPlayer>(this);
    player_->setObjectName(QStringLiteral("mediaPlayer"));
    player_->setAudioOutput(output_.get());
    player_->setVideoOutput(surface);
    phase_.start();
    progress_.start();
    watchdog_.start();
    if (!player_->isAvailable()) {
        QTimer::singleShot(0, this, [this] { fail(QStringLiteral("Media playback is unavailable in the installed Qt backend.")); });
        return;
    }
    connect(source_.get(), &StreamingMediaSource::failed, this, [this](const QString &message) {
        QTimer::singleShot(0, this, [this, message] { fail(message); });
    });
    connect(source_.get(), &StreamingMediaSource::ready, this, [this] {
        if (stopped_) return;
        status_ = QStringLiteral("Loading media…");
        emit changed();
        player_->setSourceDevice(source_.get());
    });
    connect(player_.get(), &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &detail) {
        QString message = QStringLiteral("This media file couldn't be previewed. Its format may be unsupported, or the file may be damaged.");
        if (error == QMediaPlayer::AccessDeniedError) message = QStringLiteral("Permission to play this media file was denied.");
        else if (error == QMediaPlayer::NetworkError) message = QStringLiteral("The media source could not be read. Check the connection and retry.");
        QTimer::singleShot(0, this, [this, message, detail] { fail(message, detail); });
    });
    connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (stopped_) return;
        if (status == QMediaPlayer::EndOfMedia) seekTarget_ = -1;
        if (status == QMediaPlayer::LoadedMedia && !loaded_) {
            loaded_ = true;
            // FFmpeg excludes attached music cover pictures from videoTracks.
            video_ = !player_->videoTracks().isEmpty();
            if (!video_ && !player_->hasAudio()) {
                QTimer::singleShot(0, this, [this] { fail(QStringLiteral("No playable audio or video track was found in this file.")); });
                return;
            }
            if (!video_ && output_->device().isNull()) {
                QTimer::singleShot(0, this, [this] { fail(QStringLiteral("No audio output device is available.")); });
                return;
            }
            if (video_ && output_->device().isNull()) player_->setActiveAudioTrack(-1);
            output_->setMuted(muted_);
            if (!video_) player_->setActiveVideoTrack(-1);
            player_->setActiveSubtitleTrack(-1);
            ready_ = !video_;
            progress_.restart();
            emit kindChanged(video_);
            emit metadataChanged();
            player_->play();
        }
        updateStatus();
    });
    connect(surface->videoSink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (stopped_ || !video_ || !frame.isValid()) return;
        ready_ = true;
        progress_.restart();
        if (seekTarget_ >= 0 && frame.startTime() >= 0
            && std::abs(frame.startTime() / 1000 - seekTarget_) < 3000) seekTarget_ = -1;
        updateStatus();
    });
    connect(player_.get(), &QMediaPlayer::positionChanged, this, [this] {
        if (!video_) { progress_.restart(); seekTarget_ = -1; }
        updateStatus();
    });
    connect(player_.get(), &QMediaPlayer::playbackStateChanged, this, &MediaSession::updateStatus);
    connect(player_.get(), &QMediaPlayer::seekableChanged, this, &MediaSession::changed);
    connect(player_.get(), &QMediaPlayer::durationChanged, this, &MediaSession::changed);
    connect(player_.get(), &QMediaPlayer::metaDataChanged, this, &MediaSession::metadataChanged);
    source_->start(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"),
        resource.url.toString(QUrl::FullyEncoded), 0, {QStringLiteral("--internal-media-source")});
}
void MediaSession::stop()
{
    if (stopped_) return;
    stopped_ = true;
    ready_ = false;
    watchdog_.stop();
    if (output_) output_->setMuted(true);
    if (source_) source_->cancel();
    if (player_) {
        disconnect(player_.get(), nullptr, this, nullptr);
        player_->setSource(QUrl());
        player_.reset();
    }
    output_.reset();
    source_.reset();
    status_ = QStringLiteral("Stopped");
    emit changed();
}
void MediaSession::fail(const QString &message, const QString &details)
{
    if (stopped_) return;
    stop();
    emit failed(message, details.left(2048));
}
void MediaSession::setMuted(bool value)
{
    muted_ = value;
    if (output_ && loaded_) output_->setMuted(value);
    emit changed();
}
void MediaSession::setVolume(int value)
{
    volume_ = value;
    if (output_) output_->setVolume(float(value) / 100);
}
void MediaSession::togglePlayback()
{
    if (!player_ || !ready_ || stopped_) return;
    if (player_->playbackState() == QMediaPlayer::PlayingState) player_->pause();
    else { progress_.restart(); player_->play(); }
}
void MediaSession::seek(qint64 position)
{
    if (!player_ || !ready_ || !player_->isSeekable() || stopped_) return;
    seekTarget_ = position;
    phase_.restart();
    player_->setPosition(position);
    updateStatus();
}
void MediaSession::updateStatus()
{
    if (stopped_ || !player_) return;
    if (!loaded_) { emit changed(); return; }
    if (seekTarget_ >= 0) status_ = QStringLiteral("Seeking…");
    else if (!ready_) status_ = QStringLiteral("Loading video…");
    else if (player_->mediaStatus() == QMediaPlayer::EndOfMedia) status_ = QStringLiteral("Finished");
    else if (player_->playbackState() != QMediaPlayer::PlayingState) status_ = QStringLiteral("Paused");
    else if (player_->mediaStatus() == QMediaPlayer::StalledMedia || progress_.elapsed() > 1500)
        status_ = QStringLiteral("Buffering…");
    else status_ = QStringLiteral("Playing");
    emit changed();
}
}

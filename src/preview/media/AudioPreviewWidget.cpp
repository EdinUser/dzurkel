#include "AudioPreviewWidget.h"
#include "resources/media/StreamingMediaSource.h"
#include <QAudioOutput>
#include <QAudioDevice>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <algorithm>

namespace dzurkel {
namespace {
class ClickPositionSlider final : public QSlider {
public:
    explicit ClickPositionSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            setFocus(Qt::MouseFocusReason);
            QStyleOptionSlider option;
            initStyleOption(&option);
            const auto handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
            if (!handle.contains(event->position().toPoint())) {
                const auto groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
                const int start = groove.x();
                const int span = groove.width() - handle.width();
                if (span > 0)
                    setValue(QStyle::sliderValueFromPosition(minimum(), maximum(),
                        qRound(event->position().x()) - handle.width() / 2 - start, span, option.upsideDown));
            }
        }
        // After jumping, the handle is under the pointer, so Qt retains its
        // native press/drag/release behavior and the normal themed appearance.
        QSlider::mousePressEvent(event);
    }
};
int mediaVolume = 70;
bool mediaMuted = false;
QString timestamp(qint64 milliseconds)
{
    const auto seconds = std::max<qint64>(0, milliseconds / 1000);
    if (seconds >= 3600)
        return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}
QString bounded(const QString &value) { return value.left(1024); }
}
AudioPreviewWidget::AudioPreviewWidget(const PreviewData &data, QWidget *parent)
    : QWidget(parent), data_(data)
{
    setObjectName(QStringLiteral("audioPreview"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);
    const auto label = [this, layout](const QString &name) {
        auto *result = new QLabel(this);
        result->setObjectName(name);
        result->setTextFormat(Qt::PlainText);
        result->setAlignment(Qt::AlignCenter);
        result->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(result);
        return result;
    };
    cover_ = label(QStringLiteral("audioCover"));
    cover_->setFixedHeight(220);
    cover_->setPixmap(QIcon::fromTheme(QStringLiteral("audio-x-generic"), style()->standardIcon(QStyle::SP_MediaVolume)).pixmap(96, 96));
    title_ = label(QStringLiteral("audioTitle"));
    title_->setText(QFileInfo(data.resource.path).fileName());
    auto font = title_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    title_->setFont(font);
    artist_ = label(QStringLiteral("audioArtist"));
    album_ = label(QStringLiteral("audioAlbum"));
    status_ = label(QStringLiteral("audioStatus"));
    status_->setText(QStringLiteral("Loading audio…"));
    seek_ = new ClickPositionSlider(this);
    seek_->setObjectName(QStringLiteral("audioSeek"));
    seek_->setAccessibleName(QStringLiteral("Playback position"));
    seek_->setRange(0, 100000);
    seek_->setEnabled(false);
    layout->addWidget(seek_);
    auto *controls = new QHBoxLayout;
    const auto button = [this, controls](const QString &name, const QString &text, QStyle::StandardPixmap icon) {
        auto *result = new QPushButton(text, this);
        result->setObjectName(name);
        result->setIcon(style()->standardIcon(icon));
        result->setAutoDefault(false);
        controls->addWidget(result);
        return result;
    };
    play_ = button(QStringLiteral("audioPlay"), QStringLiteral("Pause"), QStyle::SP_MediaPause);
    play_->setAccessibleName(QStringLiteral("Play or pause (P)"));
    play_->setToolTip(QStringLiteral("Play or pause (P). Space closes the preview."));
    play_->setEnabled(false);
    time_ = new QLabel(QStringLiteral("0:00 / —"), this);
    time_->setTextFormat(Qt::PlainText);
    time_->setObjectName(QStringLiteral("audioTime"));
    controls->addWidget(time_);
    controls->addStretch();
    mute_ = button(QStringLiteral("audioMute"), QStringLiteral("Mute"), QStyle::SP_MediaVolume);
    mute_->setCheckable(true);
    volume_ = new ClickPositionSlider(this);
    volume_->setObjectName(QStringLiteral("audioVolume"));
    volume_->setAccessibleName(QStringLiteral("Volume"));
    volume_->setRange(0, 100);
    volume_->setValue(mediaVolume);
    volume_->setMaximumWidth(100);
    controls->addWidget(volume_);
    layout->addLayout(controls);
    connect(play_, &QPushButton::clicked, this, &AudioPreviewWidget::togglePlayback);
    connect(mute_, &QPushButton::toggled, this, &AudioPreviewWidget::setMuted);
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        mediaVolume = value;
        if (output_) output_->setVolume(float(value) / 100);
    });
    const auto seek = [this] {
        if (player_ && player_->isSeekable()) player_->setPosition(qRound64(player_->duration() * (seek_->value() / 100000.0)));
    };
    connect(seek_, &QSlider::sliderReleased, this, seek);
    connect(seek_, &QSlider::valueChanged, this, [this, seek] { if (!seek_->isSliderDown()) seek(); });
    auto *pause = new QShortcut(QKeySequence(Qt::Key_P), this);
    pause->setContext(Qt::WidgetWithChildrenShortcut);
    pause->setAutoRepeat(false);
    connect(pause, &QShortcut::activated, this, &AudioPreviewWidget::togglePlayback);
    setMuted(mediaMuted);
    loadingTimer_.setSingleShot(true);
    connect(&loadingTimer_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Audio loading timed out.")); });
    QTimer::singleShot(0, this, &AudioPreviewWidget::start);
}
AudioPreviewWidget::~AudioPreviewWidget() { stop(); }
void AudioPreviewWidget::setCallbacks(QObject *context, AudioCallback failure, AudioCallback details)
{
    connect(this, &AudioPreviewWidget::failed, context, std::move(failure));
    connect(this, &AudioPreviewWidget::detailsChanged, context, std::move(details));
}
void AudioPreviewWidget::setMuted(bool muted)
{
    muted_ = muted;
    mediaMuted = muted;
    if (output_) output_->setMuted(muted);
    const QSignalBlocker blocker(mute_);
    mute_->setChecked(muted);
    mute_->setText(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
    mute_->setIcon(style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
}
void AudioPreviewWidget::start()
{
    if (stopped_) return;
    source_ = std::make_unique<StreamingMediaSource>(this);
    output_ = std::make_unique<QAudioOutput>(this);
    output_->setMuted(muted_);
    output_->setVolume(float(volume_->value()) / 100);
    player_ = std::make_unique<QMediaPlayer>(this);
    player_->setObjectName(QStringLiteral("audioPlayer"));
    player_->setAudioOutput(output_.get());
    if (!player_->isAvailable()) {
        QTimer::singleShot(0, this, [this] { fail(QStringLiteral("Audio playback is unavailable in the installed Qt media backend.")); });
        return;
    }
    QStringList codecs;
    for (const auto codec : QMediaFormat().supportedAudioCodecs(QMediaFormat::Decode))
        codecs.append(QMediaFormat::audioCodecName(codec));
    decoderDetails_ = QStringLiteral("Qt reports available audio decoders: %1").arg(codecs.isEmpty()
        ? QStringLiteral("none") : codecs.join(QStringLiteral(", ")));
    connect(source_.get(), &StreamingMediaSource::failed, this, [this](const QString &message) {
        QTimer::singleShot(0, this, [this, message] { fail(message); });
    });
    connect(source_.get(), &StreamingMediaSource::ready, this, [this] {
        if (stopped_) return;
        player_->setSourceDevice(source_.get());
    });
    connect(player_.get(), &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &details) {
        QString message = QStringLiteral("This audio file couldn't be previewed. Its format may be unsupported, or the file may be damaged.");
        if (error == QMediaPlayer::AccessDeniedError) message = QStringLiteral("Permission to play this audio file was denied.");
        else if (error == QMediaPlayer::NetworkError) message = QStringLiteral("The audio source could not be read.");
        else if (error == QMediaPlayer::ResourceError) message = QStringLiteral("Audio playback failed.");
        QTimer::singleShot(0, this, [this, message, details] {
            fail(message, details);
        });
    });
    connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (stopped_) return;
        if (status == QMediaPlayer::LoadedMedia && !playbackStarted_) {
            // Do not silently play a moving-video file as audio. Qt's FFmpeg
            // backend excludes attached cover pictures from this track list.
            if (!player_->videoTracks().isEmpty()) {
                QTimer::singleShot(0, this, [this] { fail(QStringLiteral("This file contains video. Video previews are not available yet.")); });
                return;
            }
            if (!player_->hasAudio()) {
                QTimer::singleShot(0, this, [this] { fail(QStringLiteral("No playable audio track was found in this file.")); });
                return;
            }
            if (output_->device().isNull()) {
                QTimer::singleShot(0, this, [this] { fail(QStringLiteral("No audio output device is available.")); });
                return;
            }
            loadingTimer_.stop();
            playbackStarted_ = true;
            player_->setActiveVideoTrack(-1);
            updateMetadata();
            play_->setEnabled(true);
            player_->play();
        }
        if (status == QMediaPlayer::StalledMedia || status == QMediaPlayer::BufferingMedia)
            status_->setText(QStringLiteral("Buffering…"));
        else updatePlayback();
    });
    connect(player_.get(), &QMediaPlayer::metaDataChanged, this, &AudioPreviewWidget::updateMetadata);
    connect(player_.get(), &QMediaPlayer::durationChanged, this, &AudioPreviewWidget::updatePosition);
    connect(player_.get(), &QMediaPlayer::positionChanged, this, &AudioPreviewWidget::updatePosition);
    connect(player_.get(), &QMediaPlayer::seekableChanged, this, &AudioPreviewWidget::updatePosition);
    connect(player_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioPreviewWidget::updatePlayback);
    loadingTimer_.start(30000);
    source_->start(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"),
        data_.resource.url.toString(QUrl::FullyEncoded), 0, {QStringLiteral("--internal-media-source")});
}
void AudioPreviewWidget::stop()
{
    stopped_ = true;
    loadingTimer_.stop();
    play_->setEnabled(false);
    seek_->setEnabled(false);
    if (output_) output_->setMuted(true);
    if (source_) source_->cancel();
    if (player_) {
        disconnect(player_.get(), nullptr, this, nullptr);
        player_->setSource(QUrl());
        player_.reset();
    }
    output_.reset();
    source_.reset();
    status_->setText(QStringLiteral("Stopped"));
}
void AudioPreviewWidget::fail(const QString &message, const QString &details)
{
    if (stopped_) return;
    stop();
    status_->setText(message);
    status_->setWordWrap(true);
    emit failed(message, bounded(details) + (details.isEmpty() ? QString() : QChar('\n')) + decoderDetails_);
}
void AudioPreviewWidget::togglePlayback()
{
    if (!player_ || stopped_ || !playbackStarted_) return;
    if (player_->playbackState() == QMediaPlayer::PlayingState) player_->pause();
    else player_->play();
}
void AudioPreviewWidget::updatePlayback()
{
    if (!player_ || stopped_) return;
    const bool playing = player_->playbackState() == QMediaPlayer::PlayingState;
    play_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    play_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (!playbackStarted_) return;
    status_->setText(player_->mediaStatus() == QMediaPlayer::EndOfMedia ? QStringLiteral("Finished")
        : playing ? QStringLiteral("Playing") : QStringLiteral("Paused"));
}
void AudioPreviewWidget::updatePosition()
{
    if (!player_ || stopped_) return;
    const auto duration = player_->duration();
    seek_->setEnabled(playbackStarted_ && player_->isSeekable() && duration > 0);
    if (!seek_->isSliderDown()) {
        const QSignalBlocker blocker(seek_);
        seek_->setValue(duration > 0 ? int(std::clamp(player_->position() * 100000.0 / duration, 0.0, 100000.0)) : 0);
    }
    time_->setText(timestamp(player_->position()) + QStringLiteral(" / ") + (duration > 0 ? timestamp(duration) : QStringLiteral("—")));
    seek_->setAccessibleDescription(time_->text());
}
void AudioPreviewWidget::updateMetadata()
{
    if (!player_ || stopped_) return;
    const auto metadata = player_->metaData();
    const auto title = bounded(metadata.stringValue(QMediaMetaData::Title));
    title_->setText(title.isEmpty() ? QFileInfo(data_.resource.path).fileName() : title);
    title_->setToolTip(title_->text());
    artist_->setText(bounded(metadata.stringValue(QMediaMetaData::ContributingArtist)));
    if (artist_->text().isEmpty()) artist_->setText(bounded(metadata.stringValue(QMediaMetaData::AlbumArtist)));
    album_->setText(bounded(metadata.stringValue(QMediaMetaData::AlbumTitle)));
    auto image = metadata.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (image.isNull()) image = metadata.value(QMediaMetaData::ThumbnailImage).value<QImage>();
    if (!image.isNull()) cover_->setPixmap(QPixmap::fromImage(image.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    QStringList summary, details;
    if (metadata.value(QMediaMetaData::AudioCodec).isValid()) {
        const auto codec = QMediaFormat::audioCodecName(metadata.value(QMediaMetaData::AudioCodec).value<QMediaFormat::AudioCodec>());
        if (!codec.isEmpty()) summary.append(codec);
    }
    const auto bitrate = metadata.value(QMediaMetaData::AudioBitRate).toLongLong();
    if (bitrate > 0) summary.append(QStringLiteral("%1 kb/s").arg(bitrate / 1000));
    details << QStringLiteral("Title: %1").arg(title_->text());
    if (!artist_->text().isEmpty()) details << QStringLiteral("Artist: %1").arg(artist_->text());
    if (!album_->text().isEmpty()) details << QStringLiteral("Album: %1").arg(album_->text());
    if (player_->duration() > 0) details << QStringLiteral("Duration: %1").arg(timestamp(player_->duration()));
    if (!summary.isEmpty()) details << summary.join(QStringLiteral(" · "));
    emit detailsChanged(summary.isEmpty() ? QStringLiteral("Audio") : summary.join(QStringLiteral(" · ")), details.join(QChar('\n')));
    updatePosition();
}
}

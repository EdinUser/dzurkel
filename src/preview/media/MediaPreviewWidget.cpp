#include "MediaPreviewWidget.h"
#include "MediaSession.h"
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
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <algorithm>

namespace dzurkel {
namespace {
class ClickPositionSlider final : public QSlider {
public:
    explicit ClickPositionSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) { setFocusPolicy(Qt::StrongFocus); }
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
                const int span = groove.width() - handle.width();
                if (span > 0) setValue(QStyle::sliderValueFromPosition(minimum(), maximum(),
                    qRound(event->position().x()) - handle.width() / 2 - groove.x(), span, option.upsideDown));
            }
        }
        QSlider::mousePressEvent(event);
    }
};
int mediaVolume = 70;
bool mediaMuted = false;
QString timestamp(qint64 milliseconds)
{
    const auto seconds = std::max<qint64>(0, milliseconds / 1000);
    if (seconds >= 3600) return QStringLiteral("%1:%2:%3").arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}
QLabel *plainLabel(const QString &name, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    return label;
}
}

class AudioPresentationWidget : public QWidget {
public:
    explicit AudioPresentationWidget(const QString &filename, QWidget *parent) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        const auto label = [this, layout](const QString &name) {
            auto *result = plainLabel(name, this);
            result->setAlignment(Qt::AlignCenter);
            result->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            layout->addWidget(result);
            return result;
        };
        cover = label(QStringLiteral("audioCover"));
        cover->setFixedHeight(220);
        cover->setPixmap(QIcon::fromTheme(QStringLiteral("audio-x-generic"), style()->standardIcon(QStyle::SP_MediaVolume)).pixmap(96, 96));
        title = label(QStringLiteral("audioTitle"));
        title->setText(filename);
        auto font = title->font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * 1.15);
        title->setFont(font);
        artist = label(QStringLiteral("audioArtist"));
        album = label(QStringLiteral("audioAlbum"));
    }
    QLabel *cover, *title, *artist, *album;
};

MediaPreviewWidget::MediaPreviewWidget(const PreviewData &data, QWidget *parent)
    : QWidget(parent), data_(data), session_(std::make_unique<MediaSession>(this)),
      videoMode_(data.kind == PreviewKind::Video)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);
    presentations_ = new QStackedWidget(this);
    audio_ = new AudioPresentationWidget(QFileInfo(data.resource.path).fileName(), presentations_);
    video_ = new QVideoWidget(presentations_);
    video_->setObjectName(QStringLiteral("videoSurface"));
    video_->setAspectRatioMode(Qt::KeepAspectRatio);
    video_->setMinimumSize(160, 100);
    video_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    presentations_->addWidget(audio_);
    presentations_->addWidget(video_);
    layout->addWidget(presentations_, 1);
    status_ = plainLabel(QString(), this);
    layout->addWidget(status_);
    seek_ = new ClickPositionSlider(this);
    seek_->setAccessibleName(QStringLiteral("Playback position"));
    seek_->setRange(0, 100000);
    layout->addWidget(seek_);
    auto *controls = new QHBoxLayout;
    const auto button = [this, controls](const QString &text, QStyle::StandardPixmap icon) {
        auto *result = new QPushButton(text, this);
        result->setIcon(style()->standardIcon(icon));
        result->setAutoDefault(false);
        controls->addWidget(result);
        return result;
    };
    play_ = button(QStringLiteral("Pause"), QStyle::SP_MediaPause);
    play_->setAccessibleName(QStringLiteral("Play or pause (P)"));
    play_->setToolTip(QStringLiteral("Play or pause (P). Space closes the preview."));
    time_ = plainLabel(QStringLiteral("0:00 / —"), this);
    controls->addWidget(time_);
    controls->addStretch();
    mute_ = button(QStringLiteral("Mute"), QStyle::SP_MediaVolume);
    mute_->setCheckable(true);
    volume_ = new ClickPositionSlider(this);
    volume_->setAccessibleName(QStringLiteral("Volume"));
    volume_->setRange(0, 100);
    volume_->setValue(mediaVolume);
    volume_->setMaximumWidth(100);
    controls->addWidget(volume_);
    layout->addLayout(controls);
    connect(play_, &QPushButton::clicked, session_.get(), &MediaSession::togglePlayback);
    connect(mute_, &QPushButton::toggled, this, &MediaPreviewWidget::setMuted);
    connect(volume_, &QSlider::valueChanged, this, [this](int value) { mediaVolume = value; session_->setVolume(value); });
    const auto seek = [this] {
        if (auto *player = session_->player()) session_->seek(qRound64(player->duration() * (seek_->value() / 100000.0)));
    };
    connect(seek_, &QSlider::sliderReleased, this, seek);
    connect(seek_, &QSlider::valueChanged, this, [this, seek] { if (!seek_->isSliderDown()) seek(); });
    auto *pause = new QShortcut(QKeySequence(Qt::Key_P), this);
    pause->setContext(Qt::WidgetWithChildrenShortcut);
    pause->setAutoRepeat(false);
    connect(pause, &QShortcut::activated, session_.get(), &MediaSession::togglePlayback);
    connect(session_.get(), &MediaSession::changed, this, &MediaPreviewWidget::updateControls);
    connect(session_.get(), &MediaSession::metadataChanged, this, &MediaPreviewWidget::updateMetadata);
    connect(session_.get(), &MediaSession::kindChanged, this, [this](bool video) {
        setKind(video);
        emit kindChanged(video);
    });
    connect(session_.get(), &MediaSession::failed, this, &MediaPreviewWidget::failed);
    setKind(videoMode_);
    session_->setMuted(mediaMuted);
    session_->setVolume(mediaVolume);
    updateControls();
    QTimer::singleShot(0, this, [this] { session_->start(data_.resource, video_); });
}
MediaPreviewWidget::~MediaPreviewWidget() { stop(); }
QSize MediaPreviewWidget::sizeHint() const { return videoMode_ ? QSize(800, 530) : QSize(460, 390); }
void MediaPreviewWidget::setKind(bool video)
{
    videoMode_ = video;
    data_.kind = video ? PreviewKind::Video : PreviewKind::Audio;
    const auto prefix = video ? QStringLiteral("video") : QStringLiteral("audio");
    setObjectName(prefix + QStringLiteral("Preview"));
    status_->setObjectName(prefix + QStringLiteral("Status"));
    play_->setObjectName(prefix + QStringLiteral("Play"));
    mute_->setObjectName(prefix + QStringLiteral("Mute"));
    seek_->setObjectName(prefix + QStringLiteral("Seek"));
    volume_->setObjectName(prefix + QStringLiteral("Volume"));
    time_->setObjectName(prefix + QStringLiteral("Time"));
    presentations_->setCurrentWidget(video ? static_cast<QWidget *>(video_) : audio_);
    updateGeometry();
}
void MediaPreviewWidget::stop() { session_->stop(); }
void MediaPreviewWidget::setMuted(bool muted)
{
    mediaMuted = muted;
    session_->setMuted(muted);
}
void MediaPreviewWidget::setCallbacks(QObject *context, MediaCallback failure, MediaCallback details, MediaKindCallback kind)
{
    connect(this, &MediaPreviewWidget::failed, context, std::move(failure));
    connect(this, &MediaPreviewWidget::detailsChanged, context, std::move(details));
    connect(this, &MediaPreviewWidget::kindChanged, context, std::move(kind));
}
void MediaPreviewWidget::updateControls()
{
    auto *player = session_->player();
    const bool ready = player && session_->ready();
    const bool playing = ready && player->playbackState() == QMediaPlayer::PlayingState;
    play_->setEnabled(ready);
    play_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    play_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    const bool muted = session_->muted();
    const QSignalBlocker muteBlocker(mute_);
    mute_->setChecked(muted);
    mute_->setText(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
    mute_->setIcon(style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
    status_->setText(session_->status());
    const auto duration = player ? player->duration() : 0;
    seek_->setEnabled(ready && player->isSeekable() && duration > 0);
    if (!seek_->isSliderDown()) {
        const QSignalBlocker blocker(seek_);
        seek_->setValue(duration > 0 ? int(std::clamp(player->position() * 100000.0 / duration, 0.0, 100000.0)) : 0);
    }
    time_->setText(timestamp(player ? player->position() : 0) + QStringLiteral(" / ")
        + (duration > 0 ? timestamp(duration) : QStringLiteral("—")));
    seek_->setAccessibleDescription(time_->text());
}
void MediaPreviewWidget::updateMetadata()
{
    auto *player = session_->player();
    if (!player) return;
    const auto metadata = player->metaData();
    const auto title = metadata.stringValue(QMediaMetaData::Title).left(1024);
    audio_->title->setText(title.isEmpty() ? QFileInfo(data_.resource.path).fileName() : title);
    audio_->title->setToolTip(audio_->title->text());
    audio_->artist->setText(metadata.stringValue(QMediaMetaData::ContributingArtist).left(1024));
    if (audio_->artist->text().isEmpty()) audio_->artist->setText(metadata.stringValue(QMediaMetaData::AlbumArtist).left(1024));
    audio_->album->setText(metadata.stringValue(QMediaMetaData::AlbumTitle).left(1024));
    if (!videoMode_) {
        auto image = metadata.value(QMediaMetaData::CoverArtImage).value<QImage>();
        if (image.isNull()) image = metadata.value(QMediaMetaData::ThumbnailImage).value<QImage>();
        if (!image.isNull()) audio_->cover->setPixmap(QPixmap::fromImage(image.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    QStringList summary, details;
    if (videoMode_) {
        const auto resolution = metadata.value(QMediaMetaData::Resolution).toSize();
        if (!resolution.isEmpty()) summary << QStringLiteral("%1 × %2 px").arg(resolution.width()).arg(resolution.height());
        if (metadata.value(QMediaMetaData::VideoCodec).isValid())
            summary << QMediaFormat::videoCodecName(metadata.value(QMediaMetaData::VideoCodec).value<QMediaFormat::VideoCodec>());
    } else {
        if (metadata.value(QMediaMetaData::AudioCodec).isValid())
            summary << QMediaFormat::audioCodecName(metadata.value(QMediaMetaData::AudioCodec).value<QMediaFormat::AudioCodec>());
        const auto bitrate = metadata.value(QMediaMetaData::AudioBitRate).toLongLong();
        if (bitrate > 0) summary << QStringLiteral("%1 kb/s").arg(bitrate / 1000);
        details << QStringLiteral("Title: %1").arg(audio_->title->text());
        if (!audio_->artist->text().isEmpty()) details << QStringLiteral("Artist: %1").arg(audio_->artist->text());
        if (!audio_->album->text().isEmpty()) details << QStringLiteral("Album: %1").arg(audio_->album->text());
    }
    if (player->duration() > 0) details << QStringLiteral("Duration: %1").arg(timestamp(player->duration()));
    if (!summary.isEmpty()) details << summary.join(QStringLiteral(" · "));
    emit detailsChanged(summary.isEmpty() ? (videoMode_ ? QStringLiteral("Video") : QStringLiteral("Audio"))
        : summary.join(QStringLiteral(" · ")), details.join(QChar('\n')));
}
}

#pragma once
#include "AudioBackend.h"
#include <QWidget>
#include <QTimer>
#include <memory>

class QAudioOutput;
class QMediaPlayer;
class QLabel;
class QPushButton;
class QSlider;
namespace dzurkel {
class StreamingMediaSource;
class AudioPreviewWidget : public QWidget, public AudioControls {
    Q_OBJECT
    Q_INTERFACES(dzurkel::AudioControls)
public:
    explicit AudioPreviewWidget(const PreviewData &data, QWidget *parent = nullptr);
    ~AudioPreviewWidget() override;
    QSize sizeHint() const override { return QSize(460, 390); }
    void stop() override;
    void setCallbacks(QObject *context, AudioCallback failure, AudioCallback details) override;
    void setMuted(bool muted) override;
    void togglePlayback();
signals:
    void failed(const QString &message, const QString &details);
    void detailsChanged(const QString &summary, const QString &details);
private:
    void start();
    void fail(const QString &message, const QString &details = {});
    void updateMetadata();
    void updatePosition();
    void updatePlayback();
    PreviewData data_;
    std::unique_ptr<StreamingMediaSource> source_;
    std::unique_ptr<QAudioOutput> output_;
    std::unique_ptr<QMediaPlayer> player_;
    QTimer loadingTimer_;
    QLabel *cover_, *title_, *artist_, *album_, *status_, *time_;
    QPushButton *play_, *mute_;
    QSlider *seek_, *volume_;
    QString decoderDetails_;
    bool stopped_ = false;
    bool playbackStarted_ = false;
    bool muted_ = false;
};
}

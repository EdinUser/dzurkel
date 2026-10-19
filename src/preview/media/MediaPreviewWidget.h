#pragma once
#include "MediaBackend.h"
#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;
class QVideoWidget;
namespace dzurkel {
class MediaSession;
class AudioPresentationWidget;
class MediaPreviewWidget : public QWidget, public MediaControls {
    Q_OBJECT
    Q_INTERFACES(dzurkel::MediaControls)
public:
    explicit MediaPreviewWidget(const PreviewData &data, QWidget *parent = nullptr);
    ~MediaPreviewWidget() override;
    QSize sizeHint() const override;
    void stop() override;
    void setMuted(bool muted) override;
    void setCallbacks(QObject *context, MediaCallback failure, MediaCallback details,
                      MediaKindCallback kind) override;
signals:
    void failed(const QString &message, const QString &details);
    void detailsChanged(const QString &summary, const QString &details);
    void kindChanged(bool video);
private:
    void updateControls();
    void updateMetadata();
    void setKind(bool video);
    PreviewData data_;
    std::unique_ptr<MediaSession> session_;
    QStackedWidget *presentations_;
    AudioPresentationWidget *audio_;
    QVideoWidget *video_;
    QLabel *status_, *time_;
    QPushButton *play_, *mute_;
    QSlider *seek_, *volume_;
    bool videoMode_;
};
}

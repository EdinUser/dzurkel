#pragma once
#include "preview/PreviewProvider.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
namespace dzurkel {
class StreamingMediaSource;

// One owner for source, decoder and cancellation. Presentation widgets never
// retain a source or decoder after stop(), even while helper reaping is pending.
class MediaSession : public QObject {
    Q_OBJECT
public:
    explicit MediaSession(QObject *parent = nullptr);
    ~MediaSession() override;
    void start(const FileResource &resource, QVideoWidget *video);
    void stop();
    void setMuted(bool muted);
    void setVolume(int volume);
    void togglePlayback();
    void seek(qint64 position);
    QMediaPlayer *player() const { return player_.get(); }
    bool ready() const { return ready_; }
    bool isVideo() const { return video_; }
    bool muted() const { return muted_; }
    QString status() const { return status_; }
signals:
    void changed();
    void metadataChanged();
    void kindChanged(bool video);
    void failed(const QString &message, const QString &details);
private:
    void fail(const QString &message, const QString &details = {});
    void updateStatus();
    std::unique_ptr<StreamingMediaSource> source_;
    std::unique_ptr<QAudioOutput> output_;
    std::unique_ptr<QMediaPlayer> player_;
    QTimer watchdog_;
    QElapsedTimer progress_, phase_;
    QString status_ = QStringLiteral("Opening media…");
    qint64 seekTarget_ = -1;
    int volume_ = 70;
    bool stopped_ = false, loaded_ = false, ready_ = false, video_ = false, muted_ = true;
};
}

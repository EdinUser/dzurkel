#pragma once
#include "preview/PreviewProvider.h"
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

class QProcess;
namespace dzurkel {
inline constexpr qint64 ImagePixelBudget = 64 * 1024 * 1024;
inline constexpr int ImageProfileBudget = 1024 * 1024;
inline constexpr int ImageHeaderBudget = 2 * 1024 * 1024;
int serveImage(const QString &path, const QString &mime);

// One-shot GUI-thread client; source access and Qt image plugins run in a child.
class ImageDecodeJob final : public QObject {
    Q_OBJECT
public:
    explicit ImageDecodeJob(const PreviewData &data, QObject *parent = nullptr,
                            const QString &program = {}, int timeoutMs = 30000);
    ~ImageDecodeJob() override;
    void stop();
    static int runningHelpers();
signals:
    void metadataReady(const QSize &size);
    void completed(const dzurkel::PreviewData &data);
private:
    void read();
    bool header();
    void finish(const QString &error = {});
    PreviewData result_;
    QPointer<QProcess> process_;
    QTimer timeout_;
    QByteArray headerBytes_;
    QList<QImage> frames_;
    int frame_ = 0;
    qint64 received_ = 0;
    bool metadataSeen_ = false;
    bool body_ = false;
    bool stopped_ = false;
};
}

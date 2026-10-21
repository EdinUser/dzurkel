#pragma once
#include "resources/FileResource.h"
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QRectF>
#include <optional>

class QProcess;
namespace dzurkel {
struct PdfPageLink {
    QRectF rectangle;
    QUrl url;
    int page = -1;
    QPointF location;
};
// GUI-thread pipe client. Owns no QPdfDocument or source file descriptor.
class PdfSession final : public QObject {
    Q_OBJECT
public:
    explicit PdfSession(const FileResource &resource, const QString &program = {}, int timeoutMs = 30000);
    ~PdfSession() override;
    void request(int page, int physicalWidth, qreal pixelsPerPoint = 0);
    void stop();
    bool busy() const { return active_.has_value(); }
    int pageCount() const { return pages_; }
    const QList<PdfPageLink> &links() const { return links_; }
    static int runningHelpers();
signals:
    void frameReady(int page, const QSizeF &points, const QImage &image);
    void failed(const QString &message);
private:
    struct Request { qint64 id; int page; int width; qreal scale; };
    void send();
    void read();
    void fail(const QString &message);
    QPointer<QProcess> process_;
    QTimer timeout_;
    QByteArray headerBytes_;
    QJsonObject header_;
    QImage incoming_;
    qint64 received_ = 0;
    qint64 nextId_ = 0;
    int pages_ = 0;
    QList<PdfPageLink> links_;
    bool stopped_ = false;
    std::optional<Request> active_;
    std::optional<Request> pending_;
};
}

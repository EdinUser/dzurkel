#pragma once
#include "preview/PreviewProvider.h"
#include <QPointer>
#include <QTimer>

class QProcess;
class QJsonObject;
class QJsonArray;
namespace dzurkel {
inline constexpr qint64 StructuredRecordLimit = 128 * 1024;
inline constexpr qint64 StructuredTransferLimit = 64 * 1024 * 1024;
int serveDocument(const QString &path, const QString &reader);

// Source access and ZIP/XML parsing run in a child. Only a complete, validated
// bounded table or document is published; incomplete protocol results never
// reach the view. XLSX and all document readers share this process lifecycle.
class StructuredDecodeJob final : public QObject {
    Q_OBJECT
public:
    explicit StructuredDecodeJob(const PreviewData &data, QObject *parent = nullptr,
                           const QString &program = {}, int timeoutMs = 30000);
    ~StructuredDecodeJob() override;
    void stop();
    static int runningHelpers();
    static bool decodeDocumentRecords(const QJsonArray &records, PreviewData &data);
signals:
    void completed(const dzurkel::PreviewData &data);
private:
    StructuredDecodeJob() = default;
    void read();
    bool record();
    bool documentRecord(const QJsonObject &object);
    void finish(const QString &error = {});
    PreviewData result_;
    std::shared_ptr<TabularDocument> document_;
    std::shared_ptr<FormattedDocument> formatted_;
    QImage pendingImage_;
    QString pendingImageId_;
    qint64 imageOffset_ = 0;
    qint64 imageBytes_ = 0;
    qint64 imagePixels_ = 0;
    QByteArray pendingProfile_;
    qint64 profileSize_ = 0;
    int sections_ = 0;
    bool documentMode_ = false;
    bool documentStarted_ = false;
    bool textFallback_ = false;
    QPointer<QProcess> process_;
    QTimer timeout_;
    QByteArray line_;
    qint64 received_ = 0;
    int cells_ = 0;
    int characters_ = 0;
    bool stopped_ = false;
};
}

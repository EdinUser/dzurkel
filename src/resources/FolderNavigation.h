#pragma once
#include "FileResource.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace dzurkel {
class FolderNavigation : public QObject {
    Q_OBJECT
public:
    explicit FolderNavigation(QObject *parent = nullptr, int timeoutMs = 30000);
    ~FolderNavigation() override;
    void start(const FileResource &resource, int direction = 0);
    void cancel();
    static constexpr int EntryLimit = 10000;
    static constexpr int ResponseLimit = 2 * 1024 * 1024;
signals:
    void ready(const QStringList &urls, int index, const QString &message, const QString &selectedUrl);
private:
    void finish(const QString &error = {});
    QProcess *process_ = nullptr;
    QTimer timer_;
    QByteArray output_;
    FileResource resource_;
    int direction_ = 0;
};
int enumerateResourceFolder(const QString &url, bool allowMissing);
}

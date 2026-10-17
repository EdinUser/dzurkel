#pragma once
#include "FileResource.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace dzurkel {
// Filesystem access is isolated from the GUI and from decoder threads. The
// helper only reads source files; its bounded snapshot lives with FileResource.
class ResourcePreparation : public QObject {
    Q_OBJECT
public:
    explicit ResourcePreparation(QObject *parent = nullptr, int timeoutMs = TimeoutMs);
    ~ResourcePreparation() override;
    void start(const FileResource &resource);
    void cancel();
    static constexpr int TimeoutMs = 30000;
signals:
    void ready(const dzurkel::FileResource &resource);
private:
    void finish(const QString &error = {});
    FileResource resource_;
    QProcess *process_ = nullptr;
    QTimer timer_;
    bool finished_ = false;
};

// Internal command-line entry point; no QApplication or preview window needed.
int prepareResourceSnapshot(const QString &url, const QString &directory);
} // namespace dzurkel

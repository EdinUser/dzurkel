#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>

namespace dzurkel {
// Wayland clients cannot position toplevels. Keep compositor-specific placement
// here, outside the preview providers, and never remap or activate a window.
class WindowPlacement : public QObject {
public:
    explicit WindowPlacement(QObject *parent);
    ~WindowPlacement() override;
    bool available() const { return available_; }
    void request(const QString &title, const QSize &clientSize,
                 const QSize &imageSize = {}, const QSize &chrome = {});
    void cancel();
private:
    struct Request {
        QString title;
        QSize clientSize, imageSize, chrome;
        quint64 revision;
    };
    void start();
    std::optional<Request> pending_;
    std::shared_ptr<std::atomic<quint64>> revision_ = std::make_shared<std::atomic<quint64>>(0);
    bool running_ = false;
    bool warned_ = false;
    bool available_ = false;
};
}

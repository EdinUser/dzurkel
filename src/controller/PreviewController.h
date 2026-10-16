#pragma once
#include "ui/PreviewWindow.h"
#include "resources/FileResource.h"
#include <QObject>

namespace dzurkel {
class PreviewController : public QObject {
public:
    explicit PreviewController(bool diagnostics = false);
    bool show(const QString &url, QString &error, ResourceError *failure = nullptr);
    void hide();
    bool toggle(const QString &url, QString &error);
    bool isVisible() const;
    QString fileUrl() const;
    const PreviewWindow *window() const { return &window_; }
private:
    PreviewWindow window_;
    QString fileUrl_;
};
} // namespace dzurkel

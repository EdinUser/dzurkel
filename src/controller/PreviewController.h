#pragma once
#include "ui/PreviewWindow.h"
#include "preview/PreviewProvider.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <functional>
#include <optional>

namespace dzurkel {
class ResourcePreparation;
class FolderNavigation;
class PreviewController : public QObject {
public:
    using Loader = std::function<PreviewData(const FileResource &, const QSize &, const Cancellation &)>;
    explicit PreviewController(bool diagnostics = false, Loader loader = {});
    ~PreviewController() override;
    bool show(const QString &url, QString &error, ResourceError *failure = nullptr,
              const QString &activationToken = {});
    void hide();
    void showSettings(const QString &activationToken = {}) { window_.showSettings(activationToken); }
    bool toggle(const QString &url, QString &error);
    bool isVisible() const;
    bool loading() const { return loading_; }
    QString previewKind() const;
    QString fileUrl() const;
    const PreviewWindow *window() const { return &window_; }
private:
    struct Job { FileResource resource; QSize target; quint64 generation; QString activationToken; };
    void startPending();
    void cancelAndClear();
    // Empty document keeps Qt's PDF runtime initialized after the first PDF.
    // Declare before the window so its runtime outlives any displayed document.
    std::unique_ptr<QPdfDocument> pdfRuntime_;
    PreviewWindow window_;
    FolderNavigation *navigation_;
    QThreadPool pool_;
    QPointer<QFutureWatcher<PreviewData>> watcher_;
    QPointer<ResourcePreparation> preparation_;
    Loader loader_;
    Cancellation cancellation_;
    std::optional<Job> pending_;
    quint64 generation_ = 0;
    QString fileUrl_;
    QString navigationParent_;
    QStringList navigationUrls_;
    QString navigationNotice_;
    PreviewKind kind_ = PreviewKind::Cancelled;
    bool loading_ = false;
    bool presentationPending_ = false;
};
} // namespace dzurkel

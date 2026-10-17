#include "PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/providers/FileInfoProvider.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "resources/ResourcePreparation.h"
#include "resources/FolderNavigation.h"
#include <QImageReader>
#include <QLabel>
#include <QLayout>
#include <QPdfDocument>
#include <QTimer>
#include <QSet>
#include <QtConcurrentRun>

namespace dzurkel {
PreviewController::PreviewController(bool diagnostics, Loader loader)
    : window_(diagnostics), loader_(std::move(loader))
{
    navigation_ = new FolderNavigation(this);
    connect(navigation_, &FolderNavigation::ready, this,
        [this](const QStringList &urls, int index, const QString &message, const QString &selected) {
            if (!isVisible()) return;
            if (index < 0) {
                window_.setNavigation(false, false, message);
                return;
            }
            const QSet<QString> listed(urls.begin(), urls.end());
            for (const auto &old : navigationUrls_) {
                if (!listed.contains(old) && !QUrl(old).fileName().startsWith('.')) {
                    navigationNotice_ = QStringLiteral("Folder changed; unavailable entries were skipped.");
                    break;
                }
            }
            navigationUrls_ = urls;
            if (!message.isEmpty()) navigationNotice_ = message;
            const auto position = QStringLiteral("%1 / %2").arg(index + 1).arg(urls.size());
            window_.setNavigation(index > 0, index + 1 < urls.size(),
                navigationNotice_.isEmpty() ? position : position + QStringLiteral(" · ") + navigationNotice_);
            if (!selected.isEmpty()) {
                QString error;
                if (!show(selected, error)) window_.setNavigation(false, false, error);
            }
        });
    connect(&window_, &PreviewWindow::navigateRequested, this, [this](int direction) {
        FileResource resource;
        QString error;
        if (!isVisible() || !parseFileResource(fileUrl_, resource, error)) return;
        window_.setNavigation(false, false, QStringLiteral("Checking folder…"));
        navigation_->start(resource, direction);
    });
    pool_.setMaxThreadCount(1);
    QImageReader::setAllocationLimit(ImagePreviewProvider::AllocationLimitMiB);
    connect(&window_, &PreviewWindow::dismissed, this, &PreviewController::cancelAndClear);
}
PreviewController::~PreviewController()
{
    navigation_->cancel();
    if (preparation_) delete preparation_;
    if (cancellation_) cancellation_->store(true);
    if (watcher_) disconnect(watcher_, nullptr, this, nullptr);
    pool_.waitForDone();
}
bool PreviewController::show(const QString &url, QString &error, ResourceError *failure,
                             const QString &activationToken)
{
    FileResource resource;
    if (!parseFileResource(url, resource, error, failure)) return false;
    if (preparation_) {
        delete preparation_;
        preparation_ = nullptr;
    }
    if (cancellation_) cancellation_->store(true);
    const auto parent = resource.url.adjusted(QUrl::RemoveFilename).toString(QUrl::FullyEncoded);
    if (parent != navigationParent_) {
        navigationParent_ = parent;
        navigationUrls_.clear();
        navigationNotice_.clear();
    }
    navigation_->cancel();
    window_.setArrowNavigation(false);
    window_.setNavigation(false, false, QStringLiteral("Checking folder…"));
    ++generation_;
    fileUrl_ = resource.url.toString(QUrl::FullyEncoded);
    loading_ = true;
    kind_ = PreviewKind::Cancelled;
    window_.setResource(resource);
    auto *label = new QLabel(QStringLiteral("Loading preview…"));
    label->setAlignment(Qt::AlignCenter);
    window_.setContent(label);
    window_.setStatus({});
    // Wayland placement uses the initial mapped size. Keep the very first
    // surface hidden until provider selection so information maps compactly.
    presentationPending_ = !window_.hasBeenShown() && !loader_;
    if (presentationPending_) window_.preparePresentation();
    else window_.present(activationToken);
    window_.layout()->activate();
    pending_ = Job{resource, window_.previewSize(), generation_, activationToken};
    // Preserve compact first-map placement for fast information results, but
    // show a normal loading window after 150 ms even when the network is slow.
    if (presentationPending_) {
        const auto generation = generation_;
        QTimer::singleShot(150, this, [this, generation, activationToken] {
            if (generation != generation_ || !presentationPending_) return;
            presentationPending_ = false;
            window_.present(activationToken);
        });
    }
    if (!watcher_) startPending();
    if (!loader_) navigation_->start(resource);
    return true;
}
void PreviewController::startPending()
{
    if (!pending_) return;
    const Job job = *pending_;
    pending_.reset();
    if (!loader_ && job.resource.metadata.isEmpty()) {
        auto *preparation = new ResourcePreparation(this);
        preparation_ = preparation;
        connect(preparation, &ResourcePreparation::ready, this, [this, preparation, job](const FileResource &resource) {
            preparation_ = nullptr;
            preparation->deleteLater();
            if (job.generation != generation_ || !isVisible()) return;
            pending_ = Job{resource, job.target, job.generation, job.activationToken};
            if (!watcher_) startPending();
        });
        preparation->start(job.resource);
        return;
    }
    auto *watcher = new QFutureWatcher<PreviewData>(this);
    watcher_ = watcher;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancellation_;
    connect(watcher, &QFutureWatcher<PreviewData>::finished, this, [this, watcher, job, cancel] {
        const PreviewData data = watcher->result();
        watcher_ = nullptr;
        watcher->deleteLater();
        if (job.generation == generation_ && !cancel->load() && isVisible()
            && data.kind != PreviewKind::Cancelled) {
            kind_ = data.kind;
            loading_ = false;
            // Repeatedly initializing/destroying PDFium is costly and grows
            // allocator retention on Qt 6.11. Keep only its runtime alive;
            // each actual document and backing file still dies on dismissal.
            if (data.kind == PreviewKind::Pdf && !pdfRuntime_)
                pdfRuntime_ = std::make_unique<QPdfDocument>();
            window_.setContent(PreviewRegistry::createWidget(data, nullptr));
            window_.setStatus(data.kind == PreviewKind::Information ? QString()
                : (data.message.isEmpty() ? data.detail : data.message));
            window_.setArrowNavigation(data.kind == PreviewKind::Image || data.kind == PreviewKind::Information);
            window_.setInformationMode(data.kind == PreviewKind::Information);
            if (presentationPending_) {
                presentationPending_ = false;
                window_.present(job.activationToken);
            }
        }
        if (pending_ && isVisible()) startPending();
    });
    const auto loader = loader_;
    watcher->setFuture(QtConcurrent::run(&pool_, [this, loader, job, cancel] {
        try {
            if (loader) return loader(job.resource, job.target, cancel);
            return PreviewRegistry::load(job.resource, job.target, cancel, [this, job] {
                // Selection precedes expensive decoding: supported files still
                // get their normal loading window without waiting for content.
                QMetaObject::invokeMethod(this, [this, job] {
                    if (job.generation != generation_ || !presentationPending_) return;
                    presentationPending_ = false;
                    window_.present(job.activationToken);
                }, Qt::QueuedConnection);
            });
        } catch (...) {
            auto data = FileInfoProvider::describe(job.resource);
            data.message = QStringLiteral("Preview loading failed; file information is shown instead.");
            return data;
        }
    }));
}
void PreviewController::cancelAndClear()
{
    navigation_->cancel();
    navigationUrls_.clear();
    navigationNotice_.clear();
    navigationParent_.clear();
    window_.setNavigation(false, false, {});
    window_.setArrowNavigation(false);
    ++generation_;
    if (cancellation_) cancellation_->store(true);
    if (preparation_) {
        delete preparation_;
        preparation_ = nullptr;
    }
    pending_.reset();
    loading_ = false;
    presentationPending_ = false;
    kind_ = PreviewKind::Cancelled;
    const auto dismissedGeneration = generation_;
    // A key event may still target a child editor/view during dismissal.
    QTimer::singleShot(0, this, [this, dismissedGeneration] {
        if (generation_ == dismissedGeneration && !window_.isVisible()) {
            window_.setContent(nullptr);
            window_.setStatus({});
        }
    });
}
void PreviewController::hide()
{
    if (window_.isVisible()) window_.hide();
    else cancelAndClear();
}
bool PreviewController::toggle(const QString &url, QString &error)
{
    if (isVisible()) { hide(); error.clear(); return true; }
    return show(url, error);
}
bool PreviewController::isVisible() const { return presentationPending_ || window_.isVisible(); }
QString PreviewController::previewKind() const
{
    return loading_ ? QStringLiteral("loading") : PreviewRegistry::kindName(kind_);
}
QString PreviewController::fileUrl() const { return fileUrl_; }
} // namespace dzurkel

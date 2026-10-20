#include "PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/providers/FileInfoProvider.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "preview/media/MediaBackend.h"
#include "resources/ResourcePreparation.h"
#include "resources/FolderNavigation.h"
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QLayout>
#include <QPdfDocument>
#include <QTimer>
#include <QSet>
#include <QtConcurrentRun>

namespace dzurkel {
PreviewController::PreviewController(bool diagnostics, Loader loader, FolderSizeBackend *statistics)
    : window_(diagnostics), statisticsBackend_(statistics), loader_(std::move(loader))
{
    navigation_ = new FolderNavigation(this);
    connect(navigation_, &FolderNavigation::ready, this,
        [this](const QStringList &urls, int index, const QString &message, const QString &selected) {
            if (!isVisible() || externalNavigation_ || !chooserOwner_.isEmpty()) return;
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
                if (!show(selected, error, nullptr, {}, false)) window_.setNavigation(false, false, error);
            }
        });
    connect(&window_, &PreviewWindow::navigateRequested, this, [this](int direction) {
        if (!chooserOwner_.isEmpty()) return;
        if (externalNavigation_) {
            if (isVisible()) emit externalNavigationRequested(direction < 0 ? 4u : 5u);
            return;
        }
        FileResource resource;
        QString error;
        if (!isVisible() || !parseFileResource(fileUrl_, resource, error)) return;
        window_.setNavigation(false, false, QStringLiteral("Checking folder…"));
        navigation_->start(resource, direction);
    });
    connect(&window_, &PreviewWindow::externalNavigationRequested, this, [this](uint direction) {
        if (isVisible() && externalNavigation_) emit externalNavigationRequested(direction);
    });
    pool_.setMaxThreadCount(1);
    QImageReader::setAllocationLimit(ImagePreviewProvider::AllocationLimitMiB);
    connect(&window_, &PreviewWindow::dismissed, this, &PreviewController::cancelAndClear);
    connect(&window_, &PreviewWindow::presentationActivated, this,
        &PreviewController::commitReadyPreview, Qt::QueuedConnection);
}
PreviewController::~PreviewController()
{
    delete folderSizeJob_.data();
    window_.stopMedia();
    navigation_->cancel();
    if (preparation_) delete preparation_;
    if (cancellation_) cancellation_->store(true);
    if (watcher_) disconnect(watcher_, nullptr, this, nullptr);
    pool_.waitForDone();
}
bool PreviewController::show(const QString &url, QString &error, ResourceError *failure,
                             const QString &activationToken, bool requestActivation, bool externalNavigation, const QString &chooserOwner)
{
    FileResource resource;
    if (!parseFileResource(url, resource, error, failure)) return false;
    delete folderSizeJob_.data();
    window_.stopMedia();
    chooserOwner_ = chooserOwner;
    externalNavigation_ = externalNavigation;
    window_.setExternalNavigation(externalNavigation);
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
    window_.setNavigation(externalNavigation, externalNavigation,
        !chooserOwner_.isEmpty() ? QStringLiteral("File chooser")
        : externalNavigation ? QStringLiteral("Files") : QStringLiteral("Checking folder…"));
    ++generation_;
    ready_.reset();
    fileUrl_ = resource.url.toString(QUrl::FullyEncoded);
    loading_ = true;
    kind_ = PreviewKind::Cancelled;
    // GNOME keeps the old content and its identity until the new result is ready.
    if (!window_.usesGnomePresentation() || !window_.isVisible()) window_.setResource(resource);
    if (!window_.isVisible()) {
        auto *label = new QLabel(QStringLiteral("Loading preview…"));
        label->setAlignment(Qt::AlignCenter);
        window_.setContent(label);
    }
    if (!window_.usesGnomePresentation() || !window_.isVisible()) window_.setStatus({});
    // Wayland placement uses the initial mapped size. Keep the very first
    // surface hidden until a fast result determines its natural window size.
    // Slow loads still present a loading window through the timer below.
    presentationPending_ = !window_.isVisible() && !loader_;
    if (!window_.isVisible()) window_.preparePresentation();
    // Explicit Show requests must reactivate an already mapped preview too.
    // Folder navigation preserves focus and never requests it asynchronously.
    if (!presentationPending_ && requestActivation) window_.present(activationToken);
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
    if (!loader_ && !externalNavigation_ && chooserOwner_.isEmpty()) navigation_->start(resource);
    emit stateChanged();
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
            ready_ = ReadyPreview{job, data};
            commitReadyPreview();
        }
        if (pending_ && isVisible()) startPending();
    });
    const auto loader = loader_;
    watcher->setFuture(QtConcurrent::run(&pool_, [this, loader, job, cancel] {
        try {
            if (loader) return loader(job.resource, job.target, cancel);
            return PreviewRegistry::load(job.resource, job.target, cancel, {},
                [this, job, cancel](const QSize &imageSize) {
                    QMetaObject::invokeMethod(this, [this, job, cancel, imageSize] {
                        if (job.generation != generation_ || cancel->load() || !isVisible()) return;
                        if (!window_.usesGnomePresentation()) window_.prepareImage(imageSize);
                    }, Qt::QueuedConnection);
                });
        } catch (...) {
            auto data = FileInfoProvider::describe(job.resource);
            data.message = QStringLiteral("Preview loading failed; file information is shown instead.");
            return data;
        }
    }));
}
void PreviewController::commitReadyPreview()
{
    if (!ready_ || ready_->job.generation != generation_ || !isVisible()
        || !window_.canCommitPreview()) return;
    auto ready = std::move(*ready_);
    ready_.reset();
    auto &data = ready.data;
    if (data.kind == PreviewKind::Audio || data.kind == PreviewKind::Video) {
        const auto availability = mediaAvailability();
        if (!availability.available) {
            data.kind = PreviewKind::Information;
            data.missingLibrary = QStringLiteral("multimedia");
            data.message = QStringLiteral("Media preview needs an optional library. Check Settings for details!");
            data.detail = availability.explanation;
        }
    }
    const bool folder = data.mimeType == QStringLiteral("inode/directory");
    auto *statistics = folder ? (statisticsBackend_ ? statisticsBackend_ : folderSizeBackend()) : nullptr;
    if (folder) {
        navigation_->cancel();
        window_.setNavigation(false, false, QStringLiteral("Folder"));
        data.folderSize.state = statistics ? FolderSizeState::Calculating : FolderSizeState::Unavailable;
        if (!statistics)
            data.folderSize.explanation = QStringLiteral("A compatible desktop folder-size adapter or its libraries are unavailable.");
    }
    kind_ = data.kind;
    loading_ = false;
    // Retain only PDFium's runtime; provider widgets own actual documents.
    if (data.kind == PreviewKind::Pdf && !pdfRuntime_)
        pdfRuntime_ = std::make_unique<QPdfDocument>();
    window_.beginPreviewUpdate();
    if (window_.usesGnomePresentation()) window_.setResource(ready.job.resource);
    window_.setContent(PreviewRegistry::createWidget(data, nullptr));
    QWidget *mediaWidget = window_.findChild<QWidget *>(data.kind == PreviewKind::Video
        ? QStringLiteral("videoPreview") : QStringLiteral("audioPreview"));
    if (auto *media = qobject_cast<MediaControls *>(mediaWidget)) {
        const auto generation = generation_;
        auto mediaData = std::make_shared<PreviewData>(data);
        media->setCallbacks(this,
            [this, generation, job = ready.job, data](const QString &message, const QString &details) {
                QTimer::singleShot(0, this, [this, generation, job, data, message, details] {
                    if (generation != generation_ || !isVisible()) return;
                    auto fallback = data;
                    fallback.kind = PreviewKind::Information;
                    fallback.message = message;
                    fallback.detail = details;
                    ready_ = ReadyPreview{job, fallback};
                    commitReadyPreview();
                });
            },
            [this, generation, mediaData](const QString &summary, const QString &details) {
                if (generation != generation_ || !isVisible()) return;
                mediaData->mediaSummary = summary;
                mediaData->detail = details;
                window_.setPreviewData(*mediaData, false);
            },
            [this, generation, mediaData](bool video) {
                if (generation != generation_ || !isVisible()) return;
                mediaData->kind = video ? PreviewKind::Video : PreviewKind::Audio;
                mediaData->readerId = video ? QStringLiteral("video") : QStringLiteral("audio");
                const bool changed = kind_ != mediaData->kind;
                kind_ = mediaData->kind;
                if (changed) {
                    window_.beginPreviewUpdate();
                    window_.setPreviewData(*mediaData);
                    window_.endPreviewUpdate();
                    emit stateChanged();
                }
            });
    }
    window_.setPreviewData(data);
    window_.setArrowNavigation(!folder && chooserOwner_.isEmpty() && (data.kind == PreviewKind::Image || data.kind == PreviewKind::Information
        || data.kind == PreviewKind::Audio || data.kind == PreviewKind::Video));
    window_.endPreviewUpdate();
    if (presentationPending_) {
        presentationPending_ = false;
        window_.present(ready.job.activationToken);
    }
    if (folder && statistics) {
        const auto generation = generation_;
        auto folderData = std::make_shared<PreviewData>(data);
        const auto update = [this, guard = QPointer<PreviewController>(this), generation, folderData](const FolderSize &size) {
            if (!guard || generation != generation_ || !isVisible()) return;
            folderData->folderSize = size;
            if (auto *label = window_.findChild<QLabel *>(QStringLiteral("folderSize")))
                label->setText(QStringLiteral("Contents size: ") + folderSizeText(size));
            if (auto *label = window_.findChild<QLabel *>(QStringLiteral("folderCounts"))) {
                label->setText(folderCountText(size));
                label->setVisible(!label->text().isEmpty());
            }
            if (auto *label = window_.findChild<QLabel *>(QStringLiteral("folderSizeNotice"))) {
                label->setText(size.explanation);
                label->setVisible(!size.explanation.isEmpty());
            }
            if (auto *stop = window_.findChild<QPushButton *>(QStringLiteral("stopFolderSize")))
                stop->setVisible(size.state == FolderSizeState::Calculating);
            window_.setPreviewData(*folderData, false);
        };
        const auto path = data.resource.metadata.value(QStringLiteral("directoryPath"))
            .toString(data.resource.url.isLocalFile() ? data.resource.path : QString());
        auto active = std::make_shared<bool>(true);
        folderSizeJob_ = statistics->start(path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path),
            [active, update](const FolderSize &size) {
                if (!*active) return;
                update(size);
                if (size.state != FolderSizeState::Calculating) *active = false;
            });
        if (!folderSizeJob_) {
            FolderSize unavailable;
            unavailable.explanation = QStringLiteral("The desktop calculator could not start.");
            update(unavailable);
        }
        if (auto *stop = window_.findChild<QPushButton *>(QStringLiteral("stopFolderSize"))) {
            connect(stop, &QPushButton::clicked, this, [this, generation, folderData, active, update] {
                if (generation != generation_) return;
                *active = false;
                delete folderSizeJob_.data();
                auto size = folderData->folderSize;
                size.state = FolderSizeState::Stopped;
                size.explanation = QStringLiteral("Calculation stopped; the total is incomplete.");
                update(size);
            });
        }
    }
    emit stateChanged();
}
void PreviewController::cancelAndClear()
{
    chooserOwner_.clear();
    delete folderSizeJob_.data();
    window_.stopMedia();
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
    ready_.reset();
    loading_ = false;
    presentationPending_ = false;
    kind_ = PreviewKind::Cancelled;
    emit stateChanged();
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
void PreviewController::releaseExternalNavigation()
{
    if (!externalNavigation_) return;
    externalNavigation_ = false;
    window_.setExternalNavigation(false);
    FileResource resource;
    QString error;
    if (isVisible() && parseFileResource(fileUrl_, resource, error)) navigation_->start(resource);
    emit stateChanged();
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

#include "preview/readers/DocumentReader.h"
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
#include "preview/pdf/PdfSession.h"
#include "preview/image/ImageDecodeJob.h"
#include "preview/StructuredDecodeJob.h"
#include "preview/lazy/LazyCoordinator.h"
#include "preview/table/TableRowBatch.h"
#include "preview/document/DocumentRecords.h"
#include "preview/document/FormattedDocument.h"
#include <QFileInfo>
#include <QStandardPaths>
#include "preview/pdf/PdfView.h"
#include <QTimer>
#include <QSet>
#include <QJsonArray>
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
    stopPdf();
    stopImage();
    stopStructured();
    stopLazy();
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
    stopPdf();
    stopImage();
    stopStructured();
    stopLazy();
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
    // Keep the mapped preview's content, geometry and identity until commit.
    if (!window_.isVisible()) window_.setResource(resource);
    if (!window_.isVisible()) {
        auto *label = new QLabel(QStringLiteral("Loading preview…"));
        label->setObjectName(QStringLiteral("previewLoading"));
        label->setAlignment(Qt::AlignCenter);
        window_.setContent(label);
    }
    if (!window_.isVisible()) window_.setStatus({});
    // Wayland placement uses the initial mapped size. Keep the very first
    // surface hidden until a fast result determines its natural window size.
    // Slow loads still present a loading window through the timer below.
    presentationPending_ = !window_.isVisible();
    if (!window_.isVisible()) window_.preparePresentation();
    // Explicit Show requests must reactivate an already mapped preview too.
    // Folder navigation preserves focus and never requests it asynchronously.
    if (!presentationPending_ && requestActivation) window_.present(activationToken);
    window_.layout()->activate();
    pending_ = Job{resource, window_.previewSize(), generation_, activationToken};
    // Preserve compact first-map placement for fast information results, but
    // show a compact loading window after 150 ms when the source is slow.
    if (presentationPending_) {
        const auto generation = generation_;
        QTimer::singleShot(150, this, [this, generation, activationToken] {
            if (generation != generation_ || !presentationPending_) return;
            presentationPending_ = false;
            window_.prepareLoading();
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
            return PreviewRegistry::load(job.resource, job.target, cancel);
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
    if (data.resource.seekableSource && !data.lazy && (data.kind == PreviewKind::Text
        || (data.kind == PreviewKind::Table && (data.readerId == QStringLiteral("csv") || data.readerId == QStringLiteral("xlsx")) && !data.table)
        || (data.kind == PreviewKind::Document && DocumentReader::supportsPortions(data.readerId) && !data.document))) {
        const bool tsv = data.mimeType == QStringLiteral("text/tab-separated-values")
            || QFileInfo(data.resource.path).suffix().compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0;
        const auto reader = (data.kind == PreviewKind::Document || data.readerId == QStringLiteral("xlsx")) ? data.readerId : data.kind == PreviewKind::Table ? (tsv ? QStringLiteral("csv-tsv") : QStringLiteral("csv")) : QStringLiteral("text");
        auto session = std::make_shared<LazyCoordinator>(data.resource, reader);
        lazyLoading_ = session;
        connect(session.get(), &LazyCoordinator::ready, this, [this, ready](const LazyPortion &portion) mutable {
            if (ready.job.generation != generation_ || !lazyLoading_) return;
            auto session = std::move(lazyLoading_);
            disconnect(session.get(), nullptr, this, nullptr);
            ready.data.lazy = session;
            lazyVisible_ = session;
            ready.data.detail = portion.content.value("encoding").toString();
            ready.data.lazyPrevious = portion.previous;
            ready.data.lazyNext = portion.next;
            ready.data.truncated = !portion.next.isEmpty();
            ready.data.message = portion.next.isEmpty() ? QString() : QStringLiteral("Content loads as you scroll; earlier content can be revisited.");
            TableRowBatch batch;
            if (portion.content.value("text").isString()) {
                ready.data.kind = PreviewKind::Text;
                ready.data.readerId = QStringLiteral("text");
                ready.data.text = portion.content.value("text").toString();
                ready.data.textLineCount = ready.data.text.count('\n') + (!ready.data.text.isEmpty() && !ready.data.text.endsWith('\n'));
                const auto notice = portion.content.value("notice").toString();
                if (!notice.isEmpty()) ready.data.message = notice + QChar(' ') + ready.data.message;
            } else if (ready.data.kind == PreviewKind::Document && decodeDocumentPortion(portion.content, ready.data.document)) {
                ready.data.kind = PreviewKind::Document;
                ready.data.detail = QStringLiteral("Content preview · Formatting simplified");
                if (!ready.data.document->notice.isEmpty()) ready.data.message = ready.data.document->notice + QChar(' ') + ready.data.message;
            } else if (decodeTableRowBatch(portion.content, batch) && !batch.firstRow) {
                auto table = std::make_shared<TabularDocument>();
                table->sheets.append(*batch.sheet);
                const auto sheets = portion.content.value("sheets").toArray();
                const auto sheetTokens = portion.content.value("sheetTokens").toArray();
                bool validSheets = sheets.size() <= 32 && (sheets.isEmpty() || sheets.first().toString() == batch.sheet->name);
                if (ready.data.readerId == QStringLiteral("xlsx")) {
                    validSheets = validSheets && !sheets.isEmpty() && sheetTokens.size() == sheets.size();
                    for (int i = 0; i < qMin(32, int(sheetTokens.size())); ++i) {
                        const auto value = sheetTokens.at(i);
                        const auto token = value.toString();
                        if (token.isEmpty() || token.size() > 1024) validSheets = false;
                        ready.data.lazySheetTokens.append(token);
                    }
                }
                for (int i = 1; i < qMin(32, int(sheets.size())); ++i) {
                    const auto name = sheets.at(i).toString();
                    if (name.isEmpty() || name.size() > 255) { validSheets = false; break; }
                    TabularSheet sheet;
                    sheet.name = name;
                    table->sheets.append(sheet);
                }
                const auto notice = portion.content.value("notice").toString();
                if (!notice.isEmpty()) ready.data.message = notice + QChar(' ') + ready.data.message;
                table->partial = ready.data.truncated;
                table->notice = ready.data.message;
                ready.data.table = table;
                if (!validSheets) {
                    session->stop();
                    ready.data.kind = PreviewKind::Information;
                    ready.data.message = QStringLiteral("Invalid worksheet list.");
                }
                if (ready.data.readerId == QStringLiteral("csv"))
                    ready.data.detail += QStringLiteral(" · %1-separated · Values shown as text").arg(portion.content.value("delimiter").toString());
            } else {
                session->stop();
                ready.data.kind = PreviewKind::Information;
                ready.data.message = QStringLiteral("Invalid initial content response.");
            }
            ready_ = std::move(ready);
            commitReadyPreview();
        });
        connect(session.get(), &LazyCoordinator::failed, this, [this, ready](const QString &error) mutable {
            if (ready.job.generation != generation_ || !lazyLoading_) return;
            auto session = std::move(lazyLoading_);
            disconnect(session.get(), nullptr, this, nullptr);
            QTimer::singleShot(0, this, [session] {});
            ready.data.kind = PreviewKind::Information;
            ready.data.message = error;
            if (ready.data.readerId == QLatin1String("doc") && QStandardPaths::findExecutable(QStringLiteral("antiword")).isEmpty()) {
                ready.data.missingLibrary = QStringLiteral("antiword");
                ready.data.message = QStringLiteral("Legacy DOC preview needs antiword. Check Settings for details!");
            }
            ready_ = std::move(ready);
            commitReadyPreview();
        });
        session->request();
        return;
    }
    if ((data.kind == PreviewKind::Table && data.readerId == QStringLiteral("xlsx") && !data.table)
        || (data.kind == PreviewKind::Document && !data.document)) {
        auto *job = new StructuredDecodeJob(data, this);
        structuredLoading_ = job;
        connect(job, &StructuredDecodeJob::completed, this, [this, job, request = ready.job](const PreviewData &decoded) {
            structuredLoading_ = nullptr;
            job->deleteLater();
            if (request.generation != generation_ || !isVisible()) return;
            ready_ = ReadyPreview{request, decoded};
            commitReadyPreview();
        });
        return;
    }
    if (data.kind == PreviewKind::Image && data.image.isNull()) {
        auto *job = new ImageDecodeJob(data, this);
        imageLoading_ = job;
        connect(job, &ImageDecodeJob::completed, this, [this, job, request = ready.job](const PreviewData &decoded) {
            imageLoading_ = nullptr;
            job->deleteLater();
            if (request.generation != generation_ || !isVisible()) return;
            ready_ = ReadyPreview{request, decoded};
            commitReadyPreview();
        });
        return;
    }
    if (data.kind == PreviewKind::Pdf && !data.pdfSession) {
        auto session = std::make_shared<PdfSession>(data.resource);
        pdfLoading_ = session;
        // Do not capture the session in its own callbacks: the loading owner
        // is cleared on replacement, cancellation, or the first valid frame.
        connect(session.get(), &PdfSession::frameReady, this,
            [this, ready](int, const QSizeF &points, const QImage &image) mutable {
                if (ready.job.generation != generation_ || !pdfLoading_) return;
                auto session = std::move(pdfLoading_);
                disconnect(session.get(), nullptr, this, nullptr);
                ready.data.pdfSession = session;
                ready.data.pdfPageCount = session->pageCount();
                ready.data.pdfFirstPageSize = points;
                ready.data.detail = QStringLiteral("%1 page(s) — pages load on demand").arg(session->pageCount());
                ready.data.image = image;
                ready_ = std::move(ready);
                commitReadyPreview();
            });
        connect(session.get(), &PdfSession::failed, this, [this, ready](const QString &message) mutable {
            if (ready.job.generation != generation_ || !pdfLoading_) return;
            // Defer destruction until the sender has returned from its signal.
            auto session = std::move(pdfLoading_);
            disconnect(session.get(), nullptr, this, nullptr);
            QTimer::singleShot(0, this, [session] {});
            ready.data.kind = PreviewKind::Information;
            ready.data.message = message;
            ready_ = std::move(ready);
            commitReadyPreview();
        });
        session->request(0, std::max(1, qRound(ready.job.target.width() * window_.devicePixelRatioF())));
        return;
    }
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
    window_.beginPreviewUpdate();
    window_.setResource(ready.job.resource);
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
    if (auto *geometry = qobject_cast<MediaGeometry *>(mediaWidget)) {
        const auto generation = generation_;
        geometry->setVideoSizeCallback(this, [this, generation](const QSize &size, QWidget *surface) {
            if (generation != generation_ || !isVisible() || kind_ != PreviewKind::Video) return;
            window_.beginPreviewUpdate();
            window_.prepareVideo(size, surface);
            window_.endPreviewUpdate();
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
void PreviewController::stopImage()
{
    auto *job = imageLoading_.data();
    if (!job) return;
    imageLoading_ = nullptr;
    disconnect(job, nullptr, this, nullptr);
    job->stop();
    job->deleteLater();
}
void PreviewController::stopStructured()
{
    auto *job = structuredLoading_.data();
    if (!job) return;
    structuredLoading_ = nullptr;
    disconnect(job, nullptr, this, nullptr);
    job->stop();
    job->deleteLater();
}
void PreviewController::stopPdf()
{
    if (pdfLoading_) {
        disconnect(pdfLoading_.get(), nullptr, this, nullptr);
        pdfLoading_->stop();
        pdfLoading_.reset();
    }
    if (auto *view = window_.findChild<PdfView *>()) view->stop();
}
void PreviewController::stopLazy()
{
    if (lazyLoading_) {
        disconnect(lazyLoading_.get(), nullptr, this, nullptr);
        lazyLoading_->stop();
        lazyLoading_.reset();
    }
    if (auto session = lazyVisible_.lock()) session->stop();
    lazyVisible_.reset();
}
void PreviewController::cancelAndClear()
{
    chooserOwner_.clear();
    delete folderSizeJob_.data();
    window_.stopMedia();
    stopPdf();
    stopImage();
    stopStructured();
    stopLazy();
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

#include "FolderSizeBackend.h"
#include <KIO/DirectorySizeJob>
#include <KJob>
#include <QPointer>
#include <QTimer>

namespace dzurkel {
class FolderSizeRequest final : public QObject {
public:
    FolderSizeRequest(const QUrl &url, FolderSizeCallback callback)
        : callback_(std::move(callback))
    {
        // Use the already-resolved local/mounted path: no second remote
        // authentication route, and no change to the core URL admission policy.
        QTimer::singleShot(0, this, [this, url] {
            if (!url.isLocalFile()) {
                result_.explanation = QStringLiteral("Folder size needs a resolved local or mounted path.");
                callback_(result_);
                return;
            }
            job_ = KIO::directorySize(url);
            job_->setUiDelegate(nullptr);
            result_.state = FolderSizeState::Calculating;
            connect(job_, &KJob::result, this, [this](KJob *job) {
                sample();
                poll_.stop();
                deadline_.stop();
                result_.state = job->error() ? FolderSizeState::Partial : FolderSizeState::Complete;
                result_.explanation = job->error() ? job->errorString() : QString();
                callback_(result_);
            });
            connect(&poll_, &QTimer::timeout, this, [this] { sample(); callback_(result_); });
            connect(&deadline_, &QTimer::timeout, this, [this] {
                sample();
                poll_.stop();
                result_.state = FolderSizeState::Stopped;
                result_.explanation = QStringLiteral("Calculation stopped after 30 seconds; the total is incomplete.");
                if (job_) job_->kill(KJob::Quietly);
                callback_(result_);
            });
            poll_.start(250);
            deadline_.setSingleShot(true);
            deadline_.start(30000);
        });
    }
    ~FolderSizeRequest() override
    {
        if (job_) {
            disconnect(job_, nullptr, this, nullptr);
            job_->kill(KJob::Quietly);
            delete job_.data();
        }
    }
private:
    void sample()
    {
        if (!job_) return;
        // Require the 6.30 ABI at load time too: an older runtime must fail
        // optional loading instead of silently including directory metadata.
        (void)job_->totalSizeOnDisk();
        result_.bytes = job_->totalSize();
        result_.files = job_->totalFiles();
        result_.folders = job_->totalSubdirs();
    }
    FolderSizeCallback callback_;
    FolderSize result_;
    QPointer<KIO::DirectorySizeJob> job_;
    QTimer poll_;
    QTimer deadline_;
};
class KdeFolderSizeBackend final : public QObject, public FolderSizeBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_FOLDER_SIZE_IID)
    Q_INTERFACES(dzurkel::FolderSizeBackend)
public:
    QObject *start(const QUrl &url, FolderSizeCallback callback) override
    {
        return new FolderSizeRequest(url, std::move(callback));
    }
};
}
#include "KdeFolderSizeBackend.moc"

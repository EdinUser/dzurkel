// Keep GLib's declarations ahead of Qt's `signals` macro.
#include <gio/gio.h>
#include "FolderSizeBackend.h"
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace dzurkel {
namespace {
std::atomic_int activeMeasurements = 0;
struct Measurement {
    GCancellable *cancel = g_cancellable_new();
    std::mutex mutex;
    FolderSize result{FolderSizeState::Calculating, 0, 0, 0, {}};
    bool finished = false;
    ~Measurement() { g_object_unref(cancel); }
};
void progress(gboolean reporting, guint64 bytes, guint64 dirs, guint64 files, gpointer data)
{
    if (!reporting) return;
    auto &measurement = *static_cast<Measurement *>(data);
    std::lock_guard lock(measurement.mutex);
    measurement.result.bytes = bytes;
    measurement.result.files = files;
    measurement.result.folders = dirs ? dirs - 1 : 0; // GIO includes the selected root.
}
class GioFolderSizeRequest final : public QObject {
public:
    GioFolderSizeRequest(const QUrl &url, FolderSizeCallback callback)
        : callback_(std::move(callback)), measurement_(std::make_shared<Measurement>())
    {
        QTimer::singleShot(0, this, [this, url] {
            if (glib_check_version(2, 78, 0)) {
                callback_({FolderSizeState::Unavailable, 0, 0, 0,
                    QStringLiteral("Contents size requires GLib/GIO 2.78 or newer.")});
                return;
            }
            if (!url.isLocalFile()) {
                callback_({FolderSizeState::Unavailable, 0, 0, 0,
                    QStringLiteral("Folder size needs a resolved local or mounted path.")});
                return;
            }
            // Cancellation is cooperative: never block the UI waiting on a slow
            // filesystem, but bound outstanding desktop calls across replacements.
            if (activeMeasurements.fetch_add(1) >= 2) {
                --activeMeasurements;
                callback_({FolderSizeState::Unavailable, 0, 0, 0,
                    QStringLiteral("Previous folder calculations are still stopping. Reopen to retry.")});
                return;
            }
            const auto path = QFile::encodeName(url.toLocalFile());
            try {
                std::thread([measurement = measurement_, path] {
                    // Follow a selected folder alias once, without following
                    // symlink entries encountered inside the measured tree.
                    const auto canonical = QFileInfo(QFile::decodeName(path)).canonicalFilePath();
                    const auto target = canonical.isEmpty() ? path : QFile::encodeName(canonical);
                    auto *file = g_file_new_for_path(target.constData());
                    guint64 bytes = 0, dirs = 0, files = 0;
                    GError *error = nullptr;
                    const auto flags = GFileMeasureFlags(G_FILE_MEASURE_APPARENT_SIZE
                        | G_FILE_MEASURE_REPORT_ANY_ERROR | G_FILE_MEASURE_NO_XDEV);
                    const bool ok = g_file_measure_disk_usage(file, flags, measurement->cancel,
                        progress, measurement.get(), &bytes, &dirs, &files, &error);
                    {
                        std::lock_guard lock(measurement->mutex);
                        if (ok) {
                            measurement->result.bytes = bytes;
                            measurement->result.files = files;
                            measurement->result.folders = dirs ? dirs - 1 : 0;
                        }
                        measurement->result.state = ok ? FolderSizeState::Complete : FolderSizeState::Partial;
                        if (error) measurement->result.explanation = QString::fromUtf8(error->message);
                        measurement->finished = true;
                    }
                    g_clear_error(&error);
                    g_object_unref(file);
                    --activeMeasurements;
                }).detach();
            } catch (const std::system_error &) {
                --activeMeasurements;
                callback_({FolderSizeState::Unavailable, 0, 0, 0,
                    QStringLiteral("Could not start the desktop folder calculation.")});
                return;
            }
            connect(&poll_, &QTimer::timeout, this, [this] {
                FolderSize result;
                bool finished;
                {
                    std::lock_guard lock(measurement_->mutex);
                    result = measurement_->result;
                    finished = measurement_->finished;
                }
                if (finished) { poll_.stop(); deadline_.stop(); }
                callback_(result);
            });
            connect(&deadline_, &QTimer::timeout, this, [this] {
                poll_.stop();
                g_cancellable_cancel(measurement_->cancel);
                FolderSize result;
                {
                    std::lock_guard lock(measurement_->mutex);
                    result = measurement_->result;
                }
                result.state = FolderSizeState::Stopped;
                result.explanation = QStringLiteral("Calculation stopped after 30 seconds; the total is incomplete.");
                callback_(result);
            });
            poll_.start(250);
            deadline_.setSingleShot(true);
            deadline_.start(30000);
        });
    }
    ~GioFolderSizeRequest() override { g_cancellable_cancel(measurement_->cancel); }
private:
    FolderSizeCallback callback_;
    std::shared_ptr<Measurement> measurement_;
    QTimer poll_, deadline_;
};
}
class GioFolderSizeBackend final : public QObject, public FolderSizeBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_FOLDER_SIZE_IID)
    Q_INTERFACES(dzurkel::FolderSizeBackend)
public:
    QObject *start(const QUrl &url, FolderSizeCallback callback) override
    {
        return new GioFolderSizeRequest(url, std::move(callback));
    }
};
}
#include "GioFolderSizeBackend.moc"

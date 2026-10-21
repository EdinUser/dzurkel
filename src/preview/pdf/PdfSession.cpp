#include "PdfSession.h"
#include "PdfProtocol.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace dzurkel {
namespace {
QSet<QProcess *> &helpers()
{
    static QSet<QProcess *> processes;
    return processes;
}
void initializeReaper()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        // A kernel-blocked reader may not acknowledge SIGKILL immediately.
        // QProcess's destructor waits: leave only those wrappers to OS exit.
        for (auto *process : helpers()) {
            if (process->state() == QProcess::NotRunning) continue;
            process->kill();
            process->setParent(nullptr);
            // Release retained staged-file ownership at application shutdown.
            process->disconnect();
        }
    });
}
}
int PdfSession::runningHelpers() { return helpers().size(); }
PdfSession::PdfSession(const FileResource &resource, const QString &program, int timeoutMs)
{
    initializeReaper();
    timeout_.setSingleShot(true);
    timeout_.setInterval(timeoutMs);
    connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("PDF rendering timed out.")); });
    if (helpers().size() >= 2) {
        QTimer::singleShot(0, this, [this] { fail(QStringLiteral("PDF renderer is still stopping. Try again shortly.")); });
        return;
    }
    auto *process = new QProcess(qApp);
    process_ = process;
    helpers().insert(process);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // This reference survives cancellation until the child has really exited.
    connect(process, &QProcess::finished, process, [process, store = resource.backingStore] {
        Q_UNUSED(store);
        helpers().remove(process);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { helpers().remove(process); process->deleteLater(); }
    });
    connect(process, &QProcess::started, this, &PdfSession::send);
    connect(process, &QProcess::readyReadStandardOutput, this, &PdfSession::read);
    connect(process, &QProcess::finished, this, [this] {
        fail(QStringLiteral("PDF renderer stopped before completing the preview."));
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(QStringLiteral("PDF renderer could not start."));
    });
    const auto executable = program.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dzurkel")) : program;
    process->start(executable, {QStringLiteral("--internal-pdf"),
        resource.backingPath.isEmpty() ? resource.path : resource.backingPath});
    timeout_.start();
}
PdfSession::~PdfSession() { stop(); }
void PdfSession::stop()
{
    stopped_ = true;
    timeout_.stop();
    active_.reset();
    pending_.reset();
    incoming_ = {};
    if (process_) {
        disconnect(process_, nullptr, this, nullptr);
        if (process_->state() != QProcess::NotRunning) process_->kill();
        process_ = nullptr;
    }
}
void PdfSession::fail(const QString &message)
{
    if (stopped_) return;
    stop();
    emit failed(message);
}
void PdfSession::request(int page, int width, qreal scale)
{
    if (stopped_) return;
    pending_ = Request{++nextId_, page, std::clamp(width, 1, PdfMaxEdge), scale};
    send();
}
void PdfSession::send()
{
    if (!process_ || stopped_ || active_ || !pending_ || process_->state() != QProcess::Running) return;
    active_ = pending_;
    pending_.reset();
    const auto &request = *active_;
    const QJsonObject json{{"id", request.id}, {"page", request.page}, {"width", request.width}, {"scale", request.scale}};
    process_->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n');
    timeout_.start();
}
void PdfSession::read()
{
    if (!process_ || stopped_ || !active_) return;
    // Yield between bounded copies even for a maximum-sized raster.
    qint64 budget = 512 * 1024;
    while (process_ && process_->bytesAvailable() > 0 && budget > 0) {
        if (incoming_.isNull()) {
            while (process_->bytesAvailable() && !headerBytes_.endsWith('\n')) {
                headerBytes_ += process_->read(1);
                if (headerBytes_.size() > PdfMaxHeader) { fail(QStringLiteral("Invalid PDF renderer header.")); return; }
            }
            if (!headerBytes_.endsWith('\n')) return;
            header_ = QJsonDocument::fromJson(headerBytes_).object();
            headerBytes_.clear();
            if (header_.value("id").toInteger(-1) != active_->id) { fail(QStringLiteral("Invalid PDF renderer response.")); return; }
            if (header_.contains("error")) { fail(header_.value("error").toString().left(1024)); return; }
            const int width = header_.value("width").toInt();
            const int height = header_.value("height").toInt();
            const int pages = header_.value("pages").toInt();
            const auto pw = header_.value("pointWidth").toDouble();
            const auto ph = header_.value("pointHeight").toDouble();
            if (width < 1 || height < 1 || width > PdfMaxEdge || height > PdfMaxEdge
                || qint64(width) * height > PdfMaxPixels || header_.value("bytes").toInteger() != qint64(width) * height * 4
                || pages < 1 || (pages_ && pages != pages_)
                || header_.value("page").toInt(-1) != active_->page || active_->page < 0 || active_->page >= pages
                || !std::isfinite(pw) || !std::isfinite(ph) || pw <= 0 || ph <= 0 || pw > 10000000 || ph > 10000000) {
                fail(QStringLiteral("Invalid PDF renderer frame.")); return;
            }
            pages_ = pages;
            incoming_ = QImage(width, height, QImage::Format_RGBA8888);
            if (incoming_.isNull()) { fail(QStringLiteral("PDF page allocation failed.")); return; }
            received_ = 0;
        }
        const auto count = process_->read(reinterpret_cast<char *>(incoming_.bits()) + received_,
            std::min({budget, qint64(64 * 1024), incoming_.sizeInBytes() - received_}));
        if (count <= 0) return;
        budget -= count;
        received_ += count;
        if (received_ == incoming_.sizeInBytes()) {
            timeout_.stop();
            const auto page = active_->page;
            const QSizeF points(header_.value("pointWidth").toDouble(), header_.value("pointHeight").toDouble());
            auto image = std::move(incoming_);
            incoming_ = {};
            active_.reset();
            if (!pending_) {
                links_.clear();
                const auto entries = header_.value("links").toArray();
                for (int i = 0; i < std::min(256, int(entries.size())); ++i) {
                    const auto entry = entries.at(i).toObject();
                    const QRectF rect(entry.value("x").toDouble(), entry.value("y").toDouble(),
                                      entry.value("w").toDouble(), entry.value("h").toDouble());
                    const QPointF location(entry.value("toX").toDouble(), entry.value("toY").toDouble());
                    const auto url = QUrl(entry.value("url").toString());
                    const int target = entry.value("page").toInt(-1);
                    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) || !std::isfinite(rect.width())
                        || !std::isfinite(rect.height()) || rect.isEmpty() || !QRectF({}, points).intersects(rect)
                        || !std::isfinite(location.x()) || !std::isfinite(location.y())
                        || target < -1 || target >= pages_ || url.toString().size() > 2048) continue;
                    links_.append({rect, url, target, location});
                }
                emit frameReady(page, points, image);
            }
            send();
            if (!active_) return;
        }
    }
    if (process_ && process_->bytesAvailable()) QTimer::singleShot(0, this, &PdfSession::read);
}
}

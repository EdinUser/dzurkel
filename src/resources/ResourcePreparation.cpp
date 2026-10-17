#include "ResourcePreparation.h"
#include "preview/providers/FileInfoProvider.h"
#include "preview/PreviewLibrary.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QTextStream>

namespace dzurkel {
namespace {
// One current helper plus one cancelled helper being reaped. A kernel-stuck
// filesystem call must not allow rapid requests to spawn unbounded processes.
int activeHelpers = 0;
QString snapshotName(const FileResource &resource)
{
    // Some optional readers use the suffix as well as content sniffing.
    const auto suffix = QFileInfo(resource.path).suffix().left(32);
    return suffix.isEmpty() ? QStringLiteral("content") : QStringLiteral("content.") + suffix;
}
QJsonObject metadata(const PreviewData &data)
{
    return {{QStringLiteral("mime"), data.mimeType}, {QStringLiteral("description"), data.description},
        {QStringLiteral("size"), double(data.size)},
        {QStringLiteral("modified"), data.modified.toString(Qt::ISODateWithMs)},
        {QStringLiteral("linkTarget"), data.linkTarget}, {QStringLiteral("message"), data.message}};
}
int report(QJsonObject object)
{
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact);
    return 0;
}
int failure(const QString &message)
{
    return report({{QStringLiteral("error"), message}});
}
}

ResourcePreparation::ResourcePreparation(QObject *parent, int timeoutMs) : QObject(parent)
{
    timer_.setSingleShot(true);
    timer_.setInterval(qMax(1, timeoutMs));
    connect(&timer_, &QTimer::timeout, this, [this] {
        finish(QStringLiteral("Preview loading timed out. Check the connection and try again."));
    });
}
ResourcePreparation::~ResourcePreparation() { cancel(); }
void ResourcePreparation::cancel()
{
    timer_.stop();
    finished_ = true;
    if (!process_) return;
    auto *process = process_;
    process_ = nullptr;
    disconnect(process, nullptr, this, nullptr);
    // Do not synchronously wait for a network filesystem syscall to return.
    // Keep the directory until the helper exits, even after request cancellation.
    const auto store = resource_.backingStore;
    process->setParent(QCoreApplication::instance());
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
        [process, store] { process->deleteLater(); });
    if (process->state() == QProcess::NotRunning) process->deleteLater();
    else process->kill();
}
void ResourcePreparation::start(const FileResource &resource)
{
    resource_ = resource;
    if (activeHelpers >= 2) {
        QTimer::singleShot(0, this, [this] { finish(QStringLiteral("Previous file access is still stopping. Try again shortly.")); });
        return;
    }
    resource_.backingStore = std::make_shared<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/dzurkel-resource-XXXXXX"));
    if (!resource_.backingStore->isValid()) {
        QTimer::singleShot(0, this, [this] { finish(QStringLiteral("Cannot create a temporary preview directory.")); });
        return;
    }
    process_ = new QProcess(this);
    ++activeHelpers;
    connect(process_, &QObject::destroyed, [] { --activeHelpers; });
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        // Helper diagnostics can contain transport details. Errors for users
        // travel through the bounded structured response instead.
        process_->readAllStandardError();
    });
    // Tests and the installed executable both keep dzurkel in the same bindir.
    process_->setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"));
    process_->setArguments({QStringLiteral("--internal-prepare-resource"),
        resource.url.toString(QUrl::FullyEncoded), resource_.backingStore->path()});
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Cannot start the resource preparation helper."));
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
        if (finished_) return;
        if (status != QProcess::NormalExit || code != 0) {
            finish(QStringLiteral("Resource preparation failed. Check that the location is accessible."));
            return;
        }
        const auto bytes = process_->readAllStandardOutput();
        const auto object = bytes.size() <= 65536 ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
        if (object.isEmpty()) { finish(QStringLiteral("Invalid resource preparation response.")); return; }
        if (object.contains(QStringLiteral("error"))) { finish(object.value(QStringLiteral("error")).toString()); return; }
        resource_.metadata = object;
        resource_.informationOnly = object.value(QStringLiteral("informationOnly")).toBool();
        if (!resource_.informationOnly)
            resource_.backingPath = resource_.backingStore->filePath(snapshotName(resource_));
        finish();
    });
    timer_.start();
    process_->start();
}
void ResourcePreparation::finish(const QString &error)
{
    if (finished_) return;
    if (!error.isEmpty()) {
        resource_.informationOnly = true;
        resource_.metadata = {{QStringLiteral("mime"), QStringLiteral("application/octet-stream")},
            {QStringLiteral("description"), QStringLiteral("Preview unavailable")},
            {QStringLiteral("message"), error}};
    }
    cancel();
    emit ready(resource_);
}

bool resolveDesktopPath(const FileResource &resource, QString &path, QString &error)
{
    const auto fail = [&error](const QString &message) { error = message; return false; };
    path = resource.path;
    if (!resource.url.isLocalFile()) {
        const auto protocol = resource.url.scheme().toUpper();
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KIOFuse"),
            QStringLiteral("/org/kde/KIOFuse"), QStringLiteral("org.kde.KIOFuse.VFS"), QStringLiteral("mountUrl"));
        message << resource.url.toString(QUrl::FullyEncoded);
        const QDBusReply<QString> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 25000);
        if (!reply.isValid())
            return fail(QStringLiteral("%1 access failed. Ensure KIO-FUSE and the matching protocol worker are installed. "
                "Open this location in Dolphin to verify access and authenticate, then retry. (%2)")
                .arg(protocol, reply.error().name()));
        path = reply.value();
        if (!QDir::isAbsolutePath(path)) return fail(QStringLiteral("KIO-FUSE returned an invalid local path."));
    }
    return true;
}

int prepareResourceSnapshot(const QString &url, const QString &directory)
{
    FileResource original;
    QString error;
    if (!parseFileResource(url, original, error)) return failure(error);
    QString path;
    if (!resolveDesktopPath(original, path, error)) return failure(error);
    FileResource source;
    if (!resolveFileResource(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), source, error))
        return failure(original.url.isLocalFile() ? error : QStringLiteral(
            "The %1 resource is not accessible. Open the location in Dolphin to check authentication and permissions, then retry.")
            .arg(original.url.scheme().toUpper()));
    auto data = FileInfoProvider::describe(source);
    auto object = metadata(data);
    object.insert(QStringLiteral("informationOnly"), true);
    if (source.informationOnly) return report(object);
    QFile input;
    if (!openPreviewFile(source, input, error)) return failure(error);
    const auto mime = QMimeDatabase().mimeTypeForFileNameAndData(original.path, &input);
    input.seek(0);
    object.insert(QStringLiteral("mime"), mime.name());
    object.insert(QStringLiteral("description"), mime.comment());
    const auto capability = PreviewLibrary::capability(PreviewLibrary::select(input, mime, original.path));
    input.seek(0);
    if (!capability) return report(object);
    const qint64 limit = capability->sourceLimit;
    const bool complete = capability->access == SourceAccess::CompleteSnapshot;
    if (input.size() > limit && complete) {
        object.insert(QStringLiteral("message"), QStringLiteral("File exceeds the %1 MiB content preview limit.").arg(limit / (1024 * 1024)));
        return report(object);
    }
    QFile output(QDir(directory).filePath(snapshotName(original)));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return failure(QStringLiteral("Cannot create preview snapshot."));
    qint64 copied = 0;
    while (copied < limit) {
        const auto bytes = input.read(qMin<qint64>(128 * 1024, limit - copied));
        if (input.error() != QFileDevice::NoError) return failure(QStringLiteral("Could not read the preview source."));
        if (bytes.isEmpty()) break;
        if (output.write(bytes) != bytes.size()) return failure(QStringLiteral("Cannot write the preview snapshot (check temporary disk space)."));
        copied += bytes.size();
    }
    if (complete && !input.atEnd()) return failure(QStringLiteral("File grew beyond the content preview limit."));
    if (!output.flush()) return failure(QStringLiteral("Cannot finish the preview snapshot."));
    object.insert(QStringLiteral("informationOnly"), false);
    return report(object);
}
} // namespace dzurkel

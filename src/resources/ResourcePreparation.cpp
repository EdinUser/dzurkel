#include "ResourcePreparation.h"
#include "preview/providers/FileInfoProvider.h"
#include "preview/PreviewLibrary.h"
#include "preview/providers/DocumentPreviewProvider.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageIOHandler>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QPointer>
#include <QTextStream>

namespace dzurkel {
namespace {
// One current helper plus one cancelled helper being reaped. A kernel-stuck
// filesystem call must not allow rapid requests to spawn unbounded processes.
int activeHelpers = 0;
constexpr qint64 ResponseLimit = 4 * 1024 * 1024;
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
    process_ = new QProcess(this);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, process_,
        [process = process_, owner = QPointer<ResourcePreparation>(this)] {
        // Disarm the active request first: its destructor must not reparent
        // an exit-detached wrapper back to the application. This connection
        // also outlives requests cancelled/destroyed before shutdown.
        if (owner && owner->process_ == process) owner->cancel();
        if (process->state() == QProcess::NotRunning) return;
        process->kill();
        process->setParent(nullptr);
        process->disconnect(); // Never wait for blocked filesystem I/O at exit.
    });
    ++activeHelpers;
    connect(process_, &QObject::destroyed, [] { --activeHelpers; });
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, &ResourcePreparation::readOutput);
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        // Helper diagnostics can contain transport details. Errors for users
        // travel through the bounded structured response instead.
        process_->readAllStandardError();
    });
    // Tests and the installed executable both keep dzurkel in the same bindir.
    process_->setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"));
    process_->setArguments({QStringLiteral("--internal-prepare-resource"),
        resource.url.toString(QUrl::FullyEncoded)});
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Cannot start the resource preparation helper."));
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
        if (finished_) return;
        readOutput();
        if (finished_) return;
        if (status != QProcess::NormalExit || code != 0) {
            finish(QStringLiteral("Resource preparation failed. Check that the location is accessible."));
            return;
        }
        auto object = QJsonDocument::fromJson(response_).object();
        response_.clear();
        if (object.isEmpty()) { finish(QStringLiteral("Invalid resource preparation response.")); return; }
        if (object.contains(QStringLiteral("error"))) { finish(object.value(QStringLiteral("error")).toString()); return; }
        resource_.informationOnly = object.value(QStringLiteral("informationOnly")).toBool();
        resource_.boundedPrefix = object.value(QStringLiteral("boundedPrefix")).toBool() && !resource_.informationOnly;
        if (resource_.boundedPrefix) {
            const auto capability = PreviewLibrary::capability(object.value(QStringLiteral("reader")).toString());
            const auto encoded = object.take(QStringLiteral("prefix"));
            if (!capability || capability->access != SourceAccess::BoundedPrefix || !encoded.isString()
                || encoded.toString().size() > (capability->sourceLimit + 2) / 3 * 4) {
                finish(QStringLiteral("Invalid text prefix response.")); return;
            }
            const auto decoded = QByteArray::fromBase64Encoding(encoded.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.size() > capability->sourceLimit) {
                finish(QStringLiteral("Invalid text prefix response.")); return;
            }
            resource_.prefix = decoded.decoded;
            resource_.backingStore.reset();
        }
        resource_.metadata = object;
        resource_.streaming = object.value(QStringLiteral("streaming")).toBool() && !resource_.informationOnly;
        resource_.seekableSource = object.value(QStringLiteral("seekableSource")).toBool() && !resource_.informationOnly;
        if (resource_.seekableSource) {
            const auto path = object.value(QStringLiteral("sourcePath")).toString();
            const auto reader = object.value(QStringLiteral("reader")).toString();
            const auto capability = PreviewLibrary::capability(reader);
            if (!QDir::isAbsolutePath(path) || !capability || capability->access != SourceAccess::SeekableSource) {
                finish(QStringLiteral("Invalid direct decoder source response."));
                return;
            }
            resource_.backingPath = path;
            resource_.backingStore.reset();
        } else if (resource_.streaming)
            resource_.backingStore.reset();
        finish();
    });
    timer_.start();
    process_->start();
}
void ResourcePreparation::readOutput()
{
    if (finished_ || !process_) return;
    response_ += process_->read(ResponseLimit + 1 - response_.size());
    if (response_.size() > ResponseLimit)
        finish(QStringLiteral("Resource preparation response exceeds its budget."));
}
void ResourcePreparation::finish(const QString &error)
{
    if (finished_) return;
    response_.clear();
    if (!error.isEmpty()) {
        resource_.prefix.clear();
        resource_.boundedPrefix = false;
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

int prepareResource(const QString &url)
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
    if (data.mimeType == QStringLiteral("inode/directory"))
        object.insert(QStringLiteral("directoryPath"), path);
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
    if (capability->access == SourceAccess::BoundedPrefix) {
        const auto bytes = input.read(capability->sourceLimit);
        if (input.error() != QFileDevice::NoError) return failure(QStringLiteral("Could not read the preview source."));
        object.insert(QStringLiteral("boundedPrefix"), true);
        object.insert(QStringLiteral("prefix"), QString::fromLatin1(bytes.toBase64()));
        object.insert(QStringLiteral("reader"), capability->id);
        object.insert(QStringLiteral("informationOnly"), false);
        return report(object);
    }
    if (capability->access == SourceAccess::SeekableSource) {
        // Reuse the header reader before the decoder process starts. These
        // built-in formats need no GUI runtime; font-using plugins such as SVG
        // publish their dimensions from the image helper instead.
        const QStringList headerFormats{QStringLiteral("image/png"), QStringLiteral("image/jpeg"),
            QStringLiteral("image/bmp"), QStringLiteral("image/x-ms-bmp"), QStringLiteral("image/gif")};
        if (capability->id == QStringLiteral("image") && headerFormats.contains(mime.name())) {
            QImageReader reader(&input);
            reader.setAutoTransform(true);
            auto size = reader.size();
            if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) size.transpose();
            if (size.isValid() && !size.isEmpty()) {
                object.insert(QStringLiteral("imageWidth"), size.width());
                object.insert(QStringLiteral("imageHeight"), size.height());
            }
        }
        object.insert(QStringLiteral("seekableSource"), true);
        object.insert(QStringLiteral("sourcePath"), source.path);
        object.insert(QStringLiteral("reader"), capability->id);
        object.insert(QStringLiteral("informationOnly"), false);
        return report(object);
    }
    if (capability->access == SourceAccess::Streaming) {
        object.insert(QStringLiteral("streaming"), true);
        object.insert(QStringLiteral("informationOnly"), false);
        object.insert(QStringLiteral("reader"), capability->id);
        return report(object);
    }
    return failure(QStringLiteral("Unsupported resource access policy."));
}
} // namespace dzurkel

#include "FileResource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dzurkel {

bool openPreviewFile(const FileResource &resource, QFile &file, QString &error)
{
    const QByteArray path = QFile::encodeName(resource.path);
    const int descriptor = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        error = QStringLiteral("File is not readable: %1 (%2)").arg(resource.path, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        ::close(descriptor);
        error = QStringLiteral("Resource changed or is not a regular file; content was not opened.");
        return false;
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        error = file.errorString();
        return false;
    }
    return true;
}

bool resolveFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure)
{
    resource = {};
    if (failure) *failure = ResourceError::InvalidUrl;
    error.clear();
    if (argument.isEmpty()) {
        error = QStringLiteral("A local file path or file URL is required.");
        return false;
    }

    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    QString path = argument;
    if (scheme.match(argument).hasMatch()) {
        const QUrl url(argument, QUrl::StrictMode);
        if (!url.isValid()) {
            error = QStringLiteral("Malformed URL: %1").arg(url.errorString());
            return false;
        }
        if (url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
            if (failure) *failure = ResourceError::UnsupportedScheme;
            error = QStringLiteral("Unsupported URL scheme: %1. Only local files are supported.")
                        .arg(url.scheme());
            return false;
        }
        if ((!url.host().isEmpty() && url.host().compare(QStringLiteral("localhost"),
                                                       Qt::CaseInsensitive) != 0)
            || !url.userInfo().isEmpty() || url.port() != -1) {
            error = QStringLiteral("Remote file URLs are not supported.");
            return false;
        }
        if (url.hasQuery() || url.hasFragment()) {
            error = QStringLiteral("File URLs must not contain a query or fragment; encode filename characters.");
            return false;
        }
        QUrl localUrl = url;
        localUrl.setHost(QString());
        path = localUrl.toLocalFile();
        if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
            error = QStringLiteral("File URLs must contain an absolute local path.");
            return false;
        }
    }
    if (path.contains(QChar::Null)) {
        error = QStringLiteral("File paths must not contain NUL characters.");
        return false;
    }
    const QFileInfo info(QDir::current().absoluteFilePath(path));
    if (!info.exists() && !info.isSymLink()) {
        if (failure) *failure = ResourceError::NotFound;
        error = QStringLiteral("File does not exist: %1").arg(info.absoluteFilePath());
        return false;
    }
    resource.path = info.absoluteFilePath();
    resource.url = QUrl::fromLocalFile(resource.path);
    resource.informationOnly = info.isSymLink() || !info.isFile();
    if (!resource.informationOnly) {
        QFile file;
        if (!info.isReadable() || !openPreviewFile(resource, file, error)) {
            if (failure) *failure = ResourceError::NotReadable;
            if (error.isEmpty()) error = QStringLiteral("File is not readable: %1").arg(resource.path);
            resource = {};
            return false;
        }
    }
    if (failure) *failure = ResourceError::None;
    resource.path = info.absoluteFilePath();
    resource.url = QUrl::fromLocalFile(resource.path);
    return true;
}

} // namespace dzurkel

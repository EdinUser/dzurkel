#include "FileResource.h"
#include "ResourceAccessPolicy.h"

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
    const QByteArray path = QFile::encodeName(resource.backingPath.isEmpty() ? resource.path : resource.backingPath);
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

bool parseFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure)
{
    resource = {};
    if (failure) *failure = ResourceError::InvalidUrl;
    error.clear();
    if (argument.isEmpty()) {
        error = QStringLiteral("A file path or supported resource URL is required.");
        return false;
    }
    if (argument.size() > 16384 || argument.contains(QChar::Null)) {
        error = QStringLiteral("Resource arguments must be at most 16,384 characters and contain no NUL characters.");
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
        if (url.scheme() == QStringLiteral("sftp")) {
            if (url.host().isEmpty() || !url.path().startsWith(QChar('/'))
                || url.userInfo(QUrl::FullyEncoded).contains(QChar(':'))
                || url.hasQuery() || url.hasFragment() || url.path().contains(QChar::Null)
                || url.port() == 0) {
                error = QStringLiteral("Expected sftp://[user@]host[:port]/absolute/path without passwords, query or fragment.");
                return false;
            }
            if (!ResourceAccessPolicy::experimentalSftpEnabled()) {
                if (failure) *failure = ResourceError::UnsupportedScheme;
                error = QStringLiteral("SFTP previews are experimental and disabled. Enable Experimental SFTP in Dzurkel Settings to try them.");
                return false;
            }
            // Preserve remote path components: resolving '..' lexically can
            // change meaning when a server-side ancestor is a symbolic link.
            resource.url = url;
            resource.path = url.path();
            if (failure) *failure = ResourceError::None;
            return true;
        }
        if (url.scheme().compare(QStringLiteral("smb"), Qt::CaseInsensitive) == 0) {
            if (url.host().isEmpty() || url.path().section('/', 1, 1).isEmpty()
                || url.userInfo(QUrl::FullyEncoded).contains(QChar(':')) || url.hasQuery() || url.hasFragment()
                || url.port() != -1 || url.path().contains(QChar::Null)) {
                error = QStringLiteral("Expected smb://[user@]host/share/path without passwords, port, query or fragment.");
                return false;
            }
            resource.url = url.adjusted(QUrl::NormalizePathSegments);
            if (resource.url.path().section('/', 1, 1).isEmpty()) {
                error = QStringLiteral("An SMB share name is required.");
                resource = {};
                return false;
            }
            resource.path = resource.url.path();
            if (failure) *failure = ResourceError::None;
            return true;
        }
        if (url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
            if (failure) *failure = ResourceError::UnsupportedScheme;
            error = QStringLiteral("Unsupported URL scheme: %1. Use file or smb.")
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
    resource.path = QDir::cleanPath(QDir::current().absoluteFilePath(path));
    resource.url = QUrl::fromLocalFile(resource.path);
    if (failure) *failure = ResourceError::None;
    return true;
}

bool resolveFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure)
{
    if (!parseFileResource(argument, resource, error, failure)) return false;
    if (!resource.url.isLocalFile()) {
        error = QStringLiteral("Remote resources require asynchronous preparation.");
        resource = {};
        return false;
    }
    const QFileInfo info(resource.path);
    struct stat status {};
    if (::lstat(QFile::encodeName(resource.path).constData(), &status) != 0) {
        const int code = errno;
        if (failure) *failure = code == ENOENT || code == ENOTDIR ? ResourceError::NotFound : ResourceError::NotReadable;
        error = code == ENOENT || code == ENOTDIR
            ? QStringLiteral("File does not exist: %1").arg(info.absoluteFilePath())
            : QStringLiteral("File metadata is not readable: %1 (%2)").arg(info.absoluteFilePath(),
                QString::fromLocal8Bit(std::strerror(code)));
        resource = {};
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

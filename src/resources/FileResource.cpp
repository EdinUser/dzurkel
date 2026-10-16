#include "FileResource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace dzurkel {

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
    if (!info.exists()) {
        if (failure) *failure = ResourceError::NotFound;
        error = QStringLiteral("File does not exist: %1").arg(info.absoluteFilePath());
        return false;
    }
    if (!info.isFile()) {
        if (failure) *failure = ResourceError::NotRegularFile;
        error = QStringLiteral("Expected a regular file: %1").arg(info.absoluteFilePath());
        return false;
    }
    QFile file(info.absoluteFilePath());
    if (!info.isReadable() || !file.open(QIODevice::ReadOnly)) {
        if (failure) *failure = ResourceError::NotReadable;
        error = QStringLiteral("File is not readable: %1").arg(info.absoluteFilePath());
        return false;
    }
    if (failure) *failure = ResourceError::None;
    resource.path = info.absoluteFilePath();
    resource.url = QUrl::fromLocalFile(resource.path);
    return true;
}

} // namespace dzurkel

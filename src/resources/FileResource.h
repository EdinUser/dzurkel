#pragma once

#include <QString>
#include <QUrl>

namespace dzurkel {

enum class ResourceError { None, InvalidUrl, UnsupportedScheme, NotFound, NotRegularFile, NotReadable };

struct FileResource {
    QString path;
    QUrl url;
};

// No remote resolution, shell expansion, decoding, or writes to the target file.
bool resolveFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure = nullptr);

} // namespace dzurkel

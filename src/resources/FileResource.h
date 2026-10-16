#pragma once

#include <QString>
#include <QUrl>

class QFile;

namespace dzurkel {

enum class ResourceError { None, InvalidUrl, UnsupportedScheme, NotFound, NotReadable };

struct FileResource {
    QString path;
    QUrl url;
    bool informationOnly = false;
};

bool openPreviewFile(const FileResource &resource, QFile &file, QString &error);

// No remote resolution, shell expansion, decoding, or writes to the target file.
bool resolveFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure = nullptr);

} // namespace dzurkel

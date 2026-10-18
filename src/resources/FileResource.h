#pragma once

#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <memory>

class QFile;
class QTemporaryDir;

namespace dzurkel {

enum class ResourceError { None, InvalidUrl, UnsupportedScheme, NotFound, NotReadable };

struct FileResource {
    QString path;
    QUrl url;
    bool informationOnly = false;
    // A media source is opened on demand by the playback helper, not snapshotted.
    bool streaming = false;
    // Original identity stays in path/url; decoding uses an owned local snapshot.
    QString backingPath;
    QJsonObject metadata;
    std::shared_ptr<QTemporaryDir> backingStore;
};

bool openPreviewFile(const FileResource &resource, QFile &file, QString &error);

// Syntax/normalization only: no filesystem or network access.
bool parseFileResource(const QString &argument, FileResource &resource, QString &error,
                       ResourceError *failure = nullptr);
// Synchronous filesystem validation. Use only outside the GUI/IPC thread.
bool resolveFileResource(const QString &argument, FileResource &resource, QString &error,
                         ResourceError *failure = nullptr);

} // namespace dzurkel

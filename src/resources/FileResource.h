#pragma once

#include <QString>
#include <QByteArray>
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
    // A decoder helper reads the resolved local/mounted source directly.
    bool seekableSource = false;
    // Bounded source bytes read by the access helper; no temporary file or reopen.
    bool boundedPrefix = false;
    QByteArray prefix;
    // Original identity stays in path/url; file-based decoding uses a snapshot
    // or resolved source, while boundedPrefix readers use prefix above.
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

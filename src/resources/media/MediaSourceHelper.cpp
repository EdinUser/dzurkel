#include "resources/media/StreamingMediaSource.h"
#include "MediaSourceHelper.h"
#include "resources/FileResource.h"
#include "resources/ResourcePreparation.h"
#include <QFile>
#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace dzurkel;
namespace {
bool transfer(char *bytes, qint64 count, bool writing)
{
    const int descriptor = writing ? STDOUT_FILENO : STDIN_FILENO;
    while (count > 0) {
        pollfd pending{descriptor, short(writing ? POLLOUT : POLLIN), 0};
        if (::poll(&pending, 1, -1) < 0) { if (errno == EINTR) continue; return false; }
        const auto done = writing ? ::send(descriptor, bytes, count, MSG_NOSIGNAL)
                                  : ::recv(descriptor, bytes, count, 0);
        if (done < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (done <= 0) return false;
        bytes += done;
        count -= done;
    }
    return true;
}
bool send(QByteArray bytes) { return transfer(bytes.data(), bytes.size(), true); }
int fail(const QString &message)
{
    const auto bytes = message.toUtf8().left(1024);
    send(mediaHeader(-1, bytes.size()) + bytes);
    return 1;
}
}
int dzurkel::serveMediaSource(const QString &url, int simulatedReadDelayMs)
{
    const int delayMs = std::clamp(simulatedReadDelayMs, 0, 30000);
    FileResource resource, source;
    QString path, error;
    if (!parseFileResource(url, resource, error)
        || !resolveDesktopPath(resource, path, error)
        || !resolveFileResource(path, source, error)) return fail(error);
    if (source.informationOnly) return fail(QStringLiteral("Media source must be a regular file."));
    QFile file;
    if (!openPreviewFile(source, file, error)) return fail(error);
    if (!send(mediaHeader(file.size(), 0))) return 1;
    QByteArray request(16, Qt::Uninitialized);
    // One fixed-size window per helper. Decoder requests stay <=64 KiB, while
    // adjacent reads reuse a single bounded filesystem read on slow mounts.
    QByteArray cache(StreamingMediaSource::ReadAheadBytes, Qt::Uninitialized);
    qint64 cacheOffset = -1, cacheSize = 0, cachedFileSize = file.size();
    while (transfer(request.data(), request.size(), false)) {
        const auto offset = mediaHeaderValue(request.constData(), 0);
        const auto count = mediaHeaderValue(request.constData(), 1);
        if (offset < 0 || count <= 0 || count > StreamingMediaSource::ChunkBytes) return 2;
        const auto currentSize = file.size();
        if (currentSize != cachedFileSize) { cacheOffset = -1; cacheSize = 0; cachedFileSize = currentSize; }
        qint64 read = -1;
        if (offset >= cacheOffset && offset < cacheOffset + cacheSize) {
            read = std::min(count, cacheOffset + cacheSize - offset);
        } else {
            // Fault injection models source I/O, not IPC/cache-hit latency.
            if (delayMs) ::usleep(useconds_t(delayMs) * 1000);
            cacheOffset = offset / StreamingMediaSource::ReadAheadBytes * StreamingMediaSource::ReadAheadBytes;
            cacheSize = file.seek(cacheOffset) ? file.read(cache.data(), cache.size()) : -1;
            if (cacheSize >= 0) read = std::clamp<qint64>(cacheOffset + cacheSize - offset, 0, count);
        }
        if (!send(mediaHeader(read, 0))) return 1;
        if (read > 0 && !transfer(cache.data() + (offset - cacheOffset), read, true)) return 1;
        if (read < 0) return 1;
    }
    return 0;
}

#include "ImageDecodeJob.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "resources/FileResource.h"
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <cerrno>
#include <unistd.h>

namespace dzurkel {
namespace {
bool writeBytes(const char *bytes, qint64 length)
{
    while (length > 0) {
        const auto count = ::write(STDOUT_FILENO, bytes, size_t(length));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        length -= count;
    }
    return true;
}
bool message(const QJsonObject &object)
{
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    return bytes.size() <= ImageHeaderBudget && writeBytes(bytes.constData(), bytes.size());
}
}
int serveImage(const QString &path, const QString &mime)
{
    QImageReader::setAllocationLimit(ImagePreviewProvider::AllocationLimitMiB);
    PreviewData data;
    data.resource.path = path;
    data.mimeType = mime;
    auto file = std::make_unique<QFile>();
    QString error;
    if (!openPreviewFile(data.resource, *file, error)) {
        data.message = error;
    } else {
        data = ImagePreviewProvider().loadWithMetadata(std::move(file), std::move(data), {},
            std::make_shared<std::atomic_bool>(false), [](const QSize &size) {
                message({{"event", "metadata"}, {"width", size.width()}, {"height", size.height()}});
            });
    }
    QList<QImage> frames;
    QJsonArray descriptors;
    if (data.kind == PreviewKind::Image) {
        frames = data.animationFrames.isEmpty() ? QList<QImage>{data.image} : data.animationFrames;
        qint64 total = 0;
        qint64 headerSize = 0;
        for (const auto &frame : frames) {
            const auto profile = frame.colorSpace().iccProfile();
            if (profile.size() > ImageProfileBudget || frame.width() > 4096 || frame.height() > 4096
                || (total += frame.sizeInBytes()) > ImagePixelBudget) {
                data.kind = PreviewKind::Information;
                data.message = QStringLiteral("Decoded image or colour profile exceeds the preview budget.");
                break;
            }
            QJsonArray palette;
            for (auto color : frame.colorTable()) palette.append(double(color));
            const QJsonObject descriptor{{"width", frame.width()}, {"height", frame.height()},
                {"format", int(frame.format())}, {"bytes", double(frame.sizeInBytes())},
                {"palette", palette}, {"profile", QString::fromLatin1(profile.toBase64())},
                {"dpr", frame.devicePixelRatio()}, {"dpmX", frame.dotsPerMeterX()}, {"dpmY", frame.dotsPerMeterY()}};
            headerSize += QJsonDocument(descriptor).toJson(QJsonDocument::Compact).size() + 1;
            if (headerSize > ImageHeaderBudget - 16384) {
                data.kind = PreviewKind::Information;
                data.message = QStringLiteral("Image metadata exceeds the preview budget.");
                break;
            }
            descriptors.append(descriptor);
        }
    }
    if (data.kind != PreviewKind::Image) { frames.clear(); descriptors = {}; }
    QJsonArray delays;
    if (frames.size() > 1) for (auto delay : data.animationDelays) delays.append(delay);
    QJsonObject header{{"event", "result"}, {"image", data.kind == PreviewKind::Image},
        {"width", data.imageSize.width()}, {"height", data.imageSize.height()},
        {"message", data.message.left(4096)}, {"detail", data.detail.left(4096)},
        {"missingLibrary", data.missingLibrary}, {"frames", descriptors},
        {"delays", delays}, {"loops", data.animationLoops}};
    if (!message(header)) {
        if (!message({{"event", "result"}, {"image", false},
                      {"message", "Image metadata exceeds the preview budget."}})) return 2;
        frames.clear();
    }
    for (const auto &frame : frames)
        if (!writeBytes(reinterpret_cast<const char *>(frame.constBits()), frame.sizeInBytes())) return 2;
    // Keep stdout alive until the client has consumed the complete bounded
    // result. EOF acknowledges success; cancellation kills this process.
    char ack;
    while (::read(STDIN_FILENO, &ack, 1) < 0 && errno == EINTR) {}
    return 0;
}
}

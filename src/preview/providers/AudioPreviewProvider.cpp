#include "AudioPreviewProvider.h"
#include "preview/media/MediaBackend.h"
#include <QFile>
#include <QFileInfo>

namespace dzurkel {
bool AudioPreviewProvider::supports(const QMimeType &mime) const
{
    return QStringList{QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"),
        QStringLiteral("audio/x-flac"), QStringLiteral("audio/ogg"), QStringLiteral("audio/opus"),
        QStringLiteral("audio/x-wav"), QStringLiteral("audio/wav"), QStringLiteral("audio/vnd.wave"),
        QStringLiteral("audio/mp4"), QStringLiteral("audio/x-m4a"), QStringLiteral("audio/aac"),
        QStringLiteral("audio/x-aac")}.contains(mime.name());
}
bool AudioPreviewProvider::accepts(const QMimeType &mime, const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    return AudioPreviewProvider().supports(mime) || QStringList{
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("mp4")}.contains(suffix);
}
PreviewData AudioPreviewProvider::load(std::unique_ptr<QFile>, PreviewData data,
                                      const QSize &, const Cancellation &cancel) const
{
    data.kind = cancel->load() ? PreviewKind::Cancelled : PreviewKind::Audio;
    data.readerId = QStringLiteral("audio");
    data.message.clear();
    return data;
}
QWidget *AudioPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return createMediaPreview(data, parent);
}
}

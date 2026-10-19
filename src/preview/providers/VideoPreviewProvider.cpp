#include "VideoPreviewProvider.h"
#include "preview/media/MediaBackend.h"
#include <QFile>
#include <QFileInfo>
namespace dzurkel {
bool VideoPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.name().startsWith(QLatin1String("video/"));
}
bool VideoPreviewProvider::accepts(const QMimeType &mime, const QString &path)
{
    return VideoPreviewProvider().supports(mime) || QStringList{
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("ogv"), QStringLiteral("wmv")}.contains(QFileInfo(path).suffix().toLower());
}
PreviewData VideoPreviewProvider::load(std::unique_ptr<QFile>, PreviewData data,
                                      const QSize &, const Cancellation &cancel) const
{
    data.kind = cancel->load() ? PreviewKind::Cancelled : PreviewKind::Video;
    data.readerId = QStringLiteral("video");
    data.message.clear();
    return data;
}
QWidget *VideoPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return createMediaPreview(data, parent);
}
}

#include "ImagePreviewProvider.h"
#include <QFile>
#include <QImageIOHandler>
#include <QImageReader>
#include <QPainter>
#include <QWidget>

namespace dzurkel {
namespace {
class ImageCanvas : public QWidget {
public:
    ImageCanvas(const QImage &image, QWidget *parent) : QWidget(parent), image_(image)
    {
        setObjectName(QStringLiteral("imagePreview"));
        setAccessibleName(QStringLiteral("Image preview"));
        setMinimumSize(0, 0);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
        painter.drawImage(area, image_);
    }
private:
    QImage image_;
};
}

bool ImagePreviewProvider::supports(const QMimeType &mime) const
{
    return mime.name().startsWith(QStringLiteral("image/"));
}
PreviewData ImagePreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                     const QSize &target, const Cancellation &cancel) const
{
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    QImageReader reader(file.get());
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        data.message = QStringLiteral("No installed Qt image reader can decode this file.");
        return data;
    }
    const QSize original = reader.size();
    if (!original.isValid() || original.isEmpty()) {
        data.message = QStringLiteral("The image has invalid or unavailable dimensions.");
        return data;
    }
    // Readers without native scaling may allocate the original before shrinking.
    const qint64 pixels = qint64(original.width()) * original.height();
    if (!reader.supportsOption(QImageIOHandler::ScaledSize)
        && pixels > qint64(AllocationLimitMiB) * 1024 * 1024 / 4) {
        data.message = QStringLiteral("Image exceeds the 64 MiB decoding budget for this reader.");
        return data;
    }
    QSize bounds = target.boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
    if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) bounds.transpose();
    reader.setScaledSize(original.scaled(bounds.boundedTo(original), Qt::KeepAspectRatio));
    data.image = reader.read();
    if (cancel->load()) {
        data.image = {};
        data.kind = PreviewKind::Cancelled;
    } else if (data.image.isNull()) {
        data.message = QStringLiteral("Image decoding failed: %1").arg(reader.errorString());
    } else if (data.image.sizeInBytes() > qint64(AllocationLimitMiB) * 1024 * 1024) {
        data.image = {};
        data.message = QStringLiteral("Decoded image exceeds the 64 MiB budget.");
    } else {
        data.kind = PreviewKind::Image;
        data.message.clear();
        data.detail = QStringLiteral("%1 × %2 pixels").arg(original.width()).arg(original.height());
    }
    return data;
}
QWidget *ImagePreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new ImageCanvas(data.image, parent);
}
} // namespace dzurkel

#include "PreviewRegistry.h"
#include "providers/FileInfoProvider.h"
#include "providers/ImagePreviewProvider.h"
#include "providers/PdfPreviewProvider.h"
#include "providers/TextPreviewProvider.h"
#include <QFile>
#include <QImageReader>
#include <QMimeDatabase>

namespace dzurkel {
PreviewData PreviewRegistry::load(const FileResource &resource, const QSize &target,
                                  const Cancellation &cancel)
{
    auto data = FileInfoProvider::describe(resource);
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    if (resource.informationOnly) return data;
    auto file = std::make_unique<QFile>();
    QString error;
    if (!openPreviewFile(resource, *file, error)) {
        data.message = error;
        return data;
    }
    const auto mime = QMimeDatabase().mimeTypeForFileNameAndData(resource.path, file.get());
    file->seek(0);
    data.mimeType = mime.name();
    data.description = mime.comment();
    ImagePreviewProvider image;
    PdfPreviewProvider pdf;
    TextPreviewProvider text;
    if (pdf.supports(mime) || file->peek(1024).startsWith("%PDF-")) return pdf.load(std::move(file), std::move(data), target, cancel);
    // Actual image-reader capability wins over misleading file extensions.
    bool canReadImage = false;
    {
        QImageReader reader(file.get());
        canReadImage = reader.canRead();
    }
    file->seek(0);
    if (canReadImage || image.supports(mime)) return image.load(std::move(file), std::move(data), target, cancel);
    if (text.supports(mime)) return text.load(std::move(file), std::move(data), target, cancel);
    return data;
}
QWidget *PreviewRegistry::createWidget(const PreviewData &data, QWidget *parent)
{
    switch (data.kind) {
    case PreviewKind::Image: return ImagePreviewProvider().createWidget(data, parent);
    case PreviewKind::Pdf: return PdfPreviewProvider().createWidget(data, parent);
    case PreviewKind::Text: return TextPreviewProvider().createWidget(data, parent);
    default: return FileInfoProvider::createWidget(data, parent);
    }
}
QString PreviewRegistry::kindName(PreviewKind kind)
{
    switch (kind) {
    case PreviewKind::Image: return QStringLiteral("image");
    case PreviewKind::Pdf: return QStringLiteral("pdf");
    case PreviewKind::Text: return QStringLiteral("text");
    case PreviewKind::Information: return QStringLiteral("information");
    default: return QStringLiteral("none");
    }
}
} // namespace dzurkel

#include "PreviewRegistry.h"
#include "PreviewLibrary.h"
#include "table/SpreadsheetView.h"
#include "providers/FileInfoProvider.h"
#include "providers/ImagePreviewProvider.h"
#include "providers/PdfPreviewProvider.h"
#include "providers/TextPreviewProvider.h"
#include <QFile>
#include <QImageReader>
#include <QMimeDatabase>

namespace dzurkel {
PreviewData PreviewRegistry::load(const FileResource &resource, const QSize &target,
                                  const Cancellation &cancel, const std::function<void()> &contentSelected,
                                  const std::function<void(const QSize &)> &imageMetadataReady)
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
    data.readerId = PreviewLibrary::select(*file, mime, resource.path);
    auto reader = PreviewLibrary::createReader(data.readerId);
    file->seek(0);
    if (reader) {
        if (contentSelected) contentSelected();
        if (auto *image = dynamic_cast<ImagePreviewProvider *>(reader.get()))
            return image->loadWithMetadata(std::move(file), std::move(data), target, cancel, imageMetadataReady);
        return reader->load(std::move(file), std::move(data), target, cancel);
    }
    return data;
}
QWidget *PreviewRegistry::createWidget(const PreviewData &data, QWidget *parent)
{
    switch (data.kind) {
    case PreviewKind::Image: return ImagePreviewProvider().createWidget(data, parent);
    case PreviewKind::Pdf: return PdfPreviewProvider().createWidget(data, parent);
    case PreviewKind::Text: return TextPreviewProvider().createWidget(data, parent);
    case PreviewKind::Table: return new SpreadsheetView(data.table, parent);
    default: return FileInfoProvider::createWidget(data, parent);
    }
}
QString PreviewRegistry::kindName(PreviewKind kind)
{
    switch (kind) {
    case PreviewKind::Image: return QStringLiteral("image");
    case PreviewKind::Pdf: return QStringLiteral("pdf");
    case PreviewKind::Text: return QStringLiteral("text");
    case PreviewKind::Table: return QStringLiteral("table");
    case PreviewKind::Information: return QStringLiteral("information");
    default: return QStringLiteral("none");
    }
}
} // namespace dzurkel

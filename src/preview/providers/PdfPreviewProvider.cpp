#include "PdfPreviewProvider.h"
#include "preview/pdf/PdfView.h"
#include <QFile>

namespace dzurkel {
bool PdfPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.inherits(QStringLiteral("application/pdf"));
}
PreviewData PdfPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                   const QSize &, const Cancellation &cancel) const
{
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    // Parsing begins asynchronously in PdfSession, after the worker has
    // released its descriptor. Normal prepared PDFs bypass this worker open.
    if (data.resource.backingPath.isEmpty()) data.resource.backingPath = file->fileName();
    data.kind = PreviewKind::Pdf;
    data.message.clear();
    return data;
}
QWidget *PdfPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new PdfView(data, parent);
}
}

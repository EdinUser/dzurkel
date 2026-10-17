#include "XlsxPreviewProvider.h"
#include "preview/readers/XlsxReader.h"
#include "preview/table/SpreadsheetView.h"
#include <QFile>
namespace dzurkel {
bool XlsxPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.name() == QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
}
PreviewData XlsxPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                    const QSize &, const Cancellation &cancel) const
{
    auto result = XlsxReader::read(*file, cancel);
    if (result.cancelled) { data.kind = PreviewKind::Cancelled; return data; }
    if (!result.error.isEmpty()) { data.message = result.error; return data; }
    data.kind = PreviewKind::Table;
    data.table = result.document;
    data.truncated = result.document->partial;
    data.message = result.document->notice;
    data.detail = QStringLiteral("%1 sheet(s) · Saved results may be stale · Recognized dates/times normalized · Other formatting omitted")
        .arg(result.document->sheets.size());
    return data;
}
QWidget *XlsxPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new SpreadsheetView(data.table, parent);
}
}

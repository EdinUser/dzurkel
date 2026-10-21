#include "XlsxDecodeJob.h"
#include "preview/PreviewRecordWriter.h"
#include "preview/providers/XlsxPreviewProvider.h"
#include "preview/table/TabularDocument.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <unistd.h>

namespace dzurkel {
int serveXlsx(const QString &path)
{
    PreviewData data;
    data.resource.path = path;
    data.mimeType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    auto file = std::make_unique<QFile>();
    QString error;
    if (!openPreviewFile(data.resource, *file, error)) data.message = error;
    else data = XlsxPreviewProvider().load(std::move(file), std::move(data), {}, std::make_shared<std::atomic_bool>(false));
    PreviewRecordWriter writer;
    if (!data.table) {
        if (!writer.send({{"event", "error"}, {"message", data.message.left(4096)}})) return 2;
    } else {
        if (!writer.send({{"event", "begin"}, {"partial", data.table->partial}, {"notice", data.table->notice}})) return 2;
        for (const auto &sheet : data.table->sheets) {
            if (!writer.send({{"event", "sheet"}, {"name", sheet.name}, {"rows", sheet.rowCount}, {"columns", sheet.columnCount}})) return 2;
            for (auto cell = sheet.cells.cbegin(); cell != sheet.cells.cend(); ++cell) {
                if (!writer.send({{"event", "cell"}, {"row", int(cell.key() >> 32)},
                    {"column", int(cell.key() & 0xffffffff)}, {"value", cell.value().toString()}})) return 2;
            }
        }
        if (!writer.send({{"event", "end"}, {"detail", data.detail}})) return 2;
    }
    // Keep the pipe alive until the client has consumed the bounded result.
    writer.waitForReceipt();
    return 0;
}
}

#include "preview/PreviewRecordWriter.h"
#include "preview/providers/DocumentPreviewProvider.h"
#include "FormattedDocument.h"
#include "DocumentRecords.h"
#include <QFile>
#include <QColorSpace>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>

namespace dzurkel {
int serveDocument(const QString &path, const QString &reader)
{
    if (!DocumentPreviewProvider::acceptsReader(reader)) return 2;
    QImageReader::setAllocationLimit(64);
    PreviewData data;
    data.resource.path = path;
    data.readerId = reader;
    auto file = std::make_unique<QFile>();
    QString error;
    if (!openPreviewFile(data.resource, *file, error)) data.message = error;
    else data = DocumentPreviewProvider().load(std::move(file), std::move(data), {}, std::make_shared<std::atomic_bool>(false));
    PreviewRecordWriter writer(2 * StructuredTransferLimit);
    if (!writeDocumentRecords(data, [&writer](const QJsonObject &record) { return writer.send(record); })) return 2;
    writer.waitForReceipt();
    return 0;
}
bool writeDocumentRecords(const PreviewData &data, const std::function<bool(const QJsonObject &)> &send)
{
    if (data.kind == PreviewKind::Information) {
        if (!send({{"event", "error"}, {"message", data.message.left(4096)}, {"missingLibrary", data.missingLibrary}})) return false;
        return true;
    }
    if (!send({{"event", "document"}, {"partial", data.truncated}, {"text", data.kind == PreviewKind::Text}})) return false;
    const auto field = [&](const QString &name, const QString &text) {
        for (qsizetype offset = 0; offset < text.size();) {
            auto length = qMin<qsizetype>(16384, text.size() - offset);
            if (offset + length < text.size() && text.at(offset + length - 1).isHighSurrogate()) --length;
            if (!send({{"event", "field"}, {"name", name}, {"value", text.mid(offset, length)}})) return false;
            offset += length;
        }
        return true;
    };
    if (!field("message", data.message) || !field("detail", data.detail) || !field("text", data.text)) return false;
    if (data.document) {
        const auto &doc = *data.document;
        if (!field("html", doc.html) || !field("markdown", doc.markdown) || !field("plainText", doc.plainText)
            || !field("title", doc.title.left(4096)) || !field("author", doc.author.left(4096)) || !field("notice", doc.notice.left(4096))) return false;
        for (const auto &section : doc.sections)
            if (!send({{"event", "section"}, {"title", section.title}, {"anchor", section.anchor}})) return false;
        qint64 imageBytes = 0;
        for (auto it = doc.images.cbegin(); it != doc.images.cend(); ++it) {
            const auto &image = it.value();
            const auto profile = image.colorSpace().iccProfile();
            if (profile.size() > 1024 * 1024 || image.sizeInBytes() > 64 * 1024 * 1024 - imageBytes) return false;
            imageBytes += image.sizeInBytes();
            QJsonArray palette;
            for (auto color : image.colorTable()) palette.append(double(color));
            if (!send({{"event", "image"}, {"id", it.key()}, {"width", image.width()}, {"height", image.height()},
                {"format", int(image.format())}, {"bytes", double(image.sizeInBytes())}, {"palette", palette},
                {"profileBytes", profile.size()}, {"dpr", image.devicePixelRatio()},
                {"dpmX", image.dotsPerMeterX()}, {"dpmY", image.dotsPerMeterY()}})) return false;
            const auto chunks = [&](const QString &event, const char *bytes, qint64 size) {
                for (qint64 offset = 0; offset < size; offset += 32768) {
                    const auto chunk = QByteArray::fromRawData(bytes + offset, qMin<qint64>(32768, size - offset));
                    if (!send({{"event", event}, {"data", QString::fromLatin1(chunk.toBase64())}})) return false;
                }
                return true;
            };
            if (!chunks("image-data", reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes())
                || !chunks("image-profile", profile.constData(), profile.size()) || !send({{"event", "image-end"}})) return false;
        }
    }
    if (!send({{"event", "end"}, {"lines", data.textLineCount}})) return false;
    return true;
}
}

#include "TextDecoder.h"
#include <QFile>
#include "TextCodec.h"
namespace dzurkel {
DecodedText decodePreviewText(QIODevice &file, qint64 byteLimit, const Cancellation &cancel)
{
    DecodedText result;
    if (cancel->load()) { result.cancelled = true; return result; }
    QByteArray bytes = file.read(byteLimit + 1);
    const auto *source = qobject_cast<QFileDevice *>(&file);
    if (source && source->error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Text could not be read: %1").arg(file.errorString());
        return result;
    }
    result.truncated = bytes.size() > byteLimit;
    bytes.truncate(byteLimit);
    const auto decoded = TextCodec(bytes, !result.truncated).decode(bytes, true, !result.truncated);
    result.encoding = decoded.encoding;
    result.error = decoded.error;
    if (!result.error.isEmpty()) return result;
    if (cancel->load()) { result.cancelled = true; return result; }
    result.text = decoded.text;
    return result;
}
}

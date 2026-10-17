#pragma once
#include "preview/PreviewProvider.h"
class QFile;
namespace dzurkel {
struct DecodedText {
    QString text;
    QString encoding;
    QString error;
    bool truncated = false;
    bool cancelled = false;
};
DecodedText decodePreviewText(QFile &file, qint64 byteLimit, const Cancellation &cancel);
}

#pragma once
#include "preview/PreviewProvider.h"
class QIODevice;
namespace dzurkel {
struct DecodedText {
    QString text;
    QString encoding;
    QString error;
    bool truncated = false;
    bool cancelled = false;
};
DecodedText decodePreviewText(QIODevice &device, qint64 byteLimit, const Cancellation &cancel);
}

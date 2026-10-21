#pragma once
#include "preview/readers/TextCodec.h"
#include <QFile>
#include <QDateTime>
#include <memory>

namespace dzurkel {
struct TextChunk { QString text; qint64 textOffset = 0; qint64 end = 0; };
// Reuse QFile/OS seeking and Qt conversion for all seekable text formats.
class TextSource {
public:
    explicit TextSource(const QString &path);
    bool check();
    QByteArray bytes(qint64 offset, qint64 count);
    qint64 boundary(qint64 nominal, int lineLookahead = 0);
    TextChunk chunk(qint64 offset, qint64 bytes = 4096);
    qint64 size() const { return size_; }
    const TextCodec &codec() const { return *codec_; }
    QString error() const { return error_; }
private:
    QFile file_;
    qint64 size_ = 0;
    QDateTime modified_;
    QString error_;
    std::unique_ptr<TextCodec> codec_;
};
}

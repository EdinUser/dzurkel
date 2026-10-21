#include "TextSource.h"
#include "resources/FileResource.h"

namespace dzurkel {
TextSource::TextSource(const QString &path)
{
    FileResource resource;
    resource.path = path;
    if (!openPreviewFile(resource, file_, error_)) return;
    size_ = file_.size();
    modified_ = file_.fileTime(QFileDevice::FileModificationTime);
    codec_ = std::make_unique<TextCodec>(file_.peek(4096), size_ <= 4096);
}
bool TextSource::check()
{
    if (!error_.isEmpty()) return false;
    if (file_.size() != size_ || file_.fileTime(QFileDevice::FileModificationTime) != modified_)
        error_ = QStringLiteral("The source changed. Reopen the preview to continue.");
    return error_.isEmpty();
}
QByteArray TextSource::bytes(qint64 offset, qint64 count)
{
    if (!error_.isEmpty()) return {};
    if (offset < 0 || count < 0 || offset > size_ || !file_.seek(offset)) {
        error_ = QStringLiteral("Cannot seek the text source."); return {};
    }
    const auto result = file_.read(qMin(count, size_ - offset));
    if (result.size() != qMin(count, size_ - offset) || file_.error() != QFileDevice::NoError)
        error_ = QStringLiteral("Cannot read the text source.");
    return result;
}
qint64 TextSource::boundary(qint64 nominal, int lineLookahead)
{
    if (!nominal || nominal >= size_) return qMin(nominal, size_);
    const int unit = codec_->unitSize();
    nominal += (unit - nominal % unit) % unit;
    const auto data = bytes(nominal, lineLookahead + 8);
    for (qsizetype i = 0; i < qMin(qsizetype(lineLookahead), data.size()); i += unit) {
        const uint ch = codec_->codeUnit(data, i);
        if (ch == '\n') return nominal + i + unit;
        if (ch == '\r') return nominal + i + unit + (codec_->codeUnit(data, i + unit) == '\n' ? unit : 0);
    }
    qsizetype skip = 0;
    if (codec_->encoding() == QStringConverter::Utf8) {
        while (skip < data.size() && (uchar(data.at(skip)) & 0xc0) == 0x80 && skip < 3) ++skip;
    } else if (unit == 2 && codec_->codeUnit(data, 0) >= 0xdc00 && codec_->codeUnit(data, 0) <= 0xdfff) skip = 2;
    // Keep CRLF atomic for parsers which report record-end byte positions.
    if (!skip && codec_->codeUnit(data, 0) == '\n' && nominal >= unit) {
        if (codec_->codeUnit(bytes(nominal - unit, unit), 0) == '\r') skip = unit;
    }
    return qMin(nominal + skip, size_);
}
TextChunk TextSource::chunk(qint64 offset, qint64 count)
{
    if (!check()) return {};
    const auto end = boundary(qMin(size_, offset + count));
    const auto data = bytes(offset, end - offset);
    auto decoded = codec_->decode(data, !offset);
    if (!decoded.error.isEmpty()) error_ = decoded.error;
    return {decoded.text, offset ? offset : codec_->bomSize(), end};
}
}

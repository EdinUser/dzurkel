#include "TextCodec.h"
#include <QStringDecoder>
#include <QStringEncoder>

namespace dzurkel {
TextCodec::TextCodec(const QByteArray &sample, bool complete)
{
    encoding_ = QStringConverter::encodingForData(sample).value_or(QStringConverter::Utf8);
    QStringDecoder detector(encoding_, complete ? QStringConverter::Flag::Stateless : QStringConverter::Flag::Default);
    const QString probe = detector(sample);
    Q_UNUSED(probe);
    if (detector.hasError() && encoding_ == QStringConverter::Utf8) encoding_ = QStringConverter::Latin1;
    if (encoding_ == QStringConverter::Utf16) encoding_ = sample.startsWith("\xff\xfe") ? QStringConverter::Utf16LE : QStringConverter::Utf16BE;
    if (encoding_ == QStringConverter::Utf32) encoding_ = sample.startsWith("\xff\xfe") ? QStringConverter::Utf32LE : QStringConverter::Utf32BE;
    if (encoding_ == QStringConverter::Utf16LE || encoding_ == QStringConverter::Utf16BE) unit_ = 2;
    if (encoding_ == QStringConverter::Utf32LE || encoding_ == QStringConverter::Utf32BE) unit_ = 4;
    littleEndian_ = encoding_ != QStringConverter::Utf16BE && encoding_ != QStringConverter::Utf32BE;
    if (unit_ > 1 && (sample.startsWith("\xff\xfe") || sample.startsWith("\xfe\xff") || sample.startsWith(QByteArray::fromHex("0000feff")))) bom_ = unit_;
    if (encoding_ == QStringConverter::Utf8 && sample.startsWith(QByteArray::fromHex("efbbbf"))) bom_ = 3;
    name_ = QString::fromLatin1(QStringConverter::nameForEncoding(encoding_));
    if (encoding_ == QStringConverter::Latin1) name_ += QStringLiteral(" fallback; encoding not detected");
}
DecodedText TextCodec::decode(const QByteArray &bytes, bool initial, bool complete) const
{
    DecodedText result;
    result.encoding = name_;
    auto flags = QStringConverter::Flags(QStringConverter::Flag::ConvertInitialBom);
    if (complete) flags |= QStringConverter::Flag::Stateless;
    QStringDecoder decoder(encoding_, flags);
    result.text = decoder(bytes);
    if (initial && result.text.startsWith(QChar(0xfeff))) result.text.remove(0, 1);
    if (decoder.hasError()) {
        result.error = QStringLiteral("The text encoding is damaged or changed.");
        return result;
    }
    int controls = 0;
    for (const auto ch : result.text) {
        if (ch.isNull()) { result.error = QStringLiteral("Binary data detected; no text preview is available."); return result; }
        if (ch.category() == QChar::Other_Control && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\f') ++controls;
    }
    if (controls > qMax(1, int(result.text.size() / 100))) result.error = QStringLiteral("Binary control characters detected; no text preview is available.");
    return result;
}
qint64 TextCodec::encodedSize(QStringView text) const
{
    QStringEncoder encoder(encoding_);
    const QByteArray bytes = encoder(text);
    return bytes.size();
}
uint TextCodec::codeUnit(const QByteArray &bytes, qsizetype offset) const
{
    if (offset < 0 || offset + unit_ > bytes.size()) return 0;
    uint value = 0;
    for (int j = 0; j < unit_; ++j)
        value |= uint(uchar(bytes.at(offset + j))) << (8 * (littleEndian_ ? j : unit_ - j - 1));
    return value;
}
void TextCodec::normalizeLines(QString &text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QChar::LineSeparator, QChar('\n'));
    text.replace(QChar::ParagraphSeparator, QChar('\n'));
}
}

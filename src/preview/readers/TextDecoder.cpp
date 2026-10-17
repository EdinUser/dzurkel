#include "TextDecoder.h"
#include <QFile>
#include <QStringDecoder>
namespace dzurkel {
DecodedText decodePreviewText(QFile &file, qint64 byteLimit, const Cancellation &cancel)
{
    DecodedText result;
    if (cancel->load()) { result.cancelled = true; return result; }
    QByteArray bytes = file.read(byteLimit + 1);
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Text could not be read: %1").arg(file.errorString());
        return result;
    }
    result.truncated = bytes.size() > byteLimit;
    bytes.truncate(byteLimit);
    auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    const auto flags = result.truncated ? QStringConverter::Flag::Default : QStringConverter::Flag::Stateless;
    QStringDecoder decoder(encoding, flags);
    QString text = decoder(bytes);
    result.encoding = QString::fromLatin1(QStringConverter::nameForEncoding(encoding));
    if (decoder.hasError()) {
        if (encoding != QStringConverter::Utf8) {
            result.error = QStringLiteral("The declared text encoding is damaged.");
            return result;
        }
        text = QString::fromLatin1(bytes);
        result.encoding = QStringLiteral("Latin-1 fallback; encoding not detected");
    }
    int controls = 0;
    for (const auto character : text) {
        if (character.isNull()) {
            result.error = QStringLiteral("Binary data detected; no text preview is available.");
            return result;
        }
        if ((character.category() == QChar::Other_Control) && character != QChar('\n') && character != QChar('\r')
            && character != QChar('\t') && character != QChar('\f')) ++controls;
    }
    if (controls > qMax(1, int(text.size() / 100))) {
        result.error = QStringLiteral("Binary control characters detected; no text preview is available.");
        return result;
    }
    if (cancel->load()) { result.cancelled = true; return result; }
    result.text = std::move(text);
    return result;
}
}

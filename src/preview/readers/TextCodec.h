#pragma once
#include "TextDecoder.h"
#include <QStringConverter>
#include <QStringDecoder>

namespace dzurkel {
// Keep Qt's decoder carry across records and detect incomplete EOF using its
// protected converter state (available on the Qt 6.4 runtime baseline).
class IncrementalTextDecoder : public QStringDecoder {
public:
    using QStringDecoder::QStringDecoder;
    bool complete() const { return !hasError() && state.remainingChars == 0; }
};
// Shared encoding decision and Unicode validation for bounded and progressive
// text consumers. Qt owns decoding/encoding; this is source policy, not a codec.
class TextCodec {
public:
    explicit TextCodec(const QByteArray &sample, bool complete = false);
    DecodedText decode(const QByteArray &bytes, bool initial, bool complete = true) const;
    qint64 encodedSize(QStringView text) const;
    uint codeUnit(const QByteArray &bytes, qsizetype offset) const;
    int unitSize() const { return unit_; }
    int bomSize() const { return bom_; }
    QStringConverter::Encoding encoding() const { return encoding_; }
    QString name() const { return name_; }
    static void normalizeLines(QString &text);
private:
    QStringConverter::Encoding encoding_ = QStringConverter::Utf8;
    QString name_;
    int unit_ = 1;
    int bom_ = 0;
    bool littleEndian_ = true;
};
}

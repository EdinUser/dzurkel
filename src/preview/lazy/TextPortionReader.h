#pragma once
#include "TextSource.h"
#include <QJsonObject>
#include <QStringConverter>

namespace dzurkel {
// Fixed source intervals, adjusted forward to a nearby paragraph or Unicode
// boundary. Tokens can be revisited without a growing in-memory offset index.
class TextPortionReader {
public:
    static constexpr qint64 PortionBytes = 4096;
    explicit TextPortionReader(const QString &path);
    explicit TextPortionReader(std::shared_ptr<TextSource> source);
    QJsonObject read(const QString &token);
private:
    std::shared_ptr<TextSource> source_;
};
}

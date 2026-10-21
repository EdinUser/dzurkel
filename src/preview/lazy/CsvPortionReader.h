#pragma once
#include "TextPortionReader.h"
#include "preview/readers/CsvReader.h"

namespace dzurkel {
class CsvPortionReader {
public:
    explicit CsvPortionReader(const QString &path, bool tsv = false);
    QJsonObject read(const QString &token);
private:
    QJsonObject batch(qint64 start, qint64 row, qint64 end, bool backward);
    bool previous(qint64 end, qint64 row, qint64 &start, qint64 &firstRow);
    std::shared_ptr<TextSource> source_;
    QChar delimiter_;
    Cancellation cancel_ = std::make_shared<std::atomic_bool>(false);
    bool fallback_ = false;
};
}

#pragma once
#include "preview/PreviewProvider.h"
#include "preview/table/TabularDocument.h"
namespace dzurkel {
struct CsvResult {
    std::shared_ptr<TabularDocument> document;
    QString error;
    QChar delimiter;
    bool cancelled = false;
};
class CsvReader {
public:
    static constexpr qint64 ByteLimit = 2 * 1024 * 1024;
    static constexpr int RowLimit = 10000;
    static constexpr int ColumnLimit = 256;
    static constexpr int CellLimit = 100000;
    static constexpr int FieldLimit = 16384;
    static CsvResult read(const QString &text, bool truncated, const Cancellation &cancel, QChar delimiter = {});
    static QChar detectDelimiter(const QString &text, bool truncated, const Cancellation &cancel);
};
}

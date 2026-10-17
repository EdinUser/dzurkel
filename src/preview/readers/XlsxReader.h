#pragma once
#include "preview/PreviewProvider.h"
#include "preview/table/TabularDocument.h"
class QFile;
namespace dzurkel {
struct XlsxResult {
    std::shared_ptr<TabularDocument> document;
    QString error;
    bool cancelled = false;
};
class XlsxReader {
public:
    static constexpr qint64 ByteLimit = 16 * 1024 * 1024;
    static constexpr qint64 ExpandedLimit = 64 * 1024 * 1024;
    static constexpr qint64 PartLimit = 16 * 1024 * 1024;
    static constexpr int EntryLimit = 2048;
    static constexpr int SheetLimit = 32;
    static constexpr int RowLimit = 10000;
    static constexpr int ColumnLimit = 256;
    static constexpr int CellLimit = 100000;
    static constexpr int StringLimit = 100000;
    static constexpr int FieldLimit = 16384;
    static constexpr int CharacterLimit = 8 * 1024 * 1024;
    static XlsxResult read(QFile &file, const Cancellation &cancel);
};
}

#pragma once
#include "preview/PreviewProvider.h"
#include "preview/table/TabularDocument.h"
#include <QJsonObject>
class QFile;
namespace dzurkel {
struct XlsxResult {
    std::shared_ptr<TabularDocument> document;
    QString error;
    bool cancelled = false;
};
// Persistent helper-side cursor; package parsing/formatting is shared with the
// bounded synchronous reader. Qt and libzip own XML/ZIP decoding.
class XlsxPortionReader {
public:
    explicit XlsxPortionReader(const QString &path);
    ~XlsxPortionReader();
    QJsonObject read(const QString &token);
private:
    struct State;
    std::unique_ptr<State> state_;
};
class XlsxReader {
public:
    // Bytes actually read, rather than total source-file size.
    static constexpr qint64 MetadataReadLimit = 4 * 1024 * 1024;
    static constexpr qint64 SourceReadLimit = 128 * 1024 * 1024;
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

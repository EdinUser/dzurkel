#pragma once
#include "TabularDocument.h"
#include <QJsonObject>
#include <memory>

namespace dzurkel {
struct TableRowBatch {
    static constexpr int RowLimit = 64;
    static constexpr int CharacterTarget = 16384;
    qint64 firstRow = 0;
    std::shared_ptr<const TabularSheet> sheet;
};
// Common validated payload for CSV now and worksheet batches later.
bool decodeTableRowBatch(const QJsonObject &content, TableRowBatch &batch);
}

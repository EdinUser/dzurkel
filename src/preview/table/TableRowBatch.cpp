#include "TableRowBatch.h"
#include "preview/readers/CsvReader.h"
#include <QJsonArray>
#include <limits>

namespace dzurkel {
bool decodeTableRowBatch(const QJsonObject &content, TableRowBatch &batch)
{
    const auto offset = content.value("rowOffset").toInteger(-1);
    const auto columns = content.value("columns").toInteger(-1);
    const auto name = content.value("sheet").toString();
    if (content.value("kind") != QJsonValue(QStringLiteral("table")) || !content.value("rows").isArray()
        || name.isEmpty() || name.size() > 255 || offset < 0 || offset > std::numeric_limits<qint64>::max() - TableRowBatch::RowLimit
        || columns < 0 || columns > CsvReader::ColumnLimit) return false;
    const auto rows = content.value("rows").toArray();
    if (rows.size() > TableRowBatch::RowLimit) return false;
    auto sheet = std::make_shared<TabularSheet>();
    sheet->name = name;
    sheet->rowCount = int(rows.size());
    sheet->columnCount = int(columns);
    int characters = 0;
    for (int row = 0; row < rows.size(); ++row) {
        if (!rows.at(row).isArray()) return false;
        const auto fields = rows.at(row).toArray();
        if (fields.size() > columns) return false;
        for (int column = 0; column < fields.size(); ++column) {
            if (!fields.at(column).isString()) return false;
            const auto text = fields.at(column).toString();
            if (text.size() > CsvReader::FieldLimit) return false;
            characters += int(text.size());
            if (characters > TableRowBatch::CharacterTarget + CsvReader::FieldLimit * CsvReader::ColumnLimit) return false;
            if (!text.isEmpty()) sheet->cells.insert(TabularSheet::key(row, column), text);
        }
    }
    batch = {offset, std::move(sheet)};
    return true;
}
}

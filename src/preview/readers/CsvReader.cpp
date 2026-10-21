#include "CsvReader.h"
#include "CsvRecordParser.h"
#include <QElapsedTimer>
#include <QVector>
#include <algorithm>

namespace dzurkel {
namespace {
struct Parsed {
    CsvResult result;
    QVector<int> widths;
};
Parsed parse(const QString &text, bool truncated, const Cancellation &cancel, QChar delimiter, int rowLimit)
{
    Parsed parsed;
    auto &result = parsed.result;
    result.delimiter = delimiter;
    result.document = std::make_shared<TabularDocument>();
    result.document->sheets.append(TabularSheet{QStringLiteral("Data"), 0, 0, {}});
    auto &sheet = result.document->sheets.first();
    int cells = 0;
    auto limit = [&] {
        result.document->partial = true;
        result.document->notice = QStringLiteral("Partial table preview — limited to 2 MiB, 10,000 rows, 256 columns, 100,000 cells and 16,384 characters per field. Only complete records are shown.");
    };
    CsvRecordParser parser(delimiter, cancel);
    const auto step = parser.feed(text, !truncated, [&](QStringList row) {
        if (sheet.rowCount >= rowLimit || cells + row.size() > CsvReader::CellLimit) { limit(); return false; }
        for (int column = 0; column < row.size(); ++column)
            if (!row.at(column).isEmpty()) sheet.cells.insert(TabularSheet::key(sheet.rowCount, column), row.at(column));
        sheet.columnCount = std::max(sheet.columnCount, int(row.size()));
        ++sheet.rowCount;
        cells += row.size();
        parsed.widths.append(row.size());
        return sheet.rowCount < rowLimit;
    });
    result.cancelled = step.cancelled;
    result.error = step.error;
    if (truncated || step.limited || step.consumed < text.size()) limit();

    return parsed;
}
}
QChar CsvReader::detectDelimiter(const QString &text, bool truncated, const Cancellation &cancel)
{
    QChar delimiter;
    {
        // Prefer consistent field counts across complete sample records. Quotes
        // and multiline records use the same parser as the actual preview.
        const auto sample = text.left(65536);
        int best = -1;
        delimiter = QChar(',');
        for (const auto candidate : {QChar(','), QChar(';'), QChar('\t')}) {
            const auto parsed = parse(sample, truncated || text.size() > sample.size(), cancel, candidate, 32);
            if (parsed.result.cancelled) return delimiter;
            if (!parsed.result.error.isEmpty()) continue;
            QHash<int, int> counts;
            for (int width : parsed.widths) if (width > 1) ++counts[width];
            int consistent = 0;
            for (int count : counts) consistent = std::max(consistent, count);
            const int score = parsed.widths.isEmpty() ? 0 : consistent * 1000 / parsed.widths.size();
            if (score > best) { best = score; delimiter = candidate; }
        }
    }
    return delimiter;
}
CsvResult CsvReader::read(const QString &text, bool truncated, const Cancellation &cancel, QChar delimiter)
{
    if (delimiter.isNull()) delimiter = detectDelimiter(text, truncated, cancel);
    return parse(text, truncated, cancel, delimiter, RowLimit).result;
}
}

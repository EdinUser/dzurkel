#include "CsvReader.h"
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
    enum class State { Start, Unquoted, Quoted, Closed } state = State::Start;
    QString field;
    QStringList row;
    int cells = 0;
    bool started = false;
    QElapsedTimer timer;
    timer.start();
    auto limit = [&] {
        result.document->partial = true;
        result.document->notice = QStringLiteral("Partial table preview — limited to 2 MiB, 10,000 rows, 256 columns, 100,000 cells and 16,384 characters per field. Only complete records are shown.");
    };
    auto fieldDone = [&] {
        if (row.size() >= CsvReader::ColumnLimit) { limit(); return false; }
        row.append(std::move(field));
        field.clear();
        state = State::Start;
        return true;
    };
    auto rowDone = [&] {
        if (!fieldDone()) return false;
        if (sheet.rowCount >= rowLimit || cells + row.size() > CsvReader::CellLimit) { limit(); return false; }
        for (int column = 0; column < row.size(); ++column)
            if (!row.at(column).isEmpty()) sheet.cells.insert(TabularSheet::key(sheet.rowCount, column), row.at(column));
        sheet.columnCount = std::max(sheet.columnCount, int(row.size()));
        ++sheet.rowCount;
        cells += row.size();
        parsed.widths.append(row.size());
        row.clear();
        started = false;
        return true;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        if ((i & 1023) == 0) {
            if (cancel->load()) { result.cancelled = true; return parsed; }
            if (timer.elapsed() >= 5000) {
                limit();
                result.document->notice = QStringLiteral("Partial table preview — parsing reached its 5-second work limit. Only complete records are shown.");
                return parsed;
            }
        }
        if (sheet.rowCount >= rowLimit) { limit(); return parsed; }
        QChar ch = text.at(i);
        if (ch == QChar('\r')) {
            if (i + 1 < text.size() && text.at(i + 1) == QChar('\n')) ++i;
            ch = QChar('\n');
        }
        started = true;
        if (state == State::Quoted) {
            if (ch == QChar('"')) state = State::Closed;
            else field += ch;
        } else if (state == State::Closed) {
            if (ch == QChar('"')) { field += ch; state = State::Quoted; }
            else if (ch == delimiter) { if (!fieldDone()) return parsed; }
            else if (ch == QChar('\n')) { if (!rowDone()) return parsed; }
            else { result.error = QStringLiteral("Unexpected content after a quoted CSV field."); return parsed; }
        } else if (ch == delimiter) {
            if (!fieldDone()) return parsed;
        } else if (ch == QChar('\n')) {
            if (!rowDone()) return parsed;
        } else if (ch == QChar('"')) {
            if (state != State::Start) { result.error = QStringLiteral("Quote inside an unquoted CSV field."); return parsed; }
            state = State::Quoted;
        } else {
            field += ch;
            state = State::Unquoted;
        }
        if (field.size() > CsvReader::FieldLimit) { limit(); return parsed; }
    }
    if (cancel->load()) { result.cancelled = true; return parsed; }
    if (truncated) limit(); // Never present a byte-truncated last record as complete.
    else if (state == State::Quoted) result.error = QStringLiteral("Unclosed quoted CSV field.");
    else if (started) rowDone();
    return parsed;
}
}
CsvResult CsvReader::read(const QString &text, bool truncated, const Cancellation &cancel, QChar delimiter)
{
    if (delimiter.isNull()) {
        // Prefer consistent field counts across complete sample records. Quotes
        // and multiline records use the same parser as the actual preview.
        const auto sample = text.left(65536);
        int best = -1;
        delimiter = QChar(',');
        for (const auto candidate : {QChar(','), QChar(';'), QChar('\t')}) {
            const auto parsed = parse(sample, truncated || text.size() > sample.size(), cancel, candidate, 32);
            if (parsed.result.cancelled) return parsed.result;
            if (!parsed.result.error.isEmpty()) continue;
            QHash<int, int> counts;
            for (int width : parsed.widths) if (width > 1) ++counts[width];
            int consistent = 0;
            for (int count : counts) consistent = std::max(consistent, count);
            const int score = parsed.widths.isEmpty() ? 0 : consistent * 1000 / parsed.widths.size();
            if (score > best) { best = score; delimiter = candidate; }
        }
    }
    return parse(text, truncated, cancel, delimiter, RowLimit).result;
}
}

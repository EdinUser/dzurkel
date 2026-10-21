#include "CsvPortionReader.h"
#include "preview/readers/CsvRecordParser.h"
#include "preview/table/TableRowBatch.h"
#include <QJsonArray>
#include <QElapsedTimer>

namespace dzurkel {
namespace {
QString continuation(bool backward, qint64 offset, qint64 row)
{
    return QStringLiteral("%1:%2:%3").arg(backward ? QChar('b') : QChar('f')).arg(offset).arg(row);
}
QJsonObject failure(const QString &error) { return {{"error", error}}; }
}
CsvPortionReader::CsvPortionReader(const QString &path, bool tsv) : source_(std::make_shared<TextSource>(path))
{
    if (tsv) delimiter_ = QChar('\t');
}
bool CsvPortionReader::previous(qint64 end, qint64 row, qint64 &start, qint64 &firstRow)
{
    // At a validated record boundary, quote parity is reversible: escaped quote
    // pairs toggle twice and embedded newlines stay inside quotes. OS seeking
    // and the shared encoding unit reader avoid an ever-growing offset index.
    const auto &codec = source_->codec();
    const int unit = codec.unitSize();
    const qint64 beginning = codec.bomSize();
    qint64 cursor = end, bufferStart = -1;
    QByteArray buffer;
    const auto take = [&]() -> uint {
        if (cursor <= beginning) return 0;
        cursor -= unit;
        if (cursor < bufferStart || cursor + unit > bufferStart + buffer.size()) {
            bufferStart = qMax(beginning, cursor / 4096 * 4096);
            buffer = source_->bytes(bufferStart, 4096);
        }
        return codec.codeUnit(buffer, qsizetype(cursor - bufferStart));
    };
    uint last = take();
    if (last == '\n' && cursor > beginning) {
        const auto saved = cursor;
        if (take() != '\r') cursor = saved;
    } else if (last != '\r') cursor = end;
    bool quoted = false;
    int records = 1;
    QElapsedTimer clock;
    clock.start();
    while (cursor > beginning) {
        if (clock.elapsed() > 5000 || end - cursor > 64 * 1024 * 1024 || !source_->error().isEmpty()) return false;
        const auto after = cursor;
        const auto ch = take();
        if (ch == '"') quoted = !quoted;
        else if (!quoted && (ch == '\n' || ch == '\r')) {
            if (records >= TableRowBatch::RowLimit || end - after >= TableRowBatch::CharacterTarget) {
                start = after;
                firstRow = row - records;
                return firstRow >= 0;
            }
            ++records;
            if (ch == '\n' && cursor > beginning) {
                const auto saved = cursor;
                if (take() != '\r') cursor = saved;
            }
        }
    }
    start = 0;
    firstRow = row - records;
    return firstRow == 0 && !quoted && source_->error().isEmpty();
}
QJsonObject CsvPortionReader::batch(qint64 start, qint64 row, qint64 end, bool backward)
{
    CsvRecordParser parser(delimiter_, cancel_);
    QJsonArray rows;
    int characters = 0, columns = 0;
    qint64 position = start;
    bool stopped = false;
    QString error;
    while (position < end && !stopped) {
        const auto chunk = source_->chunk(position, qMin(qint64(4096), end - position));
        if (!source_->error().isEmpty()) return failure(source_->error());
        const auto step = parser.feed(chunk.text, chunk.end == source_->size(), [&](QStringList fields) {
            QJsonArray values;
            for (const auto &field : fields) { characters += int(field.size()); values.append(field); }
            columns = qMax(columns, int(fields.size()));
            rows.append(values);
            stopped = !backward && (rows.size() >= TableRowBatch::RowLimit || characters >= TableRowBatch::CharacterTarget);
            return !stopped;
        });
        if (!step.error.isEmpty()) error = step.error;
        if (step.limited) error = QStringLiteral("CSV record exceeds the field, column or parsing-work budget.");
        if (!error.isEmpty()) return failure(error);
        position = step.consumed == chunk.text.size() ? chunk.end
            : chunk.textOffset + source_->codec().encodedSize(QStringView(chunk.text).left(step.consumed));
        if (position <= start && chunk.end > start) return failure(QStringLiteral("CSV reader made no progress."));
    }
    if (backward && position != end) return failure(QStringLiteral("Invalid CSV backward boundary."));
    if (!source_->check()) return failure(source_->error());
    const auto separator = delimiter_ == '\t' ? QStringLiteral("Tab") : delimiter_ == ';' ? QStringLiteral("Semicolon") : QStringLiteral("Comma");
    return {{"previous", row ? continuation(true, start, row) : QString()},
        {"next", position < source_->size() ? continuation(false, position, row + rows.size()) : QString()},
        {"content", QJsonObject{{"kind", "table"}, {"sheet", "Data"}, {"rowOffset", row},
            {"rows", rows}, {"columns", columns}, {"encoding", source_->codec().name()}, {"delimiter", separator}}}};
}
QJsonObject CsvPortionReader::read(const QString &token)
{
    if (!source_->check()) return failure(source_->error());
    if (fallback_) return TextPortionReader(source_).read(token);
    if (delimiter_.isNull()) {
        const auto sample = source_->chunk(0, 65536);
        if (!source_->error().isEmpty()) return failure(source_->error());
        delimiter_ = CsvReader::detectDelimiter(sample.text, sample.end < source_->size(), cancel_);
    }
    qint64 start = 0, row = 0, end = source_->size();
    bool backward = false;
    if (token != QStringLiteral("0")) {
        const auto parts = token.split(':');
        bool offsetOk = false, rowOk = false;
        if (parts.size() != 3 || (parts.at(0) != QLatin1String("f") && parts.at(0) != QLatin1String("b"))) return failure(QStringLiteral("Invalid CSV continuation."));
        start = parts.at(1).toLongLong(&offsetOk);
        row = parts.at(2).toLongLong(&rowOk);
        backward = parts.at(0) == QLatin1String("b");
        if (!offsetOk || !rowOk || start < 0 || start > end || row < 0 || row > source_->size()
            || start % source_->codec().unitSize() || continuation(backward, start, row) != token)
            return failure(QStringLiteral("Invalid CSV continuation."));
    }
    if (backward) {
        end = start;
        if (!row || !previous(end, row, start, row)) return failure(QStringLiteral("Cannot locate the previous complete CSV records within the work budget."));
    }
    auto result = batch(start, row, end, backward);
    if (token == QStringLiteral("0") && result.contains("error") && source_->error().isEmpty()) {
        fallback_ = true;
        auto text = TextPortionReader(source_).read(QStringLiteral("0"));
        auto content = text.value("content").toObject();
        content.insert(QStringLiteral("notice"), result.value("error").toString() + QStringLiteral(" Showing text instead."));
        text.insert(QStringLiteral("content"), content);
        return text;
    }
    return result;
}
}

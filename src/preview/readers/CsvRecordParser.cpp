#include "CsvRecordParser.h"
#include <utility>

namespace dzurkel {
CsvRecordParser::CsvRecordParser(QChar delimiter, Cancellation cancel)
    : delimiter_(delimiter), cancel_(std::move(cancel)) { clock_.start(); }
bool CsvRecordParser::fieldDone()
{
    if (row_.size() >= CsvReader::ColumnLimit) return false;
    row_.append(std::move(field_));
    field_.clear();
    state_ = State::Start;
    return true;
}
CsvParseStep CsvRecordParser::feed(QStringView text, bool final, const RowSink &sink)
{
    CsvParseStep result;
    const auto rowDone = [&] {
        if (!fieldDone()) { result.limited = true; return false; }
        started_ = false;
        return sink(std::exchange(row_, {}));
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        if ((i & 1023) == 0) {
            if (cancel_->load()) { result.cancelled = true; return result; }
            if (clock_.elapsed() >= 5000) { result.limited = true; return result; }
        }
        QChar ch = text.at(i);
        result.consumed = i + 1;
        if (skipLf_) { skipLf_ = false; if (ch == '\n') continue; }
        if (ch == '\r') { ch = '\n'; skipLf_ = true; }
        started_ = true;
        bool completed = false;
        if (state_ == State::Quoted) {
            if (ch == '"') state_ = State::Closed;
            else field_ += ch;
        } else if (state_ == State::Closed) {
            if (ch == '"') { field_ += ch; state_ = State::Quoted; }
            else if (ch == delimiter_) { if (!fieldDone()) { result.limited = true; return result; } }
            else if (ch == '\n') completed = true;
            else { result.error = QStringLiteral("Unexpected content after a quoted CSV field."); return result; }
        } else if (ch == delimiter_) {
            if (!fieldDone()) { result.limited = true; return result; }
        } else if (ch == '\n') completed = true;
        else if (ch == '"') {
            if (state_ != State::Start) { result.error = QStringLiteral("Quote inside an unquoted CSV field."); return result; }
            state_ = State::Quoted;
        } else { field_ += ch; state_ = State::Unquoted; }
        if (field_.size() > CsvReader::FieldLimit) { result.limited = true; return result; }
        if (completed && !rowDone()) {
            // Canonical continuation points consume both characters of CRLF.
            if (skipLf_ && i + 1 < text.size() && text.at(i + 1) == '\n') { ++result.consumed; skipLf_ = false; }
            return result;
        }
    }
    if (cancel_->load()) { result.cancelled = true; return result; }
    if (final) {
        if (state_ == State::Quoted) result.error = QStringLiteral("Unclosed quoted CSV field.");
        else if (started_) rowDone();
    }
    return result;
}
}

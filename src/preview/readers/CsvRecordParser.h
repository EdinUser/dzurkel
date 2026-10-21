#pragma once
#include "CsvReader.h"
#include <QElapsedTimer>
#include <functional>

namespace dzurkel {
struct CsvParseStep {
    qsizetype consumed = 0;
    QString error;
    bool limited = false;
    bool cancelled = false;
};
// The existing CSV state machine, extracted so prefix and progressive readers
// use the same quote, escape, CRLF, field and column rules.
class CsvRecordParser {
public:
    using RowSink = std::function<bool(QStringList)>;
    CsvRecordParser(QChar delimiter, Cancellation cancel);
    CsvParseStep feed(QStringView text, bool final, const RowSink &sink);
private:
    enum class State { Start, Unquoted, Quoted, Closed } state_ = State::Start;
    bool fieldDone();
    QChar delimiter_;
    Cancellation cancel_;
    QElapsedTimer clock_;
    QString field_;
    QStringList row_;
    bool started_ = false;
    bool skipLf_ = false;
};
}

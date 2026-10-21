#pragma once
#include "DocumentReaderInternal.h"
#include "TextCodec.h"
#include <QStringDecoder>
#include <optional>

namespace dzurkel::document {
// Qt owns encoding detection/conversion. Retain just one bounded physical line
// and decoder carry, shared by the line-oriented document readers.
class DocumentLines {
public:
    DocumentLines(Context &context, QIODevice &input, bool utf8Only = false)
        : c(context), input_(input), codec_(input.peek(4096)), decoder_(codec_.encoding()) {
        if (utf8Only && codec_.encoding() != QStringConverter::Utf8) c.invalid();
    }
    std::optional<QString> next() {
        for (;;) {
            c.check();
            qsizetype at = -1;
            for (qsizetype i = 0; i < pending_.size(); ++i)
                if (pending_[i] == QChar('\r') || pending_[i] == QChar('\n')) { at = i; break; }
            if (at >= 0 && (pending_[at] != QChar('\r') || at + 1 < pending_.size() || ended_)) {
                const int width = pending_[at] == QChar('\r') && at + 1 < pending_.size() && pending_[at + 1] == QChar('\n') ? 2 : 1;
                auto line = pending_.left(at); pending_.remove(0, at + width); return line;
            }
            if (ended_) {
                if (pending_.isEmpty()) return {};
                auto line = std::move(pending_); pending_.clear(); return line;
            }
            auto bytes = input_.read(4096);
            if (bytes.isEmpty()) { ended_ = true; bytes = QByteArray(); }
            QString text = decoder_.decode(bytes);
            if (ended_ && !decoder_.complete()) c.invalid();
            if (decoder_.hasError() || text.contains(QChar::Null)) c.invalid();
            if (initial_) { if (text.startsWith(QChar(0xfeff))) text.remove(0, 1); initial_ = false; }
            pending_ += text;
            if (pending_.size() > DocumentReader::CharacterLimit) {
                if (c.inputTruncated) throw Stop{{}, false, true};
                c.limit();
            }
        }
    }
private:
    Context &c;
    QIODevice &input_;
    TextCodec codec_;
    IncrementalTextDecoder decoder_;
    QString pending_;
    bool initial_ = true, ended_ = false;
};
}

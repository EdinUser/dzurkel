#pragma once
#include "StructuredDecodeJob.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <unistd.h>

namespace dzurkel {
class PreviewRecordWriter {
public:
    explicit PreviewRecordWriter(qint64 limit = StructuredTransferLimit) : limit_(limit) {}
    bool send(const QJsonObject &object) {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (bytes.size() > StructuredRecordLimit || bytes.size() > limit_ - sent_) return false;
        sent_ += bytes.size();
        qint64 offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(STDOUT_FILENO, bytes.constData() + offset, size_t(bytes.size() - offset));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            offset += count;
        }
        return true;
    }
    static void waitForReceipt() {
        char ack;
        while (::read(STDIN_FILENO, &ack, 1) < 0 && errno == EINTR) {}
    }
    // Reuse the bounded JSON-lines writer for larger logical lazy portions.
    // Framing stays independent of text, rows, images or document semantics.
    bool sendChunked(const QJsonObject &object, qint64 payloadLimit) {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (bytes.size() + 1 <= StructuredRecordLimit) return send(object);
        if (bytes.size() > payloadLimit) return false;
        const auto id = object.value("id"), token = object.value("token");
        if (!send({{"id", id}, {"token", token}, {"payloadBytes", bytes.size()}})) return false;
        for (qint64 offset = 0; offset < bytes.size(); offset += 32768) {
            if (!send({{"id", id}, {"token", token}, {"offset", offset},
                {"data", QString::fromLatin1(bytes.mid(offset, 32768).toBase64())}})) return false;
        }
        return true;
    }
private:
    qint64 limit_;
    qint64 sent_ = 0;
};
}

#include "TextPortionReader.h"

namespace dzurkel {
TextPortionReader::TextPortionReader(const QString &path) : source_(std::make_shared<TextSource>(path)) {}
TextPortionReader::TextPortionReader(std::shared_ptr<TextSource> source) : source_(std::move(source)) {}
QJsonObject TextPortionReader::read(const QString &token)
{
    auto failure = [](const QString &error) { return QJsonObject{{"error", error}}; };
    if (!source_->check()) return failure(source_->error());
    const auto size = source_->size();
    bool ok = false;
    const bool last = token == QLatin1String("end");
    auto index = token.toLongLong(&ok);
    const auto count = qMax(qint64(1), size / PortionBytes + bool(size % PortionBytes));
    if (last) index = count - 1;
    if (!last && (!ok || index < 0 || index >= count || QString::number(index) != token))
        return failure(QStringLiteral("Invalid text continuation."));
    auto start = source_->boundary(index * PortionBytes, 1024);
    // Nearby line-boundary lookahead can leave the final nominal interval
    // empty. Show the preceding useful interval instead of a blank EOF view.
    if (last && index && start == size) start = source_->boundary(--index * PortionBytes, 1024);
    const auto end = source_->boundary(qMin(size, (index + 1) * PortionBytes), 1024);
    auto decoded = source_->codec().decode(source_->bytes(start, end - start), !start);
    if (!source_->error().isEmpty()) return failure(source_->error());
    if (!decoded.error.isEmpty()) return failure(decoded.error);
    TextCodec::normalizeLines(decoded.text);
    return {{"previous", index ? QString::number(index - 1) : QString()},
        {"next", end < size && index + 1 < count ? QString::number(index + 1) : QString()},
        {"content", QJsonObject{{"text", decoded.text}, {"encoding", decoded.encoding}}}};
}
}

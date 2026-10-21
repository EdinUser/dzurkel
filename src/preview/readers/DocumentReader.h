#pragma once
#include "preview/PreviewProvider.h"
#include "preview/document/FormattedDocument.h"
#include <QJsonObject>

namespace dzurkel {
struct DocumentResult {
    std::shared_ptr<FormattedDocument> document;
    QString error;
    bool cancelled = false;
};
class RichDocumentPortionReader {
public:
    explicit RichDocumentPortionReader(const QString &path, const QString &format);
    ~RichDocumentPortionReader();
    QJsonObject read(const QString &token);
private:
    struct State;
    std::unique_ptr<State> state_;
};
class EpubPortionReader : public RichDocumentPortionReader {
public:
    explicit EpubPortionReader(const QString &path) : RichDocumentPortionReader(path, QStringLiteral("epub")) {}
};
class DocumentReader {
public:
    static bool supportsPortions(const QString &format) {
        return QStringList{"docx", "epub", "rtf", "doc", "fb2", "sfb", "mobi", "markdown"}.contains(format);
    }
    static constexpr qint64 InputPrefixLimit = 16 * 1024 * 1024;
    static constexpr int CharacterLimit = 200000;
    static DocumentResult read(QFile &file, const QString &format, const Cancellation &cancel);
};
}

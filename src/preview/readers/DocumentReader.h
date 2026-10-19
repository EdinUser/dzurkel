#pragma once
#include "preview/PreviewProvider.h"
#include "preview/document/FormattedDocument.h"

namespace dzurkel {
struct DocumentResult {
    std::shared_ptr<FormattedDocument> document;
    QString error;
    bool cancelled = false;
};
class DocumentReader {
public:
    static constexpr qint64 ByteLimit = 16 * 1024 * 1024;
    static constexpr int CharacterLimit = 200000;
    static DocumentResult read(QFile &file, const QString &format, const Cancellation &cancel);
};
}

#pragma once
#include "PreviewProvider.h"
#include <optional>

namespace dzurkel {
enum class SourceAccess { BoundedPrefix, Streaming, SeekableSource };
struct ReaderCapability {
    QString id;
    QString title;
    QString icon;
    QString informationObject;
    QString description;
    SourceAccess access;
    qint64 sourceLimit;
};
// Compiled-in reader catalog. Format selection, source budgets and Settings
// descriptions share this catalog. Optional reader adapters use controlled
// application-relative loading; this catalog never installs packages.
class PreviewLibrary {
public:
    static QList<ReaderCapability> readers();
    static std::optional<ReaderCapability> capability(const QString &id);
    static QString select(QFile &file, const QMimeType &mime, const QString &originalPath);
    static std::unique_ptr<PreviewProvider> createReader(const QString &id);
};
}

#pragma once
#include "resources/FileResource.h"
#include <QDateTime>
#include <QImage>
#include <QMimeType>
#include <atomic>
#include <memory>

class QFile;
class QPdfDocument;
class QWidget;

namespace dzurkel {
struct TabularDocument;
struct FormattedDocument;
using Cancellation = std::shared_ptr<std::atomic_bool>;
enum class PreviewKind { Cancelled, Image, Pdf, Text, Table, Audio, Video, Information, Document };

struct PreviewData {
    PreviewKind kind = PreviewKind::Information;
    FileResource resource;
    QString readerId;
    QString mimeType;
    QString description;
    qint64 size = 0;
    QDateTime modified;
    QString linkTarget;
    QString message;
    QString detail;
    QString mediaSummary;
    QString missingLibrary; // Known reader dependency; keeps unsupported/damaged input distinct.
    QImage image;
    QSize imageSize; // Oriented source dimensions; decoded pixels remain bounded.
    QList<QImage> animationFrames;
    QList<int> animationDelays;
    int animationLoops = 0; // Additional loops; -1 means repeat indefinitely.
    QString text;
    int textLineCount = 0; // Physical lines in the bounded text, excluding a trailing empty paragraph.
    std::shared_ptr<const TabularDocument> table;
    std::shared_ptr<const FormattedDocument> document;
    bool truncated = false;
    std::shared_ptr<QPdfDocument> pdf;
    QList<QSizeF> pdfPageSizes;
};

class PreviewProvider {
public:
    virtual ~PreviewProvider() = default;
    virtual bool supports(const QMimeType &mime) const = 0;
    // Worker-thread loading; the file descriptor is owned by the provider.
    virtual PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                             const QSize &target, const Cancellation &cancel) const = 0;
    // GUI-thread construction; ownership transfers to the caller/Qt parent.
    virtual QWidget *createWidget(const PreviewData &data, QWidget *parent) const = 0;
};
} // namespace dzurkel

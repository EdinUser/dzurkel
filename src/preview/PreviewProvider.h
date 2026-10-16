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
using Cancellation = std::shared_ptr<std::atomic_bool>;
enum class PreviewKind { Cancelled, Image, Pdf, Text, Information };

struct PreviewData {
    PreviewKind kind = PreviewKind::Information;
    FileResource resource;
    QString mimeType;
    QString description;
    qint64 size = 0;
    QDateTime modified;
    QString linkTarget;
    QString message;
    QString detail;
    QImage image;
    QString text;
    bool truncated = false;
    std::shared_ptr<QPdfDocument> pdf;
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

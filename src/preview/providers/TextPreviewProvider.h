#pragma once
#include "preview/PreviewProvider.h"
class QIODevice;

namespace dzurkel {
class TextPreviewProvider : public PreviewProvider {
public:
    static constexpr qint64 ByteLimit = 2 * 1024 * 1024;
    static constexpr int LineLimit = 20000;
    static constexpr int LineLengthLimit = 16384;
    bool supports(const QMimeType &mime) const override;
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel) const override;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
    PreviewData loadFromDevice(QIODevice &device, PreviewData data, const Cancellation &cancel) const;
};
} // namespace dzurkel

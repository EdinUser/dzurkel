#pragma once
#include "preview/PreviewProvider.h"

namespace dzurkel {
class PdfPreviewProvider : public PreviewProvider {
public:
    static constexpr qint64 ByteLimit = 64 * 1024 * 1024;
    bool supports(const QMimeType &mime) const override;
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel) const override;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
};
} // namespace dzurkel

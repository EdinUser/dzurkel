#pragma once
#include "preview/PreviewProvider.h"

namespace dzurkel {
class ImagePreviewProvider : public PreviewProvider {
public:
    static constexpr int AllocationLimitMiB = 64;
    bool supports(const QMimeType &mime) const override;
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel) const override;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
};
} // namespace dzurkel

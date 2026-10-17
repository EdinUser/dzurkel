#pragma once
#include "preview/PreviewProvider.h"
#include <functional>

namespace dzurkel {
class ImagePreviewProvider : public PreviewProvider {
public:
    static constexpr int AllocationLimitMiB = 64;
    static constexpr int AnimationFrameLimit = 300;
    static constexpr int AnimationWorkMs = 5000;
    bool supports(const QMimeType &mime) const override;
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel) const override;
    PreviewData loadWithMetadata(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel,
                     const std::function<void(const QSize &)> &metadataReady) const;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
};
} // namespace dzurkel

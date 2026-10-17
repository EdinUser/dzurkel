#pragma once
#include "PreviewProvider.h"
#include <functional>

namespace dzurkel {
class PreviewRegistry {
public:
    static PreviewData load(const FileResource &resource, const QSize &target,
                            const Cancellation &cancel, const std::function<void()> &contentSelected = {},
                            const std::function<void(const QSize &)> &imageMetadataReady = {});
    static QWidget *createWidget(const PreviewData &data, QWidget *parent);
    static QString kindName(PreviewKind kind);
};
} // namespace dzurkel

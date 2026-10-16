#pragma once
#include "PreviewProvider.h"

namespace dzurkel {
class PreviewRegistry {
public:
    static PreviewData load(const FileResource &resource, const QSize &target,
                            const Cancellation &cancel);
    static QWidget *createWidget(const PreviewData &data, QWidget *parent);
    static QString kindName(PreviewKind kind);
};
} // namespace dzurkel

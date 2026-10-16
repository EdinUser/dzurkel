#pragma once
#include "preview/PreviewProvider.h"

namespace dzurkel {
class FileInfoProvider {
public:
    static PreviewData describe(const FileResource &resource);
    static QWidget *createWidget(const PreviewData &data, QWidget *parent);
};
} // namespace dzurkel

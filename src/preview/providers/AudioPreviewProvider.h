#pragma once
#include "preview/PreviewProvider.h"

namespace dzurkel {
class AudioPreviewProvider : public PreviewProvider {
public:
    bool supports(const QMimeType &mime) const override;
    static bool accepts(const QMimeType &mime, const QString &path);
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data,
                     const QSize &target, const Cancellation &cancel) const override;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
};
}

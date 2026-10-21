#pragma once
#include "preview/PreviewProvider.h"
class QIODevice;
namespace dzurkel {
class CsvPreviewProvider : public PreviewProvider {
public:
    bool supports(const QMimeType &mime) const override;
    PreviewData load(std::unique_ptr<QFile> file, PreviewData data, const QSize &target, const Cancellation &cancel) const override;
    QWidget *createWidget(const PreviewData &data, QWidget *parent) const override;
    PreviewData loadFromDevice(QIODevice &device, PreviewData data, const QSize &target, const Cancellation &cancel) const;
};
}

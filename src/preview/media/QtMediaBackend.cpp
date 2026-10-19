#include "MediaBackend.h"
#include "MediaPreviewWidget.h"
#include "resources/media/StreamingMediaSource.h"

namespace dzurkel {
class QtMediaBackend : public QObject, public MediaBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_MEDIA_BACKEND_IID)
    Q_INTERFACES(dzurkel::MediaBackend)
public:
    QWidget *create(const PreviewData &data, QWidget *parent) override { return new MediaPreviewWidget(data, parent); }
    int activeHelpers() const override { return StreamingMediaSource::activeHelpers(); }
};
}
#include "QtMediaBackend.moc"

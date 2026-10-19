#include "AudioBackend.h"
#include "AudioPreviewWidget.h"
#include "resources/media/StreamingMediaSource.h"

namespace dzurkel {
class QtAudioBackend : public QObject, public AudioBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_AUDIO_BACKEND_IID)
    Q_INTERFACES(dzurkel::AudioBackend)
public:
    QWidget *create(const PreviewData &data, QWidget *parent) override { return new AudioPreviewWidget(data, parent); }
    int activeHelpers() const override { return StreamingMediaSource::activeHelpers(); }
};
}
#include "QtAudioBackend.moc"

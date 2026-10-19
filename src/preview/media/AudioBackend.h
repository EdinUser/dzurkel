#pragma once
#include "preview/PreviewProvider.h"
#include <QtPlugin>
#include <functional>

namespace dzurkel {
using AudioCallback = std::function<void(const QString &, const QString &)>;
// ABI boundary contains no Qt Multimedia types. Keep adapters matched to the
// Dzurkel release and Qt runtime; the loader pins successful modules.
class AudioControls {
public:
    virtual ~AudioControls() = default;
    virtual void stop() = 0;
    virtual void setMuted(bool muted) = 0;
    virtual void setCallbacks(QObject *context, AudioCallback failure, AudioCallback details) = 0;
};
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual QWidget *create(const PreviewData &data, QWidget *parent) = 0;
    virtual int activeHelpers() const = 0;
};
struct AudioAvailability { bool available; bool adapterFound; QString explanation; };
AudioAvailability audioAvailability(bool recheck = false);
QWidget *createAudioPreview(const PreviewData &data, QWidget *parent);
int activeAudioHelpers();
}
Q_DECLARE_INTERFACE(dzurkel::AudioControls, "org.dzurkel.AudioControls/0.7.0")
#define DZURKEL_AUDIO_BACKEND_IID "org.dzurkel.AudioBackend/0.7.0"
Q_DECLARE_INTERFACE(dzurkel::AudioBackend, DZURKEL_AUDIO_BACKEND_IID)

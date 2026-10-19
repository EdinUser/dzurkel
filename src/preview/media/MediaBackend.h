#pragma once
#include "preview/PreviewProvider.h"
#include <QtPlugin>
#include <functional>

namespace dzurkel {
using MediaCallback = std::function<void(const QString &, const QString &)>;
using MediaKindCallback = std::function<void(bool video)>;
// ABI boundary contains no Qt Multimedia types. Keep adapters matched to the
// Dzurkel release and Qt runtime; the loader pins successful modules.
class MediaControls {
public:
    virtual ~MediaControls() = default;
    virtual void stop() = 0;
    virtual void setMuted(bool muted) = 0;
    virtual void setCallbacks(QObject *context, MediaCallback failure, MediaCallback details,
                              MediaKindCallback kind) = 0;
};
class MediaBackend {
public:
    virtual ~MediaBackend() = default;
    virtual QWidget *create(const PreviewData &data, QWidget *parent) = 0;
    virtual int activeHelpers() const = 0;
};
struct MediaAvailability { bool available; bool adapterFound; QString explanation; };
MediaAvailability mediaAvailability(bool recheck = false);
QWidget *createMediaPreview(const PreviewData &data, QWidget *parent);
int activeMediaHelpers();
}
Q_DECLARE_INTERFACE(dzurkel::MediaControls, "org.dzurkel.MediaControls/1")
#define DZURKEL_MEDIA_BACKEND_IID "org.dzurkel.MediaBackend/1"
Q_DECLARE_INTERFACE(dzurkel::MediaBackend, DZURKEL_MEDIA_BACKEND_IID)

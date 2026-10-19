#include "MediaBackend.h"
#include "preview/providers/FileInfoProvider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>

namespace dzurkel {
namespace {
struct State {
    std::unique_ptr<QPluginLoader> loader;
    MediaBackend *backend = nullptr;
    bool attempted = false;
    bool found = false;
    QString explanation;
};
State &state() { static State value; return value; }
MediaBackend *backend(bool recheck = false)
{
    auto &value = state();
    if (value.backend || (value.attempted && !recheck)) return value.backend;
    value.attempted = true;
    value.found = false;
    const QDir application(QCoreApplication::applicationDirPath());
    for (const auto &path : {application.filePath(QStringLiteral("dzurkel-plugins/libdzurkel_audio.so")),
                            application.filePath(QStringLiteral(DZURKEL_AUDIO_INSTALL_PATH))}) {
        if (!QFileInfo::exists(path)) continue;
        value.found = true;
        auto candidate = std::make_unique<QPluginLoader>(path);
        candidate->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        const auto iid = candidate->metaData().value(QStringLiteral("IID")).toString();
        if (!iid.isEmpty() && iid != QLatin1String(DZURKEL_MEDIA_BACKEND_IID)) {
            value.explanation = QStringLiteral("The adapter at %1 does not match this Dzurkel executable "
                "(found interface %2; required %3). Rebuild and reinstall Dzurkel and its adapters together, "
                "then restart Dzurkel. Installing Qt Multimedia will not update this adapter.")
                .arg(path, iid, QStringLiteral(DZURKEL_MEDIA_BACKEND_IID));
            continue;
        }
        auto *object = candidate->instance();
        if (auto *instance = qobject_cast<MediaBackend *>(object)) {
            value.loader = std::move(candidate);
            value.backend = instance;
            value.explanation = QStringLiteral("The media adapter, Qt Multimedia and Multimedia Widgets are available. Individual formats require a usable media backend and codec.");
            return instance;
        }
        value.explanation = object
            ? QStringLiteral("The adapter at %1 loaded but does not implement the required media interface. "
                "Rebuild and reinstall Dzurkel and its adapters together, then restart Dzurkel.").arg(path)
            : QStringLiteral("Cannot load %1: %2. Install the matching Qt Multimedia/Multimedia Widgets "
                "runtime and Dzurkel media adapter.").arg(path, candidate->errorString());
    }
    value.explanation = value.found
        ? value.explanation
        : QStringLiteral("The Dzurkel media adapter is missing. Install a Dzurkel package containing the media adapter; Qt Multimedia alone is insufficient.");
    return nullptr;
}
}
MediaAvailability mediaAvailability(bool recheck)
{
    const bool available = backend(recheck) != nullptr;
    return {available, state().found, state().explanation};
}
QWidget *createMediaPreview(const PreviewData &data, QWidget *parent)
{
    if (auto *instance = backend()) return instance->create(data, parent);
    auto fallback = data;
    fallback.kind = PreviewKind::Information;
    fallback.missingLibrary = QStringLiteral("multimedia");
    fallback.message = QStringLiteral("Media preview needs an optional library. Check Settings for details!");
    fallback.detail = state().explanation;
    return FileInfoProvider::createWidget(fallback, parent);
}
int activeMediaHelpers() { auto *instance = backend(); return instance ? instance->activeHelpers() : 0; }
}

#include "AudioBackend.h"
#include "preview/providers/FileInfoProvider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>

namespace dzurkel {
namespace {
struct State {
    std::unique_ptr<QPluginLoader> loader;
    AudioBackend *backend = nullptr;
    bool attempted = false;
    bool found = false;
    QString explanation;
};
State &state() { static State value; return value; }
AudioBackend *backend(bool recheck = false)
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
        if (auto *instance = qobject_cast<AudioBackend *>(candidate->instance())) {
            value.loader = std::move(candidate);
            value.backend = instance;
            value.explanation = QStringLiteral("Audio adapter and Qt Multimedia are available. Individual formats require a usable media backend and codec.");
            return instance;
        }
        value.explanation = candidate->errorString();
    }
    value.explanation = value.found
        ? QStringLiteral("The audio adapter could not load. Install the matching Qt Multimedia runtime and Dzurkel audio adapter. %1").arg(value.explanation)
        : QStringLiteral("The Dzurkel audio adapter is missing. Install a Dzurkel package containing the audio adapter; Qt Multimedia alone is insufficient.");
    return nullptr;
}
}
AudioAvailability audioAvailability(bool recheck)
{
    const bool available = backend(recheck) != nullptr;
    return {available, state().found, state().explanation};
}
QWidget *createAudioPreview(const PreviewData &data, QWidget *parent)
{
    if (auto *instance = backend()) return instance->create(data, parent);
    auto fallback = data;
    fallback.kind = PreviewKind::Information;
    fallback.missingLibrary = QStringLiteral("multimedia");
    fallback.message = QStringLiteral("Audio preview needs an optional library. Check Settings for details!");
    fallback.detail = state().explanation;
    return FileInfoProvider::createWidget(fallback, parent);
}
int activeAudioHelpers() { auto *instance = backend(); return instance ? instance->activeHelpers() : 0; }
}

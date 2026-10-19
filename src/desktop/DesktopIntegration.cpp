#include "DesktopIntegration.h"
#include "DesktopBackend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>
#include <QPromise>
#include <QTextStream>
#include <QWindow>
#include <memory>
namespace dzurkel {
namespace {
struct State {
    std::unique_ptr<QPluginLoader> loader;
    DesktopBackend *instance = nullptr;
    bool attempted = false;
    bool found = false;
    QString explanation;
};
State &state() { static State value; return value; }
DesktopBackend *backend(bool recheck = false)
{
    auto &value = state();
    if (value.instance || (value.attempted && !recheck)) return value.instance;
    value.attempted = true;
    value.found = false;
    const QDir application(QCoreApplication::applicationDirPath());
    for (const auto &path : {application.filePath(QStringLiteral("dzurkel-plugins/libdzurkel_desktop.so")),
                            application.filePath(QStringLiteral(DZURKEL_DESKTOP_INSTALL_PATH))}) {
        if (!QFileInfo::exists(path)) continue;
        value.found = true;
        auto candidate = std::make_unique<QPluginLoader>(path);
        candidate->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        if (auto *plugin = qobject_cast<DesktopBackend *>(candidate->instance())) {
            value.loader = std::move(candidate);
            value.instance = plugin;
            value.explanation = QStringLiteral("Desktop activation adapter and KF6 WindowSystem are available.");
            return plugin;
        }
        value.explanation = candidate->errorString();
    }
    value.explanation = value.found
        ? QStringLiteral("The desktop adapter could not load. Install matching KF6 WindowSystem and Dzurkel adapter packages. %1").arg(value.explanation)
        : QStringLiteral("The desktop activation adapter is not included in this installation. Standard Qt window activation remains available.");
    return nullptr;
}
}
DesktopAvailability desktopAvailability(bool recheck)
{
    const bool available = backend(recheck) != nullptr;
    return {available, state().found, state().explanation};
}

QFuture<QString> settingsActivationToken(QWindow *window)
{
    if (auto *plugin = backend()) return plugin->token(window);
    QPromise<QString> result;
    result.start(); result.addResult(QString()); result.finish();
    return result.future();
}
bool supportsActivationTokens() { return backend() != nullptr; }
bool desktopThunarWindowActive() { auto *plugin = backend(); return plugin && plugin->thunarActive(); }
void activatePreview(QWindow *window, const QString &token, bool diagnostics)
{
    if (!window) return;
    if (diagnostics) QTextStream(stderr) << "Dzurkel activation requested; token present: "
        << !token.isEmpty() << " KDE integration: " << supportsActivationTokens() << '\n';
    if (auto *plugin = backend(); plugin && plugin->activate(window, token)) return;
    window->requestActivate();
}
}

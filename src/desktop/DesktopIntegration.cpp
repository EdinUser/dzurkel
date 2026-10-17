#include "DesktopIntegration.h"
#include <QTextStream>
#include <QWindow>
#include <QPromise>
#ifdef DZURKEL_KDE_ACTIVATION
#include <KWindowSystem>
#include <KWaylandExtras>
#include <kwindowsystem_version.h>
#endif

namespace dzurkel {
QFuture<QString> settingsActivationToken(QWindow *window)
{
#ifdef DZURKEL_KDE_ACTIVATION
#if KWINDOWSYSTEM_VERSION >= QT_VERSION_CHECK(6, 19, 0)
    if (window && KWindowSystem::isPlatformWayland())
        return KWaylandExtras::xdgActivationToken(window, QStringLiteral("dzurkel"));
#else
    Q_UNUSED(window)
#endif
#else
    Q_UNUSED(window)
#endif
    QPromise<QString> promise;
    promise.start();
    promise.addResult(QString());
    promise.finish();
    return promise.future();
}
bool supportsActivationTokens()
{
#ifdef DZURKEL_KDE_ACTIVATION
    return true;
#else
    return false;
#endif
}
void activatePreview(QWindow *window, const QString &token, bool diagnostics)
{
    if (!window) return;
    if (diagnostics) {
        // Activation tokens are credentials: log presence only.
        QTextStream(stderr) << "Dzurkel activation requested; token present: "
                            << !token.isEmpty() << " KDE integration: " << supportsActivationTokens() << '\n';
    }
#ifdef DZURKEL_KDE_ACTIVATION
    if (KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setCurrentXdgActivationToken(token);
        KWindowSystem::activateWindow(window);
        return;
    }
#else
    Q_UNUSED(token)
#endif
    window->requestActivate();
}
} // namespace dzurkel

#include "DesktopIntegration.h"
#include <QTextStream>
#include <QWindow>
#ifdef DZURKEL_KDE_ACTIVATION
#include <KWindowSystem>
#endif

namespace dzurkel {
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

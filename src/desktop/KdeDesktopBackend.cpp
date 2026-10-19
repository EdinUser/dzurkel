#include "DesktopBackend.h"
#include <KWindowSystem>
#include <KWaylandExtras>
#include <KWindowInfo>
#include <KX11Extras>
#include <kwindowsystem_version.h>
#include <QFile>
#include <QFileInfo>
#include <QPromise>
#include <QWindow>
#include <unistd.h>
namespace dzurkel {
class KdeDesktopBackend : public QObject, public DesktopBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_DESKTOP_BACKEND_IID)
    Q_INTERFACES(dzurkel::DesktopBackend)
public:
    QFuture<QString> token(QWindow *window) override
    {
#if KWINDOWSYSTEM_VERSION >= QT_VERSION_CHECK(6, 19, 0)
        if (window && KWindowSystem::isPlatformWayland())
            return KWaylandExtras::xdgActivationToken(window, QStringLiteral("dzurkel"));
#else
        Q_UNUSED(window);
#endif
        QPromise<QString> result;
        result.start(); result.addResult(QString()); result.finish();
        return result.future();
    }
    bool activate(QWindow *window, const QString &token) override
    {
        if (!KWindowSystem::isPlatformWayland()) return false;
        KWindowSystem::setCurrentXdgActivationToken(token);
        KWindowSystem::activateWindow(window);
        return true;
    }
    bool thunarActive() override
    {
        if (!KWindowSystem::isPlatformX11()) return false;
        const auto window = KX11Extras::activeWindow();
        if (!window) return false;
        const KWindowInfo info(window, NET::WMPid);
        if (!info.valid() || info.pid() <= 0) return false;
        const QString process = QStringLiteral("/proc/%1").arg(info.pid());
        if (QFileInfo(process).ownerId() != getuid()) return false;
        QFile name(process + QStringLiteral("/comm"));
        return name.open(QIODevice::ReadOnly) && name.readAll().trimmed().compare("thunar", Qt::CaseInsensitive) == 0;
    }
};
}
#include "KdeDesktopBackend.moc"

#include "NautilusPreviewService.h"
#include "controller/PreviewController.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QUrl>

namespace dzurkel {
NautilusPreviewService::NautilusPreviewService(PreviewController &controller, NautilusIntegration integration)
    : controller_(controller), integration_(std::move(integration)), bus_(QDBusConnection::sessionBus())
{
    controller_.setNautilusService(this);
    connect(&controller_, &PreviewController::stateChanged, this, &NautilusPreviewService::notifyProperties);
    connect(&controller_, &PreviewController::externalNavigationRequested, this, [this](uint direction) {
        if (owned_ && visible()) emit SelectionEvent(direction);
    });
}
NautilusPreviewService::~NautilusPreviewService() { stop(); controller_.setNautilusService(nullptr); }
bool NautilusPreviewService::start(QString &error)
{
    error.clear();
    if (owned_) return true;
    if (!integration_.enabled()) { error = QStringLiteral("Enable Nautilus previews in Dzurkel Settings first."); return false; }
    if (!bus_.isConnected()) { error = QStringLiteral("Cannot connect to the session bus."); return false; }
    if (!exported_) exported_ = bus_.registerObject(QString::fromLatin1(NautilusObjectPath), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);
    if (!exported_) { error = QStringLiteral("Cannot export the Nautilus preview service."); return false; }
    const auto reply = bus_.interface()->registerService(QString::fromLatin1(NautilusServiceName),
        QDBusConnectionInterface::DontQueueService, QDBusConnectionInterface::DontAllowReplacement);
    if (reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered) {
        owned_ = true;
        notifyProperties();
        return true;
    }
    bus_.unregisterObject(QString::fromLatin1(NautilusObjectPath));
    exported_ = false;
    error = QStringLiteral("Another Nautilus preview provider is running. Close its preview and quit that provider, then retry. Its registration has not been replaced.");
    return false;
}
bool NautilusPreviewService::configure(bool enable, QString &error)
{
    if (enable) {
        // Check ownership before touching files. Never replace or queue behind Sushi.
        if (!owned_) {
            if (!bus_.isConnected()) { error = QStringLiteral("Cannot connect to the session bus."); return false; }
            const auto owner = bus_.interface()->serviceOwner(QString::fromLatin1(NautilusServiceName));
            if (owner.isValid() && !owner.value().isEmpty()) {
                error = QStringLiteral("Another Nautilus preview provider is running. Close its preview and quit that provider, then retry. No configuration has changed.");
                return false;
            }
            if (!owner.isValid() && owner.error().name() != QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")
                && owner.error().type() != QDBusError::ServiceUnknown) {
                error = owner.error().message(); return false;
            }
        }
        const bool alreadyEnabled = integration_.enabled();
        if (!integration_.enable(error)) return false;
        if (!start(error)) {
            if (!alreadyEnabled) {
                QString rollback;
                if (!integration_.restore(rollback)) error += QStringLiteral("\n") + rollback;
            }
            return false;
        }
    } else {
        if (!integration_.restore(error)) return false;
        stop();
        controller_.releaseExternalNavigation();
    }
    // Ask the session daemon to rescan user registrations. This never starts a provider.
    auto reload = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ReloadConfig"));
    bus_.asyncCall(reload);
    return true;
}
void NautilusPreviewService::stop()
{
    if (owned_) bus_.unregisterService(QString::fromLatin1(NautilusServiceName));
    if (exported_) bus_.unregisterObject(QString::fromLatin1(NautilusObjectPath));
    owned_ = exported_ = false;
    caller_.clear();
    parentHandle_.clear();
}
bool NautilusPreviewService::visible() const { return controller_.externalNavigation() && controller_.isVisible(); }
void NautilusPreviewService::ShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown, const QString &activationToken)
{
    const QUrl url(uri, QUrl::StrictMode);
    QString error;
    if (!owned_) error = QStringLiteral("Nautilus previews are disabled.");
    else if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment())
        error = QStringLiteral("Nautilus previews currently support local file URLs only.");
    else if (windowHandle.size() > 16384 || activationToken.size() > 16384
        || windowHandle.contains(QChar('\0')) || activationToken.contains(QChar('\0')))
        error = QStringLiteral("Invalid preview request options.");
    if (!error.isEmpty()) {
        if (calledFromDBus()) sendErrorReply(QDBusError::InvalidArgs, error);
        return;
    }
    const QString canonical = url.toString(QUrl::FullyEncoded);
    if (closeIfAlreadyShown && visible() && canonical == controller_.fileUrl()) { Close(); return; }
    // ParentHandle identifies the requesting Files window. Keep the preview
    // unparented: Mutter's screen-centered placement is the accepted behavior.
    const auto oldParent = parentHandle_;
    const auto oldCaller = caller_;
    parentHandle_ = windowHandle;
    caller_ = calledFromDBus() ? message().service() : QString();
    // Only explicit Space requests may reactivate a mapped window. Selection
    // updates can arrive while Files has focus and must preserve that focus.
    if (!controller_.show(canonical, error, nullptr, activationToken, closeIfAlreadyShown || !controller_.isVisible(), true)) {
        parentHandle_ = oldParent;
        caller_ = oldCaller;
        if (calledFromDBus()) sendErrorReply(QDBusError::InvalidArgs, error);
    }
    notifyProperties();
}
void NautilusPreviewService::Close()
{
    if (calledFromDBus() && !caller_.isEmpty() && message().service() != caller_) return;
    if (visible()) controller_.hide();
    notifyProperties();
}
void NautilusPreviewService::notifyProperties()
{
    if (!owned_) return;
    const bool currentVisible = visible();
    const QString currentParent = parentHandle();
    QVariantMap changed;
    if (currentVisible != lastVisible_) changed.insert(QStringLiteral("Visible"), currentVisible);
    if (currentParent != lastParent_) changed.insert(QStringLiteral("ParentHandle"), currentParent);
    lastVisible_ = currentVisible;
    lastParent_ = currentParent;
    if (changed.isEmpty()) return;
    auto notification = QDBusMessage::createSignal(QString::fromLatin1(NautilusObjectPath),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    notification << QString::fromLatin1(NautilusInterfaceName) << changed << QStringList{};
    bus_.send(notification);
}
}

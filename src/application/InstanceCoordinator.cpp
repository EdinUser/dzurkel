#include "InstanceCoordinator.h"
#include "ipc/PreviewService.h"
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>

namespace dzurkel {
InstanceCoordinator::InstanceCoordinator(const QString &connectionName)
    : connectionName_(connectionName),
      connection_(QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName)) {}
InstanceCoordinator::~InstanceCoordinator()
{
    if (owned_) connection_.unregisterService(QString::fromLatin1(ServiceName));
    if (exported_) connection_.unregisterObject(QString::fromLatin1(ObjectPath));
    QDBusConnection::disconnectFromBus(connectionName_);
}
bool InstanceCoordinator::connected(QString &error) const
{
    if (connection_.isConnected()) return true;
    error = QStringLiteral("Cannot connect to the session bus: %1").arg(connection_.lastError().message());
    return false;
}
InstanceCoordinator::ForwardResult InstanceCoordinator::forward(const Request &request, QString &error)
{
    if (!connected(error)) return ForwardResult::Failed;
    const auto owner = connection_.interface()->serviceOwner(QString::fromLatin1(ServiceName));
    if (!owner.isValid()) {
        if (owner.error().name() == QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")
            || owner.error().type() == QDBusError::ServiceUnknown) return ForwardResult::NoOwner;
        error = owner.error().message();
        return ForwardResult::Failed;
    }
    QString method;
    switch (request.command) {
    case Command::Show: method = QStringLiteral("Show"); break;
    case Command::Hide: method = QStringLiteral("Hide"); break;
    case Command::Toggle: method = QStringLiteral("Toggle"); break;
    case Command::Quit: method = QStringLiteral("Quit"); break;
    default: error = QStringLiteral("Invalid service command."); return ForwardResult::Failed;
    }
    const bool opens = request.command == Command::Show || request.command == Command::Toggle;
    if (opens && !request.activationToken.isEmpty()) method += QStringLiteral("WithOptions");
    // Pin the request to a unique owner so a restart cannot duplicate Toggle.
    auto message = QDBusMessage::createMethodCall(owner.value(), QString::fromLatin1(ObjectPath),
                                                 QString::fromLatin1(InterfaceName), method);
    message.setAutoStartService(false);
    if (opens) {
        message << request.url;
        if (!request.activationToken.isEmpty())
            message << QVariantMap{{QStringLiteral("activation-token"), request.activationToken}};
    }
    const auto reply = connection_.call(message, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ErrorMessage) return ForwardResult::Delivered;
    if (reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")
        || reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")) {
        return ForwardResult::NoOwner;
    }
    // Never retry a timeout: the owner may already have applied the command.
    error = QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage());
    return ForwardResult::Failed;
}
InstanceCoordinator::ClaimResult InstanceCoordinator::claim(PreviewService &service, QString &error)
{
    if (!connected(error)) return ClaimResult::Failed;
    if (!exported_) {
        exported_ = connection_.registerObject(QString::fromLatin1(ObjectPath), &service,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
        if (!exported_) {
            error = connection_.lastError().message();
            return ClaimResult::Failed;
        }
    }
    // Export before claiming: clients can call as soon as the name is visible.
    const auto reply = connection_.interface()->registerService(QString::fromLatin1(ServiceName),
        QDBusConnectionInterface::DontQueueService, QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid()) {
        error = reply.error().message();
        return ClaimResult::Failed;
    }
    owned_ = reply.value() == QDBusConnectionInterface::ServiceRegistered;
    return owned_ ? ClaimResult::Primary : ClaimResult::AlreadyOwned;
}
} // namespace dzurkel

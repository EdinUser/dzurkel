#include "DolphinSession.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QXmlStreamReader>
#include <unistd.h>

namespace dzurkel {
namespace {
QList<qint64> dolphinProcesses()
{
    QList<qint64> result;
    for (const auto &entry : QDir(QStringLiteral("/proc")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        const qint64 pid = entry.fileName().toLongLong(&number);
        if (!number || entry.ownerId() != getuid()) continue;
        const QFileInfo executable(entry.filePath() + QStringLiteral("/exe"));
        QString target = executable.symLinkTarget();
        if (target.endsWith(QStringLiteral(" (deleted)"))) target.chop(10);
        if (QFileInfo(target).fileName() == QStringLiteral("dolphin")) result.append(pid);
    }
    return result;
}
QDBusMessage introspect(const QDBusConnection &bus, const QString &owner, const QString &path)
{
    return bus.call(QDBusMessage::createMethodCall(owner, path,
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect")), QDBus::Block, 2000);
}
}
DolphinSession::DolphinSession(QDBusConnection bus, ProcessFinder finder)
    : bus_(std::move(bus)), processes_(finder ? std::move(finder) : dolphinProcesses) {}
bool DolphinSession::isRunning() const { return !processes_().isEmpty(); }
DolphinSession::Snapshot DolphinSession::inspect() const
{
    Snapshot snapshot;
    snapshot.processes = processes_();
    if (snapshot.processes.isEmpty()) return snapshot;
    auto *interface = bus_.interface();
    if (!interface) { snapshot.error = QStringLiteral("Cannot contact the session bus. Close Dolphin manually and retry."); return snapshot; }
    const QDBusReply<QStringList> names = interface->registeredServiceNames();
    if (!names.isValid()) { snapshot.error = names.error().message(); return snapshot; }
    QSet<qint64> found;
    for (const QString &service : names.value()) {
        if (!service.startsWith(QStringLiteral("org.kde.dolphin"))) continue;
        const QDBusReply<uint> pid = interface->servicePid(service);
        const QDBusReply<QString> owner = interface->serviceOwner(service);
        if (!pid.isValid() || !owner.isValid() || !snapshot.processes.contains(pid.value()) || found.contains(pid.value())) continue;
        const auto reply = introspect(bus_, owner.value(), QStringLiteral("/dolphin"));
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) continue;
        QXmlStreamReader nodes(reply.arguments().first().toString());
        int count = 0;
        while (!nodes.atEnd() && count < 128) {
            nodes.readNext();
            if (!nodes.isStartElement() || nodes.name() != QStringLiteral("node")) continue;
            const QString name = nodes.attributes().value(QStringLiteral("name")).toString();
            if (!name.startsWith(QStringLiteral("Dolphin_")) || name.contains(QLatin1Char('/'))) continue;
            ++count;
            const QString path = QStringLiteral("/dolphin/") + name;
            const auto window = introspect(bus_, owner.value(), path);
            if (window.type() == QDBusMessage::ErrorMessage || window.arguments().isEmpty()) continue;
            QXmlStreamReader xml(window.arguments().first().toString());
            bool dolphinInterface = false;
            bool canQuit = false;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("interface"))
                    dolphinInterface = xml.attributes().value(QStringLiteral("name")) == QStringLiteral("org.kde.dolphin.MainWindow");
                if (xml.isStartElement() && xml.name() == QStringLiteral("method") && dolphinInterface
                    && xml.attributes().value(QStringLiteral("name")) == QStringLiteral("quit")) canQuit = true;
                if (xml.isEndElement() && xml.name() == QStringLiteral("interface")) dolphinInterface = false;
            }
            if (canQuit && !xml.hasError()) {
                snapshot.windows.append({owner.value(), path});
                found.insert(pid.value());
            }
        }
    }
    if (found.size() != snapshot.processes.size())
        snapshot.error = QStringLiteral("Some Dolphin processes cannot be closed through their desktop interface. Close Dolphin manually and retry.");
    return snapshot;
}
QString DolphinSession::closeAndWait(const Snapshot &snapshot, int timeoutMs) const
{
    if (!snapshot.error.isEmpty()) return snapshot.error;
    for (const auto &window : snapshot.windows) {
        // Target the captured unique owner, never a newly started replacement.
        if (!bus_.send(QDBusMessage::createMethodCall(window.owner, window.path,
                QStringLiteral("org.kde.dolphin.MainWindow"), QStringLiteral("quit"))))
            return QStringLiteral("Could not request Dolphin to close. Shortcut files were not changed.");
    }
    QElapsedTimer timer;
    timer.start();
    do {
        if (!isRunning()) return {};
        QThread::msleep(200);
    } while (timer.elapsed() < timeoutMs);
    return QStringLiteral("Dolphin is still running. Finish or cancel its close dialogs, then retry. Shortcut files were not changed.");
}
} // namespace dzurkel

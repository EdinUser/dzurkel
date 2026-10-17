#pragma once
#include <QDBusConnection>
#include <QStringList>
#include <functional>

namespace dzurkel {
// Used only during an explicit shortcut change; no resident polling or daemon.
class DolphinSession {
public:
    using ProcessFinder = std::function<QList<qint64>()>;
    struct Window { QString owner; QString path; };
    struct Snapshot {
        QList<qint64> processes;
        QList<Window> windows;
        QString error;
    };
    explicit DolphinSession(QDBusConnection bus = QDBusConnection::sessionBus(), ProcessFinder finder = {});
    Snapshot inspect() const;
    QString closeAndWait(const Snapshot &snapshot, int timeoutMs = 30000) const;
    bool isRunning() const;
private:
    QDBusConnection bus_;
    ProcessFinder processes_;
};
} // namespace dzurkel

#pragma once
#include "CommandLine.h"
#include <QDBusConnection>

namespace dzurkel {
class PreviewService;
class InstanceCoordinator {
public:
    enum class ForwardResult { Delivered, NoOwner, Failed };
    enum class ClaimResult { Primary, AlreadyOwned, Failed };
    explicit InstanceCoordinator(const QString &connectionName);
    ~InstanceCoordinator();
    InstanceCoordinator(const InstanceCoordinator &) = delete;
    InstanceCoordinator &operator=(const InstanceCoordinator &) = delete;
    bool connected(QString &error) const;
    ForwardResult forward(const Request &request, QString &error);
    ClaimResult claim(PreviewService &service, QString &error);
private:
    QString connectionName_;
    QDBusConnection connection_;
    bool exported_ = false;
    bool owned_ = false;
};
} // namespace dzurkel

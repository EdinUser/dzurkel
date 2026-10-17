#include "ResourceAccessPolicy.h"
#include <QSettings>

namespace dzurkel {
namespace {
QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral("dzurkel"), QStringLiteral("dzurkel"));
}
const QString SftpKey = QStringLiteral("resources/experimentalSftp");
}
bool ResourceAccessPolicy::experimentalSftpEnabled()
{
    auto configuration = settings();
    configuration.setFallbacksEnabled(false);
    configuration.sync();
    // Invalid/unreadable settings must never opt a user into an experiment.
    const auto value = configuration.value(SftpKey, false);
    return configuration.status() == QSettings::NoError && value.toString() == QStringLiteral("true");
}
bool ResourceAccessPolicy::setExperimentalSftpEnabled(bool enabled, QString &error)
{
    error.clear();
    auto configuration = settings();
    configuration.setFallbacksEnabled(false);
    configuration.setValue(SftpKey, enabled);
    configuration.sync();
    if (configuration.status() == QSettings::NoError) return true;
    error = QStringLiteral("Could not save the SFTP preview setting. Check your configuration directory permissions.");
    return false;
}
} // namespace dzurkel

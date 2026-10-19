#pragma once
#include <QList>
#include <QString>

namespace dzurkel {
enum class LibraryHealth { Available, Limited, Missing };
struct LibraryAvailability {
    QString id;
    QString name;
    QString purpose;
    LibraryHealth health;
    QString status;
    QString details;
    QString installationCommand = {};
};
// GUI-thread only: optional Qt adapters are loaded on this thread. Reports
// observed capabilities, independently of the user's feature preferences.
QString libraryInstallationCommand(const QString &id, const QString &distribution, const QString &release);
QList<LibraryAvailability> libraryAvailability(bool recheck = false);
}

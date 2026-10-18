#include "NautilusIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace dzurkel {
namespace {
constexpr auto name = "org.gnome.NautilusPreviewer";
bool read(const QString &path, QByteArray &bytes)
{
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return false;
    bytes = file.readAll();
    return file.error() == QFile::NoError;
}
bool write(const QString &path, const QByteArray &bytes, QFile::Permissions permissions)
{
    QSaveFile file(path);
    return !QFileInfo(path).isSymLink() && file.open(QIODevice::WriteOnly)
        && file.setPermissions(permissions) && file.write(bytes) == bytes.size() && file.commit();
}
bool fail(QString &error, const QString &message) { error = message; return false; }
bool manifest(const QString &path, QJsonObject &object)
{
    QByteArray bytes;
    if (!read(path, bytes)) return false;
    object = QJsonDocument::fromJson(bytes).object();
    return object.value("version").toInt() == 1 && object.value("installed").isString()
        && object.value("original").isString() && object.value("existed").isBool()
        && object.value("permissions").isDouble();
}
constexpr auto privatePermissions = QFile::ReadOwner | QFile::WriteOwner;
}
FileManagerIntegration detectFileManager(const QString &desktop, bool dolphin, bool nautilus)
{
    const auto parts = desktop.toUpper().split(':');
    if (parts.contains(QStringLiteral("GNOME"))) return nautilus ? FileManagerIntegration::Nautilus : FileManagerIntegration::Unavailable;
    if (parts.contains(QStringLiteral("KDE"))) return dolphin ? FileManagerIntegration::Dolphin : FileManagerIntegration::Unavailable;
    if (dolphin != nautilus) return dolphin ? FileManagerIntegration::Dolphin : FileManagerIntegration::Nautilus;
    return FileManagerIntegration::Unavailable;
}
FileManagerIntegration detectFileManager()
{
    return detectFileManager(qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        !QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty(),
        !QStandardPaths::findExecutable(QStringLiteral("nautilus")).isEmpty());
}
NautilusIntegration::NautilusIntegration()
    : NautilusIntegration(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
        qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state")) + QStringLiteral("/dzurkel"),
        QCoreApplication::applicationFilePath()) {}
NautilusIntegration::NautilusIntegration(QString dataDirectory, QString stateDirectory, QString executable)
    : data_(std::move(dataDirectory)), state_(std::move(stateDirectory)), executable_(std::move(executable)) {}
QString NautilusIntegration::serviceFile() const { return data_ + QStringLiteral("/dbus-1/services/org.gnome.NautilusPreviewer.service"); }
QString NautilusIntegration::backupFile() const { return state_ + QStringLiteral("/nautilus-preview-backup.json"); }
bool NautilusIntegration::hasBackup() const { return QFileInfo::exists(backupFile()) || QFileInfo(backupFile()).isSymLink(); }
bool NautilusIntegration::enabled() const
{
    QJsonObject saved;
    QByteArray current;
    return manifest(backupFile(), saved) && read(serviceFile(), current)
        && current == QByteArray::fromBase64(saved.value("installed").toString().toLatin1());
}
bool NautilusIntegration::enable(QString &error)
{
    error.clear();
    if (!QDir().mkpath(state_) || !QDir().mkpath(QFileInfo(serviceFile()).absolutePath()))
        return fail(error, QStringLiteral("Cannot create the user preview configuration directories."));
    QLockFile lock(state_ + QStringLiteral("/nautilus-preview.lock"));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Another preview setup is in progress."));
    if (hasBackup()) return enabled() || fail(error, QStringLiteral("The preview registration changed since setup. Restore or review it before enabling again."));
    // D-Bus scans all service files, not just the canonical filename.
    const QDir directory(QFileInfo(serviceFile()).absolutePath());
    for (const auto &entry : directory.entryList({QStringLiteral("*.service")}, QDir::Files)) {
        const QString path = directory.filePath(entry);
        QSettings settings(path, QSettings::IniFormat);
        if (path != serviceFile() && settings.value(QStringLiteral("D-BUS Service/Name")).toString() == QLatin1String(name))
            return fail(error, QStringLiteral("Another user service file registers Nautilus previews: %1. Review it first.").arg(path));
    }
    if (!QFileInfo(executable_).isAbsolute() || !QFileInfo(executable_).isExecutable()
        || executable_.contains(QChar('\n')) || executable_.contains(QChar('\r')) || executable_.contains(QChar('\0')))
        return fail(error, QStringLiteral("The Dzurkel executable path cannot be registered."));
    // Quote the argument without invoking a shell, then escape the service
    // file's string layer (which is decoded before Exec argument parsing).
    QString quoted = executable_;
    quoted.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\""))
        .replace('$', QStringLiteral("\\$")).replace('`', QStringLiteral("\\`"));
    quoted.replace('\\', QStringLiteral("\\\\")).replace('\t', QStringLiteral("\\t"));
    const auto installed = QStringLiteral("[D-BUS Service]\nName=%1\nExec=\"%2\" --nautilus-preview-service\n").arg(QLatin1String(name), quoted).toUtf8();
    const bool existed = QFileInfo::exists(serviceFile());
    QByteArray original;
    if (QFileInfo(serviceFile()).isSymLink() || (existed && !read(serviceFile(), original)) || original.size() > 64 * 1024)
        return fail(error, QStringLiteral("Cannot safely back up the existing user preview registration."));
    const auto permissions = existed ? QFileInfo(serviceFile()).permissions() : privatePermissions;
    const QJsonObject saved{{"version", 1}, {"existed", existed}, {"original", QString::fromLatin1(original.toBase64())},
        {"installed", QString::fromLatin1(installed.toBase64())}, {"permissions", int(permissions)}};
    if (!write(backupFile(), QJsonDocument(saved).toJson(), privatePermissions))
        return fail(error, QStringLiteral("Cannot save the previous preview registration."));
    if (!write(serviceFile(), installed, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther)) {
        QFile::remove(backupFile());
        return fail(error, QStringLiteral("Cannot install the user preview registration."));
    }
    return true;
}
bool NautilusIntegration::restore(QString &error)
{
    error.clear();
    QLockFile lock(state_ + QStringLiteral("/nautilus-preview.lock"));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Another preview setup is in progress."));
    QJsonObject saved;
    QByteArray current;
    if (!manifest(backupFile(), saved)) return fail(error, QStringLiteral("The previous preview registration backup cannot be read."));
    const auto original = QByteArray::fromBase64(saved.value("original").toString().toLatin1());
    const bool existed = saved.value("existed").toBool();
    const bool present = QFileInfo::exists(serviceFile());
    const bool readable = read(serviceFile(), current);
    // Also accept an already restored file if removing the backup failed earlier.
    const bool restored = existed ? readable && current == original : !present && !QFileInfo(serviceFile()).isSymLink();
    if (!restored && (!readable || current != QByteArray::fromBase64(saved.value("installed").toString().toLatin1())))
        return fail(error, QStringLiteral("The preview registration was edited after setup. It has been preserved; review it before restoring."));
    if (!restored && !(existed ? write(serviceFile(), original, QFile::Permissions(saved.value("permissions").toInt())) : QFile::remove(serviceFile())))
        return fail(error, QStringLiteral("Cannot restore the previous preview registration."));
    if (!QFile::remove(backupFile())) return fail(error, QStringLiteral("The registration was restored, but its backup could not be removed. Retry Restore."));
    return true;
}
}

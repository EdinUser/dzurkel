#pragma once
#include <QString>

namespace dzurkel {
enum class FileManagerIntegration { Dolphin, Nautilus, Unavailable };
FileManagerIntegration detectFileManager(const QString &desktop, bool dolphin, bool nautilus);
FileManagerIntegration detectFileManager();

// Opt-in user override only; packaged/system preview providers are never edited.
class NautilusIntegration {
public:
    NautilusIntegration();
    NautilusIntegration(QString dataDirectory, QString stateDirectory, QString executable);
    bool enabled() const;
    bool hasBackup() const;
    bool enable(QString &error);
    bool restore(QString &error);
    QString serviceFile() const;
private:
    QString data_, state_, executable_;
    QString backupFile() const;
};
}

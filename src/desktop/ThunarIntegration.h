#pragma once
#include <QString>
#include <functional>

namespace dzurkel {
// Opt-in setup using Thunar's shipped custom-action and accelerator support.
// Changes are made only while Thunar is closed; no plugin or global shortcut.
class ThunarIntegration {
public:
    ThunarIntegration();
    ThunarIntegration(QString configDirectory, QString stateDirectory, QString executable,
                      std::function<bool()> running = {}, QString defaultActions = {},
                      std::function<bool()> activeWindow = {});
    bool enabled() const;
    bool needsUpdate() const;
    bool hasBackup() const;
    bool enable(QString &error);
    bool restore(QString &error);
    QString actionsFile() const;
    QString shortcutsFile() const;
    static bool isRunning();
    bool running() const;
    bool activeWindow() const;
private:
    QString config_, state_, executable_;
    QString defaultActions_;
    std::function<bool()> running_;
    std::function<bool()> activeWindow_;
    QString backupFile() const;
    bool change(bool restore, QString &error);
};
}

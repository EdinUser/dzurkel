#pragma once
#include <QString>

namespace dzurkel {
class DolphinShortcuts {
public:
    DolphinShortcuts();
    // Explicit paths support isolated tests without touching desktop settings.
    DolphinShortcuts(QString configuration, QString backup);
    bool usesSpace() const;
    bool hasBackup() const;
    bool supportsAutomaticSetup() const;
    QString setupSummary(QString &error) const;
    bool useSpace(QString &error);
    bool restore(QString &error);
private:
    QString configuration_;
    QString backup_;
    bool checkScheme_ = false;
};
} // namespace dzurkel

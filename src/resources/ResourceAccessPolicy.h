#pragma once
#include <QString>

namespace dzurkel {
// Shared by CLI, resident service, helper and Settings. New remote routes remain
// explicit opt-ins until their compatibility evidence has been reviewed.
class ResourceAccessPolicy {
public:
    static bool experimentalSftpEnabled();
    static bool setExperimentalSftpEnabled(bool enabled, QString &error);
};
} // namespace dzurkel

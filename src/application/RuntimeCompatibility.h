#pragma once
#include <QByteArray>
#include <QString>

namespace dzurkel {
struct RuntimeCompatibility {
    QString summary;
    QString details;
    bool reportAvailable = false;
    static RuntimeCompatibility fromReport(const QByteArray &json);
    static RuntimeCompatibility load();
};
}

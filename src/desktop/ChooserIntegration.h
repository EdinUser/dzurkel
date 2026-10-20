#pragma once
#include <QString>
namespace dzurkel {
class ChooserIntegration {
public:
    ChooserIntegration();
    ChooserIntegration(QString configuration, QString pluginRoot, QString executable);
    bool available(QString &reason) const;
    bool enabled() const;
    bool setEnabled(bool enabled, QString &error);
private:
    QByteArray script() const;
    QByteArray disabledScript() const;
    QString configuration_, pluginRoot_, executable_;
};
}

#pragma once
#include "desktop/NautilusIntegration.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>

namespace dzurkel {
class PreviewController;
inline constexpr auto NautilusServiceName = "org.gnome.NautilusPreviewer";
inline constexpr auto NautilusObjectPath = "/org/gnome/NautilusPreviewer";
inline constexpr auto NautilusInterfaceName = "org.gnome.NautilusPreviewer2";

class NautilusPreviewService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.NautilusPreviewer2")
    Q_PROPERTY(bool Visible READ visible)
    Q_PROPERTY(QString ParentHandle READ parentHandle)
public:
    explicit NautilusPreviewService(PreviewController &controller, NautilusIntegration integration = {});
    ~NautilusPreviewService() override;
    bool start(QString &error);
    bool configure(bool enable, QString &error);
    const NautilusIntegration &integration() const { return integration_; }
    bool visible() const;
    QString parentHandle() const { return visible() ? parentHandle_ : QString(); }
    bool running() const { return owned_; }
public slots:
    void ShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown, const QString &activationToken);
    void Close();
signals:
    void SelectionEvent(uint direction);
private:
    void stop();
    void notifyProperties();
    PreviewController &controller_;
    NautilusIntegration integration_;
    QDBusConnection bus_;
    QString parentHandle_;
    QString caller_;
    bool owned_ = false;
    bool exported_ = false;
    bool lastVisible_ = false;
    QString lastParent_;
};
}

#pragma once
#include <QFuture>
#include <QString>
#include <QtPlugin>
class QWindow;
namespace dzurkel {
class DesktopBackend {
public:
    virtual ~DesktopBackend() = default;
    virtual QFuture<QString> token(QWindow *window) = 0;
    virtual bool activate(QWindow *window, const QString &token) = 0;
    virtual bool thunarActive() = 0;
};
}
#define DZURKEL_DESKTOP_BACKEND_IID "org.dzurkel.DesktopBackend/1.0.0"
Q_DECLARE_INTERFACE(dzurkel::DesktopBackend, DZURKEL_DESKTOP_BACKEND_IID)

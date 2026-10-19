#pragma once
#include <QString>
#include <QFuture>

class QWindow;
namespace dzurkel {
struct DesktopAvailability { bool available; bool adapterFound; QString explanation; };
DesktopAvailability desktopAvailability(bool recheck = false);
bool supportsActivationTokens();
bool desktopThunarWindowActive();
QFuture<QString> settingsActivationToken(QWindow *window);
void activatePreview(QWindow *window, const QString &token, bool diagnostics);
} // namespace dzurkel

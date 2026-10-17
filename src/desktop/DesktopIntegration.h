#pragma once
#include <QString>
#include <QFuture>

class QWindow;
namespace dzurkel {
bool supportsActivationTokens();
QFuture<QString> settingsActivationToken(QWindow *window);
void activatePreview(QWindow *window, const QString &token, bool diagnostics);
} // namespace dzurkel

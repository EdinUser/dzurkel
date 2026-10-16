#pragma once
#include <QString>

class QWindow;
namespace dzurkel {
bool supportsActivationTokens();
void activatePreview(QWindow *window, const QString &token, bool diagnostics);
} // namespace dzurkel

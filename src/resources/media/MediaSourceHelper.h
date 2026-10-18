#pragma once
#include <QString>

namespace dzurkel {
// Internal socket protocol on stdin/stdout. Run only in a QCoreApplication
// helper process, never on a GUI or decoder thread.
int serveMediaSource(const QString &url, int simulatedReadDelayMs = 0);
}

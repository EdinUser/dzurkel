#pragma once

class QPlainTextEdit;
#include <QString>

namespace dzurkel {

struct HighlightingAvailability {
    bool available;
    QString explanation;
    bool adapterFound = false;
};
// GUI-thread only. Explicit recheck retries a previously failed adapter load.
HighlightingAvailability highlightingAvailability(bool recheck = false);
bool highlightingEnabled();
bool setHighlightingEnabled(bool enabled, QString &error);

// GUI-thread only. Failure leaves the original plain-text view intact.
bool highlightText(QPlainTextEdit *editor, const QString &fileName, const QString &mimeType);

} // namespace dzurkel

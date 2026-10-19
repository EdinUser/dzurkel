#pragma once

#include <QtPlugin>

class QPlainTextEdit;
class QString;

namespace dzurkel {

// Keep this interface independent of KDE headers and implementation types.
class TextHighlightingBackend {
public:
    virtual ~TextHighlightingBackend() = default;
    virtual bool attach(QPlainTextEdit *editor, const QString &fileName,
                        const QString &mimeType) = 0;
};

} // namespace dzurkel

#define DZURKEL_TEXT_HIGHLIGHTING_IID "org.dzurkel.TextHighlightingBackend/1.0"
Q_DECLARE_INTERFACE(dzurkel::TextHighlightingBackend, DZURKEL_TEXT_HIGHLIGHTING_IID)

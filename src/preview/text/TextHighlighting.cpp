#include "TextHighlighting.h"
#include "TextHighlightingBackend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPluginLoader>
#include <QTextBlock>
#include <QSettings>
#include <memory>

namespace dzurkel {
namespace {
QSettings highlightingSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral("dzurkel"), QStringLiteral("dzurkel"));
}
struct BackendState {
    std::unique_ptr<QPluginLoader> loader;
    TextHighlightingBackend *instance = nullptr;
    bool attempted = false;
    bool adapterFound = false;
    QString explanation;
};
BackendState &backendState()
{
    static BackendState state;
    return state;
}
TextHighlightingBackend *backend(bool recheck = false)
{
    auto &state = backendState();
    if (state.instance || (state.attempted && !recheck)) return state.instance;
    state.attempted = true;
    const QDir application(QCoreApplication::applicationDirPath());
    const QStringList locations = {
        application.filePath(QStringLiteral("dzurkel-plugins/libdzurkel_highlighting.so")),
        application.filePath(QStringLiteral(DZURKEL_HIGHLIGHTING_INSTALL_PATH))
    };
    state.adapterFound = false;
    for (const auto &location : locations) {
        if (!QFileInfo::exists(location)) continue;
        state.adapterFound = true;
        auto candidate = std::make_unique<QPluginLoader>(location);
        candidate->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        if (auto *plugin = qobject_cast<TextHighlightingBackend *>(candidate->instance())) {
            state.loader = std::move(candidate);
            state.instance = plugin;
            state.explanation = QStringLiteral("KSyntaxHighlighting backend available.");
            return plugin;
        }
        state.explanation = candidate->errorString();
    }
    state.explanation = state.adapterFound
        ? QStringLiteral("The highlighting adapter could not load. It requires compatible KSyntaxHighlighting 6.0 or newer. %1").arg(state.explanation)
        : QStringLiteral("The highlighting adapter is missing. Install a Dzurkel package containing the adapter; the backend library alone is insufficient.");
    return nullptr;
}
}
HighlightingAvailability highlightingAvailability(bool recheck)
{
    const bool available = backend(recheck) != nullptr;
    return {available, backendState().explanation, backendState().adapterFound};
}
bool highlightingEnabled()
{
    auto settings = highlightingSettings();
    settings.setFallbacksEnabled(false);
    settings.sync();
    return settings.value(QStringLiteral("previews/syntaxHighlighting"), true).toBool();
}
bool setHighlightingEnabled(bool enabled, QString &error)
{
    auto settings = highlightingSettings();
    settings.setFallbacksEnabled(false);
    settings.setValue(QStringLiteral("previews/syntaxHighlighting"), enabled);
    settings.sync();
    error = settings.status() == QSettings::NoError ? QString()
        : QStringLiteral("Could not save the syntax highlighting setting. Check your configuration directory permissions.");
    return error.isEmpty();
}

bool highlightText(QPlainTextEdit *editor, const QString &fileName, const QString &mimeType)
{
    if (!highlightingEnabled()) return false;
    // Highlighting runs on the GUI thread. Use a smaller budget than text loading
    // so large/minified files still get a responsive plain-text preview.
    if (editor->document()->characterCount() > 256 * 1024) return false;
    for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) {
        if (block.length() > 4096) return false;
    }
    auto *plugin = backend();
    return plugin && plugin->attach(editor, fileName, mimeType);
}

} // namespace dzurkel

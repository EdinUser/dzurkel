#include "CommandLine.h"
#include <QCommandLineParser>
#include <QDir>
#include <QRegularExpression>
#include <QUrl>

namespace dzurkel {
namespace {
void configure(QCommandLineParser &parser)
{
    parser.setApplicationDescription(QStringLiteral("Dzurkel: a single-instance local file preview service."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("show"), QStringLiteral("Show or replace the preview.")});
    parser.addOption({QStringLiteral("hide"), QStringLiteral("Hide the preview; keep the service running.")});
    parser.addOption({QStringLiteral("toggle"), QStringLiteral("Hide a visible preview, or show the supplied file.")});
    parser.addOption({QStringLiteral("quit"), QStringLiteral("Stop the running service.")});
    parser.addOption({QStringLiteral("diagnostics"), QStringLiteral("Log startup and focus information for a new primary instance.")});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("One local path or file:// URL for Show/Toggle."), QStringLiteral("[file]"));
}
}

bool parseCommandLine(const QStringList &arguments, Request &request, QString &error)
{
    request = {};
    error.clear();
    QCommandLineParser parser;
    configure(parser);
    if (!parser.parse(arguments)) {
        error = parser.errorText();
        return false;
    }
    int commands = 0;
    for (const auto &name : {"show", "hide", "toggle", "quit", "help", "version"}) {
        if (parser.isSet(QString::fromLatin1(name))) {
            ++commands;
        }
    }
    if (commands > 1) {
        error = QStringLiteral("Choose exactly one command.");
        return false;
    }
    if (parser.isSet(QStringLiteral("help"))) request.command = Command::Help;
    else if (parser.isSet(QStringLiteral("version"))) request.command = Command::Version;
    else if (parser.isSet(QStringLiteral("hide"))) request.command = Command::Hide;
    else if (parser.isSet(QStringLiteral("toggle"))) request.command = Command::Toggle;
    else if (parser.isSet(QStringLiteral("quit"))) request.command = Command::Quit;
    request.diagnostics = parser.isSet(QStringLiteral("diagnostics"));
    const bool needsFile = request.command == Command::Show || request.command == Command::Toggle;
    const auto paths = parser.positionalArguments();
    if (paths.size() != (needsFile ? 1 : 0)) {
        error = needsFile ? QStringLiteral("Expected exactly one local file.")
                          : QStringLiteral("This command does not accept a file argument.");
        return false;
    }
    if (needsFile) {
        const QString input = paths.first();
        if (input.isEmpty()) {
            error = QStringLiteral("A nonempty file argument is required.");
            return false;
        }
        static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
        // Resolve against the caller's cwd, never the resident service's cwd.
        request.url = scheme.match(input).hasMatch() ? input
            : QUrl::fromLocalFile(QDir::current().absoluteFilePath(input)).toString(QUrl::FullyEncoded);
    }
    return true;
}

QString commandLineHelp()
{
    QCommandLineParser parser;
    configure(parser);
    return parser.helpText();
}
} // namespace dzurkel

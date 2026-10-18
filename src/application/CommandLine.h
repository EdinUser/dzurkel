#pragma once
#include <QStringList>

namespace dzurkel {
enum class Command { Show, Hide, Toggle, Quit, Settings, NautilusPreviewService, Help, Version };
struct Request {
    Command command = Command::Show;
    QString url;
    QString activationToken;
    bool diagnostics = false;
};
bool parseCommandLine(const QStringList &arguments, Request &request, QString &error);
QString commandLineHelp();
} // namespace dzurkel

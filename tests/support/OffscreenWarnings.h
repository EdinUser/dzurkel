#pragma once
#include <QGuiApplication>
#include <QLoggingCategory>
#include <cstdio>

// The offscreen QPA backend cannot propagate native size hints. Layout tests
// deliberately exercise that path; retain every other message and native warning.
class OffscreenWarnings {
public:
    OffscreenWarnings()
    {
        installed_ = QGuiApplication::platformName() == QLatin1String("offscreen");
        if (installed_) previous_ = qInstallMessageHandler(handle);
    }
    ~OffscreenWarnings()
    {
        if (installed_) qInstallMessageHandler(previous_);
    }
    OffscreenWarnings(const OffscreenWarnings &) = delete;
    OffscreenWarnings &operator=(const OffscreenWarnings &) = delete;
private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (type == QtWarningMsg && message == QLatin1String("This plugin does not support propagateSizeHints()"))
            return;
        if (previous_) previous_(type, context, message);
        else std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
    }
    inline static QtMessageHandler previous_ = nullptr;
    bool installed_ = false;
};

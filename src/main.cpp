#include "application/CommandLine.h"
#include "application/InstanceCoordinator.h"
#include "controller/PreviewController.h"
#include "ipc/PreviewService.h"
#include "resources/FileResource.h"

#include <QApplication>
#include <QTextStream>

namespace {
int fail(const QString &error, int code = 1)
{
    QTextStream(stderr) << "dzurkel: " << error << '\n';
    return code;
}
}

int main(int argc, char *argv[])
{
    dzurkel::Request request;
    QString error;
    {
        // Secondary invocations need neither a display nor a GUI service of
        // their own. This named bus connection is closed before GUI startup.
        QCoreApplication commandLine(argc, argv);
        commandLine.setApplicationName(QStringLiteral("dzurkel"));
        commandLine.setApplicationVersion(QStringLiteral(DZURKEL_VERSION));
        if (!dzurkel::parseCommandLine(commandLine.arguments(), request, error)) return fail(error, 2);
        if (request.command == dzurkel::Command::Help) {
            QTextStream(stdout) << dzurkel::commandLineHelp();
            return 0;
        }
        if (request.command == dzurkel::Command::Version) {
            QTextStream(stdout) << "dzurkel " << commandLine.applicationVersion() << '\n';
            return 0;
        }
        if (request.command == dzurkel::Command::Show) {
            dzurkel::FileResource resource;
            if (!dzurkel::resolveFileResource(request.url, resource, error)) return fail(error);
        }
        dzurkel::InstanceCoordinator client(QStringLiteral("dzurkel-cli"));
        const auto result = client.forward(request, error);
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Delivered) return 0;
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Failed) return fail(error);
        // Hiding or quitting an absent instance is a successful no-op.
        if (request.command == dzurkel::Command::Hide || request.command == dzurkel::Command::Quit) return 0;
        dzurkel::FileResource resource;
        if (!dzurkel::resolveFileResource(request.url, resource, error)) return fail(error);
    }

    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("dzurkel"));
    application.setApplicationVersion(QStringLiteral(DZURKEL_VERSION));
    application.setApplicationDisplayName(QStringLiteral("Dzurkel"));
    application.setQuitOnLastWindowClosed(false);
    dzurkel::PreviewController controller(request.diagnostics);
    dzurkel::PreviewService service(controller);
    dzurkel::InstanceCoordinator instance(QStringLiteral("dzurkel-primary"));
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto claim = instance.claim(service, error);
        if (claim == dzurkel::InstanceCoordinator::ClaimResult::Failed) return fail(error);
        if (claim == dzurkel::InstanceCoordinator::ClaimResult::Primary) {
            if (!controller.show(request.url, error)) return fail(error);
            if (request.diagnostics) {
                QTextStream(stderr) << "Dzurkel primary PID: " << application.applicationPid()
                    << " platform: " << application.platformName() << '\n';
            }
            return application.exec();
        }
        // Another process won startup. Forward; never create a second window.
        const auto result = instance.forward(request, error);
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Delivered) return 0;
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Failed) return fail(error);
        // The winner exited before delivery; it is safe to attempt ownership.
    }
    return fail(QStringLiteral("The primary instance kept changing during startup. Try again."));
}

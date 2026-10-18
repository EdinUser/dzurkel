#include "application/CommandLine.h"
#include "application/InstanceCoordinator.h"
#include "controller/PreviewController.h"
#include "desktop/DesktopIntegration.h"
#include "ipc/PreviewService.h"
#include "ipc/NautilusPreviewService.h"
#include "resources/FileResource.h"
#include "resources/ResourcePreparation.h"
#include "resources/FolderNavigation.h"

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
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-list-folder")) {
            if (commandLine.arguments().size() != 4) return 2;
            return dzurkel::enumerateResourceFolder(commandLine.arguments().at(2), commandLine.arguments().at(3) == QStringLiteral("navigate"));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-prepare-resource")) {
            if (commandLine.arguments().size() != 4) return 2;
            return dzurkel::prepareResourceSnapshot(commandLine.arguments().at(2), commandLine.arguments().at(3));
        }
        if (!dzurkel::parseCommandLine(commandLine.arguments(), request, error)) return fail(error, 2);
        request.activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
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
            if (!dzurkel::parseFileResource(request.url, resource, error)) return fail(error);
        }
        dzurkel::InstanceCoordinator client(QStringLiteral("dzurkel-cli"));
        const auto result = client.forward(request, error);
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Delivered) return 0;
        if (result == dzurkel::InstanceCoordinator::ForwardResult::Failed) return fail(error);
        // Hiding or quitting an absent instance is a successful no-op.
        if (request.command == dzurkel::Command::Hide || request.command == dzurkel::Command::Quit) return 0;
        if (request.command != dzurkel::Command::Settings && request.command != dzurkel::Command::NautilusPreviewService) {
            dzurkel::FileResource resource;
            if (!dzurkel::parseFileResource(request.url, resource, error)) return fail(error);
        }
    }

    // KDE consumes the saved token on the actual show request. Avoid also
    // consuming it through Qt's implicit startup activation.
    if (dzurkel::supportsActivationTokens()) qunsetenv("XDG_ACTIVATION_TOKEN");
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("dzurkel"));
    application.setApplicationVersion(QStringLiteral(DZURKEL_VERSION));
    application.setApplicationDisplayName(QStringLiteral("Dzurkel"));
    application.setDesktopFileName(QStringLiteral("dzurkel"));
    application.setQuitOnLastWindowClosed(false);
    dzurkel::PreviewController controller(request.diagnostics);
    dzurkel::NautilusPreviewService nautilus(controller);
    dzurkel::PreviewService service(controller, &nautilus);
    dzurkel::InstanceCoordinator instance(QStringLiteral("dzurkel-primary"));
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto claim = instance.claim(service, error);
        if (claim == dzurkel::InstanceCoordinator::ClaimResult::Failed) return fail(error);
        if (claim == dzurkel::InstanceCoordinator::ClaimResult::Primary) {
            if (request.command == dzurkel::Command::NautilusPreviewService) {
                if (!nautilus.start(error)) return fail(error);
            } else if (nautilus.integration().enabled()) {
                QString startupError;
                if (!nautilus.start(startupError)) QTextStream(stderr) << "dzurkel: " << startupError << '\n';
            }
            if (request.command == dzurkel::Command::Settings) controller.showSettings(request.activationToken);
            else if (request.command != dzurkel::Command::NautilusPreviewService
                && !controller.show(request.url, error, nullptr, request.activationToken)) return fail(error);
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

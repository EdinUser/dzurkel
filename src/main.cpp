#include "preview/readers/DocumentReader.h"
#include "application/CommandLine.h"
#include "application/InstanceCoordinator.h"
#include "controller/PreviewController.h"
#include "desktop/DesktopIntegration.h"
#include "ipc/PreviewService.h"
#include "ipc/NautilusPreviewService.h"
#include "resources/FileResource.h"
#include "resources/ResourcePreparation.h"
#include "resources/FolderNavigation.h"
#include "resources/media/MediaSourceHelper.h"
#include "preview/pdf/PdfProtocol.h"
#include "preview/image/ImageDecodeJob.h"
#include "preview/xlsx/XlsxDecodeJob.h"
#include "preview/lazy/LazyCoordinator.h"

#include <QApplication>
#include <QIcon>
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
    if (argc == 4 && QByteArray(argv[1]) == QByteArrayLiteral("--internal-lazy") && dzurkel::DocumentReader::supportsPortions(QString::fromLocal8Bit(argv[3]))) {
        // Reuse the document decoder's headless runtime for native raster plugins.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication decoder(argc, argv);
        return dzurkel::serveLazy(decoder.arguments().at(2), decoder.arguments().at(3));
    }
    if (argc >= 2 && QByteArray(argv[1]) == QByteArrayLiteral("--internal-document")) {
        if (argc != 4) return 2;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication decoder(argc, argv);
        return dzurkel::serveDocument(decoder.arguments().at(2), decoder.arguments().at(3));
    }
    if (argc >= 2 && QByteArray(argv[1]) == QByteArrayLiteral("--internal-image")) {
        if (argc != 4) return 2;
        // Some public image plugins (e.g. SVG with text) require a GUI runtime.
        // The decoder has no window and never connects to the user's display.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication decoder(argc, argv);
        return dzurkel::serveImage(decoder.arguments().at(2), decoder.arguments().at(3));
    }

    dzurkel::Request request;
    QString error;
    bool kdeActivation = false;
    {
        // Secondary invocations need neither a display nor a GUI service of
        // their own. This named bus connection is closed before GUI startup.
        QCoreApplication commandLine(argc, argv);
        commandLine.setApplicationName(QStringLiteral("dzurkel"));
        commandLine.setApplicationVersion(QStringLiteral(DZURKEL_VERSION));
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-lazy")) {
            if (commandLine.arguments().size() != 4) return 2;
            return dzurkel::serveLazy(commandLine.arguments().at(2), commandLine.arguments().at(3));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-xlsx")) {
            if (commandLine.arguments().size() != 3) return 2;
            return dzurkel::serveXlsx(commandLine.arguments().at(2));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-pdf")) {
            if (commandLine.arguments().size() != 3) return 2;
            return dzurkel::servePdf(commandLine.arguments().at(2));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-media-source")) {
            if (commandLine.arguments().size() != 4 || commandLine.arguments().at(3) != QStringLiteral("0")) return 2;
            return dzurkel::serveMediaSource(commandLine.arguments().at(2));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-list-folder")) {
            if (commandLine.arguments().size() != 4) return 2;
            return dzurkel::enumerateResourceFolder(commandLine.arguments().at(2), commandLine.arguments().at(3) == QStringLiteral("navigate"));
        }
        if (commandLine.arguments().value(1) == QStringLiteral("--internal-prepare-resource")) {
            if (commandLine.arguments().size() != 3) return 2;
            return dzurkel::prepareResource(commandLine.arguments().at(2));
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
        if (request.command != dzurkel::Command::Settings && request.command != dzurkel::Command::Serve && request.command != dzurkel::Command::NautilusPreviewService) {
            dzurkel::FileResource resource;
            if (!dzurkel::parseFileResource(request.url, resource, error)) return fail(error);
        }
        // Resolve the optional adapter while QCoreApplication still exists.
        kdeActivation = dzurkel::supportsActivationTokens();
    }

    // KDE consumes the saved token on the actual show request. Avoid also
    // consuming it through Qt's implicit startup activation.
    if (kdeActivation) qunsetenv("XDG_ACTIVATION_TOKEN");
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("dzurkel"));
    application.setApplicationVersion(QStringLiteral(DZURKEL_VERSION));
    application.setApplicationDisplayName(QStringLiteral("Dzurkel"));
    application.setDesktopFileName(QStringLiteral("dzurkel"));
    // Embedded raster sizes also work before installation and without Qt SVG.
    QIcon applicationIcon;
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        applicationIcon.addFile(QStringLiteral(":/icons/%1x%1/apps/dzurkel.png").arg(size), QSize(size, size));
    }
    application.setWindowIcon(QIcon::fromTheme(QStringLiteral("dzurkel"), applicationIcon));
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
            else if (request.command != dzurkel::Command::NautilusPreviewService && request.command != dzurkel::Command::Serve
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

#include "LibraryAvailability.h"
#include "text/TextHighlighting.h"
#include "media/AudioBackend.h"
#include "desktop/DesktopIntegration.h"
#include <QImageReader>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVersionNumber>
#include <zip.h>

namespace dzurkel {
QString libraryInstallationCommand(const QString &id, const QString &distribution, const QString &release)
{
    QString package;
    QString prefix;
    if (distribution == QStringLiteral("fedora") && (release == QStringLiteral("43") || release == QStringLiteral("44"))) {
        prefix = QStringLiteral("sudo dnf install ");
        if (id == QStringLiteral("antiword")) package = QStringLiteral("antiword");
        else if (id == QStringLiteral("multimedia")) package = QStringLiteral("qt6-qtmultimedia");
        else if (id == QStringLiteral("highlighting")) package = QStringLiteral("kf6-syntax-highlighting");
        else if (id == QStringLiteral("desktop")) package = QStringLiteral("kf6-kwindowsystem");
        else if (id == QStringLiteral("imageformats")) package = QStringLiteral("qt6-qtimageformats");
    } else if (distribution == QStringLiteral("debian") && (release == QStringLiteral("12") || release == QStringLiteral("13"))) {
        prefix = QStringLiteral("sudo apt install ");
        if (id == QStringLiteral("antiword")) package = QStringLiteral("antiword");
        else if (id == QStringLiteral("multimedia")) package = QStringLiteral("libqt6multimedia6");
        else if (id == QStringLiteral("imageformats")) package = QStringLiteral("qt6-image-formats-plugins");
        else if (release == QStringLiteral("13") && id == QStringLiteral("highlighting")) package = QStringLiteral("libkf6syntaxhighlighting6");
        else if (release == QStringLiteral("13") && id == QStringLiteral("desktop")) package = QStringLiteral("libkf6windowsystem6");
    } else if (distribution == QStringLiteral("arch")) {
        prefix = QStringLiteral("sudo pacman -Syu ");
        if (id == QStringLiteral("antiword")) package = QStringLiteral("antiword");
        else if (id == QStringLiteral("multimedia")) package = QStringLiteral("qt6-multimedia");
        else if (id == QStringLiteral("highlighting")) package = QStringLiteral("syntax-highlighting");
        else if (id == QStringLiteral("desktop")) package = QStringLiteral("kwindowsystem");
        else if (id == QStringLiteral("imageformats")) package = QStringLiteral("qt6-imageformats");
    }
    return package.isEmpty() ? QString() : prefix + package;
}
QList<LibraryAvailability> libraryAvailability(bool recheck)
{
    const auto highlighting = highlightingAvailability(recheck);
    const auto audio = audioAvailability(recheck);
    const auto desktop = desktopAvailability(recheck);
    const auto formats = QImageReader::supportedImageFormats();
    const bool anyExtraImages = formats.contains("webp") || formats.contains("tiff") || formats.contains("tif");
    const bool extraImages = formats.contains("webp") && (formats.contains("tiff") || formats.contains("tif"));
    const auto antiword = QStandardPaths::findExecutable(QStringLiteral("antiword"));
    const auto zipVersion = QString::fromLatin1(zip_libzip_version());
    const bool zipCompatible = QVersionNumber::fromString(zipVersion) >= QVersionNumber(1, 7);
    const auto guidance = QStringLiteral(
        "\n\nSystem: %1. Use packages compatible with this distribution release and Dzurkel build. "
        "If your enabled repositories cannot supply them, keep this optional feature unavailable "
        "or use a supported OS release with a compatible Dzurkel package. "
        "Refresh status does not install packages or add repositories.").arg(QSysInfo::prettyProductName());
    const auto highlightingDetails = highlighting.explanation + QStringLiteral(
        "\n\nProvides syntax colours for recognised text/source formats. Plain text remains available. "
        "The checkbox controls use of highlighting; it does not install or remove the backend. "
        "Changes apply to the next preview. A loaded adapter stays in use until Dzurkel exits.")
        + (highlighting.available ? QString() : QStringLiteral(
            "\n\nRequires the Dzurkel highlighting adapter and compatible KSyntaxHighlighting 6.0 or newer. "
            "For a missing adapter, install a Dzurkel package that includes it. For a load failure, "
            "install or repair the matching runtime dependency and adapter, then refresh. "
            "A KDE desktop is not required; a KF5 library cannot replace this KF6 dependency.") + guidance);
    QList<LibraryAvailability> result = {
        {QStringLiteral("highlighting"), QStringLiteral("KSyntaxHighlighting"), QStringLiteral("Source highlighting"),
         highlighting.available ? LibraryHealth::Available : highlighting.adapterFound ? LibraryHealth::Limited : LibraryHealth::Missing,
         highlighting.available ? QStringLiteral("Available") : highlighting.adapterFound ? QStringLiteral("Cannot load") : QStringLiteral("Missing adapter"),
         highlightingDetails},
        {QStringLiteral("antiword"), QStringLiteral("antiword"), QStringLiteral("Legacy Word DOC"),
         antiword.isEmpty() ? LibraryHealth::Missing : LibraryHealth::Available,
         antiword.isEmpty() ? QStringLiteral("Missing") : QStringLiteral("Available"),
         (antiword.isEmpty() ? QStringLiteral("The antiword executable was not found. Legacy .doc files show file information. "
             "Install the antiword extractor through your distribution's package manager, then refresh status.") + guidance
             : QStringLiteral("Executable found: %1. Supplies text-only previews for legacy .doc files. "
                 "Formatting is omitted. Discovery does not certify every document; extraction can still fail.").arg(antiword))},
        {QStringLiteral("pdf"), QStringLiteral("Qt PDF"), QStringLiteral("PDF documents"), LibraryHealth::Available, QStringLiteral("Available"),
         QStringLiteral("PDF support is included in this installation, using Qt runtime %1. Runtime components must match this Dzurkel package. "
             "It supplies read-only PDF rendering and zoom. Protected, damaged or oversized files can still be unavailable. "
             "Qt PDF is currently required to start Dzurkel. If it is missing or incompatible, the application cannot start. "
             "Repair or reinstall a compatible Dzurkel package if startup fails.").arg(QString::fromLatin1(qVersion()))},
        {QStringLiteral("zip"), QStringLiteral("libzip"), QStringLiteral("XLSX, DOCX and EPUB"),
         zipCompatible ? LibraryHealth::Available : LibraryHealth::Limited,
         zipCompatible ? QStringLiteral("Available") : QStringLiteral("Too old"),
         QStringLiteral("Detected libzip %1; minimum 1.7. Supplies bounded ZIP package reading for XLSX, DOCX and EPUB. "
             "RTF, plain text and CSV do not use it. It does not provide Excel formula calculation or Office layout fidelity. "
             "libzip is currently required to start this Dzurkel package.").arg(zipVersion)
             + (zipCompatible ? QString() : QStringLiteral("\n\nInstall a compatible Dzurkel/libzip package set. Do not replace a single system library with an unrelated build.") + guidance)},
        {QStringLiteral("multimedia"), QStringLiteral("Qt Multimedia"), QStringLiteral("Audio playback"),
         audio.available ? LibraryHealth::Available : audio.adapterFound ? LibraryHealth::Limited : LibraryHealth::Missing,
         audio.available ? QStringLiteral("Available") : audio.adapterFound ? QStringLiteral("Cannot load") : QStringLiteral("Missing adapter"),
         audio.explanation + QStringLiteral("\n\nAudio is optional. Text, PDF, images and document previews remain available without it. "
             "Individual audio formats still depend on the installed media backend, codec and output device. "
             "Refresh retries a missing adapter. After replacing a loaded backend, restart Dzurkel.") + guidance},
        {QStringLiteral("imageformats"), QStringLiteral("Qt image readers"), QStringLiteral("Additional image formats"),
         extraImages ? LibraryHealth::Available : anyExtraImages ? LibraryHealth::Limited : LibraryHealth::Missing,
         extraImages ? QStringLiteral("Available") : anyExtraImages ? QStringLiteral("Limited") : QStringLiteral("Missing readers"),
         QStringLiteral("Additional readers depend on the Qt plugins installed for this runtime. "
             "TIFF and WebP support is %1. The Previews tab lists currently available formats. "
             "Install the matching image-formats package for extra readers. Formats configured into Qt itself, including GIF, "
             "may instead require repairing the Qt installation. Qt image-plugin discovery may require a Dzurkel restart after installation.")
             .arg(extraImages ? QStringLiteral("available") : QStringLiteral("incomplete")) + guidance},
        {QStringLiteral("desktop"), QStringLiteral("KF6 WindowSystem"), QStringLiteral("Desktop activation"),
         desktop.available ? LibraryHealth::Available : desktop.adapterFound ? LibraryHealth::Limited : LibraryHealth::Missing,
         desktop.available ? QStringLiteral("Available") : desktop.adapterFound ? QStringLiteral("Cannot load") : QStringLiteral("Missing adapter"),
         desktop.explanation + QStringLiteral("\n\nThis optional enhancement provides KDE Wayland activation and X11 Thunar focus detection. "
             "It is independent of file-format support; core previews use standard Qt activation without it.") + guidance}

    };
    for (auto &library : result) {
        if (library.health == LibraryHealth::Available) continue;
        const bool missingAdapter = (library.id == QStringLiteral("highlighting") && !highlighting.adapterFound)
            || (library.id == QStringLiteral("multimedia") && !audio.adapterFound)
            || (library.id == QStringLiteral("desktop") && !desktop.adapterFound);
        if (missingAdapter) continue; // A runtime-library command cannot restore an absent Dzurkel adapter.
        library.installationCommand = libraryInstallationCommand(library.id, QSysInfo::productType(), QSysInfo::productVersion());
        if (!library.installationCommand.isEmpty())
            library.details += QStringLiteral("\n\nInstallation command (run it yourself):\n%1\n\nYour package manager resolves dependencies from enabled repositories. "
                "This command may install or update system packages; inspect its proposed changes. "
                "It cannot repair an adapter from an incompatible Dzurkel package.").arg(library.installationCommand);
        else library.details += QStringLiteral("\n\nNo installation command is verified for this distribution/release. Consult its package manager or a compatible Dzurkel distributor.");
    }
    return result;
}
}

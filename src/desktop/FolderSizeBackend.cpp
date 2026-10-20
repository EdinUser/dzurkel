#include "FolderSizeBackend.h"
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QPluginLoader>
#include <limits>

namespace dzurkel {
FolderSizeBackend *folderSizeBackend()
{
    // Keep a successfully loaded adapter resident, as with other desktop modules.
    static QPluginLoader *loader = nullptr;
    static FolderSizeBackend *backend = nullptr;
    if (backend) return backend;
    const QDir application(QCoreApplication::applicationDirPath());
    const bool kde = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':').contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
    const QString kio = QStringLiteral("libdzurkel_folder_size.so");
    const QString gio = QStringLiteral("libdzurkel_folder_size_gio.so");
    const QStringList modules = kde ? QStringList{kio, gio} : QStringList{gio, kio};
    for (const auto &module : modules) {
        for (const auto &path : {application.filePath(QStringLiteral("dzurkel-plugins/") + module),
                                application.filePath(QStringLiteral(DZURKEL_FOLDER_SIZE_INSTALL_PATH)).section('/', 0, -2) + '/' + module}) {
            auto *candidate = new QPluginLoader(path);
            candidate->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
            if (auto *instance = qobject_cast<FolderSizeBackend *>(candidate->instance())) {
                loader = candidate;
                backend = instance;
                return backend;
            }
            delete candidate;
        }
    }
    Q_UNUSED(loader);
    return nullptr;
}
QString folderSizeText(const FolderSize &size)
{
    if (size.state == FolderSizeState::Unavailable) return QStringLiteral("Unavailable");
    if (size.state == FolderSizeState::Calculating && !size.bytes && !size.files && !size.folders)
        return QStringLiteral("Calculating…");
    const auto bytes = QString::number(size.bytes);
    const auto formatted = size.bytes <= quint64(std::numeric_limits<qint64>::max())
        ? QLocale().formattedDataSize(qint64(size.bytes)) : bytes + QStringLiteral(" bytes");
    auto text = QStringLiteral("%1 (%2 bytes)").arg(formatted, bytes);
    switch (size.state) {
    case FolderSizeState::Calculating: return text + QStringLiteral(" counted so far — calculating…");
    case FolderSizeState::Partial: return text + QStringLiteral(" — partial");
    case FolderSizeState::Stopped: return text + QStringLiteral(" — stopped");
    default: return text;
    }
}
QString folderCountText(const FolderSize &size)
{
    if (size.state == FolderSizeState::Unavailable) return {};
    return QStringLiteral("%1 %2, %3 %4%5").arg(size.files)
        .arg(size.files == 1 ? QStringLiteral("file") : QStringLiteral("files")).arg(size.folders)
        .arg(size.folders == 1 ? QStringLiteral("subfolder") : QStringLiteral("subfolders"))
        .arg(size.state == FolderSizeState::Complete ? QString() : QStringLiteral(" counted so far"));
}
}

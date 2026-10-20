#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtPlugin>
#include <functional>

namespace dzurkel {
enum class FolderSizeState { Unavailable, Calculating, Complete, Partial, Stopped };
struct FolderSize {
    FolderSizeState state = FolderSizeState::Unavailable;
    quint64 bytes = 0;
    quint64 files = 0;
    quint64 folders = 0;
    QString explanation;
};
using FolderSizeCallback = std::function<void(const FolderSize &)>;
class FolderSizeBackend {
public:
    virtual ~FolderSizeBackend() = default;
    // GUI-thread asynchronous job. Deleting the returned object cancels it.
    // The callback runs on this thread and must not delete the job inline.
    virtual QObject *start(const QUrl &resolvedLocalUrl, FolderSizeCallback callback) = 0;
};
QString folderSizeText(const FolderSize &size);
QString folderCountText(const FolderSize &size);
FolderSizeBackend *folderSizeBackend();
}
#define DZURKEL_FOLDER_SIZE_IID "org.dzurkel.FolderSizeBackend/2"
Q_DECLARE_INTERFACE(dzurkel::FolderSizeBackend, DZURKEL_FOLDER_SIZE_IID)

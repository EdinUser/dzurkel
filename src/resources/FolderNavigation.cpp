#include "FolderNavigation.h"
#include "ResourcePreparation.h"
#include <QCoreApplication>
#include <QCollator>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QTextStream>
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace dzurkel {
namespace {
int activeListings = 0;
int report(const QJsonObject &object)
{
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact);
    return 0;
}
int failure(const QString &message) { return report({{QStringLiteral("error"), message}}); }
}
FolderNavigation::FolderNavigation(QObject *parent, int timeoutMs) : QObject(parent)
{
    timer_.setSingleShot(true);
    timer_.setInterval(timeoutMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        finish(QStringLiteral("Folder navigation timed out. Preview remains available."));
    });
}
FolderNavigation::~FolderNavigation() { cancel(); }
void FolderNavigation::cancel()
{
    timer_.stop();
    output_.clear();
    if (!process_) return;
    auto *process = process_;
    process_ = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->setParent(QCoreApplication::instance());
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
            [process] { process->deleteLater(); });
    if (process->state() == QProcess::NotRunning) process->deleteLater();
    else process->kill();
}
void FolderNavigation::start(const FileResource &resource, int direction)
{
    cancel();
    resource_ = resource;
    direction_ = direction;
    if (activeListings >= 2) {
        emit ready({}, -1, QStringLiteral("Previous folder access is still stopping. Reopen the preview to retry."), {});
        return;
    }
    process_ = new QProcess(this);
    ++activeListings;
    connect(process_, &QObject::destroyed, [] { --activeListings; });
    process_->setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"));
    process_->setArguments({QStringLiteral("--internal-list-folder"), resource.url.toString(QUrl::FullyEncoded),
                           direction ? QStringLiteral("navigate") : QStringLiteral("inspect")});
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        output_ += process_->readAllStandardOutput();
        if (output_.size() > ResponseLimit) finish(QStringLiteral("Folder navigation response exceeds its limit."));
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this] { process_->readAllStandardError(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Cannot start folder navigation."));
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (code || status != QProcess::NormalExit) finish(QStringLiteral("Folder navigation is unavailable."));
        else finish();
    });
    timer_.start();
    process_->start();
}
void FolderNavigation::finish(const QString &error)
{
    if (!process_) return;
    output_ += process_->readAllStandardOutput();
    const auto object = output_.size() <= ResponseLimit ? QJsonDocument::fromJson(output_).object() : QJsonObject();
    const auto resource = resource_;
    const auto direction = direction_;
    cancel();
    QString message = error;
    if (message.isEmpty()) message = object.value(QStringLiteral("error")).toString();
    if (!message.isEmpty() || !object.contains(QStringLiteral("names"))) {
        emit ready({}, -1, message.isEmpty() ? QStringLiteral("Folder navigation is unavailable.") : message, {});
        return;
    }
    QStringList urls;
    const auto names = object.value(QStringLiteral("names")).toArray();
    const auto currentName = resource.url.fileName();
    int index = -1;
    for (const auto &entry : names) {
        const auto name = entry.toString();
        if (name.isEmpty() || name.contains('/') || name == QStringLiteral(".") || name == QStringLiteral("..")) {
            emit ready({}, -1, QStringLiteral("Invalid folder navigation response."), {});
            return;
        }
        if (name == currentName) index = urls.size();
        auto url = resource.url;
        const auto path = url.path();
        url.setPath(path.left(path.lastIndexOf('/') + 1) + name);
        urls.append(url.toString(QUrl::FullyEncoded));
    }
    QString selected;
    if (direction && index >= 0 && index + direction >= 0 && index + direction < urls.size())
        selected = urls.at(index + direction);
    message = object.value(QStringLiteral("notice")).toString();
    emit ready(urls, index, message, selected);
}

int enumerateResourceFolder(const QString &url, bool allowMissing)
{
    FileResource resource;
    QString error, path;
    if (!parseFileResource(url, resource, error) || !resolveDesktopPath(resource, path, error)) return failure(error);
    struct stat current{};
    if (::lstat(QFile::encodeName(path).constData(), &current) != 0) {
        if (!allowMissing || errno != ENOENT) return failure(QStringLiteral("Folder navigation cannot inspect the selected file."));
    } else if (!S_ISREG(current.st_mode)) {
        return failure(QStringLiteral("Folder navigation is available for regular files only."));
    }
    const auto folder = QFileInfo(path).absolutePath();
    auto closeDirectory = [](DIR *value) { ::closedir(value); };
    std::unique_ptr<DIR, decltype(closeDirectory)> directory(::opendir(QFile::encodeName(folder).constData()), closeDirectory);
    if (!directory) return failure(QStringLiteral("Cannot list this folder. Preview remains available."));
    QElapsedTimer timer;
    timer.start();
    QStringList names;
    const auto currentName = resource.url.fileName();
    int count = 0;
    bool skipped = false;
    while (true) {
        errno = 0;
        const auto *entry = ::readdir(directory.get());
        if (!entry) {
            if (errno) return failure(QStringLiteral("Folder listing failed; navigation is unavailable."));
            break;
        }
        const QByteArray raw(entry->d_name);
        if (raw == "." || raw == "..") continue;
        if (++count > FolderNavigation::EntryLimit || timer.elapsed() > 5000)
            return failure(QStringLiteral("Folder navigation is limited to 10,000 entries and 5 seconds of listing work. No partial order is shown."));
        const auto name = QFile::decodeName(raw);
        // Skip non-round-trippable names rather than navigating to another file.
        if (QFile::encodeName(name) != raw) { skipped = true; continue; }
        if (name.startsWith('.') && name != currentName) continue;
        struct stat info{};
        if (::fstatat(::dirfd(directory.get()), raw.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
            skipped = true;
            continue;
        }
        if (S_ISREG(info.st_mode)) names.append(name);
    }
    if (!names.contains(currentName)) names.append(currentName); // Missing current file remains an ordering anchor.
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(names.begin(), names.end(), [&collator](const QString &a, const QString &b) {
        const int order = collator.compare(a, b);
        return order ? order < 0 : a.compare(b, Qt::CaseSensitive) < 0;
    });
    if (timer.elapsed() > 5000)
        return failure(QStringLiteral("Folder navigation exceeded its 5-second work limit. No partial order is shown."));
    QJsonObject result{{QStringLiteral("names"), QJsonArray::fromStringList(names)}};
    if (skipped || (allowMissing && current.st_mode == 0)) result.insert(QStringLiteral("notice"),
        QStringLiteral("Folder changed or some entries could not be read; unavailable entries were skipped."));
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > FolderNavigation::ResponseLimit)
        return failure(QStringLiteral("Folder navigation listing exceeds its response limit. No partial order is shown."));
    return report(result);
}
}

#include "FileInfoProvider.h"
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QVBoxLayout>

namespace dzurkel {
PreviewData FileInfoProvider::describe(const FileResource &resource)
{
    PreviewData data;
    data.resource = resource;
    const QFileInfo info(resource.path);
    const auto mime = QMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    data.mimeType = mime.name();
    data.description = mime.comment();
    data.size = info.size();
    data.modified = info.lastModified();
    if (info.isSymLink()) {
        data.linkTarget = info.symLinkTarget();
        data.description = QStringLiteral("Symbolic link");
        data.mimeType = QStringLiteral("inode/symlink");
        data.message = QStringLiteral("Symbolic links are shown as information only; targets are not opened.");
    } else if (info.isDir()) {
        data.message = QStringLiteral("Directory information — contents are not listed.");
    } else if (!info.isFile()) {
        data.description = QStringLiteral("Special filesystem object");
        data.mimeType = QStringLiteral("application/octet-stream");
        data.message = QStringLiteral("Special filesystem objects are never opened for content preview.");
    } else {
        data.message = QStringLiteral("Content preview is unavailable for this file type.");
    }
    return data;
}
QWidget *FileInfoProvider::createWidget(const PreviewData &data, QWidget *parent)
{
    auto *widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("fileInfoPreview"));
    auto *layout = new QVBoxLayout(widget);
    auto *icon = new QLabel(widget);
    const auto mime = QMimeDatabase().mimeTypeForName(data.mimeType);
    const QString iconName = data.mimeType == QStringLiteral("inode/symlink")
        ? QStringLiteral("emblem-symbolic-link") : mime.iconName();
    const auto fallback = QApplication::style()->standardIcon(
        data.mimeType == QStringLiteral("inode/directory") ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    icon->setPixmap(QIcon::fromTheme(iconName, fallback).pixmap(64, 64));
    layout->addWidget(icon);
    const auto add = [&](const QString &text) {
        auto *label = new QLabel(text, widget);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
    };
    add(QStringLiteral("%1 (%2)").arg(data.description, data.mimeType));
    add(QStringLiteral("Size: %1 (%2 bytes)").arg(QLocale().formattedDataSize(data.size)).arg(data.size));
    add(QStringLiteral("Modified: %1").arg(data.modified.isValid() ? QLocale().toString(data.modified, QLocale::ShortFormat) : QStringLiteral("Unavailable")));
    if (!data.linkTarget.isEmpty()) add(QStringLiteral("Target: %1").arg(data.linkTarget));
    add(data.message);
    layout->addStretch();
    return widget;
}
} // namespace dzurkel

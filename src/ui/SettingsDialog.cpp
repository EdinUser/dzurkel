#include "SettingsDialog.h"
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QShowEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace dzurkel {
SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(QStringLiteral("Dzurkel Settings"));
    setWindowModality(Qt::WindowModal);
    auto *layout = new QVBoxLayout(this);
    auto *desktop = new QGroupBox(QStringLiteral("Dolphin shortcut"), this);
    auto *desktopLayout = new QVBoxLayout(desktop);
    auto *description = new QLabel(QStringLiteral(
        "Preview selected files with Space in Dolphin (26.04+). This replaces Space selection "
        "and conflicting shortcuts; alternate bindings are kept.\n\n"
        "Close Dolphin before applying or restoring, then reopen it."), desktop);
    description->setWordWrap(true);
    desktopLayout->addWidget(description);
    status_ = new QLabel(desktop);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    desktopLayout->addWidget(status_);
    auto *actions = new QHBoxLayout;
    apply_ = new QPushButton(QStringLiteral("Use Space in Dolphin"), desktop);
    apply_->setObjectName(QStringLiteral("useSpace"));
    restore_ = new QPushButton(QStringLiteral("Restore previous shortcuts"), desktop);
    restore_->setObjectName(QStringLiteral("restoreShortcuts"));
    // Avoid Enter activating an external settings change by default.
    apply_->setAutoDefault(false);
    restore_->setAutoDefault(false);
    actions->addWidget(apply_);
    actions->addWidget(restore_);
    desktopLayout->addLayout(actions);
    layout->addWidget(desktop);
    auto *support = new QGroupBox(QStringLiteral("Preview support"), this);
    auto *supportLayout = new QVBoxLayout(support);
    auto *formats = new QLabel(QStringLiteral(
        "Images, PDF, and plain text are included. Other files show file information."), support);
    formats->setWordWrap(true);
    supportLayout->addWidget(formats);
    QStringList names;
    for (const auto &format : QImageReader::supportedImageFormats()) names.append(QString::fromLatin1(format));
    names.removeDuplicates();
    std::sort(names.begin(), names.end());
    QStringList common;
    for (const QString &name : {QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("bmp"),
             QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("tiff"),
             QStringLiteral("svg"), QStringLiteral("avif"), QStringLiteral("heif")})
        if (names.contains(name)) common.append(name.toUpper());
    auto *readers = new QLabel(QStringLiteral("Available common image formats: %1")
        .arg(common.isEmpty() ? QStringLiteral("none detected") : common.join(QStringLiteral(", "))), support);
    readers->setToolTip(QStringLiteral("All installed image reader names: %1").arg(names.join(QStringLiteral(", "))));
    readers->setWordWrap(true);
    readers->setTextFormat(Qt::PlainText);
    supportLayout->addWidget(readers);
    layout->addWidget(support);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(apply_, &QPushButton::clicked, this, [this] {
        QString error;
        if (!shortcuts_.useSpace(error)) QMessageBox::warning(this, QStringLiteral("Shortcut setup"), error);
        refresh();
    });
    connect(restore_, &QPushButton::clicked, this, [this] {
        QString error;
        if (!shortcuts_.restore(error)) QMessageBox::warning(this, QStringLiteral("Restore shortcuts"), error);
        refresh();
    });
    // Wrapped labels need height-for-width, rather than the unconstrained hint.
    const int width = 560;
    setMinimumSize(520, layout->totalHeightForWidth(520));
    resize(width, layout->totalHeightForWidth(width));
    refresh();
}
void SettingsDialog::showEvent(QShowEvent *event)
{
    refresh();
    QDialog::showEvent(event);
}
void SettingsDialog::refresh()
{
    const bool installed = !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("kio/servicemenus/dzurkel.desktop")).isEmpty();
    const bool active = shortcuts_.usesSpace();
    const bool supported = shortcuts_.supportsAutomaticSetup();
    status_->setText(!supported ? QStringLiteral("For a custom shortcut scheme, configure Dzurkel through Dolphin's shortcut editor.")
        : !installed ? QStringLiteral("Install Dzurkel's Dolphin integration to enable shortcut setup.")
        : active ? QStringLiteral("Space is configured for Dzurkel. Reopen Dolphin to load changes.")
        : QStringLiteral("Your current Dolphin shortcuts are preserved until you choose Use Space."));
    apply_->setEnabled(supported && installed && !active && !shortcuts_.hasBackup());
    restore_->setEnabled(shortcuts_.hasBackup());
}
} // namespace dzurkel

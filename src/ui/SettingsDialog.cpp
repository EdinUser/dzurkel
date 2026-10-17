#include "SettingsDialog.h"
#include "desktop/DesktopIntegration.h"
#include <QEvent>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrentRun>
#include <algorithm>

namespace dzurkel {
namespace {
QToolButton *information(QWidget *parent, const QString &name, const QString &title, const QString &text)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setIcon(QIcon::fromTheme(QStringLiteral("help-about"), parent->style()->standardIcon(QStyle::SP_MessageBoxInformation)));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped()));
    button->setAccessibleName(title + QStringLiteral(" information"));
    button->setAccessibleDescription(text);
    QObject::connect(button, &QToolButton::clicked, parent, [parent, title, text] {
        QMessageBox message(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
        message.setTextFormat(Qt::PlainText);
        message.exec();
    });
    return button;
}
QLabel *label(QWidget *parent, const QString &text, bool secondary = false)
{
    auto *result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    if (secondary) result->setForegroundRole(QPalette::PlaceholderText);
    return result;
}
}
SettingsDialog::SettingsDialog(QWidget *parent, DolphinShortcuts shortcuts,
                               DolphinSession session, std::function<bool()> reopen)
    : QDialog(parent), shortcuts_(std::move(shortcuts)), session_(std::move(session)),
      reopen_(reopen ? std::move(reopen) : [] { return QProcess::startDetached(QStringLiteral("dolphin"), QStringList{}); })
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(QStringLiteral("Dzurkel Settings"));
    setWindowModality(Qt::WindowModal);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(14);
    auto *heading = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("system-file-manager"), style()->standardIcon(QStyle::SP_DirIcon)).pixmap(32, 32));
    heading->addWidget(icon);
    auto *title = label(this, QStringLiteral("Dolphin shortcut"));
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(information(this, QStringLiteral("shortcutInfo"), QStringLiteral("Dolphin shortcut"),
        QStringLiteral("Preview selected files with Space in Dolphin 26.04 or newer. This replaces Space selection and conflicting Space shortcuts; alternate bindings are kept. Restore previous shortcuts uses the backup to undo only Dzurkel’s changes, preserving unrelated toolbar and menu edits.")));
    layout->addLayout(heading);
    status_ = label(this, {});
    status_->setObjectName(QStringLiteral("shortcutStatus"));
    layout->addWidget(status_);
    auto *restart = label(this, QStringLiteral("If Dolphin is open, you’ll be asked to restart it."), true);
    restart->setToolTip(QStringLiteral("Dolphin closes before the shortcut file is updated, so it cannot overwrite the change while saving its own settings."));
    layout->addWidget(restart);
    auto *actions = new QHBoxLayout;
    apply_ = new QPushButton(QStringLiteral("Enable Space shortcut…"), this);
    apply_->setObjectName(QStringLiteral("useSpace"));
    restore_ = new QPushButton(QStringLiteral("Restore previous shortcuts…"), this);
    restore_->setObjectName(QStringLiteral("restoreShortcuts"));
    restore_->setToolTip(QStringLiteral("Restore only the shortcut entries changed by Dzurkel. Unrelated toolbar and menu changes are preserved."));
    apply_->setAutoDefault(false);
    restore_->setAutoDefault(false);
    actions->addWidget(apply_);
    actions->addWidget(restore_);
    actions->addStretch();
    layout->addLayout(actions);
    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    layout->addWidget(separator);
    auto *supportTitle = label(this, QStringLiteral("Preview support"));
    supportTitle->setFont(font);
    layout->addWidget(supportTitle);
    auto *support = new QGridLayout;
    support->setHorizontalSpacing(12);
    support->setVerticalSpacing(8);
    QStringList names;
    for (const auto &format : QImageReader::supportedImageFormats()) names.append(QString::fromLatin1(format).toUpper());
    names.removeDuplicates();
    std::sort(names.begin(), names.end());
    const auto addSupport = [this, support](int row, const QString &name, const QString &theme,
                                          const QString &object, const QString &details) {
        auto *image = new QLabel(this);
        image->setPixmap(QIcon::fromTheme(theme, style()->standardIcon(QStyle::SP_FileIcon)).pixmap(22, 22));
        support->addWidget(image, row, 0);
        support->addWidget(label(this, name), row, 1);
        support->addWidget(label(this, QStringLiteral("Included"), true), row, 2);
        support->addWidget(information(this, object, name, details), row, 3);
    };
    addSupport(0, QStringLiteral("Images"), QStringLiteral("image-x-generic"), QStringLiteral("imageInfo"),
        QStringLiteral("Available image readers: %1. Actual support depends on the file and installed Qt plugins. Images keep their aspect ratio; animations show a static frame.").arg(names.join(QStringLiteral(", "))));
    addSupport(1, QStringLiteral("PDF"), QStringLiteral("application-pdf"), QStringLiteral("pdfInfo"),
        QStringLiteral("Read-only PDF previews with scrolling. Password-protected, damaged, or oversized documents show file information instead."));
    addSupport(2, QStringLiteral("Plain text"), QStringLiteral("text-x-generic"), QStringLiteral("textInfo"),
        QStringLiteral("Read-only text and source files. Large files are truncated with a notice; binary files show file information instead."));
    support->setColumnStretch(1, 1);
    layout->addLayout(support);
    layout->addWidget(label(this, QStringLiteral("Other files show an icon and file information."), true));
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons_, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons_);
    connect(apply_, &QPushButton::clicked, this, [this] { changeShortcuts(false); });
    connect(restore_, &QPushButton::clicked, this, [this] { changeShortcuts(true); });
    setMinimumWidth(480);
    resize(520, sizeHint().height());
    refresh();
}
void SettingsDialog::showEvent(QShowEvent *event)
{
    if (!busy_) refresh();
    QDialog::showEvent(event);
}
void SettingsDialog::reject()
{
    if (!busy_) QDialog::reject();
}
void SettingsDialog::changeShortcuts(bool restore)
{
    if (busy_) return;
    cancelPendingFocus();
    focusCancelled_ = std::make_shared<std::atomic_bool>(false);
    busy_ = true;
    apply_->setEnabled(false);
    restore_->setEnabled(false);
    buttons_->setEnabled(false);
    status_->setText(QStringLiteral("Checking Dolphin…"));
    auto *watcher = new QFutureWatcher<DolphinSession::Snapshot>(this);
    connect(watcher, &QFutureWatcher<DolphinSession::Snapshot>::finished, this, [this, watcher, restore] {
        const auto snapshot = watcher->result();
        watcher->deleteLater();
        if (!snapshot.error.isEmpty()) { finish(snapshot.error); return; }
        if (snapshot.processes.isEmpty()) { applyChange(restore, false); return; }
        QMessageBox confirm(QMessageBox::Question, QStringLiteral("Restart Dolphin?"),
            QStringLiteral("Dolphin needs to restart to apply this shortcut change. All its windows will close normally before the configuration is updated.\n\n"
                           "Dolphin will reopen using its startup settings; some windows, tabs, or split views may not be restored."),
            QMessageBox::Cancel, this);
        confirm.setObjectName(QStringLiteral("restartConfirmation"));
        auto *restart = confirm.addButton(QStringLiteral("Restart Dolphin and apply"), QMessageBox::AcceptRole);
        confirm.setDefaultButton(QMessageBox::Cancel);
        confirm.exec();
        if (confirm.clickedButton() != restart) { finish(); return; }
        // Capture the user's input authorization before Dolphin takes focus.
        focusToken_ = settingsActivationToken(windowHandle());
        status_->setText(QStringLiteral("Waiting for Dolphin to close… Finish any close dialogs in Dolphin. No shortcuts have changed yet."));
        auto *closing = new QFutureWatcher<QString>(this);
        connect(closing, &QFutureWatcher<QString>::finished, this, [this, closing, restore] {
            const auto error = closing->result();
            closing->deleteLater();
            if (!error.isEmpty()) finish(error);
            else applyChange(restore, true);
        });
        const auto session = session_;
        closing->setFuture(QtConcurrent::run([session, snapshot] { return session.closeAndWait(snapshot); }));
    });
    const auto session = session_;
    watcher->setFuture(QtConcurrent::run([session] { return session.inspect(); }));
}
void SettingsDialog::applyChange(bool restore, bool reopen)
{
    QString error;
    // Recheck after exit: a newly started Dolphin must not race our file edit.
    if (session_.isRunning()) error = QStringLiteral("Dolphin started again. Shortcut files were not changed; retry when it is closed.");
    else if (restore) shortcuts_.restore(error);
    else shortcuts_.useSpace(error);
    bool reopened = false;
    if (reopen && !session_.isRunning()) {
        reopened = reopen_();
        if (!reopened)
            error += (error.isEmpty() ? QString() : QStringLiteral("\n\n"))
                + QStringLiteral("Dolphin could not be reopened. Open it from your application launcher.");
    }
    if (reopened && error.isEmpty()) restoreFocusAfterRestart();
    else cancelPendingFocus();
    finish(error);
}
SettingsDialog::~SettingsDialog() { cancelPendingFocus(); }
void SettingsDialog::cancelPendingFocus()
{
    if (focusCancelled_) *focusCancelled_ = true;
}
bool SettingsDialog::event(QEvent *event)
{
    if (event->type() == QEvent::Hide
        || (event->type() == QEvent::WindowStateChange && isMinimized()))
        cancelPendingFocus();
    return QDialog::event(event);
}
void SettingsDialog::restoreFocusAfterRestart()
{
    const auto cancelled = focusCancelled_;
    if (!cancelled || *cancelled) return;
    auto *watcher = new QFutureWatcher<bool>(this);
    const auto token = focusToken_;
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, cancelled, token] {
        const bool ready = watcher->result();
        watcher->deleteLater();
        if (!ready || cancelled->exchange(true) || !isVisible() || isMinimized()
            || (parentWidget() && parentWidget()->isMinimized())) return;
        // One request only. No parent activation or always-on-top flags.
        const QString authorization = token.isFinished() ? token.result() : QString();
        if (!authorization.isEmpty()) activatePreview(windowHandle(), authorization, false);
        else if (windowHandle()) windowHandle()->requestActivate();
        emit restartFocusRequested();
    });
    const auto session = session_;
    watcher->setFuture(QtConcurrent::run([session, cancelled] {
        return session.waitForActiveWindow([cancelled] { return cancelled->load(); });
    }));
}
void SettingsDialog::finish(const QString &error)
{
    busy_ = false;
    buttons_->setEnabled(true);
    refresh();
    if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Dolphin shortcut"), error);
}
void SettingsDialog::refresh()
{
    const bool installed = !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("kio/servicemenus/dzurkel.desktop")).isEmpty();
    const bool active = shortcuts_.usesSpace();
    const bool supported = shortcuts_.supportsAutomaticSetup();
    const bool backup = shortcuts_.hasBackup();
    status_->setText(!supported ? QStringLiteral("Custom shortcut scheme — use Dolphin’s shortcut editor.")
        : !installed ? QStringLiteral("Install the Dolphin integration to enable shortcut setup.")
        : active ? QStringLiteral("Space shortcut enabled")
        : QStringLiteral("Space shortcut not enabled"));
    apply_->setVisible(!backup);
    apply_->setEnabled(supported && installed && !active && !backup);
    restore_->setVisible(backup);
    restore_->setEnabled(backup);
}
} // namespace dzurkel

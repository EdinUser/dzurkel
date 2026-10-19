#include "SettingsDialog.h"
#include "application/RuntimeCompatibility.h"
#include "ipc/NautilusPreviewService.h"
#include "desktop/DesktopIntegration.h"
#include "resources/ResourceAccessPolicy.h"
#include "preview/PreviewLibrary.h"
#include "preview/LibraryAvailability.h"
#include <QPainter>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainterPath>
#include <QTextBrowser>
#include <QScreen>
#include "preview/text/TextHighlighting.h"
#include <QTabWidget>
#include <QCheckBox>
#include <QEvent>
#include <QElapsedTimer>
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
#include <QSignalBlocker>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QTime>
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
QIcon libraryStatusIcon(LibraryHealth health, qreal ratio)
{
    QPixmap pixmap(QSize(20, 20) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(health == LibraryHealth::Available ? QColor(40, 145, 75)
        : health == LibraryHealth::Limited ? QColor(190, 110, 0) : QColor(200, 55, 55));
    painter.drawEllipse(QRectF(2, 2, 16, 16));
    painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (health == LibraryHealth::Available) {
        QPainterPath check;
        check.moveTo(6, 10); check.lineTo(9, 13); check.lineTo(14, 7);
        painter.drawPath(check);
    } else if (health == LibraryHealth::Limited) {
        painter.drawLine(QPointF(10, 6), QPointF(10, 11));
        painter.drawPoint(QPointF(10, 14));
    } else {
        painter.drawLine(QPointF(7, 7), QPointF(13, 13));
        painter.drawLine(QPointF(13, 7), QPointF(7, 13));
    }
    return QIcon(pixmap);
}
void showLibraryDetails(QWidget *parent, const QString &title, const QString &details, const QString &command,
                        const QString &name = QStringLiteral("libraryDetails"))
{
    QDialog dialog(parent);
    dialog.setObjectName(name);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setObjectName(name + QStringLiteral("Text"));
    text->setOpenLinks(false);
    text->setOpenExternalLinks(false);
    text->setPlainText(details);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    if (!command.isEmpty()) {
        auto *copy = buttons->addButton(QStringLiteral("Copy command"), QDialogButtonBox::ActionRole);
        copy->setObjectName(QStringLiteral("copyLibraryCommand"));
        QObject::connect(copy, &QPushButton::clicked, &dialog, [copy, command] {
            QGuiApplication::clipboard()->setText(command);
            copy->setText(QStringLiteral("Copied"));
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(QSize(520, 390).boundedTo(parent->screen()->availableGeometry().size() * .9));
    dialog.exec();
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
SettingsDialog::SettingsDialog(QWidget *parent)
    : SettingsDialog(parent, DolphinShortcuts{}, DolphinSession{}, {}, detectFileManager()) {}
SettingsDialog::SettingsDialog(QWidget *parent, DolphinShortcuts shortcuts,
                               DolphinSession session, std::function<bool()> reopen,
                               FileManagerIntegration integration, NautilusPreviewService *nautilus, ThunarIntegration thunar)
    : QDialog(parent), shortcuts_(std::move(shortcuts)), integration_(integration), nautilus_(nautilus), thunar_(std::move(thunar)), session_(std::move(session)),
      reopen_(reopen ? std::move(reopen) : [] { return QProcess::startDetached(QStringLiteral("dolphin"), QStringList{}); })
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(QStringLiteral("Dzurkel Settings"));
    setWindowModality(Qt::WindowModal);
    auto *outer = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    outer->addWidget(tabs);
    auto *integrationPage = new QWidget(this);
    auto *previewPage = new QWidget(this);
    auto *libraryPage = new QWidget(this);
    tabs->addTab(integrationPage, QStringLiteral("Integration"));
    tabs->addTab(previewPage, QStringLiteral("Previews"));
    tabs->addTab(libraryPage, QStringLiteral("Libraries"));
    auto *layout = new QVBoxLayout(integrationPage);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(14);
    auto *heading = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("system-file-manager"), style()->standardIcon(QStyle::SP_DirIcon)).pixmap(32, 32));
    heading->addWidget(icon);
    const bool dolphin = integration_ == FileManagerIntegration::Dolphin;
    const bool nautilusSelected = integration_ == FileManagerIntegration::Nautilus;
    const bool thunarSelected = integration_ == FileManagerIntegration::Thunar;
    const QString headingText = dolphin ? QStringLiteral("Dolphin shortcut")
        : nautilusSelected ? QStringLiteral("Nautilus shortcut")
        : thunarSelected ? QStringLiteral("Thunar shortcut") : QStringLiteral("File manager shortcut");
    auto *title = label(this, headingText);
    title->setObjectName(QStringLiteral("integrationTitle"));
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(information(this, QStringLiteral("shortcutInfo"), headingText,
        dolphin ? QStringLiteral("Preview selected files with Space in Dolphin 26.04 or newer. If Space stops working while the context-menu action still works, close all Dolphin windows and reopen Dolphin. This replaces Space selection and conflicting Space shortcuts; alternate bindings are kept. Restore previous shortcuts uses the backup to undo only Dzurkel’s changes, preserving unrelated toolbar and menu edits.")
        : thunarSelected ? QStringLiteral("Preview one selected local file with Space or Preview with Dzurkel in Thunar’s context menu. Setup uses Thunar’s built-in custom actions and shortcuts, with no extra plugin or global shortcut. Conflicting Space bindings are backed up. Restore removes Dzurkel’s action and restores those bindings, preserving unrelated edits. Thunar must restart when applying or restoring setup.")
        : QStringLiteral("Preview selected local files with Space in Nautilus (Files). Dzurkel becomes the user’s Nautilus preview provider. Arrow navigation follows the selection in Files; Alt+arrows work while reading documents. Restore returns the previous user registration, allowing a system provider such as Sushi to be used again. No GNOME extension or global shortcut change is needed.")));
    layout->addLayout(heading);
    status_ = label(this, {});
    status_->setObjectName(QStringLiteral("shortcutStatus"));
    layout->addWidget(status_);
    shortcutChanges_ = label(this, {}, true);
    shortcutChanges_->setObjectName(QStringLiteral("shortcutChanges"));
    layout->addWidget(shortcutChanges_);
    auto *restart = label(this, dolphin ? QStringLiteral("If Dolphin is open, you’ll be asked to restart it.")
        : thunarSelected ? QStringLiteral("Finish file operations first. If Thunar is open, you’ll be asked to restart it.")
        : QStringLiteral("If another preview provider is running, close its preview and quit it before enabling Dzurkel."), true);
    restart->setVisible(integration_ != FileManagerIntegration::Unavailable);
    if (dolphin) restart->setToolTip(QStringLiteral("Dolphin closes before the shortcut file is updated, so it cannot overwrite the change while saving its own settings."));
    layout->addWidget(restart);
    auto *actions = new QHBoxLayout;
    apply_ = new QPushButton(QStringLiteral("Enable Space shortcut…"), this);
    apply_->setObjectName(QStringLiteral("useSpace"));
    restore_ = new QPushButton(dolphin || thunarSelected ? QStringLiteral("Restore previous shortcuts…") : QStringLiteral("Restore previous previews"), this);
    restore_->setObjectName(QStringLiteral("restoreShortcuts"));
    restore_->setToolTip(thunarSelected ? QStringLiteral("Remove Dzurkel’s Thunar action and restore its shortcut changes. Unrelated actions and shortcuts are preserved.")
        : dolphin ? QStringLiteral("Restore only the shortcut entries changed by Dzurkel. Unrelated toolbar and menu changes are preserved.")
        : QStringLiteral("Restore the previous user preview registration. System providers are preserved."));
    if (nautilusSelected) apply_->setText(QStringLiteral("Enable Space shortcut"));
    apply_->setAutoDefault(false);
    restore_->setAutoDefault(false);
    actions->addWidget(apply_);
    actions->addWidget(restore_);
    actions->addStretch();
    layout->addLayout(actions);
    auto *previews = new QVBoxLayout(previewPage);
    previews->addWidget(label(this, QStringLiteral("Supported previews")));
    auto *support = new QGridLayout;
    support->setHorizontalSpacing(12);
    support->setVerticalSpacing(8);
    struct Group {
        QString title, icon, object;
        QStringList readers, formats;
    };
    const QList<Group> groups = {
        {QStringLiteral("Audio"), QStringLiteral("audio-x-generic"), QStringLiteral("audioInfo"),
         {"audio"}, {".mp3, .flac, .ogg, .oga, .opus, .wav, .aac, .m4a, audio-only .mp4"}},
        {QStringLiteral("Video"), QStringLiteral("video-x-generic"), QStringLiteral("videoInfo"),
         {"video"}, {".mp4, .m4v, .mkv, .webm, .mov, .avi, .mpg, .mpeg, .ogv, .wmv"}},
        {QStringLiteral("Images"), QStringLiteral("image-x-generic"), QStringLiteral("imageInfo"),
         {"image"}, {}},
        {QStringLiteral("Documents"), QStringLiteral("x-office-document"), QStringLiteral("documentInfo"),
         {"pdf", "docx", "rtf", "doc"}, {".pdf", ".docx", ".rtf", ".doc (requires antiword)"}},
        {QStringLiteral("Books"), QStringLiteral("application-epub+zip"), QStringLiteral("bookInfo"),
         {"epub", "fb2", "sfb", "mobi"}, {".epub", ".fb2", ".sfb", ".mobi"}},
        {QStringLiteral("Tables"), QStringLiteral("x-office-spreadsheet"), QStringLiteral("tableInfo"),
         {"csv", "xlsx"}, {".csv / .tsv", ".xlsx"}},
        {QStringLiteral("Text and source"), QStringLiteral("text-x-generic"), QStringLiteral("textInfo"),
         {"markdown", "text"}, {".md / .markdown", "Plain text and text-based source/configuration files (for example .txt, .json, .xml, .sql, .php); detected by content/MIME type"}}
    };
    int row = 0;
    for (const auto &group : groups) {
        auto *image = new QLabel(this);
        image->setPixmap(QIcon::fromTheme(group.icon, style()->standardIcon(QStyle::SP_FileIcon)).pixmap(22, 22));
        support->addWidget(image, row, 0);
        support->addWidget(label(this, group.title), row, 1);
        QString details;
        if (group.readers == QStringList{QStringLiteral("image")}) {
            QStringList formats;
            for (const auto &format : QImageReader::supportedImageFormats())
                formats.append(QStringLiteral(".") + QString::fromLatin1(format).toLower());
            formats.removeDuplicates();
            formats.sort();
            details = QStringLiteral("Supported formats: %1\n\n").arg(formats.join(QStringLiteral(", ")));
        } else {
            details = QStringLiteral("Supported files: %1\n\n").arg(group.formats.join(QStringLiteral(", ")));
        }
        for (const auto &id : group.readers) {
            if (const auto reader = PreviewLibrary::capability(id))
                details += reader->title + QStringLiteral(": ") + reader->description + QStringLiteral("\n\n");
        }
        support->addWidget(information(this, group.object, group.title, details.trimmed()), row++, 2);
    }
    support->setColumnStretch(1, 1);
    previews->addLayout(support);
    previews->addWidget(label(this, QStringLiteral("Format support depends on the file and available readers. See Libraries for optional features. Other files show file information."), true));
    previews->addStretch();

    auto *libraries = new QVBoxLayout(libraryPage);
    auto *libraryTitle = label(this, QStringLiteral("Preview libraries"));
    libraryTitle->setFont(font);
    libraries->addWidget(libraryTitle);
    auto *libraryGrid = new QGridLayout;
    libraryGrid->setHorizontalSpacing(10);
    libraryGrid->setVerticalSpacing(12);
    const auto initialLibraries = libraryAvailability();
    int libraryIndex = 0;
    for (const auto &library : initialLibraries) {
        if (library.id == QStringLiteral("highlighting")) {
            highlighting_ = new QCheckBox(this);
            highlighting_->setObjectName(QStringLiteral("syntaxHighlighting"));
            highlighting_->setAccessibleName(QStringLiteral("Enable syntax highlighting"));
            highlighting_->setToolTip(QStringLiteral("Use syntax highlighting for subsequently opened text previews"));
            libraryGrid->addWidget(highlighting_, libraryIndex, 0);
        }
        auto *description = new QVBoxLayout;
        description->setSpacing(1);
        description->addWidget(label(this, library.name));
        description->addWidget(label(this, library.purpose, true));
        libraryGrid->addLayout(description, libraryIndex, 1);
        auto *status = new QToolButton(this);
        status->setObjectName(library.id + QStringLiteral("LibraryStatus"));
        status->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        status->setAutoRaise(true);
        status->setFocusPolicy(Qt::StrongFocus);
        libraryGrid->addWidget(status, libraryIndex, 2, Qt::AlignLeft);
        auto *info = new QToolButton(this);
        info->setObjectName(library.id + QStringLiteral("LibraryInfo"));
        info->setIcon(QIcon::fromTheme(QStringLiteral("help-about"), style()->standardIcon(QStyle::SP_MessageBoxInformation)));
        info->setAutoRaise(true);
        info->setFocusPolicy(Qt::StrongFocus);
        info->setAccessibleName(library.name + QStringLiteral(" information"));
        libraryGrid->addWidget(info, libraryIndex++, 3);
        const auto showDetails = [this, status, name = library.name] {
            showLibraryDetails(this, name, status->property("details").toString(), status->property("installationCommand").toString());
        };
        connect(status, &QToolButton::clicked, this, showDetails);
        connect(info, &QToolButton::clicked, this, showDetails);
        libraryRows_.append({library.id, status, info});
    }
    libraryGrid->setColumnStretch(1, 1);
    libraries->addLayout(libraryGrid);
    libraries->addWidget(label(this, QStringLiteral("Green: available · Orange: limited or cannot load · Red: missing"), true));
    auto *recheck = new QPushButton(QStringLiteral("Refresh status"), this);
    recheck->setObjectName(QStringLiteral("checkLibraries"));
    recheck->setAutoDefault(false);
    libraries->addWidget(recheck, 0, Qt::AlignLeft);
    libraryCheckResult_ = label(this, QStringLiteral("Availability checked when Settings opens."), true);
    libraryCheckResult_->setObjectName(QStringLiteral("libraryCheckResult"));
    libraryCheckResult_->setAccessibleName(QStringLiteral("Library check result"));
    libraries->addWidget(libraryCheckResult_);
    libraries->addStretch();
    connect(recheck, &QPushButton::clicked, this, [this] { refreshLibraries(true); });
    connect(highlighting_, &QCheckBox::toggled, this, [this](bool enabled) {
        QString error;
        if (!setHighlightingEnabled(enabled, error)) {
            refreshLibraries();
            QMessageBox::warning(this, QStringLiteral("Syntax highlighting"), error);
        }
    });
    auto *locationsTitle = label(this, QStringLiteral("File locations"));
    locationsTitle->setFont(font);
    layout->addWidget(locationsTitle);
    layout->addWidget(label(this, QStringLiteral("Local paths, mounted shares and SMB URLs are enabled.")));
    auto *experimental = new QHBoxLayout;
    experimentalSftp_ = new QCheckBox(QStringLiteral("Experimental SFTP previews"), this);
    experimentalSftp_->setObjectName(QStringLiteral("experimentalSftp"));
    experimentalSftp_->setToolTip(QStringLiteral("Allow sftp:// URLs for new preview requests. Disabled by default; compatibility testing is incomplete."));
    experimental->addWidget(experimentalSftp_);
    experimental->addStretch();
    experimental->addWidget(information(this, QStringLiteral("sftpInfo"), QStringLiteral("Experimental SFTP"),
        QStringLiteral("SFTP is optional and experimental. It reuses the same image, PDF and text readers and preview limits. "
            "Open the location in Dolphin first to verify its host key and authenticate. KIO-FUSE and the SFTP worker are required. "
            "Passwords in URLs are not accepted. This setting applies to new CLI and Dolphin requests without restarting Dzurkel; "
            "an already accepted preview may finish after you turn it off. Other direct protocols are not enabled.")));
    layout->addLayout(experimental);
    connect(experimentalSftp_, &QCheckBox::toggled, this, [this](bool enabled) {
        QString error;
        if (!ResourceAccessPolicy::setExperimentalSftpEnabled(enabled, error)) {
            const QSignalBlocker blocker(experimentalSftp_);
            experimentalSftp_->setChecked(ResourceAccessPolicy::experimentalSftpEnabled());
            QMessageBox::warning(this, QStringLiteral("SFTP preview setting"), error);
        }
    });
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons_, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addStretch();
    const auto compatibility = RuntimeCompatibility::load();
    auto *requirementsHeading = new QHBoxLayout;
    auto *requirementsTitle = label(this, QStringLiteral("System requirements"));
    requirementsTitle->setFont(font);
    requirementsHeading->addWidget(requirementsTitle);
    requirementsHeading->addStretch();
    auto *compatibilityButton = new QPushButton(QStringLiteral("OS compatibility…"), this);
    compatibilityButton->setObjectName(QStringLiteral("systemRequirementsDetails"));
    compatibilityButton->setAutoDefault(false);
    requirementsHeading->addWidget(compatibilityButton);
    layout->addLayout(requirementsHeading);
    auto *requirementsSummary = label(this, compatibility.summary, true);
    requirementsSummary->setObjectName(QStringLiteral("systemRequirementsSummary"));
    requirementsSummary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(requirementsSummary);
    connect(compatibilityButton, &QPushButton::clicked, this, [this, compatibility] {
        showLibraryDetails(this, QStringLiteral("System requirements and OS compatibility"), compatibility.details, {},
            QStringLiteral("compatibilityDetails"));
    });
    outer->addWidget(buttons_);
    connect(apply_, &QPushButton::clicked, this, [this] { changeShortcuts(false); });
    connect(restore_, &QPushButton::clicked, this, [this] { changeShortcuts(true); });
    setMinimumWidth(480);
    resize(520, sizeHint().height());
    refresh();
}
void SettingsDialog::showLibrary(const QString &id)
{
    findChild<QTabWidget *>(QStringLiteral("settingsTabs"))->setCurrentIndex(2);
    refreshLibraries();
    for (const auto &row : libraryRows_) if (row.id == id) row.status->setFocus();
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
    if (integration_ == FileManagerIntegration::Thunar) { changeThunar(restore); return; }
    if (integration_ != FileManagerIntegration::Dolphin) {
        QString error;
        if (integration_ == FileManagerIntegration::Nautilus && nautilus_) nautilus_->configure(!restore, error);
        else error = QStringLiteral("Nautilus preview setup is unavailable in this instance.");
        finish(error);
        return;
    }
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
void SettingsDialog::changeThunar(bool restore)
{
    cancelPendingFocus();
    const bool restart = thunar_.running();
    if (restart) {
        QMessageBox confirmation(QMessageBox::Question, QStringLiteral("Restart Thunar?"),
            QStringLiteral("Finish all file operations before continuing. Thunar will close to apply this change, then reopen. Its current windows and tabs may not be restored."),
            QMessageBox::Cancel, this);
        confirmation.setObjectName(QStringLiteral("thunarRestartConfirmation"));
        auto *accept = confirmation.addButton(QStringLiteral("Restart Thunar and apply"), QMessageBox::AcceptRole);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.exec();
        if (confirmation.clickedButton() != accept) return;
    }
    focusCancelled_ = std::make_shared<std::atomic_bool>(false);
    busy_ = true;
    apply_->setEnabled(false);
    restore_->setEnabled(false);
    buttons_->setEnabled(false);
    status_->setText(QStringLiteral("Updating Thunar preview setup…"));
    auto setup = thunar_;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, restart] {
        const auto error = watcher->result();
        watcher->deleteLater();
        finish(error);
        if (restart && error.isEmpty()) restoreThunarFocusAfterRestart();
        else cancelPendingFocus();
    });
    watcher->setFuture(QtConcurrent::run([setup, restore, restart]() mutable {
        QString error;
        if (restart) {
            QProcess quit;
            quit.start(QStringLiteral("thunar"), {QStringLiteral("--quit")});
            if (!quit.waitForFinished(5000) || quit.exitStatus() != QProcess::NormalExit || quit.exitCode() != 0)
                return QStringLiteral("Thunar could not close normally. No setup changes were made.");
            for (int i = 0; i < 50 && setup.running(); ++i) QThread::msleep(100);
        }
        if (restore) setup.restore(error); else setup.enable(error);
        if (restart && !setup.running() && !QProcess::startDetached(QStringLiteral("thunar"), QStringList{}))
            error += (error.isEmpty() ? QString() : QStringLiteral("\n\n")) + QStringLiteral("Thunar could not reopen. Open it from your application menu.");
        return error;
    }));
}
void SettingsDialog::restoreThunarFocusAfterRestart()
{
    const auto cancelled = focusCancelled_;
    if (!cancelled || *cancelled) return;
    auto *timer = new QTimer(this);
    timer->setInterval(50);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto setup = thunar_;
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, cancelled, setup] {
        if (*cancelled || elapsed.elapsed() >= 10000 || !isVisible() || isMinimized()
            || (parentWidget() && parentWidget()->isMinimized())) {
            *cancelled = true;
            timer->stop();
            timer->deleteLater();
            return;
        }
        // Process startup is too early: wait until the reopened file manager
        // has actually taken focus, then return it once to this Settings dialog.
        if (!setup.activeWindow()) return;
        timer->stop();
        timer->deleteLater();
        if (cancelled->exchange(true)) return;
        raise();
        activatePreview(windowHandle(), {}, false);
        emit restartFocusRequested();
    });
    timer->start();
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
    if (!error.isEmpty()) QMessageBox::warning(this, integration_ == FileManagerIntegration::Dolphin
        ? QStringLiteral("Dolphin shortcut") : integration_ == FileManagerIntegration::Thunar
        ? QStringLiteral("Thunar shortcut") : QStringLiteral("Nautilus shortcut"), error);
}
void SettingsDialog::refreshLibraries(bool recheck)
{
    const auto libraries = libraryAvailability(recheck);
    QString signature;
    for (const auto &library : libraries) {
        signature += library.id + library.status + library.details;
        for (const auto &row : libraryRows_) {
            if (row.id != library.id) continue;
            row.status->setText(library.status);
            row.status->setIcon(libraryStatusIcon(library.health, devicePixelRatioF()));
            row.status->setProperty("health", static_cast<int>(library.health));
            row.status->setProperty("installationCommand", library.installationCommand);
            row.status->setProperty("details", library.status + QStringLiteral("\n\n") + library.details);
            row.status->setAccessibleName(library.name + QStringLiteral(": ") + library.status);
            row.status->setAccessibleDescription(library.details);
            row.status->setToolTip(QStringLiteral("%1. Click for details and guidance.").arg(library.status));
            row.information->setToolTip(QStringLiteral("%1: details and guidance").arg(library.name));
            row.information->setAccessibleDescription(library.details);
            if (library.id == QStringLiteral("highlighting")) {
                const QSignalBlocker blocker(highlighting_);
                highlighting_->setChecked(highlightingEnabled());
                highlighting_->setEnabled(library.health == LibraryHealth::Available);
            }
        }
    }
    if (recheck) {
        libraryCheckResult_->setText(QStringLiteral("Checked at %1. %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                signature != librarySignature_ ? QStringLiteral("Availability updated.") : QStringLiteral("No changes found.")));
    }
    librarySignature_ = signature;
}

void SettingsDialog::refresh()
{
    refreshLibraries();
    {
        const QSignalBlocker blocker(experimentalSftp_);
        experimentalSftp_->setChecked(ResourceAccessPolicy::experimentalSftpEnabled());
    }
    if (integration_ == FileManagerIntegration::Thunar) {
        const bool active = thunar_.enabled(), backup = thunar_.hasBackup();
        status_->setText(active ? QStringLiteral("Space and context-menu previews enabled in Thunar")
            : backup ? QStringLiteral("Thunar preview settings changed after setup. Review them before restoring.")
            : QStringLiteral("Space and context-menu previews not enabled in Thunar"));
        shortcutChanges_->setText(QStringLiteral("Use Space or Preview with Dzurkel on one selected local file. Conflicting Space shortcuts will be backed up for restore."));
        shortcutChanges_->setVisible(!backup);
        apply_->setVisible(!backup);
        apply_->setEnabled(!busy_ && !backup);
        restore_->setVisible(backup);
        restore_->setEnabled(!busy_ && backup);
        return;
    }
    if (integration_ != FileManagerIntegration::Dolphin) {
        const auto registration = nautilus_ ? nautilus_->integration() : NautilusIntegration{};
        const bool active = registration.enabled();
        const bool backup = registration.hasBackup();
        const bool available = integration_ == FileManagerIntegration::Nautilus && nautilus_;
        status_->setText(integration_ == FileManagerIntegration::Unavailable
            ? QStringLiteral("No supported file manager was detected for this desktop.")
            : active ? (nautilus_ && nautilus_->running() ? QStringLiteral("Space previews enabled in Nautilus (Files)")
                : QStringLiteral("Registration enabled; retry Enable to start the preview service."))
            : backup ? QStringLiteral("The preview registration changed after setup. Review it before restoring.")
            : QStringLiteral("Space previews not enabled in Nautilus (Files)"));
        shortcutChanges_->setText(QStringLiteral("Use Space on a selected local file in Files. Dzurkel will start automatically when needed."));
        shortcutChanges_->setVisible(integration_ == FileManagerIntegration::Nautilus);
        apply_->setVisible(!backup || active);
        apply_->setEnabled(available && !busy_ && (!backup || active) && !(nautilus_ && nautilus_->running()));
        restore_->setVisible(backup);
        restore_->setEnabled(available && backup && !busy_);
        return;
    }
    const bool installed = !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("kio/servicemenus/dzurkel.desktop")).isEmpty();
    const bool active = shortcuts_.usesSpace();
    const bool supported = shortcuts_.supportsAutomaticSetup();
    const bool backup = shortcuts_.hasBackup();
    QString summaryError;
    shortcutChanges_->setText(shortcuts_.setupSummary(summaryError));
    if (!summaryError.isEmpty()) shortcutChanges_->setText(summaryError);
    shortcutChanges_->setVisible(!backup && !active);
    status_->setText(!supported ? QStringLiteral("Custom shortcut scheme — use Dolphin’s shortcut editor.")
        : !installed ? QStringLiteral("Install the Dolphin integration to enable shortcut setup.")
        : active ? QStringLiteral("Space shortcut configured")
        : QStringLiteral("Space shortcut not enabled"));
    apply_->setVisible(!backup);
    apply_->setEnabled(supported && installed && !active && !backup && summaryError.isEmpty());
    restore_->setVisible(backup);
    restore_->setEnabled(backup);
}
} // namespace dzurkel

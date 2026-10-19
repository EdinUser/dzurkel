#pragma once
#include <QDialog>
#include <QFuture>
#include <atomic>
#include <memory>
#include "desktop/DolphinShortcuts.h"
#include "desktop/DolphinSession.h"
#include "desktop/NautilusIntegration.h"
#include "desktop/ThunarIntegration.h"

class QLabel;
class QPushButton;
class QDialogButtonBox;
class QCheckBox;
class QToolButton;
namespace dzurkel {
class NautilusPreviewService;
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    SettingsDialog(QWidget *parent, DolphinShortcuts shortcuts,
                   DolphinSession session = DolphinSession(), std::function<bool()> reopen = {},
                   FileManagerIntegration integration = FileManagerIntegration::Dolphin,
                   NautilusPreviewService *nautilus = nullptr, ThunarIntegration thunar = ThunarIntegration());
    ~SettingsDialog() override;
    void showLibrary(const QString &id);
signals:
    void restartFocusRequested();
protected:
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void reject() override;
private:
    void refresh();
    void refreshLibraries(bool recheck = false);
    void changeShortcuts(bool restore);
    void changeThunar(bool restore);
    void applyChange(bool restore, bool reopen);
    void finish(const QString &error = {});
    void restoreFocusAfterRestart();
    void restoreThunarFocusAfterRestart();
    void cancelPendingFocus();
    DolphinShortcuts shortcuts_;
    FileManagerIntegration integration_;
    NautilusPreviewService *nautilus_;
    ThunarIntegration thunar_;
    DolphinSession session_;
    std::function<bool()> reopen_;
    bool busy_ = false;
    std::shared_ptr<std::atomic_bool> focusCancelled_;
    QFuture<QString> focusToken_;
    QDialogButtonBox *buttons_;
    QLabel *status_;
    QLabel *shortcutChanges_;
    QPushButton *apply_;
    QPushButton *restore_;
    QCheckBox *experimentalSftp_;
    QCheckBox *highlighting_;
    struct LibraryRow {
        QString id;
        QToolButton *status;
        QToolButton *information;
    };
    QList<LibraryRow> libraryRows_;
    QString librarySignature_;
    QLabel *libraryCheckResult_;
};
} // namespace dzurkel

#pragma once
#include <QDialog>
#include <QFuture>
#include <atomic>
#include <memory>
#include "desktop/DolphinShortcuts.h"
#include "desktop/DolphinSession.h"

class QLabel;
class QPushButton;
class QDialogButtonBox;
namespace dzurkel {
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr, DolphinShortcuts shortcuts = {},
                            DolphinSession session = DolphinSession(), std::function<bool()> reopen = {});
    ~SettingsDialog() override;
signals:
    void restartFocusRequested();
protected:
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void reject() override;
private:
    void refresh();
    void changeShortcuts(bool restore);
    void applyChange(bool restore, bool reopen);
    void finish(const QString &error = {});
    void restoreFocusAfterRestart();
    void cancelPendingFocus();
    DolphinShortcuts shortcuts_;
    DolphinSession session_;
    std::function<bool()> reopen_;
    bool busy_ = false;
    std::shared_ptr<std::atomic_bool> focusCancelled_;
    QFuture<QString> focusToken_;
    QDialogButtonBox *buttons_;
    QLabel *status_;
    QPushButton *apply_;
    QPushButton *restore_;
};
} // namespace dzurkel

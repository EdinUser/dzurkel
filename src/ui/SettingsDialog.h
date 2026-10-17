#pragma once
#include <QDialog>
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
protected:
    void showEvent(QShowEvent *event) override;
    void reject() override;
private:
    void refresh();
    void changeShortcuts(bool restore);
    void applyChange(bool restore, bool reopen);
    void finish(const QString &error = {});
    DolphinShortcuts shortcuts_;
    DolphinSession session_;
    std::function<bool()> reopen_;
    bool busy_ = false;
    QDialogButtonBox *buttons_;
    QLabel *status_;
    QPushButton *apply_;
    QPushButton *restore_;
};
} // namespace dzurkel

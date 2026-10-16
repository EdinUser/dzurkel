#pragma once
#include <QDialog>
#include "desktop/DolphinShortcuts.h"

class QLabel;
class QPushButton;
namespace dzurkel {
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
protected:
    void showEvent(QShowEvent *event) override;
private:
    void refresh();
    DolphinShortcuts shortcuts_;
    QLabel *status_;
    QPushButton *apply_;
    QPushButton *restore_;
};
} // namespace dzurkel

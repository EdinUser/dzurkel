#pragma once
#include <QWidget>
#include <QPointer>

class QLabel;
class QVBoxLayout;
class QScreen;
namespace dzurkel {
struct FileResource;
class SettingsDialog;
class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow(bool diagnostics = false);
    explicit PreviewWindow(const FileResource &resource, bool diagnostics = false);
    void setResource(const FileResource &resource);
    void present(const QString &activationToken = {});
    // Takes ownership; replacing content destroys its widgets/documents.
    void setContent(QWidget *content);
    void setStatus(const QString &status);
    QSize previewSize() const;
signals:
    void dismissed();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void watchScreen(QScreen *screen);
    void fitScreen(QScreen *screen);
    bool diagnostics_;
    bool shown_ = false;
    bool trackingScreen_ = false;
    QMetaObject::Connection screenGeometryConnection_;
    QWidget *contentHost_;
    QWidget *content_ = nullptr;
    QVBoxLayout *contentLayout_;
    QLabel *status_;
    QPointer<SettingsDialog> settings_;
};
} // namespace dzurkel

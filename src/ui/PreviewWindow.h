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
    // Resolve the initial screen/normal size before an asynchronous first load.
    void preparePresentation();
    bool hasBeenShown() const { return shown_; }
    void present(const QString &activationToken = {});
    // Takes ownership; replacing content destroys its widgets/documents.
    void setContent(QWidget *content);
    void setStatus(const QString &status);
    void setInformationMode(bool information);
    QSize previewSize() const;
signals:
    void dismissed();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void fitInformation();
    void watchScreen(QScreen *screen);
    void fitScreen(QScreen *screen);
    bool diagnostics_;
    bool shown_ = false;
    bool presentationPrepared_ = false;
    bool trackingScreen_ = false;
    bool informationMode_ = false;
    bool informationRestorePending_ = false;
    bool mappingResizePending_ = false;
    QSize mappedWindowSize_;
    QSize requestedWindowSize_;
    QSize previewWindowSize_;
    QSize previewContentSize_;
    Qt::WindowStates previewWindowState_;
    QMetaObject::Connection screenGeometryConnection_;
    QWidget *contentHost_;
    QWidget *content_ = nullptr;
    QVBoxLayout *contentLayout_;
    QLabel *status_;
    QPointer<SettingsDialog> settings_;
};
} // namespace dzurkel

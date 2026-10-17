#pragma once
#include <QWidget>
#include <QPointer>

class QLabel;
class QVBoxLayout;
class QScreen;
class QPushButton;
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
    void setNavigation(bool previous, bool next, const QString &status);
    void setArrowNavigation(bool enabled) { arrowNavigation_ = enabled; }
signals:
    void dismissed();
    void navigateRequested(int direction);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void fitInformation();
    void updateZoomControls();
    void watchScreen(QScreen *screen);
    void fitScreen(QScreen *screen);
    bool arrowNavigation_ = false;
    int wheelRemainder_ = 0;
    bool diagnostics_;
    QPushButton *previous_;
    QPushButton *next_;
    QLabel *navigationStatus_;
    QWidget *zoomControls_;
    QLabel *zoomStatus_;
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

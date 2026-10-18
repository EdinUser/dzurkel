#pragma once
#include <QWidget>
#include <QPointer>

class QLabel;
class QVBoxLayout;
class QScreen;
class QPushButton;
class QDialog;
namespace dzurkel {
struct FileResource;
struct PreviewData;
class SettingsDialog;
class NautilusPreviewService;
class WindowPlacement;
class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow(bool diagnostics = false);
    explicit PreviewWindow(const FileResource &resource, bool diagnostics = false);
    void setResource(const FileResource &resource);
    void showSettings(const QString &activationToken = {});
    // Resolve the initial screen/normal size before an asynchronous first load.
    void preparePresentation();
    bool hasBeenShown() const { return shown_; }
    void present(const QString &activationToken = {});
    // Takes ownership; replacing content destroys its widgets/documents.
    void setContent(QWidget *content);
    void setStatus(const QString &status);
    void setPreviewData(const PreviewData &data);
    bool usesGnomePresentation() const;
    bool canCommitPreview() const;
    // Synchronous provider commit. GNOME remaps only if sizing/state changes.
    void beginPreviewUpdate();
    void endPreviewUpdate();
    void prepareImage(const QSize &imageSize);
    void setInformationMode(bool information);
    QSize previewSize() const;
    void setNavigation(bool previous, bool next, const QString &status);
    void setArrowNavigation(bool enabled) { arrowNavigation_ = enabled; }
    void setExternalNavigation(bool enabled) { externalNavigation_ = enabled; }
    void setNautilusService(NautilusPreviewService *service) { nautilusService_ = service; }
signals:
    void dismissed();
    void navigateRequested(int direction);
    void externalNavigationRequested(uint direction);
    void presentationActivated();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void hideForPreviewUpdate();
    void setPreviewState(Qt::WindowStates state);
    void setPreviewMinimum(const QSize &size);
    void fitInformation();
    void enterImageMode();
    void fitImage(const QSize &natural = {});
    void resizePreview(const QSize &size, const QSize &imageSize = {}, const QSize &chrome = {});
    void restoreDocumentSize();
    void showFileInformation();
    void updateZoomControls();
    void watchScreen(QScreen *screen);
    void fitScreen(QScreen *screen);
    bool arrowNavigation_ = false;
    bool externalNavigation_ = false;
    NautilusPreviewService *nautilusService_ = nullptr;
    bool previewUpdate_ = false;
    bool internalRemap_ = false;
    QSize remapMaximum_;
    int wheelRemainder_ = 0;
    bool diagnostics_;
    QPushButton *previous_;
    QPushButton *next_;
    QLabel *navigationStatus_;
    QWidget *zoomControls_;
    QLabel *zoomStatus_;
    QPushButton *fileInfoButton_;
    QLabel *summary_;
    QString location_;
    QString fileInformation_;
    QPointer<QDialog> informationDialog_;
    bool shown_ = false;
    bool presentationPrepared_ = false;
    bool trackingScreen_ = false;
    bool informationMode_ = false;
    bool imageMode_ = false;
    WindowPlacement *placement_;
    bool informationRestorePending_ = false;
    bool imageRestorePending_ = false;
    bool mappingResizePending_ = false;
    QSize mappedWindowSize_;
    QSize requestedWindowSize_;
    quint64 resizeRevision_ = 0;
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

#pragma once
#include "ui/ZoomPreview.h"
#include "preview/PreviewProvider.h"
#include <QElapsedTimer>
#include <optional>

class QTimer;
class QScrollArea;
class QSpinBox;
class QLabel;
class QPushButton;
namespace dzurkel {
class PdfCanvas;
class PdfView final : public ZoomPreview {
    Q_OBJECT
public:
    PdfView(const PreviewData &data, QWidget *parent = nullptr);
    ~PdfView() override;
    void stop();
    void goToPage(int page, bool bottom = false);
    int currentPage() const { return page_; }
    bool loading() const;
    PdfSession *session() const { return session_.get(); }
    qreal zoomFactor() const override;
    qreal maximumZoom() const override;
    bool isFit() const override { return fit_; }
    void fit() override;
    void zoomBy(int steps) override;
    void scrollVertically(int direction) override;
    QSize naturalSize() const override;
    QWidget *toolbarWidget() const override;
    QString zoomHint() const override;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void request();
    void layoutPage();
    void controls();
    void notice(const QString &message);
    qreal resolution() const;
    std::shared_ptr<PdfSession> session_;
    QScrollArea *scroll_;
    PdfCanvas *canvas_;
    QWidget *toolbar_;
    QSpinBox *number_;
    QPushButton *previous_;
    QPushButton *next_;
    QLabel *status_;
    QTimer *resizeTimer_;
    QSizeF points_;
    QSizeF firstPoints_;
    int page_ = 0;
    int requestedPage_ = 0;
    int pages_ = 0;
    bool fit_ = true;
    bool loading_ = false;
    bool stopped_ = false;
    bool enterBottom_ = false;
    qreal zoom_ = 1;
    int wheelAmount_ = 0;
    int wheelDirection_ = 0;
    bool gestureLatched_ = false;
    QElapsedTimer lastWheel_;
    std::optional<QPointF> linkLocation_;
};
}

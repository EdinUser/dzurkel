#pragma once
#include "FormattedDocument.h"
#include "ui/ZoomPreview.h"
#include <memory>

class QTextBrowser;
namespace dzurkel {
class DocumentView : public ZoomPreview {
    Q_OBJECT
public:
    DocumentView(std::shared_ptr<const FormattedDocument> document, QWidget *parent = nullptr);
    void zoomBy(int steps) override;
    void fit() override;
    qreal zoomFactor() const override { return factor_; }
    bool isFit() const override { return factor_ == 1.0; }
    qreal maximumZoom() const override { return 3.0; }
    qreal minimumZoom() const override { return .5; }
    QString zoomHint() const override;
protected:
    void changeEvent(QEvent *event) override;
private:
    void applyZoom(qreal factor);
    void applyTypography();
    void applyColors(bool rememberSource = false);
    QTextBrowser *view_;
    qreal factor_ = 1.0;
    QFont baseFont_;
};
}

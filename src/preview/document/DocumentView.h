#pragma once
#include "FormattedDocument.h"
#include "ui/ZoomPreview.h"
#include <memory>
#include <QPointer>
#include "preview/lazy/LazyWindow.h"

class QTextBrowser;
class QToolButton;
class QTextFrame;
class QLabel;
class QTextCursor;
namespace dzurkel {
struct PreviewData;
class DocumentView : public ZoomPreview {
    Q_OBJECT
public:
    DocumentView(std::shared_ptr<const FormattedDocument> document, QWidget *parent = nullptr);
    DocumentView(const PreviewData &data, QWidget *parent = nullptr);
    void zoomBy(int steps) override;
    void fit() override;
    qreal zoomFactor() const override { return factor_; }
    bool isFit() const override { return factor_ == 1.0; }
    qreal maximumZoom() const override { return 3.0; }
    qreal minimumZoom() const override { return .5; }
    QString zoomHint() const override;
    QWidget *toolbarWidget() const override;
protected:
    void changeEvent(QEvent *event) override;
private:
    void applyZoom(qreal factor);
    void acceptPortion(const LazyPortion &portion, const LazyPortion &evicted, int direction);
    void jumpTo(const QString &anchor);
    bool hasAnchor(const QString &anchor) const;
    void updateContents();
    void addPortion(std::shared_ptr<const FormattedDocument> content, int direction);
    void removePortion(int index);
    void compactFormats(QTextCursor &anchor);
    struct PortionFrame { int slot; QPointer<QTextFrame> frame; std::shared_ptr<const FormattedDocument> content; };
    QList<PortionFrame> portions_;
    QList<DocumentSection> navigation_;
    std::shared_ptr<LazyCoordinator> coordinator_;
    LazyWindow *window_ = nullptr;
    LazyBoundaryWatcher *watcher_ = nullptr;
    QLabel *status_ = nullptr;
    QString jump_;
    bool lazyFailed_ = false;
    quint64 layoutRevision_ = 0;

    void applyTypography(QTextFrame *portion = nullptr);
    void applyColors(bool rememberSource = false, QTextFrame *portion = nullptr);
    QTextBrowser *view_;
    QToolButton *contents_ = nullptr;
    qreal factor_ = 1.0;
    QFont baseFont_;
};
}

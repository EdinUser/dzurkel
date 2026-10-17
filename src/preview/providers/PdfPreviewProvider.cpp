#include "PdfPreviewProvider.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QPdfDocument>
#include <QPdfPageRenderer>
#include <QPdfView>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include "ui/ZoomPreview.h"
#include <QScrollBar>
#include <QGuiApplication>
#include <QScreen>
#include <QEvent>
#include <QPdfPageNavigator>
#include <algorithm>
#include <cmath>

namespace dzurkel {
namespace {
class PdfWidget : public ZoomPreview {
public:
    PdfWidget(const PreviewData &data, QWidget *parent)
        : ZoomPreview(parent), document_(data.pdf), pageSizes_(data.pdfPageSizes), view_(new QPdfView(this))
    {
        setObjectName(QStringLiteral("pdfPreview"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        view_->setPageMode(QPdfView::PageMode::MultiPage);
        view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        view_->setDocument(document_.get());
        layout->addWidget(view_);
        setFocusProxy(view_);
        connect(view_, &QPdfView::zoomFactorChanged, this, [this] { emit zoomChanged(); });
        connect(view_, &QPdfView::zoomModeChanged, this, [this] { emit zoomChanged(); });
        connect(view_->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this] { emit zoomChanged(); });
        view_->viewport()->installEventFilter(this);
    }
    qreal zoomFactor() const override { return pageScale(view_->pageNavigator()->currentPage()); }
    bool isFit() const override { return view_->zoomMode() != QPdfView::ZoomMode::Custom; }
    qreal maximumZoom() const override
    {
        // QPdfView renders whole pages. Bound each manual-zoom page to 16M
        // physical pixels and an 8192-pixel edge, including display scaling.
        const auto physical = resolution() * devicePixelRatioF();
        qreal limit = 8.0;
        for (const auto &size : pageSizes_) {
            limit = std::min({limit, std::sqrt(16.0 * 1024 * 1024 / (size.width() * size.height())) / physical,
                              8192.0 / (std::max(size.width(), size.height()) * physical)});
        }
        return std::max(0.000001, limit >= .1 ? std::floor(limit * 10 + 1e-8) / 10 : limit);
    }
    QString zoomHint() const override
    {
        return QStringLiteral("PDF zoom is limited to %1% on this display to bound page rendering. Fit uses the document width.")
            .arg(int(std::floor(maximumZoom() * 100)));
    }
    void fit() override
    {
        if (!isFit()) applyZoom(0, true);
        emit zoomChanged();
    }
    void zoomBy(int steps) override
    {
        const auto factor = steppedZoom(steps);
        if (qAbs(factor - zoomFactor()) > .0000001) applyZoom(factor, false);
    }
    ~PdfWidget() override
    {
        // Qt 6.11.2 stops its renderer thread on destruction but does not
        // delete that QThread. The public mode setter joins and deletes it.
        for (auto *renderer : view_->findChildren<QPdfPageRenderer *>()) {
            renderer->setRenderMode(QPdfPageRenderer::RenderMode::SingleThreaded);
        }
        // Tear down rendering before releasing its borrowed document pointer.
        delete view_;
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) {
            // The viewport geometry is applied after this filter returns.
            QTimer::singleShot(0, this, [this] { emit zoomChanged(); });
        }
        return ZoomPreview::eventFilter(watched, event);
    }
private:
    void applyZoom(qreal factor, bool fitMode)
    {
        // Anchor the viewport center to the same point on the same page.
        // QPdfPageNavigator's location is not updated by ordinary scrolling.
        const QPointF center(view_->horizontalScrollBar()->value() + view_->viewport()->width() / 2.0,
                             view_->verticalScrollBar()->value() + view_->viewport()->height() / 2.0);
        int page = 0;
        qreal top = view_->documentMargins().top();
        for (; page + 1 < pageSizes_.size(); ++page) {
            const auto height = pageSizes_.at(page).height() * pageScale(page) * resolution();
            if (top + height + view_->pageSpacing() / 2.0 >= center.y()) break;
            top += height + view_->pageSpacing();
        }
        const auto oldRect = pageRect(page);
        const auto point = (center - oldRect.topLeft()) / (pageScale(page) * resolution());
        view_->setZoomMode(fitMode ? QPdfView::ZoomMode::FitToWidth : QPdfView::ZoomMode::Custom);
        if (!fitMode) view_->setZoomFactor(factor);
        const auto position = pageRect(page).topLeft() + point * pageScale(page) * resolution();
        view_->horizontalScrollBar()->setValue(qRound(position.x() - view_->viewport()->width() / 2.0));
        view_->verticalScrollBar()->setValue(qRound(position.y() - view_->viewport()->height() / 2.0));
        emit zoomChanged();
    }
    qreal resolution() const { return QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0; }
    qreal pageScale(int page) const
    {
        if (!isFit() || page < 0 || page >= pageSizes_.size()) return view_->zoomFactor();
        return std::max(1, view_->viewport()->width() - view_->documentMargins().left() - view_->documentMargins().right())
            / (pageSizes_.at(page).width() * resolution());
    }
    QRectF pageRect(int page) const
    {
        qreal top = view_->documentMargins().top();
        qreal width = view_->viewport()->width();
        for (int i = 0; i < pageSizes_.size(); ++i) {
            const auto size = pageSizes_.at(i) * (resolution() * pageScale(i));
            width = std::max(width, size.width() + view_->documentMargins().left() + view_->documentMargins().right());
            if (i < page) top += size.height() + view_->pageSpacing();
        }
        if (page < 0 || page >= pageSizes_.size()) return {};
        const auto size = pageSizes_.at(page) * (resolution() * pageScale(page));
        return QRectF(QPointF((width - size.width()) / 2.0, top), size);
    }
    std::shared_ptr<QPdfDocument> document_;
    QList<QSizeF> pageSizes_;
    QPdfView *view_;
};
}
bool PdfPreviewProvider::supports(const QMimeType &mime) const
{
    return mime.inherits(QStringLiteral("application/pdf"));
}
PreviewData PdfPreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                   const QSize &, const Cancellation &cancel) const
{
    if (file->size() > ByteLimit) {
        data.message = QStringLiteral("PDF exceeds the 64 MiB preview limit.");
        return data;
    }
    auto *document = new QPdfDocument(nullptr);
    auto *device = file.release();
    device->setParent(document);
    bool passwordRequired = false;
    QObject::connect(document, &QPdfDocument::passwordRequired, document, [&passwordRequired] { passwordRequired = true; });
    document->load(device);
    if (document->status() == QPdfDocument::Status::Loading && !passwordRequired) {
        QEventLoop loop;
        QTimer timer;
        timer.setInterval(50);
        int ticks = 0;
        QObject::connect(document, &QPdfDocument::statusChanged, &loop, [&loop](QPdfDocument::Status status) {
            if (status != QPdfDocument::Status::Loading) loop.quit();
        });
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
            if (cancel->load() || passwordRequired || ++ticks >= 200) loop.quit();
        });
        timer.start();
        loop.exec();
    }
    QObject::disconnect(document, nullptr, document, nullptr);
    if (cancel->load()) {
        delete document;
        data.kind = PreviewKind::Cancelled;
    } else if (passwordRequired || document->error() == QPdfDocument::Error::IncorrectPassword
               || document->error() == QPdfDocument::Error::UnsupportedSecurityScheme) {
        delete document;
        data.message = QStringLiteral("Password-protected or encrypted PDF — content preview is unavailable.");
    } else if (document->status() != QPdfDocument::Status::Ready || document->pageCount() < 1) {
        delete document;
        data.message = QStringLiteral("The PDF is damaged, unsupported, or did not finish loading.");
    } else if (document->pageCount() > 10000) {
        delete document;
        data.message = QStringLiteral("PDF exceeds the 10,000-page preview limit.");
    } else {
        for (int page = 0; page < document->pageCount(); ++page) {
            if (cancel->load()) { delete document; data.kind = PreviewKind::Cancelled; return data; }
            const auto size = document->pagePointSize(page);
            if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.isEmpty()) {
                delete document;
                data.message = QStringLiteral("The PDF has invalid page dimensions.");
                return data;
            }
            data.pdfPageSizes.append(size);
        }
        data.kind = PreviewKind::Pdf;
        data.message.clear();
        data.detail = QStringLiteral("%1 page(s) — scroll to navigate").arg(document->pageCount());
        // The document and its backing QFile were created on this worker.
        // Transfer both to the GUI thread before QPdfView receives them.
        document->moveToThread(QCoreApplication::instance()->thread());
        data.pdf = std::shared_ptr<QPdfDocument>(document, [](QPdfDocument *value) {
            if (value->thread() == QThread::currentThread()) delete value;
            else value->deleteLater();
        });
    }
    return data;
}
QWidget *PdfPreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new PdfWidget(data, parent);
}
} // namespace dzurkel

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

namespace dzurkel {
namespace {
class PdfWidget : public QWidget {
public:
    PdfWidget(std::shared_ptr<QPdfDocument> document, QWidget *parent)
        : QWidget(parent), document_(std::move(document)), view_(new QPdfView(this))
    {
        setObjectName(QStringLiteral("pdfPreview"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        view_->setPageMode(QPdfView::PageMode::MultiPage);
        view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        view_->setDocument(document_.get());
        layout->addWidget(view_);
        setFocusProxy(view_);
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
private:
    std::shared_ptr<QPdfDocument> document_;
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
    return new PdfWidget(data.pdf, parent);
}
} // namespace dzurkel

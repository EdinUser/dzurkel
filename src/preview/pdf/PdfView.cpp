#include "PdfView.h"
#include "PdfSession.h"
#include "PdfProtocol.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QDesktopServices>
#include <functional>
#include <algorithm>
#include <cmath>

namespace dzurkel {
namespace {
int linkOffset(qreal point, qreal scale, const QScrollBar *bar)
{
    // PDF destinations can contain huge finite coordinates. Clamp before
    // converting to an integer, including when the multiplication overflows.
    return qRound(std::clamp(point * scale, qreal(0), qreal(bar->maximum())));
}
}
class PdfCanvas final : public QWidget {
public:
    using QWidget::QWidget;
    QImage image;
    QSizeF points;
    QList<PdfPageLink> links;
    std::function<void(const PdfPageLink &)> activate;
protected:
    const PdfPageLink *linkAt(const QPointF &position) const
    {
        const QPointF point(position.x() * points.width() / width(), position.y() * points.height() / height());
        for (const auto &link : links) if (link.rectangle.contains(point)) return &link;
        return nullptr;
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const auto *link = linkAt(event->position());
        setCursor(link ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(link ? link->url.isEmpty() ? QStringLiteral("Page %1").arg(link->page + 1) : link->url.toDisplayString() : QString());
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (const auto *link = linkAt(event->position())) { activate(*link); return; }
        }
        QWidget::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(), Qt::white);
        painter.drawImage(rect(), image);
    }
};
PdfView::PdfView(const PreviewData &data, QWidget *parent)
    : ZoomPreview(parent), session_(data.pdfSession), scroll_(new QScrollArea(this)),
      canvas_(new PdfCanvas), toolbar_(new QWidget(this)), number_(new QSpinBox(toolbar_)),
      previous_(new QPushButton(QStringLiteral("‹"), toolbar_)),
      next_(new QPushButton(QStringLiteral("›"), toolbar_)), status_(new QLabel(scroll_->viewport())),
      resizeTimer_(new QTimer(this)), points_(data.pdfFirstPageSize), firstPoints_(points_), pages_(data.pdfPageCount)
{
    setObjectName(QStringLiteral("pdfPreview"));
    scroll_->setObjectName(QStringLiteral("pdfScrollArea"));
    canvas_->setObjectName(QStringLiteral("pdfPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll_);
    scroll_->setWidget(canvas_);
    scroll_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    scroll_->setFrameShape(QFrame::NoFrame);
    // Reserve the vertical scrollbar so a page near the viewport height cannot
    // alternate between fitting with a scrollbar and overflowing without it.
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    canvas_->image = data.image;
    canvas_->points = points_;
    canvas_->links = session_->links();
    canvas_->setMouseTracking(true);
    canvas_->activate = [this](const PdfPageLink &link) {
        if (stopped_ || loading_) return;
        if (!link.url.isEmpty()) { QDesktopServices::openUrl(link.url); return; }
        if (link.page < 0 || link.page >= pages_) return;
        if (link.page == page_) {
            scroll_->verticalScrollBar()->setValue(linkOffset(link.location.y(), resolution() * zoomFactor(), scroll_->verticalScrollBar()));
            scroll_->horizontalScrollBar()->setValue(linkOffset(link.location.x(), resolution() * zoomFactor(), scroll_->horizontalScrollBar()));
        } else {
            goToPage(link.page);
            linkLocation_ = link.location;
        }
    };
    setFocusProxy(scroll_);
    auto *tools = new QHBoxLayout(toolbar_);
    tools->setContentsMargins(0, 0, 0, 0);
    previous_->setAccessibleName(QStringLiteral("Previous PDF page"));
    next_->setAccessibleName(QStringLiteral("Next PDF page"));
    number_->setObjectName(QStringLiteral("pdfPageNumber"));
    number_->setAccessibleName(QStringLiteral("PDF page"));
    number_->setRange(1, std::max(1, pages_));
    number_->setSuffix(QStringLiteral(" / %1").arg(pages_));
    number_->setMinimumWidth(number_->sizeHint().width());
    number_->setKeyboardTracking(false);
    tools->addWidget(previous_);
    tools->addWidget(number_);
    tools->addWidget(next_);
    // Overlay feedback without changing toolbar or page layout during a render.
    status_->setMargin(6);
    status_->setAutoFillBackground(true);
    status_->setAttribute(Qt::WA_TransparentForMouseEvents);
    status_->hide();
    connect(previous_, &QPushButton::clicked, this, [this] { goToPage(page_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { goToPage(page_ + 1); });
    connect(number_, &QSpinBox::valueChanged, this, [this](int value) { goToPage(value - 1); });
    resizeTimer_->setSingleShot(true);
    resizeTimer_->setInterval(80);
    connect(resizeTimer_, &QTimer::timeout, this, &PdfView::request);
    connect(session_.get(), &PdfSession::frameReady, this, [this](int page, const QSizeF &points, const QImage &image) {
        if (stopped_ || page != requestedPage_) return;
        const bool pageChanged = page != page_;
        page_ = page;
        points_ = points;
        canvas_->image = image;
        canvas_->points = points;
        canvas_->links = session_->links();
        loading_ = false;
        notice({});
        layoutPage();
        canvas_->update();
        controls();
        const auto bottom = enterBottom_;
        const auto location = linkLocation_;
        linkLocation_.reset();
        enterBottom_ = false;
        if (pageChanged) {
            scroll_->verticalScrollBar()->setValue(location ? linkOffset(location->y(), resolution() * zoomFactor(), scroll_->verticalScrollBar())
                : bottom ? scroll_->verticalScrollBar()->maximum() : 0);
            scroll_->horizontalScrollBar()->setValue(location ? linkOffset(location->x(), resolution() * zoomFactor(), scroll_->horizontalScrollBar()) : 0);
        }
        emit zoomChanged();
    });
    connect(session_.get(), &PdfSession::failed, this, [this](const QString &message) {
        loading_ = false;
        stopped_ = true;
        notice(QStringLiteral("Page unavailable"));
        status_->setToolTip(message);
        status_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        controls();
    });
    scroll_->viewport()->installEventFilter(this);
    scroll_->installEventFilter(this);
    canvas_->installEventFilter(this);
    controls();
    layoutPage();
}
PdfView::~PdfView()
{
    // The window can destroy its hosted toolbar before the content widget.
    // Teardown must not dereference the toolbar controls.
    if (session_) session_->stop();
}
void PdfView::stop()
{
    stopped_ = true;
    resizeTimer_->stop();
    if (session_) session_->stop();
    status_->hide();
    controls();
}
qreal PdfView::resolution() const { return logicalDpiX() / 72.0; }
QSize PdfView::naturalSize() const { return (firstPoints_ * resolution()).toSize(); }
QWidget *PdfView::toolbarWidget() const { return toolbar_; }
bool PdfView::loading() const { return loading_ || resizeTimer_->isActive(); }
qreal PdfView::zoomFactor() const
{
    return fit_ ? std::min(maximumZoom(), std::max(1, scroll_->viewport()->width()) / (points_.width() * resolution())) : zoom_;
}
qreal PdfView::maximumZoom() const
{
    const auto physical = resolution() * devicePixelRatioF();
    if (points_.isEmpty()) return 8;
    return std::max(.000001, std::min({8.0, PdfMaxEdge / (std::max(points_.width(), points_.height()) * physical),
        std::sqrt(double(PdfMaxPixels) / (points_.width() * points_.height())) / physical}));
}
QString PdfView::zoomHint() const
{
    return QStringLiteral("PDF pages render on demand, up to 16 million pixels and an 8192 pixel edge. Fit uses the current page width.");
}
void PdfView::layoutPage()
{
    if (points_.isEmpty()) return;
    if (!fit_) zoom_ = std::min(zoom_, maximumZoom());
    canvas_->resize((points_ * (resolution() * zoomFactor())).toSize().expandedTo(QSize(1, 1)));
}
void PdfView::controls()
{
    const QSignalBlocker blocker(number_);
    number_->setValue(page_ + 1);
    number_->setEnabled(!stopped_ && !loading_);
    previous_->setEnabled(!stopped_ && !loading_ && page_ > 0);
    next_->setEnabled(!stopped_ && !loading_ && page_ + 1 < pages_);
}
void PdfView::notice(const QString &message)
{
    status_->setText(message);
    status_->adjustSize();
    status_->move(std::max(0, scroll_->viewport()->width() - status_->width() - 8), 8);
    status_->setVisible(!message.isEmpty());
    status_->raise();
}
void PdfView::request()
{
    if (stopped_) return;
    resizeTimer_->stop();
    loading_ = true;
    notice(QStringLiteral("Loading…"));
    controls();
    session_->request(requestedPage_, qRound(scroll_->viewport()->width() * devicePixelRatioF()),
        fit_ ? 0 : zoom_ * resolution() * devicePixelRatioF());
}
void PdfView::goToPage(int page, bool bottom)
{
    if (stopped_ || loading_ || page < 0 || page >= pages_ || page == page_) return;
    requestedPage_ = page;
    enterBottom_ = bottom;
    wheelAmount_ = 0;
    request();
}
void PdfView::fit()
{
    if (stopped_) return;
    fit_ = true;
    layoutPage();
    request();
    emit zoomChanged();
}
void PdfView::zoomBy(int steps)
{
    if (stopped_) return;
    const auto next = steppedZoom(steps);
    if (qAbs(next - zoomFactor()) < .0000001) return;
    const auto oldSize = canvas_->size();
    const QPointF center(scroll_->horizontalScrollBar()->value() + scroll_->viewport()->width() / 2.0,
                         scroll_->verticalScrollBar()->value() + scroll_->viewport()->height() / 2.0);
    fit_ = false;
    zoom_ = next;
    layoutPage();
    scroll_->horizontalScrollBar()->setValue(qRound(center.x() * canvas_->width() / oldSize.width() - scroll_->viewport()->width() / 2.0));
    scroll_->verticalScrollBar()->setValue(qRound(center.y() * canvas_->height() / oldSize.height() - scroll_->viewport()->height() / 2.0));
    request();
    emit zoomChanged();
}
void PdfView::scrollVertically(int direction)
{
    auto *bar = scroll_->verticalScrollBar();
    bar->setValue(bar->value() + direction * bar->singleStep());
}
bool PdfView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == scroll_->viewport() && event->type() == QEvent::Resize) {
        layoutPage();
        notice(status_->text());
        if (fit_) resizeTimer_->start();
        emit zoomChanged();
    }
    if (event->type() == QEvent::KeyPress && watched != number_) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp)) {
            goToPage(page_ + (key->key() == Qt::Key_PageDown ? 1 : -1));
            return true;
        }
    }
    if (event->type() != QEvent::Wheel || watched != scroll_->viewport()) return ZoomPreview::eventFilter(watched, event);
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (wheel->modifiers() != Qt::NoModifier || std::abs(wheel->angleDelta().x()) > std::abs(wheel->angleDelta().y())
        || std::abs(wheel->pixelDelta().x()) > std::abs(wheel->pixelDelta().y())) return false;
    const bool pixels = !wheel->pixelDelta().isNull();
    const int delta = pixels ? wheel->pixelDelta().y() : wheel->angleDelta().y();
    const bool idle = !lastWheel_.isValid() || lastWheel_.elapsed() > 250;
    lastWheel_.restart();
    if (wheel->phase() == Qt::ScrollBegin || (wheel->phase() == Qt::NoScrollPhase && idle)) {
        gestureLatched_ = false;
        wheelAmount_ = 0;
    }
    if (!delta) return false;
    const int direction = delta < 0 ? 1 : -1;
    auto *bar = scroll_->verticalScrollBar();
    const bool atEdge = direction > 0 ? bar->value() == bar->maximum() : bar->value() == bar->minimum();
    if (!atEdge) { wheelAmount_ = 0; return false; }
    if (loading_ || gestureLatched_ || wheel->phase() == Qt::ScrollMomentum) return true;
    if (direction != wheelDirection_) wheelAmount_ = 0;
    wheelDirection_ = direction;
    wheelAmount_ += std::abs(delta);
    if (wheelAmount_ >= (pixels ? 40 : 120)) {
        goToPage(page_ + direction, direction < 0);
        wheelAmount_ = 0;
        // A mouse notch is a deliberate action. Touchpad streams stay latched
        // until another gesture; momentum can never cross a second page.
        gestureLatched_ = pixels || wheel->phase() != Qt::NoScrollPhase || std::abs(delta) % 120 != 0
            || wheel->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad;
    }
    return true;
}
}

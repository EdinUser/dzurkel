#include "LazyWindow.h"
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QLabel>

namespace dzurkel {
void connectLazyStatus(LazyWindow *window, QLabel *status)
{
    status->setTextFormat(Qt::PlainText);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    status->setFixedHeight(status->fontMetrics().height() + 4);
    QObject::connect(window, &LazyWindow::loadingChanged, status, [status](bool loading) {
        status->setText(loading ? QStringLiteral("Loading…") : QString());
    });
    QObject::connect(window, &LazyWindow::failed, status, [status](const QString &message) {
        status->setText(message);
        status->setToolTip(message);
    });
}
LazyWindow::LazyWindow(std::shared_ptr<LazyCoordinator> coordinator, const LazyPortion &first, QObject *parent)
    : QObject(parent), coordinator_(std::move(coordinator)), portions_{first}
{
    connect(coordinator_.get(), &LazyCoordinator::loadingChanged, this, &LazyWindow::loadingChanged);
    connect(coordinator_.get(), &LazyCoordinator::failed, this, [this](const QString &message) {
        expected_.clear();
        emit failed(message);
    });
    connect(coordinator_.get(), &LazyCoordinator::ready, this, [this](const LazyPortion &portion) {
        if (portion.token != expected_) return;
        LazyPortion removed;
        if (!direction_) {
            portions_ = {portion};
        } else if (direction_ > 0) {
            portions_.append(portion);
            if (portions_.size() > 3) removed = portions_.takeFirst();
        } else {
            portions_.prepend(portion);
            if (portions_.size() > 3) removed = portions_.takeLast();
        }
        expected_.clear();
        const QPointer<LazyWindow> guard(this);
        emit changed(portion, removed, direction_);
        if (guard) coordinator_->prefetch(direction_ > 0 || (!direction_ && portion.previous.isEmpty()) ? portion.next : portion.previous);
    });
    QTimer::singleShot(0, this, [this, first] { coordinator_->prefetch(first.next); });
}
void LazyWindow::reset(const LazyPortion &first)
{
    expected_.clear();
    portions_ = {first};
    coordinator_->prefetch(first.next);
}
void LazyWindow::jump(const QString &token)
{
    if (token.isEmpty()) return;
    direction_ = 0;
    expected_ = token;
    coordinator_->request(token); // Supersedes pending work through the existing generation guard.
}
void LazyWindow::request(int direction)
{
    if (!expected_.isEmpty() || portions_.isEmpty() || !direction) return;
    const auto token = direction > 0 ? portions_.last().next : portions_.first().previous;
    if (token.isEmpty()) return;
    direction_ = direction;
    expected_ = token;
    coordinator_->request(token);
}
LazyBoundaryWatcher::LazyBoundaryWatcher(QAbstractScrollArea *area, LazyWindow *window, QObject *parent)
    : QObject(parent), area_(area), window_(window), lastScroll_(area->verticalScrollBar()->value())
{
    area_->viewport()->installEventFilter(this);
    area_->installEventFilter(this);
    connect(area_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        const int direction = value > lastScroll_ ? 1 : value < lastScroll_ ? -1 : 0;
        lastScroll_ = value;
        if (direction) boundary(direction);
    });
}
void LazyBoundaryWatcher::boundary(int direction)
{
    if (paused || !window_) return;
    const auto *scroll = area_->verticalScrollBar();
    const int lookahead = qMax(2, scroll->pageStep());
    if (direction > 0 && scroll->maximum() - scroll->value() > lookahead) return;
    if (direction < 0 && scroll->value() > lookahead) return;
    window_->request(direction);
}
bool LazyBoundaryWatcher::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        const auto *wheel = static_cast<QWheelEvent *>(event);
        if (!(wheel->modifiers() & Qt::ControlModifier)) {
            const int delta = wheel->angleDelta().y() ? wheel->angleDelta().y() : wheel->pixelDelta().y();
            if (delta) boundary(delta < 0 ? 1 : -1);
        }
    } else if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_PageDown || key->key() == Qt::Key_End) boundary(1);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_Home) boundary(-1);
    }
    return QObject::eventFilter(watched, event);
}
}

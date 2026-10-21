#pragma once
#include "LazyCoordinator.h"
#include <memory>

class QAbstractScrollArea;
class QLabel;
namespace dzurkel {
// Reader-neutral adjacent-window policy shared by text and tables. Views only
// apply the changed payload using their native Qt document/model operations.
class LazyWindow final : public QObject {
    Q_OBJECT
public:
    LazyWindow(std::shared_ptr<LazyCoordinator> coordinator, const LazyPortion &first, QObject *parent);
    void request(int direction);
    void jump(const QString &token);
    void reset(const LazyPortion &first);
    QString firstToken() const { return portions_.isEmpty() ? QString() : portions_.first().token; }
    void stop() { coordinator_->stop(); }
signals:
    void changed(const dzurkel::LazyPortion &portion, const dzurkel::LazyPortion &evicted, int direction);
    void loadingChanged(bool loading);
    void failed(const QString &message);
private:
    std::shared_ptr<LazyCoordinator> coordinator_;
    QList<LazyPortion> portions_;
    QString expected_;
    int direction_ = 0;
};
class LazyBoundaryWatcher final : public QObject {
public:
    LazyBoundaryWatcher(QAbstractScrollArea *area, LazyWindow *window, QObject *parent);
    bool paused = false;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void boundary(int direction);
    QAbstractScrollArea *area_;
    QPointer<LazyWindow> window_;
    int lastScroll_ = 0;
};
void connectLazyStatus(LazyWindow *window, QLabel *status);
}

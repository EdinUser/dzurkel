#pragma once
#include "LazyWindow.h"
#include "preview/table/TableRowBatch.h"

class QTableView;
class QLabel;
namespace dzurkel {
class LazyTableWindow final : public QObject {
public:
    LazyTableWindow(QTableView *view, QLabel *status, std::shared_ptr<LazyCoordinator> coordinator,
                    const LazyPortion &first, const TableRowBatch &batch);
    QString firstToken() const { return window_->firstToken(); }
private:
    void accept(const LazyPortion &portion, const LazyPortion &evicted, int direction);
    QTableView *view_;
    QLabel *status_;
    LazyWindow *window_;
    LazyBoundaryWatcher *watcher_;
};
}

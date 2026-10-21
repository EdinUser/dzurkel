#include "LazyTableWindow.h"
#include "preview/table/SpreadsheetView.h"
#include <QLabel>
#include <QTableView>
#include <QScrollBar>
#include <QPersistentModelIndex>
#include <QScopedValueRollback>

namespace dzurkel {
LazyTableWindow::LazyTableWindow(QTableView *view, QLabel *status, std::shared_ptr<LazyCoordinator> coordinator,
                               const LazyPortion &first, const TableRowBatch &batch)
    : QObject(view), view_(view), status_(status), window_(new LazyWindow(std::move(coordinator), first, this)),
      watcher_(new LazyBoundaryWatcher(view, window_, this))
{
    auto *model = qobject_cast<SpreadsheetModel *>(view_->model());
    model->setInitialBatch(batch);
    connect(window_, &LazyWindow::changed, this, &LazyTableWindow::accept);
    connectLazyStatus(window_, status_);
}
void LazyTableWindow::accept(const LazyPortion &portion, const LazyPortion &evicted, int direction)
{
    TableRowBatch batch;
    if (!decodeTableRowBatch(portion.content, batch)) {
        window_->stop();
        status_->setText(QStringLiteral("Invalid table content."));
        return;
    }
    QScopedValueRollback<bool> updating(watcher_->paused, true);
    auto *model = qobject_cast<SpreadsheetModel *>(view_->model());
    const int row = qMax(0, view_->rowAt(0));
    const QPersistentModelIndex anchor(model->index(row, 0));
    const int top = view_->visualRect(anchor).top();
    const int horizontal = view_->horizontalScrollBar()->value();
    if (!model->applyBatch(batch, direction, !evicted.token.isEmpty())) {
        window_->stop();
        status_->setText(QStringLiteral("Invalid table continuation."));
        return;
    }
    // Qt model insert/remove notifications retain persistent indexes and the
    // current selection; resolve pending item layout before pixel anchoring.
    view_->doItemsLayout();
    if (anchor.isValid())
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() + view_->visualRect(anchor).top() - top);
    view_->horizontalScrollBar()->setValue(horizontal);
    status_->setText(portion.content.value("notice").toString());
}
}

#include "SpreadsheetView.h"
#include "preview/lazy/LazyTableWindow.h"
#include "preview/PreviewProvider.h"
#include <QLabel>
#include <QHeaderView>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QStyle>

namespace dzurkel {
SpreadsheetModel::SpreadsheetModel(std::shared_ptr<const TabularDocument> document, int sheet, QObject *parent)
    : QAbstractTableModel(parent), document_(std::move(document)), sheet_(sheet) {}
int SpreadsheetModel::rowCount(const QModelIndex &parent) const
{
    if (!batches_.isEmpty() && !parent.isValid()) {
        int count = 0;
        for (const auto &batch : batches_) count += batch.sheet->rowCount;
        return count;
    }
    return parent.isValid() || !document_ || sheet_ < 0 || sheet_ >= document_->sheets.size()
        ? 0 : document_->sheets.at(sheet_).rowCount;
}
int SpreadsheetModel::columnCount(const QModelIndex &parent) const
{
    if (!batches_.isEmpty() && !parent.isValid()) return columns_;
    return parent.isValid() || !document_ || sheet_ < 0 || sheet_ >= document_->sheets.size()
        ? 0 : document_->sheets.at(sheet_).columnCount;
}
QVariant SpreadsheetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= columnCount()) return {};
    const auto value = this->value(index.row(), index.column());
    if (role == Qt::DisplayRole) return value;
    if (role == Qt::ToolTipRole)
        return QStringLiteral("<qt>%1</qt>").arg(value.toString().toHtmlEscaped().replace(QChar('\n'), QStringLiteral("<br>")));
    if (role == Qt::TextAlignmentRole) return int(Qt::AlignLeft | Qt::AlignVCenter);
    return {};
}
QVariant SpreadsheetModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || section < 0) return {};
    if (orientation == Qt::Vertical) return batches_.isEmpty() ? QVariant(section + 1) : QVariant(batches_.first().firstRow + section + 1);
    QString name;
    for (int value = section + 1; value; value = (value - 1) / 26)
        name.prepend(QChar('A' + (value - 1) % 26));
    return name;
}
Qt::ItemFlags SpreadsheetModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}
QVariant SpreadsheetModel::value(int row, int column) const
{
    if (batches_.isEmpty()) return document_->sheets.at(sheet_).value(row, column);
    for (const auto &batch : batches_) {
        if (row < batch.sheet->rowCount) return batch.sheet->value(row, column);
        row -= batch.sheet->rowCount;
    }
    return {};
}
void SpreadsheetModel::setInitialBatch(const TableRowBatch &batch)
{
    beginResetModel();
    document_.reset();
    batches_ = {batch};
    columns_ = batch.sheet->columnCount;
    endResetModel();
}
bool SpreadsheetModel::applyBatch(const TableRowBatch &batch, int direction, bool evict)
{
    if (batches_.isEmpty() || !batch.sheet || batch.sheet->rowCount < 1
        || batch.sheet->name != batches_.first().sheet->name) return false;
    const auto end = batches_.first().firstRow + rowCount();
    if ((direction > 0 && batch.firstRow != end)
        || (direction < 0 && batch.firstRow + batch.sheet->rowCount != batches_.first().firstRow)) return false;
    if (batch.sheet->columnCount > columns_) {
        beginInsertColumns({}, columns_, batch.sheet->columnCount - 1);
        columns_ = batch.sheet->columnCount;
        endInsertColumns();
    }
    const int position = direction > 0 ? rowCount() : 0;
    beginInsertRows({}, position, position + batch.sheet->rowCount - 1);
    if (direction > 0) batches_.append(batch);
    else batches_.prepend(batch);
    endInsertRows();
    if (evict) {
        const int removed = (direction > 0 ? batches_.first() : batches_.last()).sheet->rowCount;
        const int first = direction > 0 ? 0 : rowCount() - removed;
        beginRemoveRows({}, first, first + removed - 1);
        if (direction > 0) batches_.removeFirst();
        else batches_.removeLast();
        endRemoveRows();
    }
    emit headerDataChanged(Qt::Vertical, 0, rowCount() - 1);
    return true;
}
SpreadsheetView::SpreadsheetView(std::shared_ptr<const TabularDocument> document, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("spreadsheetPreview"));
    setAccessibleName(QStringLiteral("Read-only spreadsheet preview"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("spreadsheetSheets"));
    layout->addWidget(tabs);
    if (document) for (int i = 0; i < document->sheets.size(); ++i) {
        auto *view = new QTableView(tabs);
        view->setObjectName(QStringLiteral("spreadsheetGrid"));
        view->setAccessibleName(document->sheets.at(i).name);
        view->setModel(new SpreadsheetModel(document, i, view));
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setWordWrap(false);
        view->setAlternatingRowColors(true);
        // Fixed initial sizes avoid scanning the entire sheet to autosize it.
        view->horizontalHeader()->setDefaultSectionSize(110);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->verticalHeader()->setDefaultSectionSize(qMax(22, view->fontMetrics().height() + 6));
        view->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tabs->addTab(view, document->sheets.at(i).name);
    }
    QObject::connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int index) {
        setFocusProxy(tabs->widget(index));
    });
    if (tabs->currentWidget()) setFocusProxy(tabs->currentWidget());
}
SpreadsheetView::SpreadsheetView(const PreviewData &data, QWidget *parent) : SpreadsheetView(data.table, parent)
{
    if (!data.lazy || !data.table || data.table->sheets.isEmpty()) return;
    auto *sheets = findChild<QTabWidget *>(QStringLiteral("spreadsheetSheets"));
    auto *grid = sheets ? qobject_cast<QTableView *>(sheets->currentWidget()) : nullptr;
    if (!grid) return;
    auto *status = new QLabel(this);
    status->setObjectName(QStringLiteral("tableLoadingStatus"));
    layout()->addWidget(status);
    TableRowBatch first{0, std::shared_ptr<const TabularSheet>(data.table, &data.table->sheets.first())};
    coordinator_ = data.lazy;
    activeWindow_ = new LazyTableWindow(grid, status, data.lazy,
        LazyPortion{QStringLiteral("0"), data.lazyPrevious, data.lazyNext, {}, 0}, first);
    if (data.readerId != QStringLiteral("xlsx")) return;
    sheetTokens_ = data.lazySheetTokens;
    if (sheetTokens_.size() != data.table->sheets.size()) return;
    sheetTokens_[0] = QStringLiteral("0");
    auto *tabs = findChild<QTabWidget *>(QStringLiteral("spreadsheetSheets"));
    connect(tabs, &QTabWidget::currentChanged, this, &SpreadsheetView::selectLazySheet);
    connect(coordinator_.get(), &LazyCoordinator::ready, this, &SpreadsheetView::acceptLazySheet);
    connect(coordinator_.get(), &LazyCoordinator::failed, this, [this, status](const QString &error) {
        sessionError_ = error;
        expectedSheet_.clear();
        status->setText(error);
        status->setToolTip(error);
    });
}
void SpreadsheetView::selectLazySheet(int index)
{
    if (index < 0 || index >= sheetTokens_.size()) return;
    if (!sessionError_.isEmpty()) {
        findChild<QLabel *>(QStringLiteral("tableLoadingStatus"))->setText(sessionError_);
        return;
    }
    auto *tabs = findChild<QTabWidget *>(QStringLiteral("spreadsheetSheets"));
    if (activeWindow_) {
        sheetTokens_[activeSheet_] = activeWindow_->firstToken();
        delete activeWindow_;
        auto *old = qobject_cast<QTableView *>(tabs->widget(activeSheet_));
        auto empty = std::make_shared<TabularSheet>();
        empty->name = tabs->tabText(activeSheet_);
        qobject_cast<SpreadsheetModel *>(old->model())->setInitialBatch({0, empty});
    }
    activeSheet_ = index;
    expectedSheet_ = sheetTokens_.at(index);
    auto *status = findChild<QLabel *>(QStringLiteral("tableLoadingStatus"));
    status->setText(QStringLiteral("Loading…"));
    coordinator_->request(expectedSheet_);
}
void SpreadsheetView::acceptLazySheet(const LazyPortion &portion)
{
    if (expectedSheet_.isEmpty() || portion.token != expectedSheet_) return;
    expectedSheet_.clear();
    auto *tabs = findChild<QTabWidget *>(QStringLiteral("spreadsheetSheets"));
    auto *grid = qobject_cast<QTableView *>(tabs->currentWidget());
    auto *status = findChild<QLabel *>(QStringLiteral("tableLoadingStatus"));
    TableRowBatch batch;
    if (!decodeTableRowBatch(portion.content, batch) || batch.sheet->name != tabs->tabText(activeSheet_)) {
        sessionError_ = QStringLiteral("Invalid worksheet content.");
        status->setText(sessionError_);
        coordinator_->stop();
        return;
    }
    activeWindow_ = new LazyTableWindow(grid, status, coordinator_, portion, batch);
    grid->verticalScrollBar()->setValue(0);
    status->setText(portion.content.value("notice").toString());
}
int SpreadsheetView::preferredWidth() const
{
    const auto *tabs = findChild<QTabWidget *>(QStringLiteral("spreadsheetSheets"));
    const auto *grid = tabs ? qobject_cast<QTableView *>(tabs->currentWidget()) : nullptr;
    if (!grid) return 0;
    // Use header geometry, never scan cell contents or resize columns to fit.
    // A newly created grid's viewport may still have its construction size.
    // Derive chrome from style/header metrics rather than that stale viewport.
    const auto margins = layout()->contentsMargins() + tabs->contentsMargins();
    const int chrome = grid->verticalHeader()->sizeHint().width() + 2 * grid->frameWidth()
        + grid->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, grid)
        + 2 * tabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, tabs)
        + margins.left() + margins.right();
    return grid->model()->columnCount() * grid->horizontalHeader()->defaultSectionSize() + chrome;
}
}

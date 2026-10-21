#pragma once
#include "TabularDocument.h"
#include "TableRowBatch.h"
#include <QAbstractTableModel>
#include <QWidget>
#include <QPointer>
#include <memory>

namespace dzurkel {
struct PreviewData;
struct LazyPortion;
class LazyCoordinator;
class LazyTableWindow;
class SpreadsheetModel : public QAbstractTableModel {
    Q_OBJECT
public:
    SpreadsheetModel(std::shared_ptr<const TabularDocument> document, int sheet, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void setInitialBatch(const TableRowBatch &batch);
    bool applyBatch(const TableRowBatch &batch, int direction, bool evict);
private:
    QVariant value(int row, int column) const;
    std::shared_ptr<const TabularDocument> document_;
    int sheet_;
    QList<TableRowBatch> batches_;
    int columns_ = 0;
};
class SpreadsheetView : public QWidget {
public:
    explicit SpreadsheetView(std::shared_ptr<const TabularDocument> document, QWidget *parent = nullptr);
    explicit SpreadsheetView(const PreviewData &data, QWidget *parent = nullptr);
    int preferredWidth() const;
private:
    void selectLazySheet(int index);
    void acceptLazySheet(const LazyPortion &portion);
    std::shared_ptr<LazyCoordinator> coordinator_;
    QPointer<LazyTableWindow> activeWindow_;
    QStringList sheetTokens_;
    QString expectedSheet_;
    QString sessionError_;
    int activeSheet_ = 0;

};
}

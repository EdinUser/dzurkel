#pragma once
#include "TabularDocument.h"
#include <QAbstractTableModel>
#include <QWidget>
#include <memory>

namespace dzurkel {
class SpreadsheetModel : public QAbstractTableModel {
    Q_OBJECT
public:
    SpreadsheetModel(std::shared_ptr<const TabularDocument> document, int sheet, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
private:
    std::shared_ptr<const TabularDocument> document_;
    int sheet_;
};
class SpreadsheetView : public QWidget {
public:
    explicit SpreadsheetView(std::shared_ptr<const TabularDocument> document, QWidget *parent = nullptr);
};
}

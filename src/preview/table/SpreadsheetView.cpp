#include "SpreadsheetView.h"
#include <QHeaderView>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dzurkel {
SpreadsheetModel::SpreadsheetModel(std::shared_ptr<const TabularDocument> document, int sheet, QObject *parent)
    : QAbstractTableModel(parent), document_(std::move(document)), sheet_(sheet) {}
int SpreadsheetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !document_ || sheet_ < 0 || sheet_ >= document_->sheets.size()
        ? 0 : document_->sheets.at(sheet_).rowCount;
}
int SpreadsheetModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() || !document_ || sheet_ < 0 || sheet_ >= document_->sheets.size()
        ? 0 : document_->sheets.at(sheet_).columnCount;
}
QVariant SpreadsheetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= columnCount()) return {};
    const auto value = document_->sheets.at(sheet_).value(index.row(), index.column());
    if (role == Qt::DisplayRole) return value;
    if (role == Qt::ToolTipRole)
        return QStringLiteral("<qt>%1</qt>").arg(value.toString().toHtmlEscaped().replace(QChar('\n'), QStringLiteral("<br>")));
    if (role == Qt::TextAlignmentRole) return int(Qt::AlignLeft | Qt::AlignVCenter);
    return {};
}
QVariant SpreadsheetModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || section < 0) return {};
    if (orientation == Qt::Vertical) return section + 1;
    QString name;
    for (int value = section + 1; value; value = (value - 1) / 26)
        name.prepend(QChar('A' + (value - 1) % 26));
    return name;
}
Qt::ItemFlags SpreadsheetModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
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
}

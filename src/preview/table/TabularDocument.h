#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

namespace dzurkel {
// Reader-neutral immutable payload. Readers retain only populated cells; views
// never allocate widgets or items for every row/column position.
struct TabularSheet {
    QString name;
    int rowCount = 0;
    int columnCount = 0;
    QHash<quint64, QVariant> cells;
    static quint64 key(int row, int column) { return (quint64(quint32(row)) << 32) | quint32(column); }
    QVariant value(int row, int column) const { return cells.value(key(row, column)); }
};
struct TabularDocument {
    QList<TabularSheet> sheets;
    bool partial = false;
    QString notice;
};
}

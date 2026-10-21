#pragma once
#include <QTextDocument>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextTable>
#include <QTextBlock>

namespace dzurkel {
inline void normalizeMarkdownTables(QTextDocument *document)
{
    QList<QTextFrame *> frames{document->rootFrame()};
    QTextCursor edit(document);
    edit.beginEditBlock();
    while (!frames.isEmpty()) {
        auto *frame = frames.takeLast();
        frames.append(frame->childFrames());
        auto *table = qobject_cast<QTextTable *>(frame);
        if (!table) continue;
        auto format = table->format();
        format.setCellPadding(4);
        format.setCellSpacing(0);
        table->setFormat(format);
        for (int row = 0; row < table->rows(); ++row) for (int column = 0; column < table->columns(); ++column) {
            auto cell = table->cellAt(row, column);
            auto cellFormat = cell.format().toTableCellFormat();
            cellFormat.setVerticalAlignment(QTextCharFormat::AlignTop);
            cell.setFormat(cellFormat);
            // Qt's Markdown importer can leave an empty leading block in the
            // first header cell. Remove only that synthetic empty paragraph.
            auto first = cell.firstCursorPosition();
            if (first.block().text().isEmpty() && first.block().next().position() <= cell.lastCursorPosition().position()) {
                first.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
                first.removeSelectedText();
            }
            if (!row) {
                auto header = cell.firstCursorPosition();
                header.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                QTextCharFormat bold;
                bold.setFontWeight(QFont::Bold);
                header.mergeCharFormat(bold);
            }
        }
    }
    edit.endEditBlock();
}
}

#include "LazyTextWindow.h"
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QScopedValueRollback>

namespace dzurkel {
LazyTextWindow::LazyTextWindow(QPlainTextEdit *editor, QLabel *status,
                             std::shared_ptr<LazyCoordinator> coordinator, const LazyPortion &first)
    : QObject(editor), editor_(editor), status_(status),
      window_(new LazyWindow(std::move(coordinator), first, this)),
      watcher_(new LazyBoundaryWatcher(editor, window_, this))
{
    connect(window_, &LazyWindow::changed, this, &LazyTextWindow::accept);
    connectLazyStatus(window_, status_);
    connect(window_, &LazyWindow::failed, this, [this] { failed_ = true; watcher_->paused = false; });
    editor_->installEventFilter(this);
    editor_->viewport()->installEventFilter(this);
}
void LazyTextWindow::showBeginning() { jump(false); }
void LazyTextWindow::showEnd() { jump(true); }
void LazyTextWindow::jump(bool end)
{
    if (failed_) return;
    jumpToEnd_ = end;
    watcher_->paused = true;
    window_->jump(end ? QStringLiteral("end") : QStringLiteral("0"));
}
bool LazyTextWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::MoveToEndOfDocument)) { showEnd(); return true; }
        if (key->matches(QKeySequence::MoveToStartOfDocument)) { showBeginning(); return true; }
    }
    return QObject::eventFilter(watched, event);
}
void LazyTextWindow::accept(const LazyPortion &portion, const LazyPortion &evicted, int direction)
{
    if (!portion.content.value("text").isString()) {
        failed_ = true; watcher_->paused = false;
        window_->stop();
        status_->setText(QStringLiteral("Invalid text content."));
        return;
    }
    if (!direction) {
        // Replace only the bounded resident text, keeping the editor, language
        // highlighter, zoom and native window. Explicit jumps reset selection.
        QTextCursor edit(editor_->document());
        edit.select(QTextCursor::Document);
        edit.insertText(portion.content.value("text").toString());
        editor_->moveCursor(jumpToEnd_ ? QTextCursor::End : QTextCursor::Start);
        editor_->ensureCursorVisible();
        editor_->setFocus(Qt::OtherFocusReason);
        status_->setText(jumpToEnd_ ? QStringLiteral("End of file") : QStringLiteral("Beginning of file"));
        watcher_->paused = false;
        return;
    }
    QScopedValueRollback<bool> updating(watcher_->paused, true);
    auto anchor = editor_->cursorForPosition(QPoint(4, 0));
    const int top = editor_->cursorRect(anchor).top();
    const int horizontal = editor_->horizontalScrollBar()->value();
    QTextCursor edit(editor_->document());
    edit.beginEditBlock();
    if (direction > 0) {
        edit.movePosition(QTextCursor::End);
        edit.insertText(portion.content.value("text").toString());
        if (!evicted.token.isEmpty()) {
            const auto removed = evicted.content.value("text").toString().size();
            edit.setPosition(0);
            edit.setPosition(removed, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
        }
    } else {
        edit.setPosition(0);
        edit.insertText(portion.content.value("text").toString());
        if (!evicted.token.isEmpty()) {
            const auto removed = evicted.content.value("text").toString().size();
            edit.movePosition(QTextCursor::End);
            edit.setPosition(edit.position() - removed, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
        }
    }
    edit.endEditBlock();
    // QPlainTextEdit lays out wrapped blocks lazily. After removing a prefix,
    // scrollbar deltas still include estimates for those blocks. Resolve the
    // small resident window through Qt's public layout API before restoring an
    // absolute visual-line anchor; ratios/pixel deltas drift after eviction.
    auto *layout = qobject_cast<QPlainTextDocumentLayout *>(editor_->document()->documentLayout());
    if (layout) {
        for (auto block = editor_->document()->begin(); block.isValid(); block = block.next()) layout->ensureBlockLayout(block);
        layout->requestUpdate();
    }
    const auto block = anchor.block();
    const auto line = block.layout()->lineForTextPosition(anchor.positionInBlock());
    const qreal height = line.isValid() ? line.height() : QFontMetricsF(editor_->font()).height();
    editor_->verticalScrollBar()->setValue(block.firstLineNumber() + (line.isValid() ? line.lineNumber() : 0) - qRound(top / height));
    editor_->horizontalScrollBar()->setValue(horizontal);
    status_->clear();
}
}

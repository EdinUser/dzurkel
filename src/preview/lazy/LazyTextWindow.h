#pragma once
#include "LazyWindow.h"
#include <memory>

class QPlainTextEdit;
class QLabel;
namespace dzurkel {
// Bounds rendered content as well as cached transport payloads. Edits preserve
// live QTextCursor anchors instead of rebuilding the document on each load.
class LazyTextWindow final : public QObject {
public:
    LazyTextWindow(QPlainTextEdit *editor, QLabel *status,
                   std::shared_ptr<LazyCoordinator> coordinator, const LazyPortion &first);
    void showBeginning();
    void showEnd();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void jump(bool end);
    void accept(const LazyPortion &portion, const LazyPortion &evicted, int direction);
    QPlainTextEdit *editor_;
    QLabel *status_;
    LazyWindow *window_;
    LazyBoundaryWatcher *watcher_;
    bool jumpToEnd_ = false;
    bool failed_ = false;
};
}

#pragma once
#include <QWidget>

class QLabel;
class QVBoxLayout;
namespace dzurkel {
struct FileResource;
class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow(bool diagnostics = false);
    explicit PreviewWindow(const FileResource &resource, bool diagnostics = false);
    void setResource(const FileResource &resource);
    // Takes ownership; replacing content destroys its widgets/documents.
    void setContent(QWidget *content);
    void setStatus(const QString &status);
    QSize previewSize() const;
signals:
    void dismissed();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    bool diagnostics_;
    QWidget *contentHost_;
    QWidget *content_ = nullptr;
    QVBoxLayout *contentLayout_;
    QLabel *status_;
};
} // namespace dzurkel

#pragma once

#include <QWidget>

namespace dzurkel {
struct FileResource;

class PreviewWindow : public QWidget {
public:
    explicit PreviewWindow(bool diagnostics = false);
    explicit PreviewWindow(const FileResource &resource, bool diagnostics = false);
    void setResource(const FileResource &resource);

protected:
    void changeEvent(QEvent *event) override;

private:
    bool diagnostics_;
};

} // namespace dzurkel

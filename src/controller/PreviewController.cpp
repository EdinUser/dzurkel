#include "PreviewController.h"
#include "resources/FileResource.h"

namespace dzurkel {
PreviewController::PreviewController(bool diagnostics) : window_(diagnostics) {}

bool PreviewController::show(const QString &url, QString &error, ResourceError *failure)
{
    FileResource resource;
    if (!resolveFileResource(url, resource, error, failure)) return false;
    window_.setResource(resource);
    fileUrl_ = resource.url.toString(QUrl::FullyEncoded);
    window_.show();
    return true;
}
void PreviewController::hide() { window_.hide(); }
bool PreviewController::toggle(const QString &url, QString &error)
{
    if (isVisible()) {
        hide();
        error.clear();
        return true;
    }
    return show(url, error);
}
bool PreviewController::isVisible() const { return window_.isVisible(); }
QString PreviewController::fileUrl() const { return fileUrl_; }
} // namespace dzurkel

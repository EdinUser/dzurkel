#include "PreviewService.h"
#include "controller/PreviewController.h"
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>

namespace dzurkel {
PreviewService::PreviewService(PreviewController &controller) : controller_(controller) {}
bool PreviewService::isVisible() const { return controller_.isVisible(); }
QString PreviewService::fileUrl() const { return controller_.fileUrl(); }

void PreviewService::open(const QString &url, bool toggle)
{
    // Visibility decides Toggle; an unused URL must not prevent dismissal.
    if (toggle && controller_.isVisible()) {
        controller_.hide();
        return;
    }
    const QUrl parsed(url, QUrl::StrictMode);
    ResourceError failure = ResourceError::InvalidUrl;
    QString error;
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        error = QStringLiteral("Expected an absolute file:// URL.");
    } else if (controller_.show(url, error, &failure)) {
        return;
    }
    QString suffix;
    switch (failure) {
    case ResourceError::UnsupportedScheme: suffix = QStringLiteral("UnsupportedScheme"); break;
    case ResourceError::NotFound: suffix = QStringLiteral("NotFound"); break;
    case ResourceError::NotRegularFile: suffix = QStringLiteral("NotRegularFile"); break;
    case ResourceError::NotReadable: suffix = QStringLiteral("NotReadable"); break;
    default: suffix = QStringLiteral("InvalidUrl"); break;
    }
    if (calledFromDBus()) sendErrorReply(QStringLiteral("io.github.EdinUser.Dzurkel.Error.") + suffix, error);
}
void PreviewService::Show(const QString &url) { open(url, false); }
void PreviewService::Hide() { controller_.hide(); }
void PreviewService::Toggle(const QString &url) { open(url, true); }
void PreviewService::Quit()
{
    // Return the D-Bus acknowledgement before ending the event loop.
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}
} // namespace dzurkel

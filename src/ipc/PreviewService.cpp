#include "PreviewService.h"
#include "NautilusPreviewService.h"
#include "controller/PreviewController.h"
#include "desktop/DesktopIntegration.h"
#include <QCoreApplication>
#include <QDBusMessage>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace dzurkel {
PreviewService::PreviewService(PreviewController &controller, NautilusPreviewService *nautilus)
    : controller_(controller), nautilus_(nautilus)
{
    chooserWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&chooserWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &caller) {
        if (caller != chooserCaller_) return;
        const bool owns = controller_.chooserOwner() == chooserKey();
        finishChooser(QStringLiteral("disconnected"), false);
        if (owns) controller_.hide();
    });
    connect(&controller_, &PreviewController::stateChanged, this, [this] {
        if (changingChooser_ || chooserCaller_.isEmpty()) return;
        if (controller_.chooserOwner() != chooserKey() || !controller_.isVisible())
            finishChooser(controller_.isVisible() ? QStringLiteral("superseded") : QStringLiteral("hidden"), !returningFocus_);
    });
    connect(controller_.window(), &PreviewWindow::dismissalStarting, this, [this] {
        if (chooserCaller_.isEmpty() || controller_.chooserOwner() != chooserKey()) return;
        const auto caller = chooserCaller_, session = chooserSession_;
        const auto bus = chooserConnection_;
        const auto generation = controller_.generation();
        returningFocus_ = true;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, caller, session, bus, generation] {
            const bool dismissed = !controller_.isVisible() && controller_.generation() == generation + 1;
            auto signal = QDBusMessage::createTargetedSignal(caller, QString::fromLatin1(ObjectPath),
                QString::fromLatin1(InterfaceName), QStringLiteral("ChooserClosed"));
            signal << session << (dismissed ? QStringLiteral("dismissed") : QStringLiteral("superseded"))
                << (dismissed ? watcher->result() : QString());
            bus.send(signal);
            watcher->deleteLater();
        });
        watcher->setFuture(settingsActivationToken(controller_.window()->windowHandle()));
    });
}
QString PreviewService::chooserKey() const { return chooserCaller_ + QChar('/') + chooserSession_; }
void PreviewService::finishChooser(const QString &reason, bool notify)
{
    if (notify && !chooserCaller_.isEmpty()) {
        auto signal = QDBusMessage::createTargetedSignal(chooserCaller_, QString::fromLatin1(ObjectPath),
            QString::fromLatin1(InterfaceName), QStringLiteral("ChooserClosed"));
        signal << chooserSession_ << reason << QString();
        chooserConnection_.send(signal);
    }
    chooserWatcher_.setWatchedServices({});
    chooserCaller_.clear(); chooserSession_.clear(); returningFocus_ = false;
}
void PreviewService::ShowFromChooser(const QString &session, const QString &url, const QVariantMap &options)
{
    if (!calledFromDBus()) return;
    static const QRegularExpression validSession(QStringLiteral("\\A[A-Za-z0-9_-]{1,128}\\z"));
    const auto token = options.value(QStringLiteral("activation-token"));
    if (!validSession.match(session).hasMatch() || !QUrl(url, QUrl::StrictMode).isValid()
        || QUrl(url).scheme().isEmpty() || (token.isValid()
        && (token.metaType().id() != QMetaType::QString || token.toString().size() > 16384))) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid chooser session, URL or activation token."));
        return;
    }
    const auto caller = message().service();
    QString error;
    ResourceError failure = ResourceError::InvalidUrl;
    changingChooser_ = true;
    const bool shown = controller_.show(url, error, &failure, token.toString(), true, false, caller + QChar('/') + session);
    changingChooser_ = false;
    if (!shown) {
        sendErrorReply(QStringLiteral("io.github.EdinUser.Dzurkel.Error.InvalidChooserResource"), error);
        return;
    }
    // Refreshes from one dialog retain its lease; another caller takes ownership.
    if (caller != chooserCaller_ || session != chooserSession_) finishChooser(QStringLiteral("superseded"));
    chooserConnection_ = connection();
    chooserCaller_ = caller; chooserSession_ = session;
    chooserWatcher_.setConnection(connection());
    chooserWatcher_.setWatchedServices({caller});
}
void PreviewService::HideFromChooser(const QString &session)
{
    if (!calledFromDBus() || message().service() != chooserCaller_ || session != chooserSession_
        || controller_.chooserOwner() != chooserKey()) return;
    finishChooser(QStringLiteral("closed"));
    controller_.hide();
}
bool PreviewService::isVisible() const { return controller_.isVisible(); }
QString PreviewService::fileUrl() const { return controller_.fileUrl(); }
QString PreviewService::previewKind() const { return controller_.previewKind(); }
bool PreviewService::loading() const { return controller_.loading(); }

void PreviewService::open(const QString &url, bool toggle, const QVariantMap &options)
{
    // Visibility decides Toggle; an unused URL must not prevent dismissal.
    if (toggle && controller_.isVisible()) {
        controller_.hide();
        return;
    }
    const auto token = options.value(QStringLiteral("activation-token"));
    if (token.isValid() && (token.metaType().id() != QMetaType::QString || token.toString().size() > 16384)) {
        if (calledFromDBus()) sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
            QStringLiteral("activation-token must be a string of at most 16384 characters."));
        return;
    }
    const QUrl parsed(url, QUrl::StrictMode);
    ResourceError failure = ResourceError::InvalidUrl;
    QString error;
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        error = QStringLiteral("Expected an absolute supported resource URL.");
    } else if (controller_.show(url, error, &failure, token.toString())) {
        return;
    }
    QString suffix;
    switch (failure) {
    case ResourceError::UnsupportedScheme: suffix = QStringLiteral("UnsupportedScheme"); break;
    case ResourceError::NotFound: suffix = QStringLiteral("NotFound"); break;
    case ResourceError::NotReadable: suffix = QStringLiteral("NotReadable"); break;
    default: suffix = QStringLiteral("InvalidUrl"); break;
    }
    if (calledFromDBus()) sendErrorReply(QStringLiteral("io.github.EdinUser.Dzurkel.Error.") + suffix, error);
}
void PreviewService::Show(const QString &url) { open(url, false); }
void PreviewService::ShowWithOptions(const QString &url, const QVariantMap &options) { open(url, false, options); }
void PreviewService::Hide() { controller_.hide(); }
void PreviewService::Toggle(const QString &url) { open(url, true); }
void PreviewService::ToggleWithOptions(const QString &url, const QVariantMap &options) { open(url, true, options); }
void PreviewService::ShowSettings(const QVariantMap &options)
{
    const auto token = options.value(QStringLiteral("activation-token"));
    if (token.isValid() && (token.metaType().id() != QMetaType::QString || token.toString().size() > 16384)) {
        if (calledFromDBus()) sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
            QStringLiteral("activation-token must be a string of at most 16384 characters."));
        return;
    }
    controller_.showSettings(token.toString());
}
void PreviewService::Quit()
{
    // Return the D-Bus acknowledgement before ending the event loop.
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}
void PreviewService::StartNautilusPreviewer()
{
    QString error;
    if (!nautilus_ || !nautilus_->start(error)) {
        if (calledFromDBus()) sendErrorReply(QStringLiteral("io.github.EdinUser.Dzurkel.Error.NautilusUnavailable"),
            error.isEmpty() ? QStringLiteral("Nautilus preview service is unavailable.") : error);
    }
}
} // namespace dzurkel

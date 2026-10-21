#include "WindowPlacement.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrentRun>

namespace dzurkel {
WindowPlacement::WindowPlacement(QObject *parent) : QObject(parent)
{
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) return;
    // KWin's geometry transaction keeps mapped previews on-screen without
    // remapping them. Other Wayland compositors still need ordinary Qt resizes.
    const auto testAddress = qEnvironmentVariable("DZURKEL_TEST_KWIN_BUS_ADDRESS");
    const auto connection = QStringLiteral("dzurkel-placement-capability-")
        + QUuid::createUuid().toString(QUuid::Id128);
    {
        const auto bus = testAddress.isEmpty() ? QDBusConnection::sessionBus()
            : QDBusConnection::connectToBus(testAddress, connection);
        if (const auto interface = bus.interface()) {
            const QDBusReply<bool> registered = interface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
            available_ = registered.isValid() && registered.value();
        }
    }
    if (!testAddress.isEmpty()) QDBusConnection::disconnectFromBus(connection);
}
WindowPlacement::~WindowPlacement() { cancel(); }
void WindowPlacement::cancel()
{
    ++*revision_;
    pending_.reset();
}
void WindowPlacement::request(const QString &title, const QSize &clientSize,
                              const QSize &imageSize, const QSize &chrome)
{
    pending_ = Request{title, clientSize, imageSize, chrome, ++*revision_};
    if (!running_) start();
}
void WindowPlacement::start()
{
    if (!pending_) return;
    const auto request = *pending_;
    pending_.reset();
    running_ = true;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        if (!watcher->result() && !warned_) {
            warned_ = true;
            qWarning() << "KWin placement unavailable; keeping the visible preview geometry.";
        }
        watcher->deleteLater();
        running_ = false;
        start();
    });
    const auto revision = revision_;
    const auto pid = QCoreApplication::applicationPid();
    // Only the opt-in compositor test uses a private app bus with a real KWin.
    const auto testAddress = qEnvironmentVariable("DZURKEL_TEST_KWIN_BUS_ADDRESS");
    watcher->setFuture(QtConcurrent::run([request, revision, pid, testAddress] {
        if (revision->load() != request.revision) return true;
        const QString plugin = QStringLiteral("dzurkel-placement-") + QUuid::createUuid().toString(QUuid::Id128);
        const QString connectionName = plugin;
        bool success = false;
        {
            const auto bus = testAddress.isEmpty() ? QDBusConnection::sessionBus()
                : QDBusConnection::connectToBus(testAddress, connectionName);
            const auto call = [&bus](const QString &path, const QString &interface,
                                     const QString &method, const QList<QVariant> &arguments) {
                auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), path, interface, method);
                message.setArguments(arguments);
                return bus.call(message, QDBus::Block, 1000);
            };
            const QJsonObject parameters{{"pid", pid}, {"title", request.title},
                {"width", request.clientSize.width()}, {"height", request.clientSize.height()},
                {"imageWidth", request.imageSize.width()}, {"imageHeight", request.imageSize.height()},
                {"chromeWidth", request.chrome.width()}, {"chromeHeight", request.chrome.height()}};
            // JSON serialization is essential: titles contain untrusted filenames.
            const QByteArray source = "const p = " + QJsonDocument(parameters).toJson(QJsonDocument::Compact) + R"JS(;
// Caption changes travel over Wayland independently of this D-Bus request.
// Prefer the caption, but allow the sole eligible window of this process while
// that caption is still in flight. Never select another process or a dialog.
const candidates = workspace.stackingOrder.filter(function(w) {
    return w.pid === p.pid && w.normalWindow && !w.dialog && !w.deleted && !w.hidden
        && !w.minimized && !w.fullScreen && !w.move && !w.resize;
});
const matches = candidates.filter(function(w) {
    return w.caption === p.title || w.caption.indexOf(p.title + " — ") === 0;
});
const targets = matches.length === 1 ? matches : candidates.length === 1 ? candidates : [];
targets.forEach(function(w) {
    const f = w.frameGeometry, c = w.clientGeometry;
    const area = workspace.clientArea(KWin.MaximizeArea, w);
    const dw = Math.max(0, f.width - c.width), dh = Math.max(0, f.height - c.height);
    var width = p.width, height = p.height;
    if (p.imageWidth > 0 && p.imageHeight > 0) {
        const scale = Math.min(1, Math.max(1, area.width - dw - 32 - p.chromeWidth) / p.imageWidth,
                                  Math.max(1, area.height - dh - 32 - p.chromeHeight) / p.imageHeight);
        width = Math.max(400, Math.round(p.imageWidth * scale) + p.chromeWidth);
        height = Math.max(400, Math.round(p.imageHeight * scale) + p.chromeHeight);
    }
    width = Math.min(width + dw, area.width);
    height = Math.min(height + dh, area.height);
    const x = Math.max(area.x, Math.min(f.x + (f.width - width) / 2, area.x + area.width - width));
    const y = Math.max(area.y, Math.min(f.y + (f.height - height) / 2, area.y + area.height - height));
    w.frameGeometry = {x:Math.round(x), y:Math.round(y), width:width, height:height};
});
)JS";
            QTemporaryFile script;
            if (script.open() && script.write(source) == source.size() && script.flush()) {
                const QDBusReply<int> loaded = call(QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"),
                    QStringLiteral("loadScript"), {script.fileName(), plugin});
                if (loaded.isValid() && loaded.value() >= 0) {
                    if (revision->load() == request.revision) {
                        const auto ran = call(QStringLiteral("/Scripting/Script%1").arg(loaded.value()),
                            QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"), {});
                        success = ran.type() != QDBusMessage::ErrorMessage;
                    } else success = true;
                }
                // Also clean up after a timed-out load: KWin may have loaded
                // the script even when its reply did not arrive in time.
                call(QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"),
                    QStringLiteral("unloadScript"), {plugin});
            }
        }
        if (!testAddress.isEmpty()) QDBusConnection::disconnectFromBus(connectionName);
        return success;
    }));
}
}

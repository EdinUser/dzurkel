// Test-only observation/control. Never installed or compiled into dzurkel.
#include "application/InstanceCoordinator.h"
#include "controller/PreviewController.h"
#include "ipc/NautilusPreviewService.h"
#include "ipc/PreviewService.h"
#include <QApplication>
#include <QDBusConnection>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QWindow>
#include <cstdio>

using namespace dzurkel;
class GnomeTestProbe : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.EdinUser.Dzurkel.GnomeTest1")
public:
    GnomeTestProbe(PreviewController &controller, NautilusPreviewService &adapter)
        : controller_(controller), adapter_(adapter) {
        const_cast<PreviewWindow *>(controller.window())->installEventFilter(this);
    }
public slots:
    QString Enable() { QString error; adapter_.configure(true, error); return error; }
    QString Restore() { QString error; adapter_.configure(false, error); return error; }
    QString Snapshot() {
        const auto *window = controller_.window();
        const QJsonObject state{{"visible", controller_.isVisible()}, {"loading", controller_.loading()},
            {"kind", controller_.previewKind()},
            {"qtVersion", QString::fromLatin1(qVersion())},
            {"file", controller_.fileUrl()}, {"title", window->windowTitle()},
            {"active", window->isActiveWindow()}, {"mapped", window->isVisible()},
            {"maximized", window->isMaximized()}, {"gnome", window->usesGnomePresentation()},
            {"maps", maps_}, {"hides", hides_},
            {"client", QJsonArray{window->width(), window->height()}},
            {"maximum", QJsonArray{window->maximumWidth(), window->maximumHeight()}},
            {"qtWindow", QString::number(quintptr(window->windowHandle()), 16)}};
        return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (!event->spontaneous()) {
            if (event->type() == QEvent::Show) ++maps_;
            if (event->type() == QEvent::Hide) ++hides_;
        }
        return false;
    }
private:
    PreviewController &controller_;
    NautilusPreviewService &adapter_;
    int maps_ = 0, hides_ = 0;
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dzurkel-gnome-test"));
    app.setApplicationDisplayName(QStringLiteral("Dzurkel native test"));
    app.setDesktopFileName(QStringLiteral("dzurkel"));
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 2) return 2;
    // Unique synthetic pixels identify the client rectangle in compositor
    // captures. Qt/Wayland global coordinates are deliberately not observed.
    auto palette = app.palette();
    palette.setColor(QPalette::Window, QColor(213, 238, 252));
    palette.setColor(QPalette::Base, QColor(213, 238, 252));
    app.setPalette(palette);
    PreviewController controller;
    if (!controller.window()->usesGnomePresentation()) {
        std::fputs("Native GNOME presentation is required, not a skipped test.\n", stderr);
        return 2;
    }
    NautilusIntegration registration(qEnvironmentVariable("XDG_DATA_HOME"),
        qEnvironmentVariable("XDG_STATE_HOME") + QStringLiteral("/dzurkel"), app.arguments().at(1));
    NautilusPreviewService adapter(controller, registration);
    PreviewService service(controller, &adapter);
    InstanceCoordinator instance(QStringLiteral("dzurkel-gnome-test-primary"));
    QString error;
    if (instance.claim(service, error) != InstanceCoordinator::ClaimResult::Primary) return 3;
    if (registration.enabled() && !adapter.start(error)) return 4;
    GnomeTestProbe probe(controller, adapter);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/GnomeTest"), &probe, QDBusConnection::ExportAllSlots)
        || !bus.registerService(QStringLiteral("io.github.EdinUser.Dzurkel.GnomeTest"))) return 5;
    return app.exec();
}
#include "GnomeTestProbe.moc"

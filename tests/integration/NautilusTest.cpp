#include "application/CommandLine.h"
#include "controller/PreviewController.h"
#include "desktop/NautilusIntegration.h"
#include "ipc/NautilusPreviewService.h"
#include "ipc/PreviewService.h"
#include "preview/providers/FileInfoProvider.h"
#include "ui/SettingsDialog.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

using namespace dzurkel;
class NautilusTest : public QObject {
    Q_OBJECT
public slots:
    void propertiesChanged(const QString &, const QVariantMap &changed, const QStringList &) { properties_.append(changed); }
private:
    QList<QVariantMap> properties_;
    static bool put(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QByteArray get(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
    static NautilusIntegration registration(const QTemporaryDir &dir) {
        return {dir.filePath("data"), dir.filePath("state"), QStringLiteral(DZURKEL_EXECUTABLE)};
    }
    static PreviewData load(const FileResource &file, const QSize &, const Cancellation &) { return FileInfoProvider::describe(file); }
private slots:
    void detectsDesktopAndInstalledManager() {
        QCOMPARE(detectFileManager("ubuntu:GNOME", true, true), FileManagerIntegration::Nautilus);
        QCOMPARE(detectFileManager("KDE", true, true), FileManagerIntegration::Dolphin);
        QCOMPARE(detectFileManager("GNOME", true, false), FileManagerIntegration::Unavailable);
        QCOMPARE(detectFileManager("", false, true), FileManagerIntegration::Nautilus);
        QCOMPARE(detectFileManager("", true, true), FileManagerIntegration::Unavailable);
    }
    void serviceCommandHasNoFileAndIsExclusive() {
        Request request; QString error;
        QVERIFY(parseCommandLine({"dzurkel", "--nautilus-preview-service"}, request, error));
        QCOMPARE(request.command, Command::NautilusPreviewService);
        QVERIFY(!parseCommandLine({"dzurkel", "--nautilus-preview-service", "a.txt"}, request, error));
        QVERIFY(!parseCommandLine({"dzurkel", "--nautilus-preview-service", "--settings"}, request, error));
    }
    void restoresOriginalUserRegistrationAndPermissions() {
        QTemporaryDir dir; auto config = registration(dir); QString error;
        const QByteArray original("[D-BUS Service]\nName=org.gnome.NautilusPreviewer\nExec=/example/previous\n");
        QVERIFY(put(config.serviceFile(), original));
        QVERIFY(QFile::setPermissions(config.serviceFile(), QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY2(config.enable(error), qPrintable(error));
        QVERIFY(config.enabled()); QVERIFY(config.hasBackup());
        QVERIFY(config.enable(error));
        QVERIFY2(config.restore(error), qPrintable(error));
        QCOMPARE(get(config.serviceFile()), original);
        QCOMPARE(QFileInfo(config.serviceFile()).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions{});
        QVERIFY(!config.hasBackup()); QVERIFY(!config.enabled());
    }
    void refusesToOverwriteSubsequentEditsOrSymlinks() {
        QTemporaryDir dir; auto config = registration(dir); QString error;
        QVERIFY(config.enable(error));
        QVERIFY(put(config.serviceFile(), "user edit\n"));
        QVERIFY(!config.restore(error)); QVERIFY(!config.enable(error));
        QCOMPARE(get(config.serviceFile()), QByteArray("user edit\n"));
        QVERIFY(config.hasBackup());
        QTemporaryDir other; auto linked = registration(other);
        const QString target = other.filePath("target"); QVERIFY(put(target, "original\n"));
        QDir().mkpath(QFileInfo(linked.serviceFile()).absolutePath());
        QVERIFY(QFile::link(target, linked.serviceFile()));
        QVERIFY(!linked.enable(error)); QCOMPARE(get(target), QByteArray("original\n"));
    }
    void refusesDuplicateUserServiceNames() {
        QTemporaryDir dir; auto config = registration(dir); QString error;
        QVERIFY(put(dir.filePath("data/dbus-1/services/other.service"),
            "[D-BUS Service]\nName=org.gnome.NautilusPreviewer\nExec=/example/provider\n"));
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
    }
    void refusesOversizedOriginalWithoutCreatingABackup() {
        QTemporaryDir dir; auto config = registration(dir); QString error;
        const QByteArray original(65537, '#');
        QVERIFY(put(config.serviceFile(), original));
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QCOMPARE(get(config.serviceFile()), original);
    }
    void settingsEnablesAndRestoresNautilus() {
        QTemporaryDir dir; PreviewController controller(false, load);
        NautilusPreviewService service(controller, registration(dir));
        SettingsDialog dialog(nullptr, DolphinShortcuts{}, DolphinSession{}, {}, FileManagerIntegration::Nautilus, &service);
        QCOMPARE(dialog.findChild<QLabel *>("integrationTitle")->text(), QStringLiteral("Nautilus shortcut"));
        auto *enable = dialog.findChild<QPushButton *>("useSpace");
        auto *restore = dialog.findChild<QPushButton *>("restoreShortcuts");
        QVERIFY(enable->isEnabled()); enable->click();
        QVERIFY(service.running()); QVERIFY(service.integration().enabled()); QVERIFY(restore->isEnabled());
        restore->click();
        QVERIFY(!service.running()); QVERIFY(!service.integration().hasBackup());
        QVERIFY(!QFileInfo::exists(service.integration().serviceFile()));
    }
    void competingProviderIsPreserved() {
        QTemporaryDir dir; PreviewController controller(false, load);
        auto competitor = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "nautilus-competitor");
        QVERIFY(competitor.registerService(NautilusServiceName));
        {
            NautilusPreviewService service(controller, registration(dir)); QString error;
            QVERIFY(!service.configure(true, error));
            QVERIFY(!service.integration().hasBackup());
            QCOMPARE(competitor.interface()->serviceOwner(NautilusServiceName).value(), competitor.baseService());
        }
        competitor.unregisterService(NautilusServiceName);
        QDBusConnection::disconnectFromBus("nautilus-competitor");
    }
    void followsSelectionAndTogglesOnlyTheSameFile() {
        QTemporaryDir dir; auto config = registration(dir); QString error;
        const QString first = dir.filePath("a # % ü.txt"), second = dir.filePath("b.txt");
        QVERIFY(put(first, "one")); QVERIFY(put(second, "two"));
        const QString a = QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded);
        const QString b = QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded);
        PreviewController controller(false, load); NautilusPreviewService service(controller, config);
        QVERIFY2(service.configure(true, error), qPrintable(error));
        QSignalSpy selection(&service, &NautilusPreviewService::SelectionEvent);
        service.ShowFile(a, "wayland:test", true, {});
        QVERIFY(service.visible()); QCOMPARE(service.parentHandle(), QStringLiteral("wayland:test"));
        QTRY_VERIFY(!controller.loading());
        auto *window = const_cast<PreviewWindow *>(controller.window());
        QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(selection.count(), 1); QCOMPARE(selection.at(0).at(0).toUInt(), 2u);
        QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(selection.count(), 2); QCOMPARE(selection.at(1).at(0).toUInt(), 5u);
        QCOMPARE(controller.fileUrl(), a); // Files supplies the next selection.
        service.ShowFile(b, "wayland:test", true, {});
        QVERIFY(service.visible()); QCOMPARE(controller.fileUrl(), b);
        service.ShowFile(b, "wayland:test", true, {});
        QVERIFY(!service.visible());
        service.ShowFile(a, "wayland:test", false, {});
        QTRY_VERIFY(!controller.loading());
        QVERIFY(service.configure(false, error));
        QVERIFY(controller.isVisible()); QVERIFY(!controller.externalNavigation());
    }
    void standaloneRequestsDoNotBecomeNautilusPreviews() {
        QTemporaryDir dir; QString error; QVERIFY(put(dir.filePath("a.txt"), "one"));
        const auto url = QUrl::fromLocalFile(dir.filePath("a.txt")).toString();
        PreviewController controller(false, load); NautilusPreviewService service(controller, registration(dir));
        QVERIFY(service.configure(true, error));
        QVERIFY(controller.show(url, error)); QVERIFY(!service.visible());
        service.Close(); QVERIFY(controller.isVisible());
        service.ShowFile("smb://server/share/a.txt", {}, true, {});
        QVERIFY(!controller.externalNavigation()); QCOMPARE(controller.fileUrl(), url);
    }
    void exportsTheRealNautilusContract() {
        QTemporaryDir dir; QString error; QVERIFY(put(dir.filePath("wire.txt"), "one"));
        PreviewController controller(false, load); NautilusPreviewService service(controller, registration(dir));
        QVERIFY(service.configure(true, error));
        auto bus = QDBusConnection::sessionBus();
        properties_.clear();
        QVERIFY(bus.connect(NautilusServiceName, NautilusObjectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
            this, SLOT(propertiesChanged(QString,QVariantMap,QStringList))));
        auto message = QDBusMessage::createMethodCall(NautilusServiceName, NautilusObjectPath, NautilusInterfaceName, "ShowFile");
        message << QUrl::fromLocalFile(dir.filePath("wire.txt")).toString() << QStringLiteral("wayland:wire") << false << QString();
        QDBusPendingCallWatcher call(bus.asyncCall(message));
        QTRY_VERIFY(call.isFinished());
        QVERIFY2(!call.isError(), qPrintable(call.error().message()));
        QVERIFY(service.visible());
        QTRY_VERIFY(!properties_.isEmpty());
        QCOMPARE(properties_.first().value("Visible").toBool(), true);
        QCOMPARE(properties_.first().value("ParentHandle").toString(), QStringLiteral("wayland:wire"));
        auto property = QDBusMessage::createMethodCall(NautilusServiceName, NautilusObjectPath,
            "org.freedesktop.DBus.Properties", "Get");
        property << QString::fromLatin1(NautilusInterfaceName) << QStringLiteral("Visible");
        QDBusPendingCallWatcher result(bus.asyncCall(property));
        QTRY_VERIFY(result.isFinished());
        const QDBusPendingReply<QDBusVariant> value = result;
        QVERIFY(value.isValid()); QVERIFY(value.value().variant().toBool());
        auto invalid = message;
        invalid.setArguments({QStringLiteral("https://example.invalid/a"), QString(), false, QString()});
        QDBusPendingCallWatcher rejected(bus.asyncCall(invalid));
        QTRY_VERIFY(rejected.isFinished());
        QVERIFY(rejected.isError()); QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(dir.filePath("wire.txt")).toString());
    }
    void coldActivationAndResidentForwardingWithQuotedExecutable() {
        QTemporaryDir dir;
        const auto executable = dir.filePath(QStringLiteral("preview space ü ' \" $ ` % \\ binary"));
        QVERIFY(QFile::link(QStringLiteral(DZURKEL_EXECUTABLE), executable));
        NautilusIntegration config(dir.filePath("data"), dir.filePath("state/dzurkel"), executable);
        QString error; QVERIFY2(config.enable(error), qPrintable(error));
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_DATA_HOME", dir.filePath("data"));
        env.insert("XDG_CONFIG_HOME", dir.filePath("config"));
        env.insert("XDG_STATE_HOME", dir.filePath("state"));
        env.insert("QT_QPA_PLATFORM", "offscreen");
        QProcess client; client.setProcessEnvironment(env);
        client.start(QStringLiteral("dbus-run-session"), {QStringLiteral("--"), QCoreApplication::applicationFilePath(), QStringLiteral("--activation-client")});
        QVERIFY(client.waitForStarted()); QVERIFY(client.waitForFinished(15000));
        const QByteArray output = client.readAllStandardError() + client.readAllStandardOutput();
        QCOMPARE(client.exitStatus(), QProcess::NormalExit);
        QVERIFY2(client.exitCode() == 0, qPrintable(QStringLiteral("exit %1: %2").arg(client.exitCode()).arg(QString::fromUtf8(output))));
        QVERIFY(config.restore(error));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--activation-client"))) {
        auto bus = QDBusConnection::sessionBus();
        auto ping = QDBusMessage::createMethodCall(NautilusServiceName, NautilusObjectPath, "org.freedesktop.DBus.Peer", "Ping");
        const auto response = bus.call(ping, QDBus::Block, 8000);
        if (response.type() == QDBusMessage::ErrorMessage) {
            std::fprintf(stderr, "Ping failed: %s: %s\n", qPrintable(response.errorName()), qPrintable(response.errorMessage())); return 1;
        }
        const auto owner = bus.interface()->serviceOwner(ServiceName);
        if (!owner.isValid()) return 2;
        QProcess forward;
        forward.start(QStringLiteral(DZURKEL_EXECUTABLE), {QStringLiteral("--nautilus-preview-service")});
        if (!forward.waitForFinished(5000) || forward.exitCode() != 0) return 3;
        if (bus.interface()->serviceOwner(ServiceName).value() != owner.value()) return 4;
        const auto quit = QDBusMessage::createMethodCall(ServiceName, ObjectPath, InterfaceName, "Quit");
        return bus.call(quit).type() == QDBusMessage::ErrorMessage ? 5 : 0;
    }
    NautilusTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "NautilusTest.moc"

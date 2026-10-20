#include "desktop/ThunarIntegration.h"
#include "desktop/NautilusIntegration.h"
#include "ui/SettingsDialog.h"
#include <QDir>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QXmlStreamReader>

using namespace dzurkel;
namespace {
const QByteArray path = "<Actions>/ThunarActions/uca-action-io.github.EdinUser.Dzurkel.preview";
QByteArray get(const QString &name) { QFile file(name); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool put(const QString &name, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(name).absolutePath());
    QFile file(name);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
ThunarIntegration setup(const QTemporaryDir &dir)
{
    return {dir.filePath("config"), dir.filePath("state"), QStringLiteral("/preview path/dzurkel's # 100% app"), [] { return false; }};
}
QString command(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("command")) return reader.readElementText();
    }
    return {};
}
}
class ThunarTest : public QObject {
    Q_OBJECT
private slots:
    void detectsXfceWithoutChangingKdeOrGnome() {
        QCOMPARE(detectFileManager("XFCE", true, true, true), FileManagerIntegration::Thunar);
        QCOMPARE(detectFileManager("XFCE", true, true, false), FileManagerIntegration::Unavailable);
        QCOMPARE(detectFileManager("KDE", true, true, true), FileManagerIntegration::Dolphin);
        QCOMPARE(detectFileManager("ubuntu:GNOME", true, true, true), FileManagerIntegration::Nautilus);
        QCOMPARE(detectFileManager("", false, false, true), FileManagerIntegration::Thunar);
        QCOMPARE(detectFileManager("", true, false, true), FileManagerIntegration::Unavailable);
    }
    void freshSetupRestoresMissingFilesAndCanBeEnabledAgain() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        QVERIFY2(config.enable(error), qPrintable(error));
        QVERIFY(config.enabled()); QVERIFY(config.hasBackup());
        const auto first = get(config.actionsFile());
        QVERIFY(config.enable(error)); QCOMPARE(get(config.actionsFile()), first);
        QCOMPARE(command(first), QStringLiteral("'/preview path/dzurkel'\\''s # 100%% app' --show -- %f"));
        QVERIFY(first.contains("<range>1-1</range>"));
        QVERIFY(first.contains("<directories"));
        QVERIFY(!config.needsUpdate());
        QVERIFY2(config.restore(error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(config.actionsFile()));
        QVERIFY(!QFileInfo::exists(config.shortcutsFile()));
        QVERIFY(!config.hasBackup()); QVERIFY(!config.enabled());
        QVERIFY(config.enable(error)); QVERIFY(config.restore(error));
    }
    void restoresExactOriginalBytesAndPermissions() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        const QByteArray actions("<?xml version=\"1.0\"?><actions>\n<!-- keep -->\n<action><name>Other</name><unique-id>other</unique-id><command>other %f</command><other-files/></action></actions>\n");
        const QByteArray keys("; comment\n(gtk_accel_path \"other\" \"space\")\n(gtk_accel_path \"alternate\" \"<Primary>p\")\n");
        QVERIFY(put(config.actionsFile(), actions)); QVERIFY(put(config.shortcutsFile(), keys));
        QVERIFY(QFile::setPermissions(config.actionsFile(), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        const auto permissions = QFileInfo(config.actionsFile()).permissions();
        QVERIFY2(config.enable(error), qPrintable(error));
        QVERIFY(config.enabled()); QVERIFY(get(config.shortcutsFile()).contains("\"other\" \"\""));
        QVERIFY(get(config.shortcutsFile()).contains("\"alternate\" \"<Primary>p\""));
        QVERIFY2(config.restore(error), qPrintable(error));
        QCOMPARE(get(config.actionsFile()), actions); QCOMPARE(get(config.shortcutsFile()), keys);
        QCOMPARE(QFileInfo(config.actionsFile()).permissions(), permissions);
    }
    void upgradesLegacyActionAndPreservesOriginalRestoreAndUnrelatedEdits() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        const QByteArray originalActions = "<actions><!-- original --></actions>";
        const QByteArray originalKeys = "(gtk_accel_path \"other\" \"space\")\n";
        QVERIFY(put(config.actionsFile(), originalActions));
        QVERIFY(put(config.shortcutsFile(), originalKeys));
        QVERIFY(config.enable(error));
        auto legacy = get(config.actionsFile());
        legacy.replace("<directories/>", "");
        legacy.replace("Preview the selected file or folder", "Preview the selected file");
        QVERIFY(put(config.actionsFile(), legacy));
        const auto backupPath = dir.filePath("state/thunar-preview-backup.json");
        auto saved = QJsonDocument::fromJson(get(backupPath)).object();
        auto managed = QJsonDocument::fromJson(QByteArray::fromBase64(saved.value("managed").toString().toLatin1())).object();
        managed.remove("directories");
        managed.insert("description", "Preview the selected file");
        saved.insert("managed", QString::fromLatin1(QJsonDocument(managed).toJson(QJsonDocument::Compact).toBase64()));
        saved.insert("installedActions", QString::fromLatin1(legacy.toBase64()));
        QVERIFY(put(backupPath, QJsonDocument(saved).toJson()));
        QVERIFY(config.enabled()); QVERIFY(config.needsUpdate());
        legacy.replace("</actions>", "<action><unique-id>new</unique-id><name>Keep me</name></action></actions>");
        QVERIFY(put(config.actionsFile(), legacy));
        const auto keys = get(config.shortcutsFile());
        QVERIFY2(config.enable(error), qPrintable(error));
        QVERIFY(config.enabled()); QVERIFY(!config.needsUpdate());
        QVERIFY(get(config.actionsFile()).contains("<directories"));
        QVERIFY(get(config.actionsFile()).contains("Keep me"));
        QCOMPARE(get(config.shortcutsFile()), keys);
        const auto upgraded = get(config.actionsFile());
        QVERIFY(config.enable(error)); QCOMPARE(get(config.actionsFile()), upgraded);
        QVERIFY2(config.restore(error), qPrintable(error));
        QVERIFY(get(config.actionsFile()).contains("Keep me"));
        QVERIFY(!get(config.actionsFile()).contains("<directories"));
        QCOMPARE(get(config.shortcutsFile()), originalKeys);
    }
    void retainsSystemDefaultsWhenCreatingUserActions() {
        QTemporaryDir dir; QString error;
        const auto defaults = dir.filePath("system/uca.xml");
        QVERIFY(put(defaults, "<actions><action><unique-id>terminal</unique-id><name>Terminal</name><command>terminal %f</command></action></actions>"));
        ThunarIntegration config(dir.filePath("config"), dir.filePath("state"), "/example/dzurkel", [] { return false; }, defaults);
        QVERIFY(config.enable(error));
        QVERIFY(get(config.actionsFile()).contains("Terminal"));
        QVERIFY(config.restore(error));
        QVERIFY(!QFileInfo::exists(config.actionsFile()));
        QVERIFY(get(defaults).contains("Terminal"));
    }
    void preservesUnrelatedEditsWhenRestoring() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        QVERIFY(put(config.shortcutsFile(), "(gtk_accel_path \"old-space\" \"space\")\n"));
        QVERIFY(config.enable(error));
        auto xml = get(config.actionsFile());
        xml.replace("<action>", "<action>\n  ");
        xml.replace("<patterns>*</patterns>", "<submenu/><patterns>*</patterns>");
        xml.replace("</actions>", "<action><unique-id>new</unique-id><name>Keep me</name><command>other %f</command></action></actions>");
        QVERIFY(put(config.actionsFile(), xml));
        auto savedKeys = get(config.shortcutsFile());
        savedKeys.replace("(gtk_accel_path \"old-space\" \"\")", "; (gtk_accel_path \"old-space\" \"\")");
        QVERIFY(put(config.shortcutsFile(), savedKeys + "(gtk_accel_path \"new\" \"F9\")\n"));
        QVERIFY(config.enabled());
        QVERIFY2(config.restore(error), qPrintable(error));
        QVERIFY(get(config.actionsFile()).contains("Keep me"));
        QVERIFY(!get(config.actionsFile()).contains("Preview with Dzurkel"));
        QVERIFY(get(config.shortcutsFile()).contains("\"new\" \"F9\""));
        QVERIFY(get(config.shortcutsFile()).contains("\"old-space\" \"space\""));
        QVERIFY(!get(config.shortcutsFile()).contains(path));
        QVERIFY(config.enable(error)); QVERIFY(config.restore(error));
    }
    void refusesEditedManagedActionOrShortcutWithoutPartialRestore() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        QVERIFY(config.enable(error));
        const auto actions = get(config.actionsFile()), keys = get(config.shortcutsFile());
        auto edited = actions; edited.replace("Preview the selected file", "User edited this");
        QVERIFY(put(config.actionsFile(), edited));
        QVERIFY(!config.restore(error)); QVERIFY(!config.enable(error));
        QCOMPARE(get(config.actionsFile()), edited); QCOMPARE(get(config.shortcutsFile()), keys);
        QVERIFY(put(config.actionsFile(), actions));
        auto editedKeys = keys; editedKeys.replace("\"space\"", "\"F8\"");
        QVERIFY(put(config.shortcutsFile(), editedKeys));
        QVERIFY(!config.restore(error));
        QCOMPARE(get(config.actionsFile()), actions); QCOMPARE(get(config.shortcutsFile()), editedKeys);
        QVERIFY(config.hasBackup());
    }
    void refusesMalformedFilesDuplicatesAndSymlinks() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        QVERIFY(put(config.actionsFile(), "<actions><bad>"));
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QVERIFY(put(config.actionsFile(), "<actions/>"));
        QVERIFY(put(config.shortcutsFile(), "(unrecognized syntax)\n"));
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QVERIFY(put(config.shortcutsFile(), "(gtk_accel_path \"same\" \"space\")\n(gtk_accel_path \"same\" \"F9\")\n"));
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QVERIFY(QFile::remove(config.shortcutsFile()));
        QVERIFY(QFile::remove(config.actionsFile()));
        QVERIFY(put(dir.filePath("target"), "<actions/>"));
        QVERIFY(QFile::link(dir.filePath("target"), config.actionsFile()));
        QVERIFY(!config.enable(error)); QCOMPARE(get(dir.filePath("target")), QByteArray("<actions/>"));
        QVERIFY(!config.hasBackup());
    }
    void refusesIncompleteBackupWithoutChangingEitherFile() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        QVERIFY(config.enable(error));
        const auto actions = get(config.actionsFile()), keys = get(config.shortcutsFile());
        QVERIFY(put(dir.filePath("state/thunar-preview-backup.json"),
            "{\"version\":1,\"actions\":{},\"shortcuts\":{},\"changes\":[],\"managed\":\"\"}"));
        QVERIFY(!config.restore(error));
        QCOMPARE(get(config.actionsFile()), actions); QCOMPARE(get(config.shortcutsFile()), keys);
    }
    void restoresLargeValidFilesWhoseBackupExceedsTheInputLimit() {
        QTemporaryDir dir; auto config = setup(dir); QString error;
        const QByteArray actions = "<actions><!--" + QByteArray(500000, 'a') + "--></actions>";
        const QByteArray keys = "; " + QByteArray(500000, 'b') + '\n';
        QVERIFY(put(config.actionsFile(), actions)); QVERIFY(put(config.shortcutsFile(), keys));
        QVERIFY2(config.enable(error), qPrintable(error)); QVERIFY(config.enabled());
        QVERIFY(QFileInfo(dir.filePath("state/thunar-preview-backup.json")).size() > 2 * 1024 * 1024);
        QVERIFY2(config.restore(error), qPrintable(error));
        QCOMPARE(get(config.actionsFile()), actions); QCOMPARE(get(config.shortcutsFile()), keys);
    }
    void refusesChangesWhileThunarRuns() {
        QTemporaryDir dir; QString error; bool running = true;
        ThunarIntegration config(dir.filePath("config"), dir.filePath("state"), "/example/dzurkel", [&] { return running; });
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QVERIFY(!QFileInfo::exists(config.actionsFile()));
        running = false; QVERIFY(config.enable(error));
        const auto installed = get(config.actionsFile());
        running = true; QVERIFY(!config.restore(error));
        QCOMPARE(get(config.actionsFile()), installed); QVERIFY(config.hasBackup());
        running = false; QVERIFY(config.restore(error));
    }
    void processStartingDuringSetupLeavesFilesUntouched() {
        QTemporaryDir dir; QString error; int checks = 0;
        ThunarIntegration config(dir.filePath("config"), dir.filePath("state"), "/example/dzurkel", [&] { return ++checks > 1; });
        QVERIFY(!config.enable(error)); QVERIFY(!config.hasBackup());
        QVERIFY(!QFileInfo::exists(config.actionsFile()));
        QVERIFY(!QFileInfo::exists(config.shortcutsFile()));
    }
    void settingsProvidesThunarSetupAndRestore() {
        QTemporaryDir dir; auto config = setup(dir);
        SettingsDialog dialog(nullptr, DolphinShortcuts{}, DolphinSession{}, {}, FileManagerIntegration::Thunar, nullptr, config);
        QCOMPARE(dialog.findChild<QLabel *>("integrationTitle")->text(), QStringLiteral("Thunar shortcut"));
        auto *enable = dialog.findChild<QPushButton *>("useSpace");
        auto *restore = dialog.findChild<QPushButton *>("restoreShortcuts");
        QVERIFY(enable->isEnabled()); enable->click();
        QTRY_VERIFY(config.enabled());
        QTRY_VERIFY(restore->isEnabled()); restore->click();
        QTRY_VERIFY(!config.hasBackup());
        QTRY_VERIFY(enable->isEnabled());
        QVERIFY(!QFileInfo::exists(config.actionsFile()));
    }
    void cancelledRestartDoesNotChangeConfiguration() {
        QTemporaryDir dir;
        ThunarIntegration config(dir.filePath("config"), dir.filePath("state"), "/example/dzurkel", [] { return true; });
        SettingsDialog dialog(nullptr, DolphinShortcuts{}, DolphinSession{}, {}, FileManagerIntegration::Thunar, nullptr, config);
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->reject();
        });
        timer.start(10);
        dialog.findChild<QPushButton *>("useSpace")->click();
        QVERIFY(!config.hasBackup()); QVERIFY(!QFileInfo::exists(config.actionsFile()));
    }
    void restartFocusWaitsForWindowAndCancels_data() {
        QTest::addColumn<int>("cancel");
        QTest::newRow("once-after-window-is-active") << 0;
        QTest::newRow("closed-and-reopened") << 1;
        QTest::newRow("minimized-and-restored") << 2;
        QTest::newRow("destroyed") << 3;
    }
    void restartFocusWaitsForWindowAndCancels() {
        QFETCH(int, cancel);
        QTemporaryDir dir;
        const auto running = dir.filePath("running");
        QVERIFY(put(running, "running"));
        const auto program = dir.filePath("thunar");
        const auto quoted = "'" + running.toUtf8().replace("'", "'\\''") + "'";
        QVERIFY(put(program, "#!/bin/sh\nif [ \"$1\" = --quit ]; then rm -f " + quoted + "; else touch " + quoted + "; fi\n"));
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto oldPath = qgetenv("PATH");
        const auto restorePath = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });
        qputenv("PATH", dir.path().toUtf8() + ':' + oldPath);
        bool active = false;
        ThunarIntegration config(dir.filePath("config"), dir.filePath("state"), "/example/dzurkel",
            [running] { return QFileInfo::exists(running); }, {}, [&active] { return active; });
        auto dialog = std::make_unique<SettingsDialog>(nullptr, DolphinShortcuts{}, DolphinSession{},
            std::function<bool()>{}, FileManagerIntegration::Thunar, nullptr, config);
        QSignalSpy focus(dialog.get(), &SettingsDialog::restartFocusRequested);
        dialog->show();
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!message || message->objectName() != QLatin1String("thunarRestartConfirmation")) return;
            for (auto *button : message->buttons())
                if (message->buttonRole(button) == QMessageBox::AcceptRole) { button->click(); return; }
        });
        answer.start(10);
        dialog->findChild<QPushButton *>("useSpace")->click();
        QTRY_VERIFY(config.enabled());
        QTRY_VERIFY(dialog->findChild<QPushButton *>("restoreShortcuts")->isEnabled());
        QTRY_VERIFY(QFileInfo::exists(running));
        QCOMPARE(focus.count(), 0); // Process readiness alone must not activate.
        if (cancel == 1) { dialog->hide(); dialog->show(); }
        if (cancel == 2) { dialog->showMinimized(); QTRY_VERIFY(dialog->isMinimized()); dialog->showNormal(); }
        if (cancel == 3) dialog.reset();
        active = true;
        if (!cancel) QTRY_COMPARE(focus.count(), 1);
        QTest::qWait(200);
        QCOMPARE(focus.count(), cancel ? 0 : 1);
        if (dialog) QVERIFY(!(dialog->windowFlags() & Qt::WindowStaysOnTopHint));
    }
    void realThunarRestartReturnsFocusToSettings() {
        if (!qEnvironmentVariableIsSet("DZURKEL_TEST_THUNAR_RESTART")) QSKIP("Opt-in native XFCE/X11 restart test");
        const auto root = qEnvironmentVariable("XDG_CONFIG_HOME");
        QVERIFY(root.startsWith(QStringLiteral("/tmp/dzurkel-thunar-focus-")));
        QVERIFY(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").startsWith(QStringLiteral("unix:path=/tmp/")));
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("xcb"));
        const auto running = [] { return QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.xfce.Thunar")).value(); };
        QVERIFY(!running()); // Never terminate an inherited desktop file manager.
        ThunarIntegration config(root + "/Thunar", root + "/state", QCoreApplication::applicationDirPath() + "/dzurkel", running);
        QProcess original;
        original.start(QStringLiteral("thunar"), {root});
        QVERIFY(original.waitForStarted());
        QTRY_VERIFY(running());
        QTRY_VERIFY(config.activeWindow());
        SettingsDialog dialog(nullptr, DolphinShortcuts{}, DolphinSession{}, {}, FileManagerIntegration::Thunar, nullptr, config);
        QSignalSpy focus(&dialog, &SettingsDialog::restartFocusRequested);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!message || message->objectName() != QLatin1String("thunarRestartConfirmation")) return;
            for (auto *button : message->buttons())
                if (message->buttonRole(button) == QMessageBox::AcceptRole) { button->click(); return; }
        });
        answer.start(10);
        dialog.findChild<QPushButton *>("useSpace")->click();
        QTRY_VERIFY(config.enabled());
        QTRY_COMPARE(focus.count(), 1);
        QTRY_VERIFY(dialog.isActiveWindow());
        QTest::qWait(400);
        QCOMPARE(focus.count(), 1);
        QVERIFY(dialog.isActiveWindow());
        dialog.hide();
        QCOMPARE(QProcess::execute(QStringLiteral("thunar"), {QStringLiteral("--quit")}), 0);
        QTRY_VERIFY(!running());
        QString error;
        QVERIFY2(config.restore(error), qPrintable(error));
        original.waitForFinished(1000);
    }
};
QTEST_MAIN(ThunarTest)
#include "ThunarTest.moc"

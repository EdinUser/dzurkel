#include "desktop/DolphinSession.h"
#include "ui/SettingsDialog.h"
#include "application/RuntimeCompatibility.h"
#include <QApplication>
#include <QClipboard>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCheckBox>
#include "resources/ResourceAccessPolicy.h"
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTabWidget>
#include <QTextBrowser>
#include "preview/LibraryAvailability.h"
#include "preview/text/TextHighlighting.h"
#include <QtConcurrentRun>
#include <atomic>

using namespace dzurkel;
class FakeDolphin : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.dolphin.MainWindow")
public:
    std::atomic_bool alive{true};
    std::atomic_bool active{false};
    std::atomic_int activeQueries{0};
    int requests = 0;
    bool permit = true;
public slots:
    bool isActiveWindow() { ++activeQueries; return alive && active; }
    void quit() {
        ++requests;
        if (permit) QTimer::singleShot(200, this, [this] { alive = false; });
    }
};
class SettingsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void informationIsAccessibleAndClickable();
    void systemRequirementsUseBuildReport();
    void compatibilityDistinguishesCandidatesFromSupport();
    void compatibilityRejectsInvalidReports();
    void experimentalSftpIsExplicitAndPersistent();
    void libraryPreferencePersistsAndRechecks();
    void missingLibraryHasGuidanceAndRefreshes();
    void installationCommandsAreReleaseSpecific();
    void appliesAndRestoresWhenDolphinIsClosed();
    void explainsConflictsBeforeApplying();
    void cancellationLeavesConfigurationUntouched();
    void waitsForEveryWindowBeforeApplying();
    void refusedCloseTimesOutWithoutWriting();
    void unknownProcessPreventsSetup();
    void newProcessPreventsSetup();
    void reopenFailureKeepsAppliedChange();
    void restartFocusWaitsAndRunsOncePerChange();
    void pendingRestartFocusIsCancelled_data();
    void pendingRestartFocusIsCancelled();
    void readinessTimesOutAndCancels();
    void realDolphinClosesGracefully();
private:
    DolphinShortcuts shortcuts() const;
    DolphinSession session();
    void answer(QTimer &timer, bool accept);
    FakeDolphin first_, second_;
    QTemporaryDir directory_;
    QString service_ = QStringLiteral("org.kde.dolphin-DzurkelTest");
};
void SettingsTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
}
void SettingsTest::init()
{
    QString policyError;
    QVERIFY2(ResourceAccessPolicy::setExperimentalSftpEnabled(false, policyError), qPrintable(policyError));
    first_.alive = true; second_.alive = true;
    first_.permit = second_.permit = true;
    first_.requests = second_.requests = 0;
    first_.active = second_.active = false;
    first_.activeQueries = second_.activeQueries = 0;
    QFile::remove(directory_.filePath(QStringLiteral("dolphinui.rc")));
    QFile::remove(directory_.filePath(QStringLiteral("backup.json")));
    if (QByteArray(QTest::currentTestFunction()) == "realDolphinClosesGracefully") return;
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(service_));
    QVERIFY(bus.registerObject(QStringLiteral("/dolphin/Dolphin_1"), &first_, QDBusConnection::ExportAllSlots));
    QVERIFY(bus.registerObject(QStringLiteral("/dolphin/Dolphin_2"), &second_, QDBusConnection::ExportAllSlots));
}
void SettingsTest::experimentalSftpIsExplicitAndPersistent()
{
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    auto *option = dialog.findChild<QCheckBox *>(QStringLiteral("experimentalSftp"));
    QVERIFY(option);
    QVERIFY(!option->isChecked());
    QVERIFY(!ResourceAccessPolicy::experimentalSftpEnabled());
    dialog.show();
    option->setFocus();
    QTest::keyClick(option, Qt::Key_Space);
    QVERIFY(option->isChecked());
    QVERIFY(ResourceAccessPolicy::experimentalSftpEnabled());
    SettingsDialog reopened(nullptr, shortcuts(), session(), [] { return true; });
    QVERIFY(reopened.findChild<QCheckBox *>(QStringLiteral("experimentalSftp"))->isChecked());
    QTest::keyClick(option, Qt::Key_Space);
    QVERIFY(!ResourceAccessPolicy::experimentalSftpEnabled());
    reopened.show();
    QVERIFY(!reopened.findChild<QCheckBox *>(QStringLiteral("experimentalSftp"))->isChecked());
    QVERIFY(dialog.grab().save(QStringLiteral("settings-sftp.png")));
}
void SettingsTest::cleanup()
{
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QStringLiteral("/dolphin/Dolphin_1"));
    bus.unregisterObject(QStringLiteral("/dolphin/Dolphin_2"));
    bus.unregisterService(service_);
}
DolphinShortcuts SettingsTest::shortcuts() const
{
    return {directory_.filePath(QStringLiteral("dolphinui.rc")), directory_.filePath(QStringLiteral("backup.json"))};
}
DolphinSession SettingsTest::session()
{
    return DolphinSession(QDBusConnection::sessionBus(), [this] {
        return first_.alive || second_.alive ? QList<qint64>{QCoreApplication::applicationPid()} : QList<qint64>{};
    });
}
void SettingsTest::answer(QTimer &timer, bool accept)
{
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, this, [accept] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!message) return;
        if (message->objectName() == QStringLiteral("restartConfirmation") && accept) {
            message->grab().save(QStringLiteral("settings-restart-confirmation.png"));
            for (auto *button : message->buttons())
                if (message->buttonRole(button) == QMessageBox::AcceptRole) { button->click(); return; }
        } else message->reject();
    });
    timer.start();
}
void SettingsTest::informationIsAccessibleAndClickable()
{
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    dialog.show();
    for (const auto &name : {"shortcutInfo", "imageInfo", "documentInfo", "textInfo", "tableInfo", "bookInfo", "audioInfo", "videoInfo", "sftpInfo"}) {
        auto *button = dialog.findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY(button);
        QVERIFY(!button->toolTip().isEmpty());
        QVERIFY(!button->accessibleName().isEmpty());
        QCOMPARE(button->focusPolicy(), Qt::StrongFocus);
    }
    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    tabs->setCurrentIndex(1);
    for (const auto &name : {"imageInfo", "documentInfo", "textInfo", "tableInfo", "bookInfo", "audioInfo", "videoInfo"}) {
        auto *button = dialog.findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY(button->isVisible());
        QVERIFY(button->accessibleDescription().contains(QStringLiteral("Supported")));
    }
    QVERIFY(dialog.height() < 800);
    for (const auto &name : {"imageInfo", "documentInfo", "bookInfo", "tableInfo", "textInfo", "audioInfo", "videoInfo"}) {
        bool detailsSeen = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                detailsSeen = message->text().contains(QStringLiteral("Supported"));
                message->grab().save(QStringLiteral("settings-%1.png").arg(QString::fromLatin1(name)));
                message->accept();
            }
        });
        timer.start(10);
        dialog.findChild<QToolButton *>(QString::fromLatin1(name))->click();
        QVERIFY(detailsSeen);
    }
    QVERIFY(dialog.grab().save(QStringLiteral("settings-redesign.png")));
}
void SettingsTest::systemRequirementsUseBuildReport()
{
    const auto compatibility = RuntimeCompatibility::load();
    QVERIFY(compatibility.reportAvailable);
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    dialog.show();
    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
    QCOMPARE(tabs->currentIndex(), 0);
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("systemRequirementsSummary"));
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("systemRequirementsDetails"));
    QVERIFY(summary);
    QVERIFY(button);
    QVERIFY(summary->isVisible());
    QVERIFY(button->isVisible());
    QCOMPARE(summary->text(), compatibility.summary);
    QVERIFY(dialog.grab().save(QStringLiteral("settings-system-requirements.png")));
    bool detailsSeen = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
        auto *details = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!details || details->objectName() != QStringLiteral("compatibilityDetails")) return;
        auto *text = details->findChild<QTextBrowser *>(QStringLiteral("compatibilityDetailsText"));
        detailsSeen = text && text->toPlainText() == compatibility.details;
        details->grab().save(QStringLiteral("settings-os-compatibility.png"));
        details->accept();
    });
    timer.start(10);
    button->click();
    QVERIFY(detailsSeen);
}
void SettingsTest::compatibilityDistinguishesCandidatesFromSupport()
{
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/build-compatibility.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto report = QJsonDocument::fromJson(file.readAll()).object();
    report.insert(QStringLiteral("artifact_requirements"), QJsonObject{
        {"qt_base", "6.11.2"}, {"qt_pdf", "6.11.2"}, {"libzip", "1.7"}});
    report.insert(QStringLiteral("baseline_requirements"), QJsonObject{
        {"qt_base", "6.4"}, {"qt_pdf", "6.4"}, {"libzip", "1.7"}});
    report.insert(QStringLiteral("baseline_os_candidates"), QJsonArray{QJsonObject{
        {"distribution", "debian"}, {"earliest_reviewed_candidate", "12"}}});
    report.insert(QStringLiteral("artifact_os_candidates"), QJsonArray{
        QJsonObject{{"distribution", "debian"}, {"earliest_reviewed_candidate", QJsonValue::Null}},
        QJsonObject{{"distribution", "fedora"}, {"earliest_reviewed_candidate", "44"}},
        QJsonObject{{"distribution", "arch"}, {"earliest_reviewed_candidate", QJsonValue::Null}}
    });
    auto compatibility = RuntimeCompatibility::fromReport(QJsonDocument(report).toJson());
    QVERIFY(compatibility.reportAvailable);
    QVERIFY(compatibility.summary.contains(QStringLiteral("Qt 6.11.2")));
    QVERIFY(compatibility.details.contains(QStringLiteral("Debian: no minimum established for this build")));
    QVERIFY(compatibility.details.contains(QStringLiteral("Fedora: earliest reviewed dependency candidate: 44")));
    QVERIFY(compatibility.details.contains(QStringLiteral("package and desktop validation pending")));
    QVERIFY(compatibility.details.contains(QStringLiteral("Arch Linux: no minimum established")));
    QVERIFY(compatibility.details.contains(QStringLiteral("Qt 6.4")));
    QVERIFY(!compatibility.details.contains(QStringLiteral("validated minimum:")));
    auto candidates = report.value(QStringLiteral("artifact_os_candidates")).toArray();
    auto fedora = candidates[1].toObject();
    fedora.insert(QStringLiteral("supported_minimum"), QStringLiteral("44"));
    candidates[1] = fedora;
    report.insert(QStringLiteral("artifact_os_candidates"), candidates);
    compatibility = RuntimeCompatibility::fromReport(QJsonDocument(report).toJson());
    QVERIFY(compatibility.details.contains(QStringLiteral("Fedora: validated minimum: 44")));
}
void SettingsTest::compatibilityRejectsInvalidReports()
{
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/build-compatibility.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto valid = QJsonDocument::fromJson(file.readAll()).object();
    QVERIFY(RuntimeCompatibility::fromReport(QJsonDocument(valid).toJson()).reportAvailable);
    QList<QByteArray> invalid{QByteArray(), QByteArray("invalid JSON"), QByteArray("{}")};
    for (const auto &field : {"schema_version", "dzurkel_version", "artifact", "artifact_requirements", "artifact_os_candidates"}) {
        auto report = valid;
        report.remove(QLatin1String(field));
        invalid.append(QJsonDocument(report).toJson());
    }
    auto otherVersion = valid;
    otherVersion.insert(QStringLiteral("dzurkel_version"), QStringLiteral("0.0.0"));
    invalid.append(QJsonDocument(otherVersion).toJson());
    for (const auto &json : invalid) {
        const auto compatibility = RuntimeCompatibility::fromReport(json);
        QVERIFY(!compatibility.reportAvailable);
        QVERIFY(compatibility.summary.contains(QStringLiteral("unavailable")));
        QVERIFY(!compatibility.details.contains(QStringLiteral("validated minimum:")));
    }
}
void SettingsTest::libraryPreferencePersistsAndRechecks()
{
    QString error;
    QVERIFY(setHighlightingEnabled(true, error));
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
    tabs->setCurrentIndex(2);
    dialog.show();
    auto *toggle = dialog.findChild<QCheckBox *>(QStringLiteral("syntaxHighlighting"));
    QVERIFY(toggle);
    QCOMPARE(toggle->isEnabled(), highlightingAvailability().available);
    auto *highlightingStatus = dialog.findChild<QToolButton *>(QStringLiteral("highlightingLibraryStatus"));
    QVERIFY(highlightingStatus);
    const auto backendHealth = highlightingStatus->property("health").toInt();
    for (const auto &id : {"highlighting", "antiword", "pdf", "zip", "multimedia"}) {
        auto *status = dialog.findChild<QToolButton *>(QString::fromLatin1(id) + QStringLiteral("LibraryStatus"));
        QVERIFY(status);
        QVERIFY(!status->text().isEmpty());
        QVERIFY(!status->icon().isNull());
        QVERIFY(!status->accessibleDescription().isEmpty());
    }
    QCOMPARE(dialog.findChild<QToolButton *>(QStringLiteral("pdfLibraryStatus"))->property("health").toInt(),
        static_cast<int>(LibraryHealth::Available));
    if (toggle->isEnabled()) {
        toggle->click();
        QVERIFY(!highlightingEnabled());
        QCOMPARE(highlightingStatus->property("health").toInt(), backendHealth);
        SettingsDialog reopened(nullptr, shortcuts(), session(), [] { return true; });
        QVERIFY(!reopened.findChild<QCheckBox *>(QStringLiteral("syntaxHighlighting"))->isChecked());
    }
    auto *result = dialog.findChild<QLabel *>(QStringLiteral("libraryCheckResult"));
    QVERIFY(result);
    const auto initialResult = result->text();
    dialog.findChild<QPushButton *>(QStringLiteral("checkLibraries"))->click();
    QVERIFY(result->text() != initialResult);
    QVERIFY(result->text().contains(QStringLiteral("Checked at")));
    QVERIFY(result->text().contains(QStringLiteral("No changes found.")));
    QCOMPARE(toggle->isEnabled(), highlightingAvailability().available);
    QVERIFY(dialog.grab().save(QStringLiteral("settings-libraries.png")));
    QVERIFY(setHighlightingEnabled(true, error));
}
void SettingsTest::installationCommandsAreReleaseSpecific()
{
    QCOMPARE(libraryInstallationCommand(QStringLiteral("antiword"), QStringLiteral("fedora"), QStringLiteral("44")),
        QStringLiteral("sudo dnf install antiword"));
    QCOMPARE(libraryInstallationCommand(QStringLiteral("multimedia"), QStringLiteral("debian"), QStringLiteral("12")),
        QStringLiteral("sudo apt install libqt6multimedia6 libqt6multimediawidgets6"));
    QVERIFY(libraryInstallationCommand(QStringLiteral("highlighting"), QStringLiteral("debian"), QStringLiteral("12")).isEmpty());
    QVERIFY(libraryInstallationCommand(QStringLiteral("antiword"), QStringLiteral("fedora"), QStringLiteral("14")).isEmpty());
    QVERIFY(libraryInstallationCommand(QStringLiteral("antiword"), QStringLiteral("rhel"), QStringLiteral("9")).isEmpty());
    QVERIFY(libraryInstallationCommand(QStringLiteral("antiword"), QStringLiteral("unknown"), QStringLiteral("44")).isEmpty());
    QCOMPARE(libraryInstallationCommand(QStringLiteral("multimedia"), QStringLiteral("arch"), QStringLiteral("rolling")),
        QStringLiteral("sudo pacman -Syu qt6-multimedia"));
}
void SettingsTest::missingLibraryHasGuidanceAndRefreshes()
{
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"))->setCurrentIndex(2);
    dialog.show();
    auto *status = dialog.findChild<QToolButton *>(QStringLiteral("antiwordLibraryStatus"));
    QVERIFY(status);
    const auto originalHealth = status->property("health");
    const auto originalPath = qgetenv("PATH");
    qputenv("PATH", directory_.path().toUtf8());
    dialog.findChild<QPushButton *>(QStringLiteral("checkLibraries"))->click();
    const auto missingHealth = status->property("health").toInt();
    qputenv("PATH", originalPath);
    QCOMPARE(missingHealth, static_cast<int>(LibraryHealth::Missing));
    QVERIFY(status->accessibleDescription().contains(QStringLiteral("Install the antiword")));
    QVERIFY(status->accessibleDescription().contains(QStringLiteral("distribution release")));
    QVERIFY(dialog.grab().save(QStringLiteral("settings-library-missing.png")));
    bool guidanceSeen = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
        auto *details = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!details || details->objectName() != QStringLiteral("libraryDetails")) return;
        const auto text = details->findChild<QTextBrowser *>(QStringLiteral("libraryDetailsText"))->toPlainText();
        guidanceSeen = text.contains(QStringLiteral("Install the antiword")) && text.contains(QStringLiteral("cannot supply"));
        if (auto *copy = details->findChild<QPushButton *>(QStringLiteral("copyLibraryCommand"))) {
            // Offscreen clipboard is isolated; never overwrite the user's native clipboard.
            if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
                copy->click();
                guidanceSeen = guidanceSeen && QApplication::clipboard()->text() == status->property("installationCommand").toString();
            }
        }
        details->grab().save(QStringLiteral("settings-library-guidance.png"));
        details->accept();
    });
    timer.start(10);
    status->click();
    QVERIFY(guidanceSeen);
    dialog.findChild<QPushButton *>(QStringLiteral("checkLibraries"))->click();
    QCOMPARE(status->property("health"), originalHealth);
}
void SettingsTest::explainsConflictsBeforeApplying()
{
    QFile file(directory_.filePath(QStringLiteral("dolphinui.rc")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original = "<gui name=\"dolphin\"><ActionProperties><Action name=\"custom_action\" shortcut=\"Space; Ctrl+J\"/></ActionProperties></gui>";
    QCOMPARE(file.write(original), original.size());
    file.close();
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    dialog.show();
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("shortcutChanges"));
    QVERIFY(summary);
    QVERIFY(summary->isVisible());
    QVERIFY(summary->text().contains(QStringLiteral("custom_action")));
    QVERIFY(summary->text().contains(QStringLiteral("Select files")));
    QVERIFY(!shortcuts().hasBackup());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    QVERIFY(dialog.grab().save(QStringLiteral("settings-shortcut-conflicts.png")));
}
void SettingsTest::appliesAndRestoresWhenDolphinIsClosed()
{
    first_.alive = second_.alive = false;
    int reopened = 0;
    SettingsDialog dialog(nullptr, shortcuts(), session(), [&] { ++reopened; return true; });
    dialog.show();
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("useSpace"));
    QVERIFY(apply->isEnabled());
    apply->click();
    QTRY_VERIFY(shortcuts().usesSpace());
    auto *restore = dialog.findChild<QPushButton *>(QStringLiteral("restoreShortcuts"));
    QTRY_VERIFY(restore->isEnabled());
    restore->click();
    QTRY_VERIFY(!shortcuts().hasBackup());
    QVERIFY(!shortcuts().usesSpace());
    QCOMPARE(reopened, 0);
}
void SettingsTest::cancellationLeavesConfigurationUntouched()
{
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return true; });
    dialog.show();
    QTimer timer;
    answer(timer, false);
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("useSpace"));
    apply->click();
    QTRY_VERIFY(apply->isEnabled());
    QCOMPARE(first_.requests + second_.requests, 0);
    QVERIFY(!shortcuts().hasBackup());
    QVERIFY(!QFile::exists(directory_.filePath(QStringLiteral("dolphinui.rc"))));
}
void SettingsTest::waitsForEveryWindowBeforeApplying()
{
    int reopened = 0;
    bool stoppedBeforeReopen = false;
    SettingsDialog dialog(nullptr, shortcuts(), session(), [&] {
        ++reopened;
        stoppedBeforeReopen = !first_.alive && !second_.alive
            && shortcuts().usesSpace() == (reopened == 1);
        return true;
    });
    dialog.show();
    QTimer timer;
    answer(timer, true);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("useSpace"))->isEnabled());
    dialog.findChild<QPushButton *>(QStringLiteral("useSpace"))->click();
    QTRY_COMPARE(first_.requests, 1);
    QTRY_COMPARE(second_.requests, 1);
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(dialog.isVisible());
    QVERIFY(!shortcuts().hasBackup()); // both delayed close requests still pending
    QTRY_COMPARE(reopened, 1);
    QVERIFY(stoppedBeforeReopen);
    QVERIFY(shortcuts().usesSpace());
    QVERIFY(dialog.grab().save(QStringLiteral("settings-enabled.png")));
    first_.alive = second_.alive = true;
    dialog.findChild<QPushButton *>(QStringLiteral("restoreShortcuts"))->click();
    QTRY_COMPARE(reopened, 2);
    QVERIFY(stoppedBeforeReopen);
    QVERIFY(!shortcuts().hasBackup());
    QVERIFY(!shortcuts().usesSpace());
    QCOMPARE(first_.requests + second_.requests, 4);
}
void SettingsTest::refusedCloseTimesOutWithoutWriting()
{
    first_.permit = second_.permit = false;
    const auto controller = session();
    QFutureWatcher<DolphinSession::Snapshot> inspecting;
    inspecting.setFuture(QtConcurrent::run([controller] { return controller.inspect(); }));
    QTRY_VERIFY(inspecting.isFinished());
    const auto snapshot = inspecting.result();
    QVERIFY2(snapshot.error.isEmpty(), qPrintable(snapshot.error));
    QCOMPARE(snapshot.windows.size(), 2);
    QFutureWatcher<QString> closing;
    closing.setFuture(QtConcurrent::run([controller, snapshot] { return controller.closeAndWait(snapshot, 150); }));
    QTRY_VERIFY(closing.isFinished());
    QVERIFY(closing.result().contains(QStringLiteral("still running")));
    QVERIFY(!shortcuts().hasBackup());
    QCOMPARE(first_.requests + second_.requests, 2);
}
void SettingsTest::unknownProcessPreventsSetup()
{
    DolphinSession unknown(QDBusConnection::sessionBus(), [] { return QList<qint64>{999999999}; });
    SettingsDialog dialog(nullptr, shortcuts(), unknown, [] { return true; });
    dialog.show();
    QTimer timer;
    answer(timer, false);
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("useSpace"));
    apply->click();
    QTRY_VERIFY(apply->isEnabled());
    QVERIFY(!shortcuts().hasBackup());
    QCOMPARE(first_.requests + second_.requests, 0);
}
void SettingsTest::newProcessPreventsSetup()
{
    std::atomic_int scans = 0;
    DolphinSession racing(QDBusConnection::sessionBus(), [&] {
        return ++scans == 1 ? QList<qint64>{} : QList<qint64>{999999999};
    });
    SettingsDialog dialog(nullptr, shortcuts(), racing, [] { return true; });
    dialog.show();
    QTimer timer;
    answer(timer, false);
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("useSpace"));
    apply->click();
    QTRY_VERIFY(apply->isEnabled());
    QVERIFY(!shortcuts().hasBackup());
    QVERIFY(!QFile::exists(directory_.filePath(QStringLiteral("dolphinui.rc"))));
}
void SettingsTest::realDolphinClosesGracefully()
{
    if (!qEnvironmentVariableIsSet("DZURKEL_TEST_REAL_DOLPHIN")) QSKIP("Opt-in test of installed Dolphin on this isolated bus");
    QProcess dolphin;
    std::atomic_bool alive = true;
    connect(&dolphin, &QProcess::finished, this, [&] { alive = false; });
    dolphin.start(QStringLiteral("dolphin"), {QStringLiteral("--new-window"), directory_.path()});
    QVERIFY(dolphin.waitForStarted());
    const qint64 pid = dolphin.processId();
    DolphinSession real(QDBusConnection::sessionBus(), [&] { return alive ? QList<qint64>{pid} : QList<qint64>{}; });
    QTest::qWait(1000);
    QFutureWatcher<DolphinSession::Snapshot> inspecting;
    inspecting.setFuture(QtConcurrent::run([real] { return real.inspect(); }));
    QTRY_VERIFY(inspecting.isFinished());
    const auto snapshot = inspecting.result();
    QVERIFY2(snapshot.error.isEmpty(), qPrintable(snapshot.error));
    QVERIFY(!snapshot.windows.isEmpty());
    QFutureWatcher<QString> closing;
    closing.setFuture(QtConcurrent::run([real, snapshot] { return real.closeAndWait(snapshot, 5000); }));
    QTRY_VERIFY(closing.isFinished());
    QVERIFY2(closing.result().isEmpty(), qPrintable(closing.result()));
    QTRY_COMPARE(dolphin.state(), QProcess::NotRunning);
    QCOMPARE(dolphin.exitStatus(), QProcess::NormalExit);
}
void SettingsTest::reopenFailureKeepsAppliedChange()
{
    bool informed = false;
    SettingsDialog dialog(nullptr, shortcuts(), session(), [] { return false; });
    dialog.show();
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (message->text().contains(QStringLiteral("could not be reopened"))) informed = true;
    });
    answer(timer, true);
    dialog.findChild<QPushButton *>(QStringLiteral("useSpace"))->click();
    QTRY_VERIFY(informed);
    QVERIFY(shortcuts().usesSpace());
    QVERIFY(shortcuts().hasBackup());
}
void SettingsTest::restartFocusWaitsAndRunsOncePerChange()
{
    int reopened = 0;
    SettingsDialog dialog(nullptr, shortcuts(), session(), [&] {
        ++reopened;
        first_.alive = second_.alive = true;
        return true;
    });
    QSignalSpy focus(&dialog, &SettingsDialog::restartFocusRequested);
    dialog.show();
    QTimer timer;
    answer(timer, true);
    for (const auto &action : {"useSpace", "restoreShortcuts"}) {
        first_.active = false;
        const int previous = reopened;
        const int queries = first_.activeQueries;
        dialog.findChild<QPushButton *>(QString::fromLatin1(action))->click();
        QTRY_COMPARE(reopened, previous + 1);
        QTRY_VERIFY(first_.activeQueries > queries);
        QCOMPARE(focus.count(), previous); // D-Bus registration alone is too early.
        first_.active = true;
        QTRY_COMPARE(focus.count(), reopened);
        QTest::qWait(300);
        QCOMPARE(focus.count(), reopened);
        QVERIFY(!(dialog.windowFlags() & Qt::WindowStaysOnTopHint));
    }
    QVERIFY(!shortcuts().hasBackup());
}
void SettingsTest::pendingRestartFocusIsCancelled_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("closed-and-reopened") << 0;
    QTest::newRow("minimized-and-restored") << 1;
    QTest::newRow("destroyed") << 2;
}
void SettingsTest::pendingRestartFocusIsCancelled()
{
    QFETCH(int, action);
    auto dialog = std::make_unique<SettingsDialog>(nullptr, shortcuts(), session(), [&] {
        first_.alive = second_.alive = true;
        return true;
    });
    QSignalSpy focus(dialog.get(), &SettingsDialog::restartFocusRequested);
    dialog->show();
    QTimer timer;
    answer(timer, true);
    dialog->findChild<QPushButton *>(QStringLiteral("useSpace"))->click();
    QTRY_VERIFY(first_.activeQueries > 0);
    if (action == 0) { dialog->close(); dialog->show(); }
    else if (action == 1) { dialog->showMinimized(); dialog->showNormal(); }
    else dialog.reset();
    first_.active = true;
    QTest::qWait(350);
    QCOMPARE(focus.count(), 0);
    QVERIFY(shortcuts().usesSpace());
}
void SettingsTest::readinessTimesOutAndCancels()
{
    const auto controller = session();
    QFutureWatcher<bool> ready;
    ready.setFuture(QtConcurrent::run([controller] {
        return controller.waitForActiveWindow([] { return false; }, 150);
    }));
    QTRY_VERIFY(ready.isFinished());
    QVERIFY(!ready.result());
    QVERIFY(first_.activeQueries > 0);
    first_.activeQueries = 0;
    first_.active = true;
    ready.setFuture(QtConcurrent::run([controller] {
        return controller.waitForActiveWindow([] { return true; });
    }));
    QTRY_VERIFY(ready.isFinished());
    QVERIFY(!ready.result());
    QCOMPARE(first_.activeQueries.load(), 0);
}
int main(int argc, char **argv)
{
    QTemporaryDir environment;
    qputenv("XDG_CONFIG_HOME", (environment.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (environment.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (environment.path() + QStringLiteral("/cache")).toUtf8());
    const QString menu = environment.path() + QStringLiteral("/data/kio/servicemenus");
    QDir().mkpath(menu);
    QFile integration(menu + QStringLiteral("/dzurkel.desktop"));
    if (!integration.open(QIODevice::WriteOnly)) return 1;
    integration.write("[Desktop Entry]\nType=Service\n");
    integration.close();
    // Keep modal widgets inside this test process so Qt Test can answer them;
    // the application itself continues to use the platform's dialog backend.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    SettingsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "SettingsTest.moc"

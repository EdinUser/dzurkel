#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QScopeGuard>
#include <QSettings>

#include <memory>
#include <vector>

namespace {
const QString service = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer");
const QString path = QStringLiteral("/io/github/EdinUser/Dzurkel/Previewer");
const QString interface = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer1");
}

class ActivationProbe : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.EdinUser.Dzurkel.Previewer1")
public:
    QString url;
    QVariantMap options;
    QString method;
public slots:
    void ShowWithOptions(const QString &value, const QVariantMap &data)
    { url = value; options = data; method = QStringLiteral("ShowWithOptions"); }
    void ToggleWithOptions(const QString &value, const QVariantMap &data)
    { url = value; options = data; method = QStringLiteral("ToggleWithOptions"); }
};

class DBusTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void absentInstanceControlsAreHeadless();
    void settingsStartsWithoutFileAndReusesOwner();
    void visibleShowRequestsFreshActivation();
    void reusesPrimaryAndWindowLifecycle();
    void returnsResourceErrors();
    void handlesStartupRacesAndCrashRecovery();
    void forwardsActivationTokensWithoutGui();
    void validatesActivationOptions();
    void experimentalSftpPolicyAppliesToResidentService();
private:
    bool registered() const;
    uint ownerPid() const;
    QVariant property(const QString &name) const;
    QDBusMessage call(const QString &method, const QVariantList &arguments = {}) const;
    int cli(const QStringList &arguments, const QString &cwd = {});
    void startPrimary();
    QTemporaryDir directory_;
    QString first_;
    QString second_;
    QProcess primary_;
};

void DBusTest::forwardsActivationTokensWithoutGui()
{
    ActivationProbe probe;
    auto bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerObject(path, &probe, QDBusConnection::ExportAllSlots));
    const auto removeObject = qScopeGuard([&] { bus.unregisterObject(path); });
    QVERIFY(bus.registerService(service));
    const auto removeService = qScopeGuard([&] { bus.unregisterService(service); });
    for (const QString &command : {QStringLiteral("--show"), QStringLiteral("--toggle")}) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), QStringLiteral("synthetic-test-token"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("dzurkel_invalid_backend"));
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(DZURKEL_EXECUTABLE), {command, first_});
        QVERIFY(process.waitForStarted());
        // Dispatch incoming calls while the CLI waits for its reply.
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 7000);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(probe.url, QUrl::fromLocalFile(first_).toString(QUrl::FullyEncoded));
        QCOMPARE(probe.options.value(QStringLiteral("activation-token")).toString(), QStringLiteral("synthetic-test-token"));
        QCOMPARE(probe.method, command == QStringLiteral("--show")
            ? QStringLiteral("ShowWithOptions") : QStringLiteral("ToggleWithOptions"));
    }
}

void DBusTest::validatesActivationOptions()
{
    startPrimary();
    const QString previous = property(QStringLiteral("FileUrl")).toString();
    const QString next = QUrl::fromLocalFile(second_).toString(QUrl::FullyEncoded);
    for (const QVariant &invalid : {QVariant(123), QVariant(QString(16385, 'x'))}) {
        const auto reply = call(QStringLiteral("ShowWithOptions"),
            {next, QVariantMap{{QStringLiteral("activation-token"), invalid}}});
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
        QCOMPARE(property(QStringLiteral("FileUrl")).toString(), previous);
    }
    QCOMPARE(call(QStringLiteral("ShowWithOptions"), {next, QVariantMap{}}).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), next);
    // A visible Toggle still dismisses regardless of unused options or URL.
    QCOMPARE(call(QStringLiteral("ToggleWithOptions"), {QString(), QVariantMap{}}).type(), QDBusMessage::ReplyMessage);
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
}
void DBusTest::experimentalSftpPolicyAppliesToResidentService()
{
    startPrimary();
    const QString previous = property(QStringLiteral("FileUrl")).toString();
    const QString remote = QStringLiteral("sftp://user@127.0.0.1:9/file.txt");
    QCOMPARE(call(QStringLiteral("Show"), {remote}).errorName(),
        QStringLiteral("io.github.EdinUser.Dzurkel.Error.UnsupportedScheme"));
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), previous);
    QSettings configuration(QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral("dzurkel"), QStringLiteral("dzurkel"));
    configuration.setValue(QStringLiteral("resources/experimentalSftp"), true);
    configuration.sync();
    QCOMPARE(configuration.status(), QSettings::NoError);
    QCOMPARE(call(QStringLiteral("Show"), {remote}).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), remote);
    QCOMPARE(call(QStringLiteral("Hide")).type(), QDBusMessage::ReplyMessage);
    configuration.setValue(QStringLiteral("resources/experimentalSftp"), false);
    configuration.sync();
    QCOMPARE(cli({first_}), 0);
    const auto reply = call(QStringLiteral("Show"), {remote});
    QCOMPARE(reply.errorName(), QStringLiteral("io.github.EdinUser.Dzurkel.Error.UnsupportedScheme"));
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), previous);
    QVERIFY(property(QStringLiteral("Visible")).toBool());
}

bool DBusTest::registered() const
{
    const auto reply = QDBusConnection::sessionBus().interface()->isServiceRegistered(service);
    return reply.isValid() && reply.value();
}
uint DBusTest::ownerPid() const
{
    return QDBusConnection::sessionBus().interface()->servicePid(service).value();
}
QVariant DBusTest::property(const QString &name) const
{
    auto message = QDBusMessage::createMethodCall(service, path,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    message << interface << name;
    const QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(message);
    return reply.isValid() ? reply.value().variant() : QVariant();
}
QDBusMessage DBusTest::call(const QString &method, const QVariantList &arguments) const
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 5000);
}
int DBusTest::cli(const QStringList &arguments, const QString &cwd)
{
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    // A secondary invocation must work without access to a GUI backend.
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("dzurkel_invalid_backend"));
    process.setProcessEnvironment(env);
    if (!cwd.isEmpty()) process.setWorkingDirectory(cwd);
    process.start(QStringLiteral(DZURKEL_EXECUTABLE), arguments);
    if (!process.waitForFinished(7000)) return -1;
    if (process.exitStatus() != QProcess::NormalExit) return -2;
    return process.exitCode();
}
void DBusTest::startPrimary()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    primary_.setProcessEnvironment(env);
    primary_.start(QStringLiteral(DZURKEL_EXECUTABLE), {first_});
    QVERIFY(primary_.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(registered(), 5000);
    QTRY_VERIFY(property(QStringLiteral("Visible")).toBool());
    QTRY_VERIFY(!property(QStringLiteral("Loading")).toBool());
}
void DBusTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
    QVERIFY(QDBusConnection::sessionBus().isConnected());
    QVERIFY(!registered());
    first_ = directory_.filePath(QStringLiteral("first.txt"));
    second_ = directory_.filePath(QString::fromUtf8("second space пример #%.txt"));
    for (const auto &fileName : {first_, second_}) {
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
    }
}
void DBusTest::cleanup()
{
    if (registered()) call(QStringLiteral("Quit"));
    if (primary_.state() != QProcess::NotRunning && !primary_.waitForFinished(3000)) {
        primary_.kill();
        primary_.waitForFinished();
    }
    QTRY_VERIFY(!registered());
}
void DBusTest::absentInstanceControlsAreHeadless()
{
    QCOMPARE(cli({QStringLiteral("--hide")}), 0);
    QCOMPARE(cli({QStringLiteral("--quit")}), 0);
    QVERIFY(!registered());
}
void DBusTest::settingsStartsWithoutFileAndReusesOwner()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    primary_.setProcessEnvironment(env);
    primary_.start(QStringLiteral(DZURKEL_EXECUTABLE), {QStringLiteral("--settings")});
    QVERIFY(primary_.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(registered(), 5000);
    const uint pid = ownerPid();
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
    QVERIFY(property(QStringLiteral("FileUrl")).toString().isEmpty());
    QCOMPARE(cli({QStringLiteral("--settings")}), 0);
    QCOMPARE(ownerPid(), pid);
    QCOMPARE(cli({first_}), 0);
    QTRY_VERIFY(property(QStringLiteral("Visible")).toBool());
    const auto url = property(QStringLiteral("FileUrl")).toString();
    QCOMPARE(cli({QStringLiteral("--settings")}), 0);
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), url);
    QCOMPARE(ownerPid(), pid);
    const auto invalid = call(QStringLiteral("ShowSettings"),
        {QVariantMap{{QStringLiteral("activation-token"), 123}}});
    QCOMPARE(invalid.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
}
void DBusTest::visibleShowRequestsFreshActivation()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    primary_.setProcessEnvironment(env);
    primary_.start(QStringLiteral(DZURKEL_EXECUTABLE), {QStringLiteral("--diagnostics"), first_});
    QVERIFY(primary_.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(registered(), 5000);
    QTRY_VERIFY(property(QStringLiteral("Visible")).toBool());
    QTRY_VERIFY(!property(QStringLiteral("Loading")).toBool());
    const uint pid = ownerPid();
    primary_.readAllStandardError();
    const auto reply = call(QStringLiteral("ShowWithOptions"),
        {QUrl::fromLocalFile(second_).toString(QUrl::FullyEncoded),
         QVariantMap{{QStringLiteral("activation-token"), QStringLiteral("synthetic-refocus-token")}}});
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QByteArray log;
    QTRY_VERIFY((log += primary_.readAllStandardError()).contains("activation requested; token present: 1"));
    QCOMPARE(ownerPid(), pid);
    QTRY_VERIFY(!property(QStringLiteral("Loading")).toBool());
    QTest::qWait(200);
    log += primary_.readAllStandardError();
    // Loading completion must not replay the launch token and steal focus later.
    QCOMPARE(log.count("activation requested; token present: 1"), 1);
}
void DBusTest::reusesPrimaryAndWindowLifecycle()
{
    startPrimary();
    const uint pid = ownerPid();
    QCOMPARE(pid, uint(primary_.processId()));
    QCOMPARE(cli({QStringLiteral("--show"), QFileInfo(second_).fileName()}, directory_.path()), 0);
    QCOMPARE(ownerPid(), pid);
    QCOMPARE(property(QStringLiteral("FileUrl")).toString(), QUrl::fromLocalFile(second_).toString(QUrl::FullyEncoded));
    QCOMPARE(call(QStringLiteral("Hide")).type(), QDBusMessage::ReplyMessage);
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
    QCOMPARE(ownerPid(), pid);
    QCOMPARE(cli({QStringLiteral("--toggle"), first_}), 0);
    QVERIFY(property(QStringLiteral("Visible")).toBool());
    // Toggle is based on visibility, not file identity or validity.
    QCOMPARE(cli({QStringLiteral("--toggle"), directory_.filePath(QStringLiteral("missing"))}), 0);
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
    QCOMPARE(cli({QStringLiteral("--toggle"), directory_.filePath(QStringLiteral("missing"))}), 0);
    QTRY_COMPARE(property(QStringLiteral("PreviewKind")).toString(), QStringLiteral("information"));
    QVERIFY(property(QStringLiteral("Visible")).toBool());
    QCOMPARE(call(QStringLiteral("Hide")).type(), QDBusMessage::ReplyMessage);
    for (int i = 0; i < 10; ++i) {
        QCOMPARE(cli({first_}), 0);
        QVERIFY(property(QStringLiteral("Visible")).toBool());
        QCOMPARE(cli({QStringLiteral("--hide")}), 0);
        QVERIFY(!property(QStringLiteral("Visible")).toBool());
        QCOMPARE(ownerPid(), pid);
    }
    QCOMPARE(cli({QStringLiteral("--quit")}), 0);
    if (primary_.state() != QProcess::NotRunning) QVERIFY(primary_.waitForFinished(3000));
    QCOMPARE(primary_.exitStatus(), QProcess::NormalExit);
    QCOMPARE(primary_.exitCode(), 0);
    QVERIFY(!registered());
}
void DBusTest::returnsResourceErrors()
{
    startPrimary();
    const QString previous = property(QStringLiteral("FileUrl")).toString();
    const QList<QPair<QString, QString>> cases = {
        {QStringLiteral("not-a-url"), QStringLiteral("InvalidUrl")},
        {QStringLiteral("https://example.com/file"), QStringLiteral("UnsupportedScheme")},
        {QStringLiteral("file:///tmp/invalid%ZZ"), QStringLiteral("InvalidUrl")},
        {QStringLiteral("file://remote/tmp/test"), QStringLiteral("InvalidUrl")},
        {QStringLiteral("smb://user:password@server/share/file"), QStringLiteral("InvalidUrl")}
    };
    for (const auto &entry : cases) {
        const auto reply = call(QStringLiteral("Show"), {entry.first});
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("io.github.EdinUser.Dzurkel.Error.") + entry.second);
        QCOMPARE(property(QStringLiteral("FileUrl")).toString(), previous);
        QVERIFY(property(QStringLiteral("Visible")).toBool());
    }
    QCOMPARE(call(QStringLiteral("Show"), {QUrl::fromLocalFile(directory_.filePath(QStringLiteral("missing"))).toString()}).type(),
        QDBusMessage::ReplyMessage);
    QTRY_COMPARE(property(QStringLiteral("PreviewKind")).toString(), QStringLiteral("information"));
    QVERIFY(!property(QStringLiteral("Loading")).toBool());
    QFile file(second_);
    QVERIFY(file.setPermissions(QFileDevice::WriteOwner));
    const auto reply = call(QStringLiteral("Show"), {QUrl::fromLocalFile(second_).toString(QUrl::FullyEncoded)});
    QTRY_COMPARE(property(QStringLiteral("PreviewKind")).toString(), QStringLiteral("information"));
    QUrl localhost = QUrl::fromLocalFile(second_);
    localhost.setHost(QStringLiteral("localhost"));
    const auto localReply = call(QStringLiteral("Show"), {localhost.toString(QUrl::FullyEncoded)});
    QTRY_COMPARE(property(QStringLiteral("PreviewKind")).toString(), QStringLiteral("information"));
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(localReply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(call(QStringLiteral("Toggle"), {QStringLiteral("invalid")}).type(), QDBusMessage::ReplyMessage);
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
    QCOMPARE(call(QStringLiteral("Toggle"), {QStringLiteral("invalid")}).errorName(), QStringLiteral("io.github.EdinUser.Dzurkel.Error.InvalidUrl"));
}
void DBusTest::handlesStartupRacesAndCrashRecovery()
{
    std::vector<std::unique_ptr<QProcess>> contenders;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    for (int i = 0; i < 8; ++i) {
        auto process = std::make_unique<QProcess>();
        process->setProcessEnvironment(env);
        process->start(QStringLiteral(DZURKEL_EXECUTABLE), {i % 2 ? first_ : second_});
        contenders.push_back(std::move(process));
    }
    QTRY_VERIFY_WITH_TIMEOUT(registered(), 5000);
    const uint pid = ownerPid();
    QProcess *winner = nullptr;
    for (const auto &process : contenders) {
        if (uint(process->processId()) == pid) {
            winner = process.get();
        } else {
            if (process->state() != QProcess::NotRunning) QVERIFY(process->waitForFinished(7000));
            QCOMPARE(process->exitStatus(), QProcess::NormalExit);
            QCOMPARE(process->exitCode(), 0);
        }
    }
    QVERIFY(winner);
    QCOMPARE(winner->state(), QProcess::Running);
    QCOMPARE(cli({first_}), 0);
    QCOMPARE(ownerPid(), pid);
    winner->kill();
    QVERIFY(winner->waitForFinished());
    QTRY_VERIFY(!registered());
    startPrimary();
    QVERIFY(ownerPid() != pid);
    QCOMPARE(cli({QStringLiteral("--quit")}), 0);
    if (primary_.state() != QProcess::NotRunning) QVERIFY(primary_.waitForFinished(3000));
}

QTEST_GUILESS_MAIN(DBusTest)
#include "DBusTest.moc"

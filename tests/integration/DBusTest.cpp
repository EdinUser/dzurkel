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

#include <memory>
#include <vector>

namespace {
const QString service = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer");
const QString path = QStringLiteral("/io/github/EdinUser/Dzurkel/Previewer");
const QString interface = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer1");
}

class DBusTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void absentInstanceControlsAreHeadless();
    void reusesPrimaryAndWindowLifecycle();
    void returnsResourceErrors();
    void handlesStartupRacesAndCrashRecovery();
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
    QCOMPARE(cli({QStringLiteral("--toggle"), directory_.filePath(QStringLiteral("missing"))}), 1);
    QVERIFY(!property(QStringLiteral("Visible")).toBool());
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
        {QUrl::fromLocalFile(directory_.filePath(QStringLiteral("missing"))).toString(), QStringLiteral("NotFound")}
    };
    for (const auto &entry : cases) {
        const auto reply = call(QStringLiteral("Show"), {entry.first});
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("io.github.EdinUser.Dzurkel.Error.") + entry.second);
        QCOMPARE(property(QStringLiteral("FileUrl")).toString(), previous);
        QVERIFY(property(QStringLiteral("Visible")).toBool());
    }
    QFile file(second_);
    QVERIFY(file.setPermissions(QFileDevice::WriteOwner));
    const auto reply = call(QStringLiteral("Show"), {QUrl::fromLocalFile(second_).toString(QUrl::FullyEncoded)});
    QUrl localhost = QUrl::fromLocalFile(second_);
    localhost.setHost(QStringLiteral("localhost"));
    const auto localReply = call(QStringLiteral("Show"), {localhost.toString(QUrl::FullyEncoded)});
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(reply.errorName(), QStringLiteral("io.github.EdinUser.Dzurkel.Error.NotReadable"));
    QCOMPARE(localReply.errorName(), reply.errorName());
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

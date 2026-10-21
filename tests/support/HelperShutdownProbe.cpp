#include "resources/FolderNavigation.h"
#include "resources/ResourcePreparation.h"
#include "resources/media/StreamingMediaSource.h"
#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <memory>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace dzurkel;

namespace {
int serveHelper(const QString &command)
{
    QFile pids(qEnvironmentVariable("DZURKEL_TEST_HELPER_PIDS"));
    if (!pids.open(QIODevice::WriteOnly | QIODevice::Append)) return 2;
    pids.write(QByteArray::number(::getpid()) + '\n');
    pids.close();
    ::signal(SIGTERM, SIG_IGN); // A real SIGKILL must still terminate this helper.
    const bool complete = qEnvironmentVariable("DZURKEL_TEST_HELPER_MODE") == QLatin1String("complete");
    QByteArray response;
    if (command == QLatin1String("--media-helper")) response = mediaHeader(0, 0);
    else if (complete && command == QLatin1String("--internal-prepare-resource"))
        response = R"({"informationOnly":true,"mime":"application/octet-stream"})";
    else if (complete && command == QLatin1String("--internal-list-folder"))
        response = R"({"names":["fixture"]})";
    if (!response.isEmpty() && ::write(STDOUT_FILENO, response.constData(), response.size()) != response.size()) return 2;
    if (complete) return 0;
    while (true) ::pause();
}
}

class HelperShutdownProbe : public QObject {
    Q_OBJECT
public:
    HelperShutdownProbe(QString family, QString scenario)
        : family_(std::move(family)), scenario_(std::move(scenario)) {}
private:
    std::unique_ptr<QObject> start()
    {
        error_.clear();
        FileResource resource;
        resource.path = QStringLiteral("/tmp/fixture");
        resource.url = QUrl::fromLocalFile(resource.path);
        if (family_ == QLatin1String("prepare")) {
            auto request = std::make_unique<ResourcePreparation>();
            connect(request.get(), &ResourcePreparation::ready, this, [this](const FileResource &result) {
                ++completed_;
                error_ = result.metadata.value(QStringLiteral("message")).toString();
            });
            request->start(resource);
            return request;
        }
        if (family_ == QLatin1String("folder")) {
            auto request = std::make_unique<FolderNavigation>();
            connect(request.get(), &FolderNavigation::ready, this,
                [this](const QStringList &, int, const QString &message, const QString &) {
                ++completed_;
                error_ = message;
            });
            request->start(resource);
            return request;
        }
        auto request = std::make_unique<StreamingMediaSource>();
        connect(request.get(), &StreamingMediaSource::ready, this, [this] { ++completed_; });
        connect(request.get(), &StreamingMediaSource::failed, this, [this](const QString &message) {
            ++completed_;
            error_ = message;
        });
        request->start(QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel"), resource.path,
                       0, {QStringLiteral("--media-helper")});
        return request;
    }
    void cancel(QObject *owner)
    {
        if (auto *request = qobject_cast<ResourcePreparation *>(owner)) request->cancel();
        else if (auto *request = qobject_cast<FolderNavigation *>(owner)) request->cancel();
        else qobject_cast<StreamingMediaSource *>(owner)->cancel();
    }
    QList<QProcess *> processes(QObject *owner = nullptr)
    {
        auto result = qApp->findChildren<QProcess *>();
        if (owner) result.append(owner->findChildren<QProcess *>());
        return result;
    }
    QString family_, scenario_, error_;
    int completed_ = 0;
private slots:
    void lifecycle()
    {
        if (scenario_ == QLatin1String("complete") || scenario_ == QLatin1String("failed-start")) {
            // More than the independent two-helper budget: every iteration must
            // release its wrapper and slot, including FailedToStart.
            for (int i = 0; i < 4; ++i) {
                auto owner = start();
                QTRY_COMPARE_WITH_TIMEOUT(completed_, i + 1, 2000);
                QCOMPARE(error_.isEmpty(), scenario_ == QLatin1String("complete"));
                owner.reset();
                QTRY_VERIFY_WITH_TIMEOUT(processes().isEmpty(), 2000);
                if (family_ == QLatin1String("media")) QCOMPARE(StreamingMediaSource::activeHelpers(), 0);
            }
            return;
        }
        if (scenario_ == QLatin1String("budget")) {
            auto first = start();
            auto second = start();
            // Let both handshakes finish before closing their sockets. Killing
            // a helper with SIGPIPE during its first write would not exercise
            // the occupied-slot case this test is intended to preserve.
            if (family_ == QLatin1String("media")) QTRY_COMPARE_WITH_TIMEOUT(completed_, 2, 2000);
            cancel(first.get());
            cancel(second.get());
            QTRY_COMPARE_WITH_TIMEOUT(processes().size(), 2, 2000);
            for (auto *process : processes()) QTRY_COMPARE_WITH_TIMEOUT(process->state(), QProcess::Running, 2000);
            const int before = completed_;
            auto rejected = start();
            QTRY_COMPARE_WITH_TIMEOUT(completed_, before + 1, 2000);
            QVERIFY(error_.contains(QStringLiteral("stopping")));
            QCOMPARE(processes().size(), 2);
            // The supervisor's fault injection suppresses libc kill(). Bypass
            // it only for our known children to verify eventual runtime recovery.
            for (auto *process : processes())
                QCOMPARE(::syscall(SYS_kill, pid_t(process->processId()), SIGKILL), 0L);
            QTRY_VERIFY_WITH_TIMEOUT(processes().isEmpty(), 2000);
            qputenv("DZURKEL_TEST_HELPER_MODE", "complete");
            const int recovered = completed_;
            auto next = start();
            QTRY_COMPARE_WITH_TIMEOUT(completed_, recovered + 1, 2000);
            QVERIFY(error_.isEmpty());
            next.reset();
            QTRY_VERIFY_WITH_TIMEOUT(processes().isEmpty(), 2000);
            return;
        }

        auto owner = start();
        if (scenario_ == QLatin1String("replaced")) {
            FileResource resource;
            resource.path = QStringLiteral("/tmp/fixture");
            resource.url = QUrl::fromLocalFile(resource.path);
            qobject_cast<FolderNavigation *>(owner.get())->start(resource);
        }
        const auto wrappers = processes(owner.get());
        QCOMPARE(wrappers.size(), scenario_ == QLatin1String("replaced") ? 2 : 1);
        QList<QPointer<QProcess>> guarded;
        for (auto *process : wrappers) {
            guarded.append(process);
            QTRY_COMPARE_WITH_TIMEOUT(process->state(), QProcess::Running, 2000);
        }
        if (family_ == QLatin1String("media")) QTRY_COMPARE_WITH_TIMEOUT(completed_, 1, 2000);
        if (scenario_ == QLatin1String("cancelled")) cancel(owner.get());
        if (scenario_ == QLatin1String("destroyed")) owner.reset();
        const int beforeQuit = completed_;
        bool checkedShutdown = false;
        // Connected after launch, so this observes the production shutdown slot.
        connect(qApp, &QCoreApplication::aboutToQuit, this, [&] {
            checkedShutdown = true;
            for (const auto &process : guarded)
                if (process && process->state() != QProcess::NotRunning) QVERIFY(!process->parent());
        });
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        QCOMPARE(QCoreApplication::exec(), 0);
        QVERIFY(checkedShutdown);
        QCOMPARE(completed_, beforeQuit);
        if (owner) { cancel(owner.get()); owner.reset(); }
        // Destruction after the event loop stops must not adopt the wrapper.
        for (const auto &process : guarded)
            if (process && process->state() != QProcess::NotRunning) QVERIFY(!process->parent());
        // Do not pump events here: application destruction must be safe even
        // when finished notifications/deferred deletions cannot run again.
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() > 1 && arguments.at(1).startsWith(QLatin1String("--")))
        return serveHelper(arguments.at(1));
    if (arguments.size() != 3) return 2;
    HelperShutdownProbe probe(arguments.at(1), arguments.at(2));
    return QTest::qExec(&probe, 1, argv);
}

#include "HelperShutdownProbe.moc"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
const QString service = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer");
const QString path = QStringLiteral("/io/github/EdinUser/Dzurkel/Previewer");
const QString interface = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer1");
void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}
QDBusMessage call(const QString &method)
{
    return QDBusConnection::sessionBus().call(
        QDBusMessage::createMethodCall(service, path, interface, method), QDBus::Block, 5000);
}
QVariantMap properties()
{
    auto message = QDBusMessage::createMethodCall(service, path,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << interface;
    const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return {};
    return qdbus_cast<QVariantMap>(reply.arguments().first());
}
void ready(QProcess &primary, const QString &kind, const QString &file)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15000) {
        const auto state = properties();
        if (state.value(QStringLiteral("Visible")).toBool()
            && !state.value(QStringLiteral("Loading")).toBool()
            && state.value(QStringLiteral("PreviewKind")).toString() == kind
            && state.value(QStringLiteral("FileUrl")).toString() == QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)) return;
        require(primary.state() != QProcess::NotRunning, QStringLiteral("Primary exited: ") + primary.readAllStandardError());
        QThread::msleep(2);
    }
    throw std::runtime_error("Timed out waiting for the expected preview");
}
QJsonObject memory(qint64 pid)
{
    QJsonObject values;
    for (const QString &name : {QStringLiteral("status"), QStringLiteral("smaps_rollup")}) {
        QFile file(QStringLiteral("/proc/%1/%2").arg(pid).arg(name));
        require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot read ") + file.fileName());
        const auto lines = file.readAll().split('\n');
        for (const auto &line : lines) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() < 2) continue;
            const QString key = QString::fromLatin1(parts.first());
            if (key == QStringLiteral("VmRSS:") || key == QStringLiteral("Pss:")
                || key == QStringLiteral("Private_Clean:") || key == QStringLiteral("Private_Dirty:")
                || key == QStringLiteral("Threads:")) values.insert(key.chopped(1), double(parts[1].toLongLong()));
        }
    }
    values.insert(QStringLiteral("Private_KiB"), values.value(QStringLiteral("Private_Clean")).toDouble()
        + values.value(QStringLiteral("Private_Dirty")).toDouble());
    return values;
}
void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), QStringLiteral("Cannot write fixture ") + path);
}
QByteArray pdf()
{
    const QByteArray content("0.2 0.4 0.7 rg 40 40 300 500 re f\n");
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 600] /Resources << >> /Contents 4 0 R >>",
        "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream"};
    QByteArray bytes("%PDF-1.4\n");
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(bytes.size());
        bytes += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = bytes.size();
    bytes += "xref\n0 5\n0000000000 65535 f \n";
    for (const auto offset : offsets) bytes += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    bytes += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return bytes;
}
QJsonObject summary(QList<double> values)
{
    std::sort(values.begin(), values.end());
    const qsizetype middle = values.size() / 2;
    const double median = values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    return {{"min", values.first()}, {"median", median},
            {"p95", values[qsizetype(std::ceil(values.size() * 0.95)) - 1]}, {"max", values.last()}};
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int cycles = 30;
    QString output;
    QString platform = QStringLiteral("offscreen");
    QString executable = QStringLiteral(DZURKEL_EXECUTABLE);
    QStringList kinds = {QStringLiteral("image"), QStringLiteral("pdf"), QStringLiteral("text"), QStringLiteral("information")};
    const auto arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        const QString option = arguments[i];
        if (option == QStringLiteral("--help")) {
            QTextStream(stdout) << "Run under dbus-run-session -- dzurkel_benchmark [--cycles 1..1000] "
                "[--platform offscreen|wayland] [--output report.json] [--executable /path/to/dzurkel] "
                "[--kinds image,pdf,text,information]\n";
            return 0;
        }
        if (++i >= arguments.size()) { QTextStream(stderr) << "Missing option value\n"; return 2; }
        if (option == QStringLiteral("--cycles")) cycles = arguments[i].toInt();
        else if (option == QStringLiteral("--output")) output = arguments[i];
        else if (option == QStringLiteral("--platform")) platform = arguments[i];
        else if (option == QStringLiteral("--executable")) executable = arguments[i];
        else if (option == QStringLiteral("--kinds")) kinds = arguments[i].split(',');
        else { QTextStream(stderr) << "Unknown option\n"; return 2; }
    }
    if (cycles < 1 || cycles > 1000 || (platform != QStringLiteral("offscreen") && platform != QStringLiteral("wayland"))) return 2;
    QProcess primary;
    try {
        auto bus = QDBusConnection::sessionBus();
        require(bus.isConnected(), QStringLiteral("No session bus; run with dbus-run-session"));
        // Never control an unrelated resident instance, even on accidental use
        // of the desktop bus. Every preview is pinned to the child PID below.
        const auto existing = bus.interface()->isServiceRegistered(service);
        require(existing.isValid() && !existing.value(), QStringLiteral("A Dzurkel instance already owns this bus; use dbus-run-session"));
        QTemporaryDir fixtures;
        require(fixtures.isValid(), QStringLiteral("Cannot create fixtures"));
        const QString image = fixtures.filePath(QStringLiteral("image.png"));
        QImage pixels(1600, 1200, QImage::Format_RGB32);
        pixels.fill(QColor(48, 103, 177));
        require(pixels.save(image), QStringLiteral("Cannot create PNG fixture"));
        pixels = {};
        const QString document = fixtures.filePath(QStringLiteral("document.pdf"));
        write(document, pdf());
        const QString text = fixtures.filePath(QStringLiteral("text.txt"));
        write(text, QByteArray("Dzurkel preview performance fixture\n").repeated(2000));
        const QString information = fixtures.filePath(QStringLiteral("unknown.bin"));
        write(information, QByteArray::fromHex("000102030405ff"));
        const QList<QPair<QString, QString>> fixturesByKind = {{"image", image}, {"pdf", document}, {"text", text}, {"information", information}};
        QList<QPair<QString, QString>> inputs;
        require(!kinds.isEmpty(), QStringLiteral("No provider kinds selected"));
        for (const auto &kind : kinds) {
            const auto match = std::find_if(fixturesByKind.begin(), fixturesByKind.end(),
                [&kind](const auto &item) { return item.first == kind; });
            require(match != fixturesByKind.end(), QStringLiteral("Unknown kind: ") + kind);
            inputs.append(*match);
        }
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), platform);
        env.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
        primary.setProcessEnvironment(env);
        QElapsedTimer timer;
        timer.start();
        primary.start(executable, {text});
        require(primary.waitForStarted(), QStringLiteral("Cannot start benchmark primary"));
        ready(primary, QStringLiteral("text"), text);
        const double cold = timer.nsecsElapsed() / 1e6;
        require(bus.interface()->servicePid(service).value() == uint(primary.processId()), QStringLiteral("Bus owner is not the benchmark child"));
        const auto initialMemory = memory(primary.processId());
        QMap<QString, QList<double>> cliTimes, readyTimes;
        QJsonArray samples;
        for (int cycle = 0; cycle < cycles; ++cycle) {
            for (const auto &[kind, file] : inputs) {
                QProcess client;
                auto clientEnv = env;
                clientEnv.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("dzurkel_invalid_backend"));
                client.setProcessEnvironment(clientEnv);
                timer.restart();
                client.start(executable, {file});
                require(client.waitForFinished(7000) && client.exitStatus() == QProcess::NormalExit && client.exitCode() == 0,
                    QStringLiteral("Secondary command failed: ") + client.readAllStandardError());
                cliTimes[kind].append(timer.nsecsElapsed() / 1e6);
                ready(primary, kind, file);
                readyTimes[kind].append(timer.nsecsElapsed() / 1e6);
                require(call(QStringLiteral("Hide")).type() == QDBusMessage::ReplyMessage, QStringLiteral("Hide failed"));
            }
            // Let deferred widget destruction and pending PDF renderer cleanup run.
            QThread::msleep(20);
            auto sample = memory(primary.processId());
            sample.insert(QStringLiteral("cycle"), cycle + 1);
            samples.append(sample);
        }
        require(call(QStringLiteral("Quit")).type() == QDBusMessage::ReplyMessage, QStringLiteral("Quit failed"));
        require(primary.waitForFinished(10000) && primary.exitStatus() == QProcess::NormalExit && primary.exitCode() == 0,
            QStringLiteral("Primary did not exit cleanly: ") + primary.readAllStandardError());
        QJsonObject timings;
        for (const auto &[kind, file] : inputs) {
            Q_UNUSED(file)
            timings.insert(kind, QJsonObject{{"cli_ms", summary(cliTimes[kind])}, {"ready_ms", summary(readyTimes[kind])}});
        }
        const QJsonObject report{{"harness_version", QStringLiteral(DZURKEL_VERSION)}, {"qt_version", QString::fromLatin1(qVersion())},
            {"executable", executable}, {"platform", platform}, {"cycles", cycles}, {"previews", cycles * inputs.size()},
            {"cold_ready_ms", cold}, {"initial_text_memory_KiB", initialMemory}, {"timings", timings},
            {"hidden_memory_samples_KiB", samples},
            {"kinds", QJsonArray::fromStringList(kinds)},
            {"measurement", "CLI elapsed includes process startup/acknowledgement; ready adds 2ms state polling, not first painted frame. Cold means new process, warm OS cache. RSS includes shared pages; PSS apportions them. Memory samples follow each selected-provider cycle."}};
        const auto json = QJsonDocument(report).toJson();
        if (!output.isEmpty()) {
            QSaveFile file(output);
            require(file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit(), QStringLiteral("Cannot write report"));
        }
        if (output.isEmpty()) QTextStream(stdout) << json;
        else QTextStream(stdout) << "Report saved to " << output << "; " << cycles * inputs.size()
                                << " previews completed and primary exited cleanly.\n";
        return 0;
    } catch (const std::exception &error) {
        QTextStream(stderr) << "Benchmark failed: " << error.what() << '\n';
        // Only terminate the child that this harness launched.
        if (primary.state() != QProcess::NotRunning) { primary.kill(); primary.waitForFinished(); }
        return 1;
    }
}

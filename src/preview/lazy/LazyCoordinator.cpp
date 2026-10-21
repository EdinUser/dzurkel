#include "preview/readers/DocumentReader.h"
#include "LazyCoordinator.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <signal.h>
#include <unistd.h>

namespace dzurkel {
namespace {
QSet<QProcess *> &helpers() { static QSet<QProcess *> processes; return processes; }
void initializeReaper()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        for (auto *process : helpers()) {
            if (process->state() == QProcess::NotRunning) continue;
            if (process->processId() > 0) ::kill(-pid_t(process->processId()), SIGKILL);
            process->kill();
            process->setParent(nullptr);
            process->disconnect(); // Never wait for blocked source I/O at exit.
        }
    });
}
}
int LazyCoordinator::runningHelpers() { return helpers().size(); }
LazyCoordinator::LazyCoordinator(const FileResource &resource, const QString &reader,
                                 const QString &program, int timeoutMs)
{
    initializeReaper();
    if (reader == QStringLiteral("csv") || reader == QStringLiteral("csv-tsv") || reader == QStringLiteral("xlsx") || DocumentReader::supportsPortions(reader)) {
        payloadLimit_ = TablePayloadLimit;
        cacheBudget_ = TableCacheLimit;
    }
    timeout_.setSingleShot(true);
    timeout_.setInterval(qMax(1, timeoutMs));
    connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Content loading timed out.")); });
    if (helpers().size() >= 2) {
        QTimer::singleShot(0, this, [this] { fail(QStringLiteral("Previous content reader is still stopping. Try again shortly.")); });
        return;
    }
    auto *process = new QProcess(qApp);
    process_ = process;
    helpers().insert(process);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->setChildProcessModifier([] { if (::setpgid(0, 0) != 0) ::_exit(127); });
    auto group = std::make_shared<qint64>(0);
    connect(process, &QProcess::started, process, [process, group] { *group = process->processId(); });
    connect(process, &QProcess::finished, process, [process, group, store = resource.backingStore] {
        Q_UNUSED(store);
        if (*group > 0) ::kill(-pid_t(*group), SIGKILL);
        helpers().remove(process);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { helpers().remove(process); process->deleteLater(); }
    });
    connect(process, &QProcess::started, this, &LazyCoordinator::send);
    connect(process, &QProcess::readyReadStandardOutput, this, &LazyCoordinator::read);
    connect(process, &QProcess::finished, this, [this] { fail(QStringLiteral("Content reader stopped unexpectedly.")); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(QStringLiteral("Cannot start the content reader."));
    });
    const auto executable = program.isEmpty() ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dzurkel")) : program;
    process->start(executable, {QStringLiteral("--internal-lazy"),
        resource.backingPath.isEmpty() ? resource.path : resource.backingPath, reader});
    timeout_.start();
}
LazyCoordinator::~LazyCoordinator() { stop(); }
qint64 LazyCoordinator::cachedBytes() const
{
    qint64 bytes = 0;
    for (const auto &portion : cache_) bytes += portion.cost;
    return bytes;
}
const LazyPortion *LazyCoordinator::cached(const QString &token) const
{
    for (const auto &portion : cache_) if (portion.token == token) return &portion;
    return nullptr;
}
void LazyCoordinator::setLoading(bool loading)
{
    if (loading_ == loading) return;
    loading_ = loading;
    emit loadingChanged(loading);
}
void LazyCoordinator::request(const QString &token)
{
    if (stopped_ || token.isEmpty() || token.size() > 4096) return;
    if (wanted_ == token && loading_) return;
    wanted_ = token;
    ++generation_;
    setLoading(true);
    if (cached(token)) deliver();
    else send();
}
void LazyCoordinator::prefetch(const QString &token)
{
    if (stopped_ || token.isEmpty() || token.size() > 4096 || cached(token)) return;
    lookahead_ = token;
    send();
}
void LazyCoordinator::deliver()
{
    const auto generation = generation_;
    // Cache hits must have the same asynchronous/reentrancy behavior as I/O.
    QTimer::singleShot(0, this, [this, generation] {
        if (stopped_ || generation != generation_ || !loading_) return;
        for (qsizetype i = 0; i < cache_.size(); ++i) {
            if (cache_.at(i).token != wanted_) continue;
            auto portion = cache_.takeAt(i);
            cache_.append(portion);
            setLoading(false);
            const QPointer<LazyCoordinator> guard(this);
            emit ready(portion);
            if (guard) send();
            return;
        }
        send();
    });
}
void LazyCoordinator::send()
{
    if (stopped_ || !process_ || active_ || process_->state() != QProcess::Running) return;
    QString token;
    if (loading_ && !cached(wanted_)) token = wanted_;
    else if (!loading_ && !lookahead_.isEmpty() && !cached(lookahead_)) token = lookahead_;
    if (token.isEmpty()) { timeout_.stop(); return; }
    if (token == lookahead_) lookahead_.clear();
    active_ = Request{++nextId_, token};
    process_->write(QJsonDocument(QJsonObject{{"id", active_->id}, {"token", token}}).toJson(QJsonDocument::Compact) + '\n');
    timeout_.start();
}
void LazyCoordinator::read()
{
    if (stopped_ || !process_) return;
    qint64 budget = 512 * 1024;
    while (!stopped_ && process_ && process_->bytesAvailable() && budget > 0) {
        // QIODevice's maxSize includes space for the terminating NUL even for
        // the QByteArray overload. Always allow at least one actual byte.
        const auto bytes = process_->readLine(qMin(RecordLimit + 1 - incoming_.size(), budget) + 1);
        if (bytes.isEmpty()) break;
        budget -= bytes.size();
        incoming_ += bytes;
        if (incoming_.size() > RecordLimit || (incoming_.size() == RecordLimit && !incoming_.endsWith('\n'))) {
            fail(QStringLiteral("Content response exceeds its budget.")); return;
        }
        if (!incoming_.endsWith('\n')) continue;
        const auto object = QJsonDocument::fromJson(incoming_).object();
        const auto cost = qint64(incoming_.size());
        incoming_.clear();
        record(object, cost);
    }
    if (!stopped_ && process_ && process_->bytesAvailable()) QTimer::singleShot(0, this, &LazyCoordinator::read);
}
void LazyCoordinator::record(const QJsonObject &object, qint64 cost)
{
    if (!active_ || object.value("id").toInteger(-1) != active_->id
        || object.value("token").toString() != active_->token) {
        fail(QStringLiteral("Invalid content reader response.")); return;
    }
    if (payloadSize_) {
        const auto encoded = object.value("data");
        if (!encoded.isString() || encoded.toString().size() > 65536 || object.value("offset").toInteger(-1) != payload_.size()) {
            fail(QStringLiteral("Invalid content payload chunk.")); return;
        }
        const auto decoded = QByteArray::fromBase64Encoding(encoded.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > payloadSize_ - payload_.size()) {
            fail(QStringLiteral("Invalid content payload size.")); return;
        }
        payload_ += decoded.decoded;
        if (payload_.size() == payloadSize_) {
            const auto reply = QJsonDocument::fromJson(payload_).object();
            const auto size = payloadSize_;
            payload_.clear();
            payloadSize_ = 0;
            accept(reply, size);
        }
        return;
    }
    if (object.contains("payloadBytes")) {
        const auto size = object.value("payloadBytes").toInteger(-1);
        if (size < 1 || size > payloadLimit_) { fail(QStringLiteral("Content payload exceeds its budget.")); return; }
        payloadSize_ = size;
        return;
    }
    accept(object, cost);
}
void LazyCoordinator::accept(const QJsonObject &object, qint64 cost)
{
    if (!active_ || object.value("id").toInteger(-1) != active_->id
        || object.value("token").toString() != active_->token) {
        fail(QStringLiteral("Invalid content payload identity.")); return;
    }
    timeout_.stop();
    if (object.contains("error")) { fail(object.value("error").toString().left(1024)); return; }
    const auto validToken = [&object](const char *name) {
        return object.value(QLatin1String(name)).isString() && object.value(QLatin1String(name)).toString().size() <= 1024;
    };
    if (!object.value("content").isObject() || !validToken("previous") || !validToken("next")
        || object.value("previous").toString() == active_->token || object.value("next").toString() == active_->token) {
        fail(QStringLiteral("Invalid content continuation.")); return;
    }
    // Charge serialized bytes plus retained UTF-16/JSON storage conservatively.
    LazyPortion portion{active_->token, object.value("previous").toString(), object.value("next").toString(),
        object.value("content").toObject(), cost * 4};
    if (portion.cost > cacheBudget_) { fail(QStringLiteral("Content portion exceeds its cache budget.")); return; }
    active_.reset();
    while (!cache_.isEmpty() && (cache_.size() >= CacheEntries || cachedBytes() + portion.cost > cacheBudget_)) cache_.removeFirst();
    cache_.append(portion);
    if (loading_ && cached(wanted_)) deliver();
    send();
}
void LazyCoordinator::stop()
{
    if (stopped_) return;
    stopped_ = true;
    ++generation_;
    timeout_.stop();
    active_.reset();
    incoming_.clear();
    payload_.clear();
    payloadSize_ = 0;
    cache_.clear();
    lookahead_.clear();
    wanted_.clear();
    setLoading(false);
    if (process_) {
        disconnect(process_, nullptr, this, nullptr);
        if (process_->state() != QProcess::NotRunning) {
            if (process_->processId() > 0) ::kill(-pid_t(process_->processId()), SIGKILL);
            process_->kill();
        }
        process_ = nullptr;
    }
}
void LazyCoordinator::fail(const QString &message)
{
    if (stopped_) return;
    stop();
    emit failed(message);
}
}

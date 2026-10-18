#include "StreamingMediaSource.h"
#include <QCoreApplication>
#include <QProcess>
#include <QSocketNotifier>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dzurkel {
namespace {
int helperCount = 0; // Accessed only on the application thread.
qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
QByteArray mediaHeader(qint64 first, qint64 second)
{
    QByteArray bytes(16, Qt::Uninitialized);
    qToBigEndian<qint64>(first, bytes.data());
    qToBigEndian<qint64>(second, bytes.data() + 8);
    return bytes;
}
qint64 mediaHeaderValue(const char *header, int index)
{
    return qFromBigEndian<qint64>(header + index * 8);
}
int StreamingMediaSource::activeHelpers() { return helperCount; }
StreamingMediaSource::StreamingMediaSource(QObject *parent, int readTimeoutMs)
    : QIODevice(parent), timeoutMs_(std::max(1, readTimeoutMs))
{
    startupTimer_.setSingleShot(true);
    connect(&startupTimer_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Media source opening timed out.")); });
}
StreamingMediaSource::~StreamingMediaSource()
{
    cancel();
    if (socket_ >= 0) ::close(socket_);
    if (childSocket_ >= 0) ::close(childSocket_);
}
void StreamingMediaSource::start(const QString &helper, const QString &url, int simulatedReadDelayMs,
                                  const QStringList &helperArguments)
{
    if (started_) { emit failed(QStringLiteral("A media source cannot be reused.")); return; }
    started_ = true;
    if (helperCount >= 2) { fail(QStringLiteral("Previous media sources are still stopping.")); return; }
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets) != 0) {
        fail(QStringLiteral("Cannot create the media source channel."));
        return;
    }
    socket_ = sockets[0];
    childSocket_ = sockets[1];
    // QProcess and its reaping outlive this source on asynchronous cancellation.
    auto *process = new QProcess(QCoreApplication::instance());
    process_ = process;
    ++helperCount;
    connect(process, &QObject::destroyed, [] { --helperCount; });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    const int child = childSocket_;
    const int parent = socket_;
    process->setChildProcessModifier([child, parent] {
        // Only async-signal-safe operations between fork and exec.
        if (::dup2(child, STDIN_FILENO) < 0 || ::dup2(child, STDOUT_FILENO) < 0
            || ::fcntl(STDIN_FILENO, F_SETFD, 0) < 0 || ::fcntl(STDOUT_FILENO, F_SETFD, 0) < 0)
            ::_exit(127);
        if (child > STDOUT_FILENO) ::close(child);
        if (parent > STDOUT_FILENO) ::close(parent);
    });
    const auto closeChild = [this] {
        if (childSocket_ >= 0) { ::close(childSocket_); childSocket_ = -1; }
    };
    connect(process, &QProcess::started, this, closeChild);
    connect(process, &QProcess::errorOccurred, this, [this, closeChild](QProcess::ProcessError error) {
        closeChild();
        if (!cancelled_.load() && error == QProcess::FailedToStart)
            fail(QStringLiteral("Cannot start the media source helper."));
    });
    connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    process->setProgram(helper);
    process->setArguments(helperArguments + QStringList{url, QString::number(std::max(0, simulatedReadDelayMs))});
    notifier_ = new QSocketNotifier(socket_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &StreamingMediaSource::receiveHandshake);
    startupTimer_.start(30000);
    process->start();
}
void StreamingMediaSource::cancel()
{
    cancelled_.store(true);
    startupTimer_.stop();
    if (notifier_) notifier_->setEnabled(false);
    // shutdown, not close: wake a concurrent poll/read without descriptor reuse.
    if (socket_ >= 0) ::shutdown(socket_, SHUT_RDWR);
    if (process_ && process_->state() != QProcess::NotRunning) process_->kill();
}
void StreamingMediaSource::fail(const QString &message)
{
    setErrorString(message);
    cancel();
    emit failed(message);
}
void StreamingMediaSource::receiveHandshake()
{
    char bytes[1040];
    const auto count = ::recv(socket_, bytes, sizeof(bytes), MSG_DONTWAIT);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (count <= 0) { fail(QStringLiteral("Media source helper closed while opening.")); return; }
    handshake_.append(bytes, count);
    if (handshake_.size() < 16) return;
    const auto result = mediaHeaderValue(handshake_.constData(), 0);
    const auto detail = mediaHeaderValue(handshake_.constData(), 1);
    if (detail < 0 || detail > 1024 || (result >= 0 && detail != 0)) {
        fail(QStringLiteral("Invalid media source response."));
        return;
    }
    if (handshake_.size() < 16 + detail) return;
    startupTimer_.stop();
    notifier_->setEnabled(false);
    if (result < 0) { fail(QString::fromUtf8(handshake_.mid(16, detail))); return; }
    if (handshake_.size() != 16) { fail(QStringLiteral("Unexpected media source data.")); return; }
    size_ = result;
    handshake_.clear();
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    emit ready();
}
bool StreamingMediaSource::seek(qint64 offset)
{
    if (cancelled_.load() || offset < 0 || offset > size_) return false;
    ++seeks_;
    return QIODevice::seek(offset);
}
bool StreamingMediaSource::transfer(char *bytes, qint64 count, bool writing, qint64 deadline)
{
    while (count > 0 && !cancelled_.load()) {
        const auto remaining = deadline - nowMs();
        if (remaining <= 0) { setErrorString(QStringLiteral("Media source read timed out.")); return false; }
        pollfd descriptor{socket_, short(writing ? POLLOUT : POLLIN), 0};
        const int result = ::poll(&descriptor, 1, int(remaining));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { setErrorString(QStringLiteral("Media source read timed out or failed.")); return false; }
        const auto transferred = writing ? ::send(socket_, bytes, count, MSG_NOSIGNAL | MSG_DONTWAIT)
                                         : ::recv(socket_, bytes, count, MSG_DONTWAIT);
        if (transferred < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (transferred <= 0) { setErrorString(QStringLiteral("Media source disconnected.")); return false; }
        count -= transferred;
        bytes += transferred;
    }
    return count == 0 && !cancelled_.load();
}
qint64 StreamingMediaSource::readData(char *data, qint64 maximum)
{
    if (cancelled_.load()) return -1;
    if (QThread::currentThread() == thread()) {
        ++ownerReads_;
        return readFailed(QStringLiteral("The installed media backend does not support this streaming preview."));
    }
    const auto count = std::min({maximum, ChunkBytes, size_ - pos()});
    if (count <= 0) return 0;
    ++requests_;
    peakRead_.store(std::max(peakRead_.load(), count));
    auto request = mediaHeader(pos(), count);
    QByteArray reply(16, Qt::Uninitialized);
    const auto deadline = nowMs() + timeoutMs_;
    if (!transfer(request.data(), request.size(), true, deadline)
        || !transfer(reply.data(), reply.size(), false, deadline)) {
        return readFailed(errorString()); // A partial reply must never be reused as a header.
    }
    const auto received = mediaHeaderValue(reply.constData(), 0);
    if (received < 0 || received > count || mediaHeaderValue(reply.constData(), 1) != 0
        || !transfer(data, received, false, deadline)) {
        return readFailed(QStringLiteral("Media source read failed or the file changed."));
    }
    // A regular file promised these bytes at open time. Truncation is an error,
    // not a temporary zero-byte read or a silently shortened successful preview.
    if (received == 0) {
        return readFailed(QStringLiteral("Media source was truncated."));
    }
    transferred_ += received;
    return received;
}
qint64 StreamingMediaSource::readFailed(const QString &message)
{
    if (!cancelled_.exchange(true)) {
        setErrorString(message);
        QMetaObject::invokeMethod(this, [this, message] { fail(message); }, Qt::QueuedConnection);
    }
    return -1;
}
} // namespace dzurkel

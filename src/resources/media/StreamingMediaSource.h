#pragma once

#include <QIODevice>
#include <QPointer>
#include <QTimer>
#include <atomic>

class QProcess;
class QSocketNotifier;

namespace dzurkel {

// Linux source. The helper owns the original file; this
// device transfers bounded byte ranges, never a complete-file snapshot.
// Construct/start/cancel on the owner thread. After ready(), one decoder thread
// may use the QIODevice interface. Only cancel() may run concurrently with it.
// Cancel and detach the player before destroying the device.
class StreamingMediaSource : public QIODevice {
    Q_OBJECT
public:
    static constexpr qint64 ChunkBytes = 64 * 1024;
    explicit StreamingMediaSource(QObject *parent = nullptr, int readTimeoutMs = 2000);
    ~StreamingMediaSource() override;
    void start(const QString &helper, const QString &url, int simulatedReadDelayMs = 0,
               const QStringList &helperArguments = {});
    void cancel();
    bool isSequential() const override { return false; }
    qint64 size() const override { return size_; }
    bool atEnd() const override { return pos() >= size_; }
    bool seek(qint64 offset) override;
    qint64 bytesTransferred() const { return transferred_.load(); }
    qint64 readRequests() const { return requests_.load(); }
    qint64 seekRequests() const { return seeks_.load(); }
    qint64 ownerThreadReads() const { return ownerReads_.load(); }
    qint64 peakReadBytes() const { return peakRead_.load(); }
    static int activeHelpers();
signals:
    void ready();
    void failed(const QString &message);
protected:
    qint64 readData(char *data, qint64 maximum) override;
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    void receiveHandshake();
    void fail(const QString &message);
    qint64 readFailed(const QString &message);
    bool transfer(char *bytes, qint64 count, bool writing, qint64 deadline);
    QPointer<QProcess> process_;
    QSocketNotifier *notifier_ = nullptr;
    QTimer startupTimer_;
    QByteArray handshake_;
    int socket_ = -1;
    int childSocket_ = -1;
    int timeoutMs_;
    qint64 size_ = 0;
    bool started_ = false;
    std::atomic_bool cancelled_{false};
    std::atomic<qint64> transferred_{0}, requests_{0}, seeks_{0}, ownerReads_{0}, peakRead_{0};
};

// Fixed-size big-endian IPC headers: request=(offset,count), reply=(count,0).
// The initial reply contains (file size,0), or (-1,error byte count)+UTF-8.
QByteArray mediaHeader(qint64 first, qint64 second);
qint64 mediaHeaderValue(const char *header, int index);

} // namespace dzurkel

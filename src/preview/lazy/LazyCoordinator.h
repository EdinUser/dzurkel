#pragma once
#include "resources/FileResource.h"
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <optional>

class QProcess;
namespace dzurkel {
// Tokens and payloads belong to readers. The coordinator never interprets a
// token as a byte offset, row or chapter, and retains no unbounded token index.
struct LazyPortion {
    QString token;
    QString previous;
    QString next;
    QJsonObject content;
    qint64 cost = 0;
};
class LazyCoordinator final : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 RecordLimit = 128 * 1024;
    static constexpr qint64 CacheLimit = 384 * 1024;
    static constexpr int CacheEntries = 5;
    static constexpr qint64 TablePayloadLimit = 16 * 1024 * 1024;
    static constexpr qint64 TableCacheLimit = 64 * 1024 * 1024;
    explicit LazyCoordinator(const FileResource &resource, const QString &reader,
                             const QString &program = {}, int timeoutMs = 30000);
    ~LazyCoordinator() override;
    void request(const QString &token = QStringLiteral("0"));
    void prefetch(const QString &token);
    void stop();
    bool loading() const { return loading_; }
    qint64 cachedBytes() const;
    int cachedPortions() const { return cache_.size(); }
    qint64 cacheBudget() const { return cacheBudget_; }
    static int runningHelpers();
signals:
    void ready(const dzurkel::LazyPortion &portion);
    void loadingChanged(bool loading);
    void failed(const QString &message);
private:
    struct Request { qint64 id; QString token; };
    void send();
    void read();
    void record(const QJsonObject &object, qint64 cost);
    void accept(const QJsonObject &object, qint64 cost);
    void fail(const QString &message);
    void setLoading(bool loading);
    void deliver();
    const LazyPortion *cached(const QString &token) const;
    QPointer<QProcess> process_;
    QTimer timeout_;
    QList<LazyPortion> cache_; // LRU, with both byte and entry budgets.
    QByteArray incoming_;
    QByteArray payload_;
    qint64 payloadSize_ = 0;
    qint64 payloadLimit_ = RecordLimit;
    qint64 cacheBudget_ = CacheLimit;
    std::optional<Request> active_;
    QString wanted_;
    QString lookahead_; // At most one speculative request.
    qint64 nextId_ = 0;
    quint64 generation_ = 0;
    bool stopped_ = false;
    bool loading_ = false;
};
int serveLazy(const QString &path, const QString &reader);
}
Q_DECLARE_METATYPE(dzurkel::LazyPortion)

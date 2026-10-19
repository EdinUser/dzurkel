#pragma once
#include "DocumentReader.h"
#include <QElapsedTimer>
#include <QMap>
#include <vector>

namespace dzurkel::document {
struct Stop { QString error; bool cancelled = false; bool partial = false; };
struct Node {
    QString name;
    QString ns;
    QString text;
    QMap<QString, QString> attributes;
    std::vector<Node> children;
    QString attr(const char *key) const { return attributes.value(QLatin1String(key)); }
    const Node &child(const char *key) const;
    QString allText() const;
};
class Context {
public:
    explicit Context(Cancellation cancellation) : cancel(std::move(cancellation)) { clock.start(); }
    void check() const;
    [[noreturn]] void invalid() const;
    [[noreturn]] void limit() const;
    void markup(const QString &value);
    void text(const QString &value);
    void paragraph();
    void notice(const QString &value);
    Node xml(const QByteArray &bytes, bool xhtml = false);
    std::shared_ptr<FormattedDocument> doc = std::make_shared<FormattedDocument>();
    Cancellation cancel;
    QElapsedTimer clock;
    int nodes = 0;
    int blocks = 0;
};
QString decode(const QByteArray &bytes, const QByteArray &encoding);
void readRtf(Context &context, const QByteArray &bytes);
void readLegacyDoc(Context &context, const QByteArray &bytes);
void readFb2(Context &context, const QByteArray &bytes);
void readSfb(Context &context, const QByteArray &bytes);
void readMobi(Context &context, const QByteArray &bytes);
}

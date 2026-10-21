#pragma once
#include "DocumentReader.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <vector>
#include <QXmlStreamReader>

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
    Node element(QXmlStreamReader &xml);
    std::shared_ptr<FormattedDocument> doc = std::make_shared<FormattedDocument>();
    Cancellation cancel;
    QElapsedTimer clock;
    int nodes = 0;
    qint64 treeUnits = 0;
    int blocks = 0;
    qint64 sectionSerial = 0;
    qint64 sourceRead = 0;
    bool inputTruncated = false;
};
inline bool nextDocumentElement(Context &c, QXmlStreamReader &xml)
{
    while (!xml.atEnd()) {
        c.check(); xml.readNext();
        if (xml.isDTD() || xml.isEntityReference()) c.invalid();
        if (xml.isStartElement()) return true;
        if (xml.isEndElement()) return false;
    }
    if (xml.hasError()) c.invalid();
    return false;
}
inline void skipDocumentElement(Context &c, QXmlStreamReader &xml)
{
    int depth = 1;
    while (depth && !xml.atEnd()) {
        c.check(); xml.readNext();
        if (xml.isDTD() || xml.isEntityReference()) c.invalid();
        if (xml.isStartElement() && ++depth > 64) c.limit();
        if (xml.isEndElement()) --depth;
    }
    if (xml.hasError()) c.invalid();
}
// Parser state stays in the reader. The session only requests another bounded
// semantic unit and can restart this same parser for an older cache miss.
class PortionParser {
public:
    virtual ~PortionParser() = default;
    virtual void begin() {}
    virtual void end() {}
    virtual bool next() = 0; // false only after validated EOF
};
class DocumentInput final : public QIODevice {
public:
    DocumentInput(Context &context, QFile &file) : c(context), file_(file) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return file_.atEnd() && QIODevice::atEnd(); }
    qint64 readData(char *data, qint64 count) override {
        c.check();
        if (c.sourceRead >= 64 * 1024 * 1024) throw Stop{QStringLiteral("Document replay exceeds the per-request read budget.")};
        const auto size = file_.read(data, qMin(count, 64 * 1024 * 1024 - c.sourceRead));
        if (size < 0 || file_.error() != QFileDevice::NoError) c.invalid();
        c.sourceRead += size; return size;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    Context &c;
    QFile &file_;
};
std::unique_ptr<PortionParser> markdownParser(Context &c, QIODevice &input, QFile &file);
std::unique_ptr<PortionParser> rtfParser(Context &c, QIODevice &input);
std::unique_ptr<PortionParser> fb2Parser(Context &c, QIODevice &input);
std::unique_ptr<PortionParser> sfbParser(Context &c, QIODevice &file);
std::unique_ptr<PortionParser> mobiParser(Context &c, QFile &file);
std::unique_ptr<PortionParser> legacyDocParser(Context &c, QFile &file);
QString decode(const QByteArray &bytes, const QByteArray &encoding);
void readRtf(Context &context, const QByteArray &bytes);
void readLegacyDoc(Context &context, QFile &file);
void readFb2(Context &context, const QByteArray &bytes);
void readSfb(Context &context, const QByteArray &bytes);
void readMobi(Context &context, QFile &file);
}

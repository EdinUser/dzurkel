#include "DocumentReaderInternal.h"
#include "DocumentLines.h"
#include <QSet>
#include <QBuffer>
#include <QFile>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>

namespace dzurkel::document {
namespace {
void heading(Context &c, const QString &text, int level)
{
    if (text.trimmed().isEmpty()) return;
    const auto tag = QStringLiteral("h%1").arg(std::clamp(level, 1, 6));
    c.paragraph();
    QString anchor;
    if (c.doc->sections.size() < 128) {
        anchor = QStringLiteral("book-section-%1").arg(c.sectionSerial++);
        c.markup(QStringLiteral("<a name=\"%1\"></a>").arg(anchor));
    }
    c.markup(QChar('<') + tag + QChar('>'));
    c.text(text);
    c.markup(QStringLiteral("</") + tag + QChar('>'));
    c.paragraph();
    if (!anchor.isEmpty()) c.doc->sections.append({text.left(160), anchor});
}
void paragraph(Context &c, const QString &text)
{
    c.markup(QStringLiteral("<p>")); c.text(text); c.markup(QStringLiteral("</p>")); c.paragraph();
}
QString titleText(const Node &n)
{
    QString text = n.text;
    for (const auto &child : n.children) {
        if (child.name == QLatin1String("p") && !text.isEmpty()) text += QChar(' ');
        text += titleText(child);
    }
    return text;
}
void fb2Node(Context &c, const Node &n, int level, bool inlineText = false)
{
    c.check();
    if (n.name.isEmpty()) { if (inlineText || !n.text.trimmed().isEmpty()) c.text(n.text); return; }
    if (n.name == QLatin1String("binary") || n.name == QLatin1String("description")) return;
    if (n.name == QLatin1String("section")) ++level;
    if (n.name == QLatin1String("title")) { heading(c, titleText(n).simplified(), std::max(1, level)); return; }
    if (n.name == QLatin1String("image")) { paragraph(c, QStringLiteral("[Image]")); return; }
    if (n.name == QLatin1String("empty-line")) { c.markup(QStringLiteral("<br>")); c.paragraph(); return; }
    QString tag;
    const auto &name = n.name;
    if (name == QLatin1String("p") || name == QLatin1String("v") || name == QLatin1String("text-author")) tag = QStringLiteral("p");
    else if (name == QLatin1String("strong") || name == QLatin1String("subtitle")) tag = QStringLiteral("b");
    else if (name == QLatin1String("emphasis")) tag = QStringLiteral("i");
    else if (name == QLatin1String("strikethrough")) tag = QStringLiteral("s");
    else if (name == QLatin1String("sup") || name == QLatin1String("sub") || name == QLatin1String("code")) tag = name;
    else if (name == QLatin1String("cite") || name == QLatin1String("epigraph")) tag = QStringLiteral("blockquote");
    const bool block = tag == QLatin1String("p") || tag == QLatin1String("blockquote") || name == QLatin1String("subtitle");
    if (block) c.paragraph();
    if (!tag.isEmpty()) c.markup(QChar('<') + tag + QChar('>'));
    for (const auto &child : n.children) fb2Node(c, child, level, inlineText || tag == QLatin1String("p") || name == QLatin1String("subtitle"));
    if (!tag.isEmpty()) c.markup(QStringLiteral("</") + tag + QChar('>'));
    if (block) c.paragraph();
    if (name == QLatin1String("tr") || name == QLatin1String("stanza")) { c.markup(QStringLiteral("<br>")); c.paragraph(); }
    if (name == QLatin1String("td") || name == QLatin1String("th")) c.text(QStringLiteral("\t"));
}
void sfbInline(Context &c, const QString &text)
{
    // Only matched emphasis markers are interpreted. Everything else remains
    // escaped text, including unfamiliar SFB commands and link syntax.
    qsizetype start = 0;
    while (start < text.size()) {
        c.check();
        auto at = text.indexOf(QChar('_'), start);
        if (at < 0) { c.text(text.mid(start)); break; }
        const int width = text.mid(at, 2) == QLatin1String("__") ? 2 : 1;
        const auto marker = QString(width, QChar('_'));
        const auto end = text.indexOf(marker, at + width);
        if (end <= at + width) { c.text(text.mid(start, at + width - start)); start = at + width; continue; }
        c.text(text.mid(start, at - start));
        const auto tag = width == 2 ? QStringLiteral("b") : QStringLiteral("i");
        c.markup(QChar('<') + tag + QChar('>')); c.text(text.mid(at + width, end - at - width));
        c.markup(QStringLiteral("</") + tag + QChar('>')); start = end + width;
    }
}
QString entities(const QString &source)
{
    static const QHash<QString, QString> named{{"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},
        {"nbsp",QString(QChar(160))},{"mdash",QString(QChar(0x2014))},{"ndash",QString(QChar(0x2013))},
        {"hellip",QString(QChar(0x2026))},{"ldquo",QString(QChar(0x201c))},{"rdquo",QString(QChar(0x201d))},
        {"lsquo",QString(QChar(0x2018))},{"rsquo",QString(QChar(0x2019))}};
    QString result;
    for (qsizetype i = 0; i < source.size(); ++i) {
        if (source.at(i) == QChar('&')) {
            const auto relative = QStringView(source).mid(i + 1, 16).indexOf(QChar(';'));
            const auto end = relative < 0 ? qsizetype(-1) : i + 1 + relative;
            if (end > i && end - i <= 16) {
                const auto key = source.mid(i + 1, end - i - 1);
                QString value = named.value(key);
                if (key.startsWith(QChar('#'))) {
                    bool ok = false;
                    const bool hex = key.mid(1, 1).compare(QLatin1String("x"), Qt::CaseInsensitive) == 0;
                    const auto cp = key.mid(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
                    if (ok && cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
                        const char32_t scalar = cp; value = QString::fromUcs4(&scalar, 1);
                    }
                }
                if (!value.isEmpty()) { result += value; i = end; continue; }
            }
        }
        result += source.at(i);
    }
    return result;
}
// MOBI HTML is often not XML. Tokenize it without handing source markup to Qt:
// retain a small structural vocabulary, discard all attributes and resources.
class MobiMarkup {
    Context &c;
    QString html;
    QChar lastCharacter;
    bool inlineText() const {
        return c.doc->plainText.isEmpty() ? !lastCharacter.isNull() && lastCharacter != QChar('\n')
            : !c.doc->plainText.endsWith(QChar('\n'));
    }
    const QSet<QString> allowed{"p","div","h1","h2","h3","h4","h5","h6","b","strong","i","em","u","s","sup","sub","blockquote","ul","ol","li","pre","code"};
    const QSet<QString> blocked{"head","script","style","iframe","object"};
    QString suppressed;
    QStringList stack;
public:
    explicit MobiMarkup(Context &context) : c(context) {}
    void feed(const QString &text, bool final = false) {
        html += text;
    qsizetype cursor = 0;
    while (cursor < html.size()) {
        c.check();
        if (++c.nodes > 200000) c.limit();
        const auto open = html.indexOf(QChar('<'), cursor);
        auto endText = open < 0 ? html.size() : open;
        if (open < 0 && !final) {
            const auto amp = html.lastIndexOf(QChar('&'));
            if (amp >= cursor && html.size() - amp <= 17 && !html.mid(amp).contains(QChar(';'))) endText = amp;
        }
        if (suppressed.isEmpty()) {
            const auto text = entities(html.mid(cursor, endText - cursor));
            if (!text.trimmed().isEmpty()) c.text(text);
            else if (!text.isEmpty() && inlineText()) c.text(QStringLiteral(" "));
        }
        if (open < 0) { cursor = endText; break; }
        if (html.mid(open, 4) == QLatin1String("<!--")) {
            const auto end = html.indexOf(QLatin1String("-->"), open + 4);
            if (end < 0) { cursor = open; break; }
            cursor = end + 3; continue;
        }
        auto end = open + 1;
        QChar quote;
        for (; end < html.size(); ++end) {
            const auto ch = html.at(end);
            if (!quote.isNull()) { if (ch == quote) quote = {}; }
            else if (ch == QChar('\'') || ch == QChar('"')) quote = ch;
            else if (ch == QChar('>')) break;
        }
        if (end == html.size()) { cursor = open; break; }
        auto token = html.mid(open + 1, end - open - 1).trimmed();
        const bool closing = token.startsWith(QChar('/'));
        if (closing) token.remove(0, 1);
        qsizetype length = 0;
        while (length < token.size() && (token.at(length).isLetterOrNumber() || token.at(length) == QChar(':'))) ++length;
        const auto name = token.left(length).toLower();
        cursor = end + 1;
        if (!suppressed.isEmpty()) { if (closing && name == suppressed) suppressed.clear(); continue; }
        if (blocked.contains(name)) { if (!closing && !token.endsWith(QChar('/'))) suppressed = name; continue; }
        if (name == QLatin1String("br") || name == QLatin1String("hr") || name == QLatin1String("mbp:pagebreak") || name == QLatin1String("tr")) {
            if (inlineText()) {
                c.markup(QStringLiteral("<br>")); c.paragraph();
            }
            continue;
        }
        if (name == QLatin1String("img")) { c.text(QStringLiteral("[Image]")); continue; }
        if (name == QLatin1String("td") || name == QLatin1String("th")) { c.text(QStringLiteral("\t")); continue; }
        // Mobipocket exporters use deeply nested blockquotes as a layout tool.
        // Keep their paragraph boundaries without accumulating indentation.
        if (name == QLatin1String("blockquote")) {
            if (inlineText()) {
                c.markup(QStringLiteral("<br>")); c.paragraph();
            }
            continue;
        }
        if (!allowed.contains(name)) continue;
        const bool block = name == QLatin1String("p") || name == QLatin1String("div") || name.startsWith(QChar('h')) || name == QLatin1String("li");
        if (block) c.paragraph();
        if (closing) {
            const auto index = stack.lastIndexOf(name);
            if (index < 0) continue;
            while (stack.size() > index) c.markup(QStringLiteral("</") + stack.takeLast() + QChar('>'));
        } else {
            // Old Mobipocket HTML commonly omits closing paragraph tags.
            if (block) {
                const auto paragraphIndex = stack.lastIndexOf(QStringLiteral("p"));
                if (paragraphIndex >= 0)
                    while (stack.size() > paragraphIndex) c.markup(QStringLiteral("</") + stack.takeLast() + QChar('>'));
            }
            if (stack.size() >= 64) c.limit();
            c.markup(QChar('<') + name + QChar('>'));
            if (token.endsWith(QChar('/'))) c.markup(QStringLiteral("</") + name + QChar('>'));
            else stack.append(name);
        }
    }
    if (!c.doc->plainText.isEmpty()) lastCharacter = c.doc->plainText.back();
    html.remove(0, cursor);
    if (html.size() > DocumentReader::CharacterLimit) c.limit();
    if (final) html.clear(); // Match the existing incomplete-tag simplification.
}
    void begin() { for (const auto &tag : stack) c.markup(QChar('<') + tag + QChar('>')); }
    void end() { for (auto i = stack.crbegin(); i != stack.crend(); ++i) c.markup(QStringLiteral("</") + *i + QChar('>')); }
};
QByteArray palmDoc(Context &c, const QByteArray &record)
{
    QByteArray output;
    for (qsizetype i = 0; i < record.size();) {
        c.check();
        const auto code = quint8(record.at(i++));
        if (code >= 1 && code <= 8) {
            if (record.size() - i < code) c.invalid();
            output += record.mid(i, code); i += code;
        } else if (code < 128) output += char(code);
        else if (code >= 192) { output += ' '; output += char(code ^ 128); }
        else {
            if (i == record.size()) c.invalid();
            const auto pair = (quint16(code) << 8) | quint8(record.at(i++));
            const auto distance = (pair >> 3) & 2047;
            const auto count = (pair & 7) + 3;
            if (distance == 0 || distance > output.size()) c.invalid();
            for (int j = 0; j < count; ++j) output += output.at(output.size() - distance);
        }
        if (output.size() > 4096) c.limit();
    }
    return output;
}
}
namespace {
void fb2Metadata(Context &c, const Node &root)
{
    const auto &info = root.child("description").child("title-info");
    c.doc->title = info.child("book-title").allText().simplified().left(1024);
    for (const auto &author : info.children) {
        if (author.name != QLatin1String("author")) continue;
        QStringList parts;
        for (const auto &part : author.children)
            if (part.name == QLatin1String("first-name") || part.name == QLatin1String("middle-name") || part.name == QLatin1String("last-name")) parts.append(part.allText().simplified());
        if (parts.isEmpty()) parts.append(author.child("nickname").allText().simplified());
        if (!c.doc->author.isEmpty()) c.doc->author += QStringLiteral(", ");
        c.doc->author += parts.join(QChar(' ')); c.doc->author.truncate(1024);
    }
}
class Fb2Parser final : public PortionParser {
    Context &c;
    QXmlStreamReader xml;
    int level = 0;
    bool body = false;
public:
    Fb2Parser(Context &context, QIODevice &input) : c(context), xml(&input) {
        if (!nextDocumentElement(c, xml) || xml.name() != QLatin1String("FictionBook")
            || (!xml.namespaceUri().isEmpty() && xml.namespaceUri() != QLatin1String("http://www.gribuser.ru/xml/fictionbook/2.0"))) c.invalid();
    }
    bool next() override {
        while (!xml.atEnd()) {
            c.check(); xml.readNext();
            if (xml.isDTD() || xml.isEntityReference()) c.invalid();
            if (xml.isEndElement()) {
                if (xml.name() == QLatin1String("section")) --level;
                if (xml.name() == QLatin1String("body")) body = false;
            }
            if (!xml.isStartElement()) continue;
            const auto name = xml.name().toString();
            if (name == QLatin1String("body")) { body = true; continue; }
            if (body && name == QLatin1String("section")) { if (++level > 64) c.limit(); continue; }
            c.nodes = 0;
            if (!body && name != QLatin1String("description")) { skipDocumentElement(c, xml); continue; }
            auto node = c.element(xml);
            if (name == QLatin1String("description")) {
                Node root; root.children.push_back(std::move(node)); fb2Metadata(c, root);
            } else { fb2Node(c, node, level); return true; }
        }
        if (xml.hasError()) c.invalid();
        return false;
    }
};
}
std::unique_ptr<PortionParser> fb2Parser(Context &c, QIODevice &input) { return std::make_unique<Fb2Parser>(c, input); }
void readFb2(Context &c, const QByteArray &bytes)
{
    const auto xml = c.xml(bytes);
    const auto &root = xml.child("FictionBook");
    if (root.name.isEmpty() || (!root.ns.isEmpty() && root.ns != QLatin1String("http://www.gribuser.ru/xml/fictionbook/2.0"))) c.invalid();
    fb2Metadata(c, root);
    for (const auto &body : root.children) if (body.name == QLatin1String("body")) fb2Node(c, body, 0);
}
namespace {
class SfbParser final : public PortionParser {
    Context &c;
    DocumentLines lines;
    int titleLine = 0;
    QString pendingHeading;
    int headingLevel = 0;
public:
    SfbParser(Context &context, QIODevice &input) : c(context), lines(context, input, true) {}
    bool next() override {
        auto flushHeading = [&] {
            if (!pendingHeading.isEmpty()) heading(c, pendingHeading, headingLevel);
            pendingHeading.clear(); headingLevel = 0;
        };
        const QSet<QString> blockMarkers{"D>","D$","E>","E$","P>","P$","I>","I$","A>","A$","C>","C$"};
        const auto before = c.doc->plainText.size();
        while (auto nextLine = lines.next()) {
            const QStringView line(*nextLine);
            c.check();
            if (line.trimmed().isEmpty()) { flushHeading(); continue; }
            const auto tab = line.indexOf(QChar('\t'));
            const auto command = tab < 0 ? QString() : line.left(tab).toString();
            auto content = tab < 0 ? line.toString() : line.mid(tab + 1).toString();
            if (!command.isEmpty() && command.size() <= 5 && command == QString(command.size(), QChar('>'))) {
                if (headingLevel != command.size()) flushHeading();
                headingLevel = command.size();
                if (!pendingHeading.isEmpty()) pendingHeading += QChar(' ');
                pendingHeading += content;
                if (pendingHeading.size() > DocumentReader::CharacterLimit) c.limit();
                continue;
            }
            flushHeading();
            if (blockMarkers.contains(line.trimmed().toString())) continue;
            if (command == QLatin1String("|")) {
                if (titleLine++ == 0) { c.doc->author = content.left(1024); paragraph(c, content); }
                else { if (c.doc->title.isEmpty()) c.doc->title = content.left(1024); heading(c, content, 1); }
            } else {
                // Unknown commands retain their entire line as plain text.
                if (!command.isEmpty() && command != QLatin1String("N") && command != QLatin1String("@")) content = line.toString();
                c.markup(QStringLiteral("<p>")); sfbInline(c, content); c.markup(QStringLiteral("</p>")); c.paragraph();
            }
            if (c.doc->plainText.size() > before) return true;
        }
        flushHeading();
        return false;
    }
};
}
std::unique_ptr<PortionParser> sfbParser(Context &c, QIODevice &input) { return std::make_unique<SfbParser>(c, input); }
void readSfb(Context &c, const QByteArray &bytes)
{
    QBuffer input; input.setData(bytes); input.open(QIODevice::ReadOnly);
    SfbParser parser(c, input);
    while (parser.next()) {}
}

namespace {
class MobiParser final : public PortionParser {
    Context &c;
    QFile &file;
    QList<qint64> offsets;
    qint64 sourceSize = 0;
    quint16 compression = 0, textRecords = 0;
    quint32 encoding = 0, textLength = 0, flags = 0;
    int i = 1;
    quint64 consumed = 0;
    IncrementalTextDecoder decoder{QStringDecoder::Utf8};
    MobiMarkup renderer;
    bool finished = false;
public:
    MobiParser(Context &context, QFile &source) : c(context), file(source), renderer(context) {
        QByteArray bytes = file.read(78);
        if (file.error() != QFileDevice::NoError) c.invalid();
        sourceSize = file.size();
        auto u16 = [&](qsizetype pos) -> quint16 { if (pos < 0 || pos > bytes.size() - 2) c.invalid(); return qFromBigEndian<quint16>(bytes.constData() + pos); };
        auto u32 = [&](qsizetype pos) -> quint32 { if (pos < 0 || pos > bytes.size() - 4) c.invalid(); return qFromBigEndian<quint32>(bytes.constData() + pos); };
        if (bytes.size() < 78 || bytes.mid(60, 8) != QByteArray("BOOKMOBI")) c.invalid();
        const auto records = u16(76);
        if (records < 2 || records > 8192 || 78 + qint64(records) * 8 + 2 > sourceSize) c.invalid();
        bytes += file.read(qint64(records) * 8 + 2);
        if (bytes.size() != 78 + records * 8 + 2 || file.error() != QFileDevice::NoError) c.invalid();
        for (int i = 0; i < records; ++i) {
            c.check(); const qint64 offset = u32(78 + i * 8);
            if (offset < 78 + records * 8 + 2 || offset >= sourceSize || (!offsets.isEmpty() && offset <= offsets.last())) c.invalid();
            offsets.append(offset);
        }
        offsets.append(sourceSize);
        const auto headerOffset = offsets.first();
        const auto headerSize = offsets.at(1) - headerOffset;
        if (!file.seek(headerOffset)) c.invalid();
        bytes = file.read(qMin<qint64>(headerSize, 244));
        if (file.error() != QFileDevice::NoError || bytes.size() != qMin<qint64>(headerSize, 244)) c.invalid();
        const qsizetype header = 0;
        if (headerSize < 40 || bytes.mid(header + 16, 4) != QByteArray("MOBI")) c.invalid();
        const auto mobiSize = u32(header + 20);
        if (mobiSize < 24 || mobiSize > headerSize - 16) c.invalid();
        compression = u16(header);
        if (u16(header + 12) != 0) throw Stop{QStringLiteral("Encrypted MOBI books cannot be previewed.")};
        if (u32(header + 36) > 6 || (compression != 1 && compression != 2))
            throw Stop{QStringLiteral("This MOBI variant is not supported. Preview supports unencrypted MOBI 6 with PalmDOC or uncompressed text.")};
        encoding = u32(header + 28);
        if (encoding != 65001 && encoding != 1252) throw Stop{QStringLiteral("This MOBI text encoding is not supported.")};
        textRecords = u16(header + 8);
        textLength = u32(header + 4);
        if (textRecords == 0 || textRecords >= records || u16(header + 10) > 4096) c.invalid();
        flags = mobiSize >= 228 ? u16(header + 242) : 0;
        if (mobiSize >= 92) {
            const auto titleOffset = u32(header + 84), titleLength = u32(header + 88);
            if (titleLength <= 4096 && quint64(titleOffset) + titleLength <= quint64(headerSize))
            {
                if (!file.seek(headerOffset + titleOffset)) c.invalid();
                const auto title = file.read(titleLength);
                if (title.size() != titleLength || file.error() != QFileDevice::NoError) c.invalid();
                c.doc->title = decode(title, encoding == 65001 ? "UTF-8" : "WINDOWS-1252").simplified().left(1024);
            }
        }
    }
    void begin() override { renderer.begin(); }
    void end() override { renderer.end(); }
    bool next() override {
        if (finished) return false;
        if (i > textRecords || consumed >= textLength) {
            if (consumed < textLength) c.invalid();
            if (encoding == 65001) {
                if (!decoder.complete()) c.invalid();
            }
            renderer.feed({}, true); finished = true; return false;
        }
        c.check();
        const auto length = offsets.at(i + 1) - offsets.at(i);
        if (length > 64 * 1024) c.limit();
        if (!file.seek(offsets.at(i))) c.invalid();
        auto record = file.read(length);
        if (record.size() != length || file.error() != QFileDevice::NoError) c.invalid();
        for (quint32 mask = 0x8000; mask >= 2; mask >>= 1) {
            if (!(flags & mask)) continue;
            quint32 length = 0; bool found = false;
            for (int j = 0; j < 4 && j < record.size(); ++j) {
                const auto byte = quint8(record.at(record.size() - 1 - j));
                length |= quint32(byte & 127) << (7 * j);
                if (byte & 128) { found = true; if (length < quint32(j + 1)) c.invalid(); break; }
            }
            if (!found || length > quint32(record.size())) c.invalid();
            record.chop(length);
        }
        if (flags & 1) {
            if (record.isEmpty()) c.invalid();
            const auto overlap = (quint8(record.back()) & 3) + 1;
            if (record.size() < overlap) c.invalid();
            record.chop(overlap);
        }
        const auto expanded = compression == 2 ? palmDoc(c, record) : record;
        if (expanded.size() > 4096) c.limit();
        const auto retained = expanded.left(qsizetype(qMin(quint64(expanded.size()), quint64(textLength) - consumed)));
        consumed += retained.size(); ++i;
        c.sourceRead += length;
        if (c.sourceRead > 64 * 1024 * 1024) c.limit();
        QString text = encoding == 65001 ? QString(decoder.decode(retained)) : decode(retained, "WINDOWS-1252");
        if (decoder.hasError()) c.invalid();
        renderer.feed(text);
        return true;
    }
};
}
std::unique_ptr<PortionParser> mobiParser(Context &c, QFile &file) { return std::make_unique<MobiParser>(c, file); }
void readMobi(Context &c, QFile &file)
{
    MobiParser parser(c, file);
    parser.begin();
    while (parser.next()) {}
    parser.end();
}
}

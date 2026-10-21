#include "DocumentReaderInternal.h"
#include "SeekableZipSource.h"
#include "ZipEntryDevice.h"
#include "preview/lazy/TextPortionReader.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>
#include <zip.h>
#include <algorithm>
#include <optional>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include "preview/document/DocumentRecords.h"

namespace dzurkel::document {
const Node &Node::child(const char *key) const
{
    for (const auto &c : children) if (c.name == QLatin1String(key)) return c;
    static const Node empty;
    return empty;
}
QString Node::allText() const
{
    QString result = text;
    for (const auto &c : children) result += c.allText();
    return result;
}
void Context::check() const
{
    if (cancel && cancel->load()) throw Stop{{}, true};
    if (clock.elapsed() > 5000) throw Stop{QStringLiteral("Document preview loading timed out.")};
}
void Context::invalid() const { throw Stop{QStringLiteral("Cannot extract readable content from this damaged, encrypted or unsupported document.")}; }
void Context::limit() const { throw Stop{QStringLiteral("Document exceeds the preview resource limits.")}; }
void Context::markup(const QString &value)
{
    check();
    if (doc->html.size() + value.size() > 2 * 1024 * 1024) throw Stop{{}, false, true};
    doc->html += value;
}
void Context::text(const QString &value)
{
    check();
    const auto remaining = DocumentReader::CharacterLimit - doc->plainText.size();
    QString retained = value.left(remaining);
    if (retained.size() < value.size() && !retained.isEmpty() && retained.back().isHighSurrogate()) retained.chop(1);
    doc->plainText += retained;
    markup(retained.toHtmlEscaped());
    if (retained.size() < value.size()) throw Stop{{}, false, true};
}
void Context::paragraph()
{
    if (++blocks > 5000) throw Stop{{}, false, true};
    if (!doc->plainText.endsWith(QChar('\n'))) {
        if (doc->plainText.size() >= DocumentReader::CharacterLimit) throw Stop{{}, false, true};
        doc->plainText += QChar('\n');
    }
}
void Context::notice(const QString &value)
{
    if (!doc->notice.contains(value)) {
        if (!doc->notice.isEmpty()) doc->notice += QChar(' ');
        doc->notice += value;
    }
}
Node Context::xml(const QByteArray &bytes, bool xhtml)
{
    class HtmlEntities : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &name) override {
            static const QHash<QString, ushort> known{{"nbsp",160},{"shy",173},{"ndash",0x2013},{"mdash",0x2014},
                {"lsquo",0x2018},{"rsquo",0x2019},{"ldquo",0x201c},{"rdquo",0x201d},{"hellip",0x2026},
                {"copy",169},{"reg",174},{"bull",0x2022},{"laquo",171},{"raquo",187},{"thinsp",0x2009},
                {"ensp",0x2002},{"emsp",0x2003},{"euro",0x20ac}};
            return known.contains(name) ? QString(QChar(known.value(name))) : QString();
        }
    } entities;
    QXmlStreamReader xml(bytes);
    if (xhtml) xml.setEntityResolver(&entities);
    Node root;
    while (!xml.atEnd()) {
        check(); xml.readNext();
        if (xml.isDTD() && (!xhtml || xml.dtdName() != QLatin1String("html") || xml.text().contains(QChar('[')))) invalid();
        if (xml.isEntityReference()) invalid();
        if (xml.isStartElement()) root.children.push_back(element(xml));
    }
    if ((xml.hasError() && !(inputTruncated && xml.error() == QXmlStreamReader::PrematureEndOfDocumentError))
        || root.children.empty()) invalid();
    return root;
}
Node Context::element(QXmlStreamReader &xml)
{
    Node root;
    std::vector<Node *> stack{&root};
    qint64 characters = 0;
    do {
        check();
        if (xml.isDTD() || xml.isEntityReference()) invalid();
        if (xml.isStartElement()) {
            if (++nodes > 200000 || stack.size() > 64) limit();
            Node node;
            node.name = xml.name().toString(); node.ns = xml.namespaceUri().toString();
            for (const auto &a : xml.attributes()) {
                characters += a.value().size();
                node.attributes.insert(a.name().toString(), a.value().toString());
            }
            stack.back()->children.push_back(std::move(node));
            stack.push_back(&stack.back()->children.back());
        } else if (xml.isCharacters()) {
            if (++nodes > 200000) limit();
            Node node; node.text = xml.text().toString(); characters += node.text.size();
            stack.back()->children.push_back(std::move(node));
        } else if (xml.isEndElement()) {
            if (stack.size() <= 1) invalid();
            stack.pop_back();
            if (stack.size() == 1) break;
        }
        if (characters > 4 * 1024 * 1024) limit();
        xml.readNext();
    } while (!xml.atEnd());
    if (xml.hasError() && !(inputTruncated && xml.error() == QXmlStreamReader::PrematureEndOfDocumentError)) invalid();
    if (root.children.empty()) invalid();
    treeUnits = characters;
    return std::move(root.children.front());
}

namespace {
bool word(const Node &n)
{
    return n.ns == QLatin1String("http://schemas.openxmlformats.org/wordprocessingml/2006/main")
        || n.ns == QLatin1String("http://purl.oclc.org/ooxml/wordprocessingml/main");
}
bool enabled(const Node &n, const char *property)
{
    const auto &p = n.child(property);
    return !p.name.isEmpty() && p.attr("val") != QLatin1String("0") && p.attr("val") != QLatin1String("false")
        && p.attr("val") != QLatin1String("off");
}
struct Relationship { QString path; QString type; };
class Package {
public:
    Package(Context &context, QFile &file) : c(context), source_(file, context.cancel, context.clock)
    {
        zip_error_t error;
        zip_error_init(&error);
        auto *source = zip_source_function_create(&SeekableZipSource::callback, &source_, &error);
        if (source) archive = zip_open_from_source(source, ZIP_RDONLY, &error);
        if (!archive && source) zip_source_free(source);
        zip_error_fini(&error);
        if (!archive) { c.check(); if (source_.exceeded) c.limit(); c.invalid(); }
        source_.remaining = SeekableZipSource::SourceReadLimit;
        // All failure paths after opening must leave the archive owned.
        try {
            const auto count = zip_get_num_entries(archive, 0);
            if (count < 0 || count > 2048) c.limit();
            for (zip_int64_t i = 0; i < count; ++i) {
                c.check();
                zip_stat_t stat;
                zip_stat_init(&stat);
                if (zip_stat_index(archive, i, 0, &stat) || !(stat.valid & ZIP_STAT_SIZE) || !stat.name) c.invalid();
                const auto name = QString::fromUtf8(stat.name);
                if (entries.contains(name)) c.invalid();
                entries.insert(name, zip_uint64_t(i));
            }
        } catch (...) { zip_discard(archive); archive = nullptr; throw; }
    }
    ~Package() { if (archive) zip_discard(archive); }
    bool contains(const QString &path) const { return entries.contains(path); }
    quint64 partSize(const QString &path) const
    {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (!entries.contains(path) || zip_stat_index(archive, entries.value(path), 0, &stat) || !(stat.valid & ZIP_STAT_SIZE)) c.invalid();
        return stat.size;
    }
    QByteArray part(const QString &path)
    {
        c.check();
        if (!entries.contains(path)) c.invalid();
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive, entries.value(path), 0, &stat) || stat.size > 8 * 1024 * 1024) c.limit();
        std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(zip_fopen_index(archive, entries.value(path), 0), zip_fclose);
        if (!file) c.invalid();
        QByteArray data;
        char buffer[65536];
        for (;;) {
            c.check();
            const auto size = zip_fread(file.get(), buffer, sizeof(buffer));
            if (size < 0) { c.check(); if (source_.exceeded) c.limit(); c.invalid(); }
            if (!size) break;
            consumed += size;
            if (data.size() + size > 8 * 1024 * 1024 || consumed > 64 * 1024 * 1024) c.limit();
            data.append(buffer, qsizetype(size));
        }
        if (zip_fclose(file.release()) || zip_uint64_t(data.size()) != stat.size) c.invalid();
        return data;
    }
    std::unique_ptr<ZipEntryDevice> stream(const QString &path) {
        if (!entries.contains(path)) c.invalid();
        return std::make_unique<ZipEntryDevice>(archive, entries.value(path), consumed, 64 * 1024 * 1024,
            [](const QString &message) { throw Stop{message}; });
    }
    QString resolve(const QString &owner, const QString &href) const
    {
        QUrl url(href);
        if (!url.isRelative() || url.hasQuery() || !url.authority().isEmpty() || href.contains(QChar('\\'))) return {};
        const auto path = url.path(QUrl::FullyDecoded);
        if (path.contains(QChar::Null) || path.contains(QChar('\\'))) return {};
        const auto target = QDir::cleanPath(path.startsWith(QChar('/')) ? path.mid(1)
            : owner.left(owner.lastIndexOf(QChar('/')) + 1) + path);
        if (target == QLatin1String("..") || target.startsWith(QLatin1String("../")) || target.startsWith(QChar('/'))) return {};
        return target;
    }
    QHash<QString, Relationship> relationships(const QString &owner)
    {
        const auto slash = owner.lastIndexOf(QChar('/'));
        const auto path = owner.left(slash + 1) + QStringLiteral("_rels/") + owner.mid(slash + 1) + QStringLiteral(".rels");
        QHash<QString, Relationship> result;
        if (!contains(path)) return result;
        const auto root = c.xml(part(path));
        for (const auto &r : root.child("Relationships").children) {
            if (r.name != QLatin1String("Relationship")) continue;
            if (r.attr("TargetMode") == QLatin1String("External")) continue;
            const auto target = resolve(owner, r.attr("Target"));
            if (target.isEmpty() || r.attr("Id").isEmpty() || result.contains(r.attr("Id"))) c.invalid();
            result.insert(r.attr("Id"), {target, r.attr("Type")});
        }
        return result;
    }
    void image(const QString &path)
    {
        auto placeholder = [&] { c.text(QStringLiteral(" [Image] ")); };
        if (imageNames.contains(path)) { c.markup(imageNames.value(path)); return; }
        if (path.isEmpty() || !contains(path) || c.doc->images.size() >= 16) { placeholder(); return; }
        if (partSize(path) > 4 * 1024 * 1024) { placeholder(); return; }
        QByteArray bytes = part(path);
        if (bytes.size() > 4 * 1024 * 1024) { placeholder(); return; }
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto format = reader.format().toLower();
        if (!QList<QByteArray>{"png", "jpeg", "jpg", "gif", "webp", "bmp"}.contains(format)) { placeholder(); return; }
        const auto size = reader.size();
        if (size.width() <= 0 || size.height() <= 0 || qint64(size.width()) * size.height() > 2 * 1024 * 1024
            || pixels + qint64(size.width()) * size.height() > pixelLimit) { placeholder(); return; }
        c.check();
        auto image = reader.read();
        if (image.isNull()) { placeholder(); return; }
        pixels += qint64(image.width()) * image.height();
        const auto shown = image.size().scaled(QSize(640, 640), Qt::KeepAspectRatio).boundedTo(image.size());
        const auto id = QStringLiteral("dzurkel-image:%1").arg(c.doc->images.size());
        c.doc->images.insert(id, image);
        const auto html = QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" />").arg(id).arg(shown.width()).arg(shown.height());
        imageNames.insert(path, html);
        c.markup(html);
    }
    void beginRequest() {
        c.clock.restart();
        consumed = 0;
        source_.remaining = SeekableZipSource::SourceReadLimit;
        source_.exceeded = false;
    }
    void beginPortion() { imageNames.clear(); pixels = 0; }
    qint64 pixelLimit = 8 * 1024 * 1024;
    Context &c;
private:
    SeekableZipSource source_;
    zip_t *archive = nullptr;
    QHash<QString, zip_uint64_t> entries;
    QHash<QString, QString> imageNames;
    qint64 consumed = 0;
    qint64 pixels = 0;
};

QString anchor(const QString &path, const QString &fragment = {})
{
    return QStringLiteral("a") + QString::fromLatin1(path.toUtf8().toHex()) + QChar('-') + QString::fromLatin1(fragment.toUtf8().toHex());
}
void section(Context &c, const QString &title, const QString &id)
{
    c.markup(QStringLiteral("<a name=\"%1\"></a>").arg(id));
    if (c.doc->sections.size() < 64 && !title.trimmed().isEmpty()) c.doc->sections.append({title.simplified().left(100), id});
}

class WordReader {
public:
    explicit WordReader(Package &package) : p(package), c(package.c) {}
    void initialize()
    {
        const auto rootRels = p.relationships({});
        for (const auto &r : rootRels) if (r.type.endsWith(QLatin1String("/officeDocument"))) main = r.path;
        if (main.isEmpty()) c.invalid();
        rels = p.relationships(main);
        for (const auto &r : rels) {
            if (r.type.endsWith(QLatin1String("/styles"))) {
                const auto root = c.xml(p.part(r.path));
                for (const auto &n : root.child("styles").children)
                    if (n.name == QLatin1String("style")) styles.insert(n.attr("styleId"), n);
            } else if (r.type.endsWith(QLatin1String("/numbering"))) {
                const auto root = c.xml(p.part(r.path));
                numbering = root.child("numbering");
            }
        }
    }
    void read()
    {
        initialize();
        const auto root = c.xml(p.part(main));
        const auto &document = root.child("document");
        if (!word(document) || document.child("body").name.isEmpty()) c.invalid();
        render(document.child("body"));
        for (const auto &r : rels) {
            if (!r.type.endsWith(QLatin1String("/footnotes")) && !r.type.endsWith(QLatin1String("/endnotes"))) continue;
            const auto notes = c.xml(p.part(r.path));
            c.markup(QStringLiteral("<h2>Notes</h2>"));
            for (const auto &container : notes.children) for (const auto &note : container.children) {
                if (note.name != QLatin1String("footnote") && note.name != QLatin1String("endnote")) continue;
                if (note.attr("id").toInt() <= 0) continue;
                c.text(QStringLiteral("[%1] ").arg(note.attr("id")));
                render(note);
            }
        }
    }
private:
    QList<const Node *> inherited(const QString &id) const
    {
        QList<const Node *> result;
        QString current = id;
        QSet<QString> seen;
        while (styles.contains(current) && result.size() < 16 && !seen.contains(current)) {
            seen.insert(current);
            const auto found = styles.constFind(current);
            const auto &style = found.value();
            result.prepend(&style);
            current = style.child("basedOn").attr("val");
        }
        return result;
    }
    Node paragraphProperties(const Node &n) const
    {
        Node props = n.child("pPr");
        for (const auto *style : inherited(props.child("pStyle").attr("val"))) {
            if (props.child("numPr").name.isEmpty() && !style->child("pPr").child("numPr").name.isEmpty())
                props.children.push_back(style->child("pPr").child("numPr"));
        }
        return props;
    }
    struct ListInfo { QString id; int level; bool ordered; int start; };
    std::optional<ListInfo> listInfo(const Node &n) const
    {
        const auto props = paragraphProperties(n);
        const auto &num = props.child("numPr");
        const auto id = num.child("numId").attr("val");
        if (id.isEmpty() || id == QLatin1String("0")) return {};
        const auto level = num.child("ilvl").attr("val");
        QString abstractId;
        for (const auto &n : numbering.children) if (n.name == QLatin1String("num") && n.attr("numId") == id)
            abstractId = n.child("abstractNumId").attr("val");
        QString format;
        int start = 1;
        for (const auto &n : numbering.children) if (n.name == QLatin1String("abstractNum") && n.attr("abstractNumId") == abstractId)
            for (const auto &l : n.children) if (l.name == QLatin1String("lvl") && l.attr("ilvl") == (level.isEmpty() ? QStringLiteral("0") : level)) {
                format = l.child("numFmt").attr("val");
                bool ok;
                const auto value = l.child("start").attr("val").toInt(&ok);
                if (ok && value > 0 && value < 1000000) start = value;
            }
        return ListInfo{id, std::clamp(level.toInt(), 0, 7), format == QLatin1String("decimal"), start};
    }
    void children(const Node &n, int tableDepth)
    {
        struct OpenList { QString id; QString tag; bool item = false; };
        QList<OpenList> lists;
        const auto close = [&] {
            const auto last = lists.takeLast();
            if (last.item) c.markup(QStringLiteral("</li>"));
            c.markup(QStringLiteral("</") + last.tag + QChar('>'));
        };
        for (const auto &child : n.children) {
            const auto info = word(child) && child.name == QLatin1String("p") ? listInfo(child) : std::optional<ListInfo>();
            if (!info) {
                // Ignore package XML indentation between consecutive list items.
                if (child.name.isEmpty() && child.text.trimmed().isEmpty()) continue;
                while (!lists.isEmpty()) close();
                render(child, tableDepth);
                continue;
            }
            const auto tag = info->ordered ? QStringLiteral("ol") : QStringLiteral("ul");
            const int depth = std::min(info->level + 1, int(lists.size()) + 1);
            while (lists.size() > depth) close();
            if (lists.size() == depth && (lists.back().id != info->id || lists.back().tag != tag)) close();
            const auto key = info->id + QChar(':') + QString::number(info->level);
            const int number = counters.contains(key) ? counters.value(key) + 1 : info->start;
            counters.insert(key, number);
            if (counters.size() > 4096) c.limit();
            for (int level = info->level + 1; level <= 7; ++level) counters.remove(info->id + QChar(':') + QString::number(level));
            if (lists.size() < depth) {
                c.markup(QChar('<') + tag + (info->ordered ? QStringLiteral(" start=\"%1\">").arg(number) : QStringLiteral(">")));
                lists.append({info->id, tag, false});
            }
            if (lists.back().item) c.markup(QStringLiteral("</li>"));
            c.markup(QStringLiteral("<li>"));
            lists.back().item = true;
            render(child, tableDepth, true);
        }
        while (!lists.isEmpty()) close();
    }
    bool hasVisibleContent(const Node &n) const
    {
        if ((n.name == QLatin1String("t") && !n.allText().trimmed().isEmpty())
            || QStringList{"drawing", "pict", "object", "br", "cr", "oMath", "footnoteReference", "endnoteReference", "sym"}.contains(n.name)) return true;
        for (const auto &child : n.children) if (hasVisibleContent(child)) return true;
        return false;
    }
    void render(const Node &n, int tableDepth = 0, bool listItem = false)
    {
        c.check();
        if (n.name.isEmpty()) return; // XML indentation is not document text.
        const auto name = n.name;
        if (word(n)) {
            if (name == QLatin1String("del") || name == QLatin1String("moveFrom")) { c.notice(QStringLiteral("Revisions present; showing current text.")); return; }
            if (name == QLatin1String("ins") || name == QLatin1String("moveTo")) c.notice(QStringLiteral("Revisions present; showing current text."));
            if (name.endsWith(QLatin1String("Pr")) || name == QLatin1String("instrText") || name == QLatin1String("delText")) return;
            if (name == QLatin1String("t")) { c.text(n.allText()); return; }
            if (name == QLatin1String("tab")) { c.text(QStringLiteral("\t")); return; }
            if (name == QLatin1String("br") || name == QLatin1String("cr")) { c.markup(QStringLiteral("<br/>")); c.paragraph(); return; }
            if (name == QLatin1String("footnoteReference") || name == QLatin1String("endnoteReference")) {
                c.text(QStringLiteral("[%1]").arg(n.attr("id"))); return;
            }
            if (name == QLatin1String("object") || name == QLatin1String("altChunk")) { c.text(QStringLiteral(" [Embedded object] ")); return; }
            if (name == QLatin1String("p")) {
                const bool empty = !hasVisibleContent(n);
                if (empty && emptyParagraph_) return;
                emptyParagraph_ = empty;
                c.paragraph();
                const auto props = paragraphProperties(n);
                const auto styleId = props.child("pStyle").attr("val");
                int heading = 0;
                QString role;
                const auto chain = inherited(styleId);
                for (const auto *s : chain) {
                    const auto &pp = s->child("pPr");
                    if (!pp.child("outlineLvl").name.isEmpty()) heading = pp.child("outlineLvl").attr("val").toInt() + 1;
                    for (const auto &styleName : {s->attr("styleId"), s->child("name").attr("val")}) {
                        if (styleName.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0) role = QStringLiteral("title");
                        if (styleName.compare(QLatin1String("Subtitle"), Qt::CaseInsensitive) == 0) role = QStringLiteral("subtitle");
                    }
                }
                if (styleId.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0) role = QStringLiteral("title");
                if (styleId.compare(QLatin1String("Subtitle"), Qt::CaseInsensitive) == 0) role = QStringLiteral("subtitle");
                if (!props.child("outlineLvl").name.isEmpty()) heading = props.child("outlineLvl").attr("val").toInt() + 1;
                // Common styles without outline metadata still convey structure.
                if (!heading && styleId.startsWith(QLatin1String("Heading"), Qt::CaseInsensitive)) heading = styleId.mid(7).toInt();
                const auto tag = role == QLatin1String("title") ? QStringLiteral("h1")
                    : heading >= 1 && heading <= 6 ? QStringLiteral("h%1").arg(heading) : QStringLiteral("p");
                if (!listItem) c.markup(QChar('<') + tag + QChar('>'));
                if (!role.isEmpty()) {
                    section(c, role == QLatin1String("title") ? n.allText() : QString(), QStringLiteral("dzurkel-%1-%2").arg(role).arg(++headings_));
                    if (role == QLatin1String("title") && c.doc->title.isEmpty()) c.doc->title = n.allText().simplified().left(500);
                } else if (tag.startsWith(QChar('h'))) section(c, n.allText(), QStringLiteral("heading%1").arg(++headings_));
                const auto previousStyles = paragraphStyles_;
                paragraphStyles_ = chain;
                children(n, tableDepth);
                paragraphStyles_ = previousStyles;
                if (!listItem) c.markup(QStringLiteral("</") + tag + QChar('>'));
                c.paragraph();
                return;
            }
            if (name == QLatin1String("r")) {
                const auto &props = n.child("rPr");
                if (enabled(props, "vanish") || enabled(props, "webHidden")) return;
                bool bold = false, italic = false, underline = false, strike = false;
                QString color;
                const auto readProperties = [&](const Node &r) {
                    if (!r.child("b").name.isEmpty()) bold = enabled(r, "b");
                    if (!r.child("i").name.isEmpty()) italic = enabled(r, "i");
                    if (!r.child("u").name.isEmpty()) underline = enabled(r, "u") && r.child("u").attr("val") != QLatin1String("none");
                    if (!r.child("strike").name.isEmpty()) strike = enabled(r, "strike");
                    if (!r.child("color").name.isEmpty()) color = r.child("color").attr("val");
                };
                for (const auto *s : paragraphStyles_) readProperties(s->child("rPr"));
                for (const auto *s : inherited(props.child("rStyle").attr("val"))) {
                    const auto &r = s->child("rPr");
                    readProperties(r);
                }
                readProperties(props);
                bool rgb = color.size() == 6;
                for (auto ch : color) rgb &= (ch >= QChar('0') && ch <= QChar('9')) || (ch.toLower() >= QChar('a') && ch.toLower() <= QChar('f'));
                if (rgb) c.markup(QStringLiteral("<span style=\"color:#%1\">").arg(color));
                if (bold) c.markup(QStringLiteral("<b>"));
                if (italic) c.markup(QStringLiteral("<i>"));
                if (underline) c.markup(QStringLiteral("<u>"));
                if (strike) c.markup(QStringLiteral("<s>"));
                for (const auto &child : n.children) render(child, tableDepth);
                if (strike) c.markup(QStringLiteral("</s>"));
                if (underline) c.markup(QStringLiteral("</u>"));
                if (italic) c.markup(QStringLiteral("</i>"));
                if (bold) c.markup(QStringLiteral("</b>"));
                if (rgb) c.markup(QStringLiteral("</span>"));
                return;
            }
            if (name == QLatin1String("tbl") || name == QLatin1String("tr") || name == QLatin1String("tc")) {
                const auto depth = tableDepth + (name == QLatin1String("tbl"));
                if (depth > 4) {
                    c.notice(QStringLiteral("Complex tables simplified to text."));
                    children(n, depth);
                    c.paragraph(); return;
                }
                const auto tag = name == QLatin1String("tbl") ? QStringLiteral("table") : name == QLatin1String("tr") ? QStringLiteral("tr") : QStringLiteral("td");
                c.markup(QChar('<') + tag + (tag == QLatin1String("table") ? QStringLiteral(" border=\"1\" cellspacing=\"0\" cellpadding=\"4\">") : QStringLiteral(">")));
                children(n, depth);
                c.markup(QStringLiteral("</") + tag + QChar('>'));
                if (name == QLatin1String("tc")) c.text(QStringLiteral("\t"));
                else c.paragraph();
                return;
            }
        }
        if (name == QLatin1String("blip")) { p.image(rels.value(n.attr("embed")).path); return; }
        if (name == QLatin1String("imagedata")) { p.image(rels.value(n.attr("id")).path); return; }
        if (name == QLatin1String("oMath")) {
            const auto text = n.allText().simplified();
            c.text(text.isEmpty() ? QStringLiteral(" [Equation] ") : text);
            return;
        }
        if (name == QLatin1String("chart")) { c.text(QStringLiteral(" [Chart] ")); return; }
        if (name == QLatin1String("AlternateContent")) {
            // Choice and Fallback describe the same content. Never duplicate it.
            const auto &choice = n.child("Choice");
            render(choice.name.isEmpty() ? n.child("Fallback") : choice, tableDepth);
            return;
        }
        // Unsupported wrappers (including content controls and text boxes)
        // contribute recognized descendant text rather than raw XML.
        children(n, tableDepth);
    }
    Package &p;
    Context &c;
    QHash<QString, Relationship> rels;
    QMap<QString, Node> styles;
    Node numbering;
    QHash<QString, int> counters;
    QList<const Node *> paragraphStyles_;
    bool emptyParagraph_ = false;
    QString main;
    int headings_ = 0;
    std::unique_ptr<ZipEntryDevice> entry_;
    std::unique_ptr<QXmlStreamReader> xml_;
    QStringList parts_;
    int part_ = -1, position_ = 0, previous_ = 0;
    void openPart(int index) {
        xml_.reset(); entry_.reset();
        part_ = index;
        entry_ = p.stream(parts_.at(index));
        xml_ = std::make_unique<QXmlStreamReader>(entry_.get());
        if (!nextDocumentElement(c, *xml_)) c.invalid();
        if (!part_) {
            Node root; root.name = xml_->name().toString(); root.ns = xml_->namespaceUri().toString();
            if (!word(root) || root.name != QLatin1String("document") || !nextDocumentElement(c, *xml_)
                || xml_->name() != QLatin1String("body")) c.invalid();
        } else if (xml_->name() != QLatin1String("footnotes") && xml_->name() != QLatin1String("endnotes")) c.invalid();
    }
    std::optional<Node> nextBlock() {
        for (;;) {
            c.check();
            if (nextDocumentElement(c, *xml_)) {
                c.nodes = 0;
                auto node = c.element(*xml_);
                if (part_ && node.attr("id").toInt() <= 0) continue;
                if (part_) {
                    Node run; run.name = QStringLiteral("t"); run.ns = node.ns;
                    Node text; text.text = QStringLiteral("[%1] ").arg(node.attr("id"));
                    run.children.push_back(std::move(text)); node.children.insert(node.children.begin(), std::move(run));
                }
                return node;
            }
            // Complete the root/document, checking trailing damage before EOF.
            while (!xml_->atEnd()) { xml_->readNext(); if (xml_->isStartElement() || xml_->isDTD()) c.invalid(); }
            if (xml_->hasError()) c.invalid();
            if (part_ + 1 >= parts_.size()) return {};
            openPart(part_ + 1);
        }
    }
public:
    QJsonObject portion(const QString &token);

};

QJsonObject documentPortion(Context &c, const QString &previous, const QString &next)
{
    QJsonArray records;
    PreviewData data; data.kind = PreviewKind::Document; data.document = c.doc;
    qint64 bytes = 0;
    if (!writeDocumentRecords(data, [&](const QJsonObject &record) {
        bytes += QJsonDocument(record).toJson(QJsonDocument::Compact).size();
        if (bytes > 15 * 1024 * 1024) return false;
        records.append(record); return true;
    })) c.limit();
    return {{"previous", previous}, {"next", next},
        {"content", QJsonObject{{"kind", "document"}, {"records", records}, {"navigation", QJsonArray{}}, {"targetAnchor", QString()}}}};
}
QJsonObject WordReader::portion(const QString &token)
{
    p.beginRequest();
    bool ok = false;
    const int requested = token.toInt(&ok);
    if (!ok || requested < 0 || QString::number(requested) != token) c.invalid();
    if (parts_.isEmpty()) {
        initialize(); parts_.append(main);
        for (const auto &r : rels)
            if (r.type.endsWith(QLatin1String("/footnotes")) || r.type.endsWith(QLatin1String("/endnotes"))) parts_.append(r.path);
    }
    if (!xml_ || requested != position_) {
        counters.clear(); headings_ = 0; emptyParagraph_ = false;
        position_ = previous_ = 0; openPart(0);
    }
    int first = 0, previous = 0;
    bool ended = false;
    do {
        first = position_; previous = previous_;
        c.doc = std::make_shared<FormattedDocument>(); c.blocks = 0; p.beginPortion();
        Node group;
        int characters = 0, treeNodes = 0;
        qint64 treeUnits = 0;
        // Also stop on XML cost: attributes/empty runs need not contribute
        // visible text. One final bounded subtree can cross these soft targets.
        for (int count = 0; count < 64 && characters < 8192 && treeNodes < 100000 && treeUnits < 2 * 1024 * 1024; ++count) {
            auto node = nextBlock();
            if (!node) { ended = true; break; }
            characters += node->allText().size();
            treeNodes += c.nodes; treeUnits += c.treeUnits;
            group.children.push_back(std::move(*node)); ++position_;
        }
        children(group, 0);
        previous_ = first;
        if (position_ <= first && first != requested) c.invalid();
        if (first == requested) break;
        if (position_ > requested || ended) c.invalid();
    } while (true);
    if (c.doc->plainText.trimmed().isEmpty() && c.doc->images.isEmpty()) c.text(QStringLiteral("[Empty section]"));
    return documentPortion(c, first ? QString::number(previous) : QString(), ended ? QString() : QString::number(position_));
}

struct NavigationStop { QString message; };
class EpubReader {
public:
    explicit EpubReader(Package &package) : p(package), c(package.c) {}
    void initialize()
    {
        if (p.part(QStringLiteral("mimetype")) != QByteArray("application/epub+zip")) c.invalid();
        const auto container = c.xml(p.part(QStringLiteral("META-INF/container.xml")));
        QString path;
        for (const auto &n : container.child("container").child("rootfiles").children)
            if (n.name == QLatin1String("rootfile") && n.attr("media-type") == QLatin1String("application/oebps-package+xml")) { path = p.resolve({}, n.attr("full-path")); break; }
        if (path.isEmpty()) c.invalid();
        const auto root = c.xml(p.part(path));
        const auto &package = root.child("package");
        if (package.name.isEmpty()) c.invalid();
        for (const auto &n : package.child("metadata").children) {
            if (n.name == QLatin1String("title") && c.doc->title.isEmpty()) c.doc->title = n.allText().left(500);
            if (n.name == QLatin1String("creator") && c.doc->author.isEmpty()) c.doc->author = n.allText().left(500);
        }
        QHash<QString, QString> manifest;
        for (const auto &n : package.child("manifest").children) if (n.name == QLatin1String("item")) {
            const auto target = p.resolve(path, n.attr("href"));
            if (n.attr("media-type") == QLatin1String("application/xhtml+xml") && !target.isEmpty()) manifest.insert(n.attr("id"), target);
        }
        if (p.contains(QStringLiteral("META-INF/encryption.xml"))) {
            // Font obfuscation is harmless here: embedded fonts are not loaded.
            const auto encryption = c.xml(p.part(QStringLiteral("META-INF/encryption.xml")));
            checkEncryption(encryption);
        }
        for (const auto &n : package.child("metadata").children)
            if (n.name == QLatin1String("meta") && (n.attr("property") == QLatin1String("rendition:layout")) && n.allText() == QLatin1String("pre-paginated"))
                c.notice(QStringLiteral("Fixed layout simplified; some content may be omitted."));
        for (const auto &n : package.child("spine").children) {
            if (n.name != QLatin1String("itemref") || n.attr("linear") == QLatin1String("no")) continue;
            const auto owner = manifest.value(n.attr("idref"));
            if (owner.isEmpty()) { c.notice(QStringLiteral("Some book content could not be previewed.")); continue; }
            if (owners_.size() >= 2048) c.limit();
            owners_.append(owner);
        }
        if (owners_.isEmpty()) c.invalid();
        title_ = c.doc->title;
        author_ = c.doc->author;
        notice_ = c.doc->notice;
    }
    void read()
    {
        initialize();
        int count = 0;
        for (const auto &owner : owners_) {
            if (++count > 128) throw Stop{{}, false, true};
            const auto chapter = c.xml(p.part(owner), true);
            const auto &body = chapter.child("html").child("body");
            if (body.name.isEmpty()) c.invalid();
            if (count == 1 && body.allText().trimmed().isEmpty()) continue; // Skip an image-only cover.
            const auto title = heading(body);
            section(c, title.isEmpty() ? QStringLiteral("Section %1").arg(count) : title, anchor(owner));
            render(body, owner);
            c.paragraph();
        }
        if (!count) c.invalid();
    }
    QJsonObject portion(const QString &token)
    {
        p.beginRequest();
        int chapter = 0, requested = 0;
        bool last = false;
        QString target;
        if (token.startsWith(QChar('a'))) {
            const int separator = token.indexOf(QChar('-'));
            const auto owner = QString::fromUtf8(QByteArray::fromHex(token.mid(1, separator - 1).toLatin1()));
            const auto fragment = QString::fromUtf8(QByteArray::fromHex(token.mid(separator + 1).toLatin1()));
            chapter = owners_.indexOf(owner);
            if (separator < 1 || chapter < 0 || anchor(owner, fragment) != token) throw NavigationStop{QStringLiteral("This link does not target a readable book chapter.")};
            loadChapter(chapter);
            if (!fragment.isEmpty()) {
                bool found = false;
                for (size_t i = 0; i < units_.size(); ++i) if (units_[i].kind == UnitKind::Content ? hasId(*units_[i].node, fragment)
                    : units_[i].kind == UnitKind::Open && units_[i].node->attr("id").left(512) == fragment) {
                    requested = int(i); found = true; break;
                }
                if (!found) throw NavigationStop{QStringLiteral("This section could not be located in the book chapter.")};
            }
            target = token;
        } else if (token != QStringLiteral("0")) {
            const auto parts = token.split(':');
            bool chapterOk = false, offsetOk = false;
            if (parts.size() != 3 || (parts[0] != QLatin1String("f") && parts[0] != QLatin1String("b"))) c.invalid();
            chapter = parts[1].toInt(&chapterOk); requested = parts[2].toInt(&offsetOk);
            last = parts[0] == QLatin1String("b");
            if (!chapterOk || !offsetOk || chapter < 0 || chapter >= owners_.size() || requested < 0
                || (last && requested) || QStringLiteral("%1:%2:%3").arg(parts[0]).arg(chapter).arg(requested) != token) c.invalid();
        }
        loadChapter(chapter);
        if (requested >= int(units_.size()) && requested) c.invalid();
        int position = !last && target.isEmpty() && requested == nextOffset_ ? requested : 0;
        int first = 0, previous = 0, previousFirst = position ? previousStart_ : 0;
        do {
            first = position;
            previous = previousFirst;
            p.beginPortion();
            c.doc = std::make_shared<FormattedDocument>();
            c.doc->title = title_; c.doc->author = author_; c.doc->notice = notice_;
            c.blocks = 0;
            if (!first) section(c, heading(root_).isEmpty() ? QStringLiteral("Section %1").arg(chapter + 1) : heading(root_), anchor(owners_[chapter]));
            const auto beginning = position;
            while (position < int(units_.size())) {
                const auto &unit = units_[position++];
                if (unit.kind == UnitKind::Content) render(*unit.node, owners_[chapter]);
                else boundary(unit, chapter);
                if (c.doc->plainText.size() >= 8192 || (position - beginning >= 64
                    && (!c.doc->plainText.trimmed().isEmpty() || !c.doc->images.isEmpty()))) break;
            }
            // Consume trailing container closures with their content rather
            // than publishing an extra empty portion at a chapter boundary.
            while (position < int(units_.size()) && units_[position].kind == UnitKind::Close) boundary(units_[position++], chapter);
            previousFirst = first;
            // Fragment targets use the complete semantic block containing the
            // anchor. No inline state, list or table is split between portions.
            if (!last && (first == requested || (!target.isEmpty() && first <= requested && position > requested))) break;
            if (!last && position > requested) c.invalid();
        } while (position < int(units_.size()));
        if (!last && target.isEmpty() && first != requested) c.invalid();
        nextOffset_ = position; previousStart_ = first;
        if (c.doc->plainText.trimmed().isEmpty() && c.doc->images.isEmpty()) c.text(QStringLiteral("[Empty section]"));
        const auto forward = [](int part, int offset) { return QStringLiteral("f:%1:%2").arg(part).arg(offset); };
        QJsonArray records, navigation;
        PreviewData data;
        data.kind = PreviewKind::Document;
        data.document = c.doc;
        qint64 bytes = 0;
        if (!writeDocumentRecords(data, [&](const QJsonObject &record) {
            bytes += QJsonDocument(record).toJson(QJsonDocument::Compact).size();
            if (bytes > 15 * 1024 * 1024) return false;
            records.append(record); return true;
        })) c.limit();
        for (int i = 0; i < owners_.size(); ++i)
            navigation.append(QJsonObject{{"title", QStringLiteral("%1 · %2").arg(i + 1).arg(QFileInfo(owners_[i]).fileName())}, {"anchor", anchor(owners_[i])}});
        return {{"previous", first ? forward(chapter, previous) : chapter ? QStringLiteral("b:%1:0").arg(chapter - 1) : QString()},
            {"next", position < int(units_.size()) ? forward(chapter, position) : chapter + 1 < owners_.size() ? forward(chapter + 1, 0) : QString()},
            {"content", QJsonObject{{"kind", "document"}, {"records", records}, {"navigation", navigation}, {"targetAnchor", target}}}};
    }

private:
    enum class UnitKind { Content, Open, Close };
    struct Unit { const Node *node; UnitKind kind; };
    void boundary(const Unit &unit, int chapter) {
        if (unit.node->name != QLatin1String("body")) c.paragraph();
        if (unit.kind == UnitKind::Open && !unit.node->attr("id").isEmpty())
            c.markup(QStringLiteral("<a name=\"%1\"></a>").arg(anchor(owners_[chapter], unit.node->attr("id").left(512))));
    }
    QStringList owners_;
    QString title_, author_, notice_;
    Node root_;
    std::vector<Unit> units_;
    int loaded_ = -1;
    int nextOffset_ = 0, previousStart_ = 0;
    bool hasId(const Node &node, const QString &id) const {
        if (node.attr("id").left(512) == id) return true;
        for (const auto &child : node.children) if (hasId(child, id)) return true;
        return false;
    }
    void flatten(const Node &node) {
        if (QStringList{"body", "div", "section", "article"}.contains(node.name)) {
            units_.push_back({&node, UnitKind::Open});
            for (const auto &child : node.children) flatten(child);
            units_.push_back({&node, UnitKind::Close});
        } else if (!node.name.isEmpty() || !node.text.trimmed().isEmpty()) units_.push_back({&node, UnitKind::Content});
    }
    void loadChapter(int index) {
        if (loaded_ == index) return;
        units_.clear(); root_ = {};
        c.nodes = 0;
        root_ = c.xml(p.part(owners_.at(index)), true);
        const auto &body = root_.child("html").child("body");
        if (body.name.isEmpty()) c.invalid();
        flatten(body);
        loaded_ = index;
        nextOffset_ = previousStart_ = 0;
    }
    void checkEncryption(const Node &n)
    {
        if (n.name == QLatin1String("EncryptedData")) {
            const auto method = n.child("EncryptionMethod").attr("Algorithm");
            if (method != QLatin1String("http://www.idpf.org/2008/embedding") && method != QLatin1String("http://ns.adobe.com/pdf/enc#RC")) c.invalid();
        }
        for (const auto &child : n.children) checkEncryption(child);
    }
    QString heading(const Node &n) const
    {
        if (n.name == QLatin1String("h1") || n.name == QLatin1String("h2")) return n.allText().simplified();
        for (const auto &child : n.children) { auto text = heading(child); if (!text.isEmpty()) return text; }
        return {};
    }
    void render(const Node &n, const QString &owner, int tableDepth = 0)
    {
        c.check();
        if (n.name.isEmpty()) { c.text(n.text); return; }
        const auto name = n.name.toLower();
        if (name == QLatin1String("svg") || name == QLatin1String("math")) {
            const auto text = n.allText().simplified();
            c.text(text.isEmpty() ? QStringLiteral(" [Embedded content] ") : text);
            return;
        }
        if (QStringList{"head", "script", "style", "iframe", "object", "audio", "video"}.contains(name)) {
            if (name != QLatin1String("head") && name != QLatin1String("script") && name != QLatin1String("style")) c.text(QStringLiteral(" [Embedded content] "));
            return;
        }
        if (!n.attr("id").isEmpty()) c.markup(QStringLiteral("<a name=\"%1\"></a>").arg(anchor(owner, n.attr("id").left(512))));
        if (name == QLatin1String("img")) { p.image(p.resolve(owner, n.attr("src"))); return; }
        if (name == QLatin1String("br")) { c.markup(QStringLiteral("<br/>")); c.paragraph(); return; }
        const bool block = QStringList{"p", "div", "section", "article", "h1", "h2", "h3", "h4", "h5", "h6", "li", "blockquote", "pre", "tr"}.contains(name);
        if (block) c.paragraph();
        QString tag;
        if (QStringList{"p", "h1", "h2", "h3", "h4", "h5", "h6", "b", "strong", "i", "em", "u", "s", "sup", "sub", "ul", "ol", "li", "blockquote", "pre", "code"}.contains(name)) tag = name;
        const int depth = tableDepth + (name == QLatin1String("table"));
        if (depth <= 4 && QStringList{"table", "tr", "td", "th"}.contains(name)) tag = name;
        if (name == QLatin1String("a")) {
            const QUrl href(n.attr("href"));
            const auto target = href.path().isEmpty() ? owner : p.resolve(owner, n.attr("href"));
            if (href.isRelative() && !target.isEmpty()) {
                c.markup(QStringLiteral("<a href=\"#%1\">").arg(anchor(target, href.fragment().left(512))));
                tag = QStringLiteral("a");
            }
        } else if (!tag.isEmpty()) {
            c.markup(QChar('<') + tag + (tag == QLatin1String("table") ? QStringLiteral(" border=\"1\" cellspacing=\"0\" cellpadding=\"4\">") : QStringLiteral(">")));
        }
        for (const auto &child : n.children) render(child, owner, depth);
        if (!tag.isEmpty()) c.markup(QStringLiteral("</") + tag + QChar('>'));
        if (block) c.paragraph();
        if (name == QLatin1String("td") || name == QLatin1String("th")) c.text(QStringLiteral("\t"));
    }
    Package &p;
    Context &c;
};
}
}

namespace dzurkel {
DocumentResult DocumentReader::read(QFile &file, const QString &format, const Cancellation &cancel)
{
    document::Context context(cancel);
    try {
        context.check();
        if (format == QLatin1String("docx") || format == QLatin1String("epub")) {
            document::Package package(context, file);
            if (format == QLatin1String("docx")) document::WordReader(package).read();
            else if (format == QLatin1String("epub")) document::EpubReader(package).read();
            else context.invalid();
        } else if (format == QLatin1String("doc")) document::readLegacyDoc(context, file);
        else if (format == QLatin1String("mobi")) document::readMobi(context, file);
        else {
            auto bytes = file.read(InputPrefixLimit + 1);
            if (file.error() != QFileDevice::NoError) context.invalid();
            context.inputTruncated = bytes.size() > InputPrefixLimit;
            bytes.truncate(InputPrefixLimit);
            if (format == QLatin1String("rtf")) document::readRtf(context, bytes);
            else if (format == QLatin1String("fb2")) document::readFb2(context, bytes);
            else if (format == QLatin1String("sfb")) document::readSfb(context, bytes);
            else context.invalid();
            if (context.inputTruncated) throw document::Stop{{}, false, true};
        }
    } catch (const document::Stop &stop) {
        if (stop.cancelled) return {{}, {}, true};
        if (!stop.partial) return {{}, stop.error, false};
        context.doc->partial = true;
        context.notice(QStringLiteral("Partial preview — showing the beginning of this document."));
    }
    if (context.doc->plainText.trimmed().isEmpty() && context.doc->images.isEmpty())
        return {{}, QStringLiteral("No readable content could be extracted from this document."), false};
    return {context.doc, {}, false};
}
struct RichDocumentPortionReader::State {
    QFile file;
    QString error;
    qint64 size = 0;
    QDateTime modified;
    document::Context context{Cancellation{}};
    std::unique_ptr<document::Package> package;
    std::unique_ptr<document::EpubReader> reader;
    std::unique_ptr<document::WordReader> word;
    QString format;
    std::unique_ptr<document::DocumentInput> input;
    std::unique_ptr<document::PortionParser> parser;
    int next = 0;
    QString title, author, deferredError, path;
    std::unique_ptr<TextPortionReader> textFallback;
    void resetParser() {
        parser.reset(); input.reset();
        if (!file.seek(0)) context.invalid();
        context.doc = std::make_shared<FormattedDocument>();
        context.sectionSerial = 0;
        next = 0; title.clear(); author.clear();
        input = std::make_unique<document::DocumentInput>(context, file);
        if (format == QLatin1String("rtf")) parser = document::rtfParser(context, *input);
        else if (format == QLatin1String("fb2")) parser = document::fb2Parser(context, *input);
        else if (format == QLatin1String("sfb")) parser = document::sfbParser(context, *input);
        else if (format == QLatin1String("doc")) parser = document::legacyDocParser(context, file);
        else if (format == QLatin1String("mobi")) parser = document::mobiParser(context, file);
        else if (format == QLatin1String("markdown")) parser = document::markdownParser(context, *input, file);
        else context.invalid();
        title = context.doc->title; author = context.doc->author;
    }
    QJsonObject sequential(const QString &token) {
        if (!deferredError.isEmpty()) throw document::Stop{deferredError};
        context.clock.restart(); context.sourceRead = 0;
        bool ok = false;
        const int requested = token.toInt(&ok);
        if (!ok || requested < 0 || QString::number(requested) != token) context.invalid();
        if (!parser || requested != next) resetParser();
        bool more = true;
        while (next <= requested) {
            context.check();
            context.doc = std::make_shared<FormattedDocument>(); context.blocks = context.nodes = 0;
            context.doc->title = title; context.doc->author = author;
            parser->begin();
            for (int units = 0; units < 64 && context.doc->plainText.size() < 8192; ++units) {
                const auto before = std::make_shared<FormattedDocument>(*context.doc);
                try { if (!parser->next()) { more = false; break; } }
                catch (const document::Stop &stop) {
                    if (stop.cancelled || before->plainText.trimmed().isEmpty()) throw;
                    deferredError = stop.partial ? QStringLiteral("A document block exceeds the per-portion content budget.") : stop.error;
                    context.doc = before; break;
                }
            }
            parser->end();
            title = context.doc->title; author = context.doc->author;
            if (next++ == requested) break;
            if (!more) context.invalid();
        }
        if (context.doc->plainText.trimmed().isEmpty() && context.doc->images.isEmpty()) context.text(QStringLiteral("[Empty section]"));
        return document::documentPortion(context, requested ? QString::number(requested - 1) : QString(), more ? QString::number(next) : QString());
    }
    explicit State(const QString &sourcePath, const QString &readerFormat) : format(readerFormat), path(sourcePath) {
        FileResource resource; resource.path = sourcePath;
        if (!openPreviewFile(resource, file, error)) return;
        size = file.size(); modified = file.fileTime(QFileDevice::FileModificationTime);
    }
};
RichDocumentPortionReader::RichDocumentPortionReader(const QString &path, const QString &format) : state_(std::make_unique<State>(path, format)) {}
RichDocumentPortionReader::~RichDocumentPortionReader() = default;
QJsonObject RichDocumentPortionReader::read(const QString &token)
{
    auto &s = *state_;
    if (!s.error.isEmpty()) return {{"error", s.error}};
    const auto changed = [&] { return s.file.size() != s.size || s.file.fileTime(QFileDevice::FileModificationTime) != s.modified; };
    if (changed()) return {{"error", QStringLiteral("The source changed. Reopen the preview to continue.")}};
    try {
        if (s.textFallback) {
            auto reply = s.textFallback->read(token);
            auto content = reply.value("content").toObject();
            content.insert(QStringLiteral("notice"), QStringLiteral("Markdown shown as plain text; table layout exceeds preview limits."));
            reply.insert(QStringLiteral("content"), content); return reply;
        }
        if (s.format != QLatin1String("docx") && s.format != QLatin1String("epub")) {
            auto reply = s.sequential(token);
            if (changed()) return {{"error", QStringLiteral("The source changed. Reopen the preview to continue.")}};
            return reply;
        }
        if (!s.package) {
            s.context.clock.restart();
            s.package = std::make_unique<document::Package>(s.context, s.file);
            s.package->pixelLimit = 2 * 1024 * 1024;
            if (s.format == QLatin1String("docx")) s.word = std::make_unique<document::WordReader>(*s.package);
            else { s.reader = std::make_unique<document::EpubReader>(*s.package); s.reader->initialize(); }
        }
        auto reply = s.word ? s.word->portion(token) : s.reader->portion(token);
        if (changed()) return {{"error", QStringLiteral("The source changed. Reopen the preview to continue.")}};
        return reply;
    } catch (const document::NavigationStop &stop) {
        return {{"previous", ""}, {"next", ""}, {"content", QJsonObject{{"kind", "navigation-error"}, {"message", stop.message}}}};
    } catch (const document::Stop &stop) {
        if (s.format == QLatin1String("markdown") && token == QLatin1String("0")
            && stop.error.startsWith(QLatin1String("Markdown shown as plain text"))) {
            s.parser.reset(); s.input.reset();
            s.textFallback = std::make_unique<TextPortionReader>(s.path);
            return read(token);
        }
        s.error = stop.partial ? QStringLiteral("A document block exceeds the per-portion content budget.") : stop.error;
        return {{"error", s.error}};
    }
}

}

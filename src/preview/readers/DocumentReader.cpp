#include "DocumentReaderInternal.h"
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
    std::vector<Node *> stack{&root};
    while (!xml.atEnd()) {
        check();
        xml.readNext();
        // Older EPUBs commonly declare XHTML's external DTD. Qt does not fetch
        // it; only our fixed entity map is used. Internal subsets are rejected.
        if (xml.isDTD() && (!xhtml || xml.dtdName() != QLatin1String("html") || xml.text().contains(QChar('[')))) invalid();
        if (xml.isEntityReference()) invalid();
        if (xml.isStartElement()) {
            if (++nodes > 200000 || stack.size() > 64) limit();
            Node node;
            node.name = xml.name().toString();
            node.ns = xml.namespaceUri().toString();
            for (const auto &a : xml.attributes()) node.attributes.insert(a.name().toString(), a.value().toString());
            stack.back()->children.push_back(std::move(node));
            stack.push_back(&stack.back()->children.back());
        } else if (xml.isCharacters()) {
            if (++nodes > 200000) limit();
            Node node;
            node.text = xml.text().toString();
            stack.back()->children.push_back(std::move(node));
        } else if (xml.isEndElement()) {
            if (stack.size() <= 1) invalid();
            stack.pop_back();
        }
    }
    if (xml.hasError() || root.children.empty()) invalid();
    return root;
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
    Package(Context &context, QByteArray bytes) : c(context), bytes_(std::move(bytes))
    {
        zip_error_t error;
        zip_error_init(&error);
        auto *source = zip_source_buffer_create(bytes_.constData(), size_t(bytes_.size()), 0, &error);
        if (source) archive = zip_open_from_source(source, ZIP_RDONLY, &error);
        if (!archive && source) zip_source_free(source);
        zip_error_fini(&error);
        if (!archive) c.invalid();
        // All failure paths after opening must leave the archive owned.
        try {
            const auto count = zip_get_num_entries(archive, 0);
            if (count < 0 || count > 2048) c.limit();
            quint64 expanded = 0;
            for (zip_int64_t i = 0; i < count; ++i) {
                c.check();
                zip_stat_t stat;
                zip_stat_init(&stat);
                if (zip_stat_index(archive, i, 0, &stat) || !(stat.valid & ZIP_STAT_SIZE) || !stat.name) c.invalid();
                const auto name = QString::fromUtf8(stat.name);
                if (entries.contains(name)) c.invalid();
                entries.insert(name, zip_uint64_t(i));
                if (stat.size > 64 * 1024 * 1024 || expanded > 64 * 1024 * 1024 - stat.size) c.limit();
                expanded += stat.size;
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
            if (size < 0) c.invalid();
            if (!size) break;
            consumed += size;
            if (data.size() + size > 8 * 1024 * 1024 || consumed > 64 * 1024 * 1024) c.limit();
            data.append(buffer, qsizetype(size));
        }
        if (zip_fclose(file.release()) || zip_uint64_t(data.size()) != stat.size) c.invalid();
        return data;
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
            || pixels + qint64(size.width()) * size.height() > 8 * 1024 * 1024) { placeholder(); return; }
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
    Context &c;
private:
    QByteArray bytes_;
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
    void read()
    {
        const auto rootRels = p.relationships({});
        QString main;
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
                    section(c, role == QLatin1String("title") ? n.allText() : QString(), QStringLiteral("dzurkel-%1-%2").arg(role).arg(c.blocks));
                    if (role == QLatin1String("title") && c.doc->title.isEmpty()) c.doc->title = n.allText().simplified().left(500);
                } else if (tag.startsWith(QChar('h'))) section(c, n.allText(), QStringLiteral("heading%1").arg(c.blocks));
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
};

class EpubReader {
public:
    explicit EpubReader(Package &package) : p(package), c(package.c) {}
    void read()
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
        int count = 0;
        for (const auto &n : package.child("spine").children) {
            if (n.name != QLatin1String("itemref") || n.attr("linear") == QLatin1String("no")) continue;
            if (++count > 128) throw Stop{{}, false, true};
            const auto owner = manifest.value(n.attr("idref"));
            if (owner.isEmpty()) { c.notice(QStringLiteral("Some book content could not be previewed.")); continue; }
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
private:
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
        if (file.size() > ByteLimit) context.limit();
        const auto bytes = file.read(ByteLimit + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > ByteLimit) context.invalid();
        if (format == QLatin1String("rtf") || bytes.startsWith("{\\rtf")) document::readRtf(context, bytes);
        else if (format == QLatin1String("doc")) document::readLegacyDoc(context, bytes);
        else if (format == QLatin1String("fb2")) document::readFb2(context, bytes);
        else if (format == QLatin1String("sfb")) document::readSfb(context, bytes);
        else if (format == QLatin1String("mobi")) document::readMobi(context, bytes);
        else {
            document::Package package(context, bytes);
            if (format == QLatin1String("docx")) document::WordReader(package).read();
            else if (format == QLatin1String("epub")) document::EpubReader(package).read();
            else context.invalid();
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
}

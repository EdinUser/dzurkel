#include "XlsxReader.h"
#include "SeekableZipSource.h"
#include <QDir>
#include <QDate>
#include <QTime>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>
#include "ZipEntryDevice.h"
#include <QJsonArray>
#include <QDateTime>
#include "preview/table/TableRowBatch.h"
#include <zip.h>
#include <cmath>

namespace dzurkel {
namespace {
const QString Main = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
const QString StrictMain = QStringLiteral("http://purl.oclc.org/ooxml/spreadsheetml/main");
const QString Rels = QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships");
bool spreadsheet(const QXmlStreamReader &xml) { return xml.namespaceUri() == Main || xml.namespaceUri() == StrictMain; }
struct Relationship { QString target; QString type; };
struct Sheet { QString name; QString id; };
struct Stop { QString error; bool cancelled = false; };


enum class NumberKind { Raw, Date, Time, DateTime, Duration };
NumberKind numberKind(int id, const QString &code)
{
    if (code.isEmpty()) {
        if (id >= 14 && id <= 17) return NumberKind::Date;
        if ((id >= 18 && id <= 21) || id == 45 || id == 47) return NumberKind::Time;
        if (id == 22) return NumberKind::DateTime;
        if (id == 46) return NumberKind::Duration;
        return NumberKind::Raw;
    }
    // Recognize simple date/time formats, not Excel's full formatting language.
    // Quoted/escaped literals and locale tags must never turn numbers into dates.
    QString tokens;
    bool quoted = false;
    bool duration = false;
    for (int i = 0; i < code.size(); ++i) {
        const auto c = code.at(i).toLower();
        if (c == QChar('"')) { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == QChar('\\') || c == QChar('_') || c == QChar('*')) { ++i; continue; }
        if (c == QChar(';')) return NumberKind::Raw;
        if (c == QChar('[')) {
            const int end = code.indexOf(QChar(']'), i + 1);
            if (end < 0) return NumberKind::Raw;
            const auto tag = code.mid(i + 1, end - i - 1).toLower();
            if (tag == QLatin1String("h") || tag == QLatin1String("hh")
                || tag == QLatin1String("m") || tag == QLatin1String("mm")
                || tag == QLatin1String("s") || tag == QLatin1String("ss")) duration = true;
            else if (!tag.startsWith(QChar('$'))) return NumberKind::Raw;
            i = end;
            continue;
        }
        tokens += c;
    }
    tokens.replace(QStringLiteral("am/pm"), QString());
    tokens.replace(QStringLiteral("a/p"), QString());
    for (auto c : tokens) if (c.isLetter() && !QStringLiteral("ymdhs").contains(c)) return NumberKind::Raw;
    if (quoted || tokens.contains(QChar('0')) || tokens.contains(QChar('#'))
        || tokens.contains(QChar('?')) || tokens.contains(QChar('%'))) return NumberKind::Raw;
    if (duration) return NumberKind::Duration;
    const bool date = tokens.contains(QChar('y')) || tokens.contains(QChar('d'));
    const bool time = tokens.contains(QChar('h')) || tokens.contains(QChar('s'));
    if (date) return time ? NumberKind::DateTime : NumberKind::Date;
    if (time) return NumberKind::Time;
    if (tokens.contains(QChar('m'))) return NumberKind::Date;
    return NumberKind::Raw;
}

QString formattedNumber(const QString &raw, NumberKind kind, bool date1904)
{
    if (kind == NumberKind::Raw) return raw;
    bool ok = false;
    const double serial = raw.toDouble(&ok);
    // Bound conversions before any integer casts; invalid dates remain literal.
    if (!ok || !std::isfinite(serial) || serial < 0 || serial >= 2957004) return raw;
    qint64 days = qint64(std::floor(serial));
    qint64 millis = qRound64((serial - double(days)) * 86400000);
    if (millis == 86400000) {
        if (kind != NumberKind::Date) ++days;
        millis = 0;
    }
    if (kind == NumberKind::Duration) {
        const qint64 total = days * 86400000 + millis;
        auto result = QStringLiteral("%1:%2:%3").arg(total / 3600000)
            .arg((total / 60000) % 60, 2, 10, QChar('0'))
            .arg((total / 1000) % 60, 2, 10, QChar('0'));
        if (total % 1000) result += QStringLiteral(".%1").arg(total % 1000, 3, 10, QChar('0'));
        return result;
    }
    const auto time = QTime(0, 0).addMSecs(int(millis));
    const auto timeText = time.toString(millis % 1000 ? QStringLiteral("HH:mm:ss.zzz") : QStringLiteral("HH:mm:ss"));
    if (kind == NumberKind::Time) return timeText;
    // Excel's 1900 calendar includes a fictional leap day. Preserve its label
    // rather than silently mapping serial 60 to a different real date.
    QString dateText;
    if (!date1904 && days == 60) dateText = QStringLiteral("1900-02-29 (Excel)");
    else {
        const auto date = date1904 ? QDate(1904, 1, 1).addDays(days)
            : QDate(1899, 12, 31).addDays(days > 60 ? days - 1 : days);
        if (!date.isValid() || date.year() > 9999) return raw;
        dateText = date.toString(Qt::ISODate);
    }
    return kind == NumberKind::DateTime ? dateText + QChar(' ') + timeText : dateText;
}

// Package reads seek in the validated source descriptor. ZIP paths are lookup
// keys only: no archive content is extracted to the filesystem or fetched.
class Reader {
public:
    explicit Reader(const Cancellation &cancel) : cancel_(cancel) { clock_.start(); }
    ~Reader() { xml_.reset(); entry_.reset(); if (zip_) zip_discard(zip_); }
    std::shared_ptr<TabularDocument> read(QFile &file);
    void initialize(QFile &file);
    QJsonObject portion(const QString &token);
private:
    void check() const {
        if (cancel_ && cancel_->load()) throw Stop{{}, true};
        if (clock_.elapsed() > 5000) throw Stop{QStringLiteral("XLSX preview loading timed out.")};
    }
    [[noreturn]] void invalid() const {
        check();
        if (input_ && input_->exceeded) limit();
        throw Stop{QStringLiteral("Cannot preview this damaged, encrypted or unsupported XLSX workbook.")};
    }
    [[noreturn]] void limit() const { throw Stop{QStringLiteral("XLSX workbook exceeds the preview resource limits.")}; }
    void next(QXmlStreamReader &xml) {
        check();
        xml.readNext();
        if (xml.isDTD() || xml.tokenType() == QXmlStreamReader::EntityReference) invalid();
        if (xml.isStartElement() && ++depth_ > 64) invalid();
        if (xml.isEndElement()) --depth_;
        if (xml.hasError()) invalid();
    }
    void skip(QXmlStreamReader &xml) {
        const int level = depth_;
        do { next(xml); } while (!xml.atEnd() && depth_ >= level);
    }
    QString text(QXmlStreamReader &xml) {
        QString result;
        const int level = depth_;
        while (!xml.atEnd()) {
            next(xml);
            if (depth_ < level) break;
            if (xml.isCharacters()) {
                if (result.size() + xml.text().size() > XlsxReader::FieldLimit) limit();
                result += xml.text();
            }
        }
        return result;
    }
    QString richText(QXmlStreamReader &xml) {
        QString result;
        const int level = depth_;
        while (!xml.atEnd()) {
            next(xml);
            if (depth_ < level) break;
            if (!xml.isStartElement()) continue;
            if (spreadsheet(xml) && xml.name() == QLatin1String("t")) result += text(xml);
            else if (!(spreadsheet(xml) && xml.name() == QLatin1String("r"))) skip(xml);
            if (result.size() > XlsxReader::FieldLimit) limit();
        }
        return result;
    }
    QByteArray part(const QString &name);
    QHash<QString, Relationship> relationships(const QString &owner);
    QString target(const QString &owner, const QString &path);
    void strings(const QString &path);
    void styles(const QString &path);
    TabularSheet worksheet(const QString &path, const QString &name);
    TabularSheet worksheetXml(QXmlStreamReader &xml, const QString &name, int firstRow = -1);
    void openSheet(int index);
    QString cell(QXmlStreamReader &xml);
    void charge(const QString &value) {
        characters_ += value.size();
        if (characters_ > XlsxReader::CharacterLimit) limit();
    }
    Cancellation cancel_;
    QElapsedTimer clock_;
    std::unique_ptr<SeekableZipSource> input_;
    zip_t *zip_ = nullptr;
    QHash<QString, zip_uint64_t> entries_;
    QStringList strings_;
    QList<NumberKind> formats_;
    bool date1904_ = false;
    int missingResults_ = 0;
    std::shared_ptr<TabularDocument> document_ = std::make_shared<TabularDocument>();
    qint64 expanded_ = 0;
    int characters_ = 0;
    int cells_ = 0;
    int depth_ = 0;
    QList<Sheet> sheets_;
    QHash<QString, Relationship> workbookRels_;
    // Only one XML cursor is retained. Backward/sheet changes replay through
    // libzip and Qt, with per-request work budgets rather than a growing index.
    std::unique_ptr<ZipEntryDevice> entry_;
    std::unique_ptr<QXmlStreamReader> xml_;
    int activeSheet_ = -1, position_ = 0, previous_ = 0;
    int row_ = 0, column_ = 0;
    bool held_ = false;
    QString heldValue_;
    QString deferredError_;
    bool cellMissing_ = false;

};

QByteArray Reader::part(const QString &name)
{
    check();
    if (!entries_.contains(name)) invalid();
    zip_stat_t stat;
    zip_stat_init(&stat);
    const auto index = entries_.value(name);
    if (zip_stat_index(zip_, index, 0, &stat) != 0 || !(stat.valid & ZIP_STAT_SIZE)) invalid();
    if (stat.size > XlsxReader::PartLimit || expanded_ + qint64(stat.size) > XlsxReader::ExpandedLimit) limit();
    std::unique_ptr<zip_file_t, decltype(&zip_fclose)> entry(zip_fopen_index(zip_, index, 0), zip_fclose);
    if (!entry) invalid();
    QByteArray result;
    char buffer[65536];
    for (;;) {
        check();
        const auto count = zip_fread(entry.get(), buffer, sizeof(buffer));
        if (count < 0) invalid();
        if (!count) break;
        expanded_ += count;
        if (result.size() + count > XlsxReader::PartLimit || expanded_ > XlsxReader::ExpandedLimit) limit();
        result.append(buffer, qsizetype(count));
    }
    if (zip_fclose(entry.release()) != 0 || zip_uint64_t(result.size()) != stat.size) invalid();
    return result;
}
QString Reader::target(const QString &owner, const QString &path)
{
    const QUrl url(path);
    if (!url.isRelative() || url.hasQuery() || url.hasFragment() || path.contains(QChar('\\'))) invalid();
    const auto decoded = QUrl::fromPercentEncoding(path.toUtf8());
    const auto resolved = QDir::cleanPath(decoded.startsWith(QChar('/')) ? decoded.mid(1)
        : owner.left(owner.lastIndexOf(QChar('/')) + 1) + decoded);
    if (resolved == QLatin1String("..") || resolved.startsWith(QLatin1String("../")) || resolved.startsWith(QChar('/'))) invalid();
    return resolved;
}
QHash<QString, Relationship> Reader::relationships(const QString &owner)
{
    const int slash = owner.lastIndexOf(QChar('/'));
    const auto path = owner.left(slash + 1) + QStringLiteral("_rels/") + owner.mid(slash + 1) + QStringLiteral(".rels");
    QXmlStreamReader xml(part(path));
    depth_ = 0;
    QHash<QString, Relationship> result;
    while (!xml.atEnd()) {
        next(xml);
        if (!xml.isStartElement()) continue;
        if (depth_ == 1) {
            if (xml.name() != QLatin1String("Relationships") || xml.namespaceUri() != Rels) invalid();
        } else if (depth_ == 2 && xml.name() == QLatin1String("Relationship") && xml.namespaceUri() == Rels) {
            const auto attributes = xml.attributes();
            // External links are never resolved, including authentication URLs.
            if (attributes.value(QLatin1String("TargetMode")) == QLatin1String("External")) { skip(xml); continue; }
            const auto id = attributes.value(QLatin1String("Id")).toString();
            if (id.isEmpty() || result.contains(id)) invalid();
            result.insert(id, {target(owner, attributes.value(QLatin1String("Target")).toString()),
                attributes.value(QLatin1String("Type")).toString()});
            skip(xml);
        } else skip(xml);
    }
    return result;
}
void Reader::strings(const QString &path)
{
    QXmlStreamReader xml(part(path));
    depth_ = 0;
    while (!xml.atEnd()) {
        next(xml);
        if (!xml.isStartElement()) continue;
        if (depth_ == 1) {
            if (!spreadsheet(xml) || xml.name() != QLatin1String("sst")) invalid();
        } else if (depth_ == 2 && spreadsheet(xml) && xml.name() == QLatin1String("si")) {
            if (strings_.size() >= XlsxReader::StringLimit) limit();
            auto value = richText(xml);
            charge(value);
            strings_.append(value);
        } else skip(xml);
    }
}
void Reader::styles(const QString &path)
{
    QXmlStreamReader xml(part(path));
    depth_ = 0;
    QHash<int, QString> custom;
    QList<int> ids;
    QString section;
    while (!xml.atEnd()) {
        next(xml);
        if (xml.isEndElement() && depth_ == 1) section.clear();
        if (!xml.isStartElement()) continue;
        if (depth_ == 1) {
            if (!spreadsheet(xml) || xml.name() != QLatin1String("styleSheet")) invalid();
        } else if (depth_ == 2 && spreadsheet(xml)
                   && (xml.name() == QLatin1String("numFmts") || xml.name() == QLatin1String("cellXfs"))) {
            section = xml.name().toString();
        } else if (depth_ == 3 && spreadsheet(xml) && section == QLatin1String("numFmts")
                   && xml.name() == QLatin1String("numFmt")) {
            bool ok = false;
            const int id = xml.attributes().value(QLatin1String("numFmtId")).toInt(&ok);
            const auto code = xml.attributes().value(QLatin1String("formatCode")).toString();
            if (!ok || id < 0 || custom.contains(id)) invalid();
            if (custom.size() >= XlsxReader::CellLimit || code.size() > XlsxReader::FieldLimit) limit();
            charge(code);
            custom.insert(id, code);
            skip(xml);
        } else if (depth_ == 3 && spreadsheet(xml) && section == QLatin1String("cellXfs")
                   && xml.name() == QLatin1String("xf")) {
            const auto attr = xml.attributes().value(QLatin1String("numFmtId"));
            bool ok = true;
            const int id = attr.isEmpty() ? 0 : attr.toInt(&ok);
            if (!ok || id < 0) invalid();
            if (ids.size() >= XlsxReader::CellLimit) limit();
            ids.append(id);
            skip(xml);
        } else skip(xml);
    }
    QHash<int, NumberKind> kinds;
    for (int id : ids) {
        check();
        if (!kinds.contains(id)) kinds.insert(id, numberKind(id, custom.value(id)));
        formats_.append(kinds.value(id));
    }
}
TabularSheet Reader::worksheet(const QString &path, const QString &name)
{
    QXmlStreamReader xml(part(path));
    depth_ = 0;
    row_ = column_ = 0;
    held_ = false;
    return worksheetXml(xml, name);
}
TabularSheet Reader::worksheetXml(QXmlStreamReader &xml, const QString &name, int firstRow)
{
    const bool lazy = firstRow >= 0;
    const int offset = lazy ? firstRow : 0;
    TabularSheet sheet;
    sheet.name = name;
    auto &row = row_;
    auto &column = column_;
    try {
        while (!xml.atEnd()) {
            const bool pending = held_;
            if (!pending) next(xml);
            held_ = false;
            if (!pending && !xml.isStartElement()) continue;
            if (depth_ == 1) {
                if (!spreadsheet(xml) || xml.name() != QLatin1String("worksheet")) invalid();
            } else if (depth_ == 2 && spreadsheet(xml) && xml.name() == QLatin1String("sheetData")) {
                // Only actual sheetData cells determine the extent; dimension and
                // formatted empty rows/columns may describe millions of positions.
            } else if (depth_ == 3 && spreadsheet(xml) && xml.name() == QLatin1String("row")) {
                const auto r = xml.attributes().value(QLatin1String("r"));
                bool ok = true;
                const int previousRow = row;
                row = r.isEmpty() ? row + 1 : r.toInt(&ok);
                if (lazy && row <= previousRow) invalid();
                if (!ok || row < 1 || row > 1048576) invalid();
                column = 0;
            } else if (pending || (depth_ == 4 && spreadsheet(xml) && xml.name() == QLatin1String("c"))) {
                if (!pending && ++cells_ > XlsxReader::CellLimit) {
                    if (lazy) limit();
                    document_->partial = true;
                    break;
                }
                const auto value = pending ? heldValue_ : cell(xml);
                if (value.isNull()) continue;
                if ((!lazy && row > XlsxReader::RowLimit) || column > XlsxReader::ColumnLimit) { document_->partial = true; continue; }
                if (lazy && (row > offset + TableRowBatch::RowLimit
                    || (row > offset + sheet.rowCount && characters_ >= TableRowBatch::CharacterTarget))) {
                    held_ = true;
                    heldValue_ = value;
                    sheet.rowCount = qMax(sheet.rowCount, qMin(TableRowBatch::RowLimit, row - 1 - offset));
                    break;
                }
                if (cellMissing_) ++missingResults_;
                charge(value);
                sheet.rowCount = qMax(sheet.rowCount, row - offset);
                sheet.columnCount = qMax(sheet.columnCount, column);
                sheet.cells.insert(TabularSheet::key(row - 1 - offset, column - 1), value);
            } else skip(xml);
        }
    } catch (const Stop &stop) {
        if (!lazy || !sheet.rowCount || stop.cancelled) throw;
        // Publish the useful batch, then surface a later parse/read failure on
        // continuation instead of discarding content already decoded safely.
        deferredError_ = stop.error;
        held_ = true;
    }
    return sheet;
}
QString Reader::cell(QXmlStreamReader &xml)
{
    auto &row = row_;
    auto &column = column_;
    cellMissing_ = false;
    const auto attributes = xml.attributes();
    const auto ref = attributes.value(QLatin1String("r"));
    if (!ref.isEmpty()) {
        int pos = 0;
        column = 0;
        while (pos < ref.size() && ref.at(pos) >= QChar('A') && ref.at(pos) <= QChar('Z')) {
            column = column * 26 + ref.at(pos++).unicode() - 'A' + 1;
            if (column > 16384) invalid();
        }
        bool ok = false;
        const int cellRow = ref.mid(pos).toInt(&ok);
        if (!ok || !column || cellRow != row) invalid();
    } else ++column;
    if (column > 16384) invalid();
    const auto type = attributes.value(QLatin1String("t")).toString();
    const auto style = attributes.value(QLatin1String("s")).toString();
    QString value;
    bool hasValue = false;
    bool valueElement = false;
    bool formula = false;
    const int level = depth_;
    while (!xml.atEnd()) {
        next(xml);
        if (depth_ < level) break;
        if (!xml.isStartElement()) continue;
        if (depth_ == level + 1 && spreadsheet(xml) && xml.name() == QLatin1String("v")) {
            value = text(xml); hasValue = !value.isEmpty(); valueElement = true;
        } else if (depth_ == level + 1 && spreadsheet(xml) && xml.name() == QLatin1String("is")) {
            value = richText(xml); hasValue = !value.isEmpty();
        } else {
            if (depth_ == level + 1 && spreadsheet(xml) && xml.name() == QLatin1String("f")) formula = true;
            skip(xml);
        }
    }
    if (!hasValue) {
        // An explicit empty string cache is a valid calculated result.
        if (!formula || (type == QLatin1String("str") && valueElement)) return {};
        value = QStringLiteral("[Result unavailable]");
        cellMissing_ = true;
    }
    if (hasValue && type == QLatin1String("s")) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (!ok || index < 0 || index >= strings_.size()) invalid();
        value = strings_.at(index);
    } else if (hasValue && type == QLatin1String("b")) {
        if (value != QLatin1String("0") && value != QLatin1String("1")) invalid();
        value = value == QLatin1String("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    } else if (hasValue && !type.isEmpty() && type != QLatin1String("n") && type != QLatin1String("str")
               && type != QLatin1String("inlineStr") && type != QLatin1String("e") && type != QLatin1String("d")) invalid();
    // Only recognized numeric date/time formats are interpreted;
    // other numeric lexemes remain intact to avoid rounding identifiers.
    if (hasValue && (type.isEmpty() || type == QLatin1String("n"))) {
        bool ok = true;
        const int index = style.isEmpty() ? 0 : style.toInt(&ok);
        if (!ok || index < 0) invalid();
        if (index < formats_.size()) value = formattedNumber(value, formats_.at(index), date1904_);
    }
    return value.isNull() ? QStringLiteral("") : value;
}
void Reader::initialize(QFile &file)
{
    clock_.restart();
    check();
    input_ = std::make_unique<SeekableZipSource>(file, cancel_, clock_);
    auto *source = zip_source_function_create(&SeekableZipSource::callback, input_.get(), nullptr);
    if (!source) invalid();
    zip_ = zip_open_from_source(source, ZIP_RDONLY, nullptr);
    if (!zip_) { zip_source_free(source); invalid(); }
    input_->remaining = XlsxReader::SourceReadLimit;
    check();
    const auto count = zip_get_num_entries(zip_, 0);
    if (count < 0) invalid();
    if (count > XlsxReader::EntryLimit) limit();
    for (zip_int64_t i = 0; i < count; ++i) {
        check();
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(zip_, zip_uint64_t(i), 0, &stat) != 0 || !(stat.valid & ZIP_STAT_SIZE)
            || !(stat.valid & ZIP_STAT_NAME) || !stat.name || stat.encryption_method != ZIP_EM_NONE) invalid();
        // Unused media/parts are indexed but never decompressed. The aggregate
        // expanded-byte budget is charged only when a required part is read.
        const auto name = QString::fromUtf8(stat.name);
        if (entries_.contains(name)) invalid();
        entries_.insert(name, zip_uint64_t(i));
    }
    QString workbook;
    for (const auto &rel : relationships({})) {
        if (rel.type.endsWith(QLatin1String("/officeDocument"))) {
            if (!workbook.isEmpty()) invalid();
            workbook = rel.target;
        }
    }
    if (workbook.isEmpty()) invalid();
    const auto rels = relationships(workbook);
    QList<Sheet> sheets;
    {
        QXmlStreamReader xml(part(workbook));
        depth_ = 0;
        while (!xml.atEnd()) {
            next(xml);
            if (!xml.isStartElement()) continue;
            if (depth_ == 1) {
                if (!spreadsheet(xml) || xml.name() != QLatin1String("workbook")) invalid();
            } else if (depth_ == 2 && spreadsheet(xml) && xml.name() == QLatin1String("workbookPr")) {
                const auto value = xml.attributes().value(QLatin1String("date1904"));
                if (!value.isEmpty() && value != QLatin1String("0") && value != QLatin1String("1")
                    && value != QLatin1String("true") && value != QLatin1String("false")) invalid();
                date1904_ = value == QLatin1String("1") || value == QLatin1String("true");
                skip(xml);
            } else if (depth_ == 2 && spreadsheet(xml) && xml.name() == QLatin1String("sheets")) {
            } else if (depth_ == 3 && spreadsheet(xml) && xml.name() == QLatin1String("sheet")) {
                QString id;
                for (const auto &a : xml.attributes())
                    if (a.name() == QLatin1String("id") && (a.namespaceUri() == QLatin1String("http://schemas.openxmlformats.org/officeDocument/2006/relationships")
                        || a.namespaceUri() == QLatin1String("http://purl.oclc.org/ooxml/officeDocument/relationships"))) id = a.value().toString();
                const auto name = xml.attributes().value(QLatin1String("name")).toString();
                if (name.isEmpty() || name.size() > 255 || !rels.contains(id)) invalid();
                if (rels.value(id).type.endsWith(QLatin1String("/worksheet"))) {
                    if (sheets.size() < XlsxReader::SheetLimit) sheets.append({name, id});
                    else document_->partial = true;
                }
                skip(xml);
            } else skip(xml);
        }
    }
    if (sheets.isEmpty()) invalid();
    QString sharedStrings;
    for (const auto &rel : rels) if (rel.type.endsWith(QLatin1String("/sharedStrings"))) {
        if (!sharedStrings.isEmpty()) invalid();
        sharedStrings = rel.target;
    }
    if (!sharedStrings.isEmpty()) strings(sharedStrings);
    QString stylePath;
    for (const auto &rel : rels) if (rel.type.endsWith(QLatin1String("/styles"))) {
        if (!stylePath.isEmpty()) invalid();
        stylePath = rel.target;
    }
    if (!stylePath.isEmpty()) styles(stylePath);
    sheets_ = sheets;
    workbookRels_ = rels;
}
std::shared_ptr<TabularDocument> Reader::read(QFile &file)
{
    initialize(file);
    for (const auto &sheet : sheets_) {
        check();
        if (cells_ >= XlsxReader::CellLimit) { document_->partial = true; break; }
        document_->sheets.append(worksheet(workbookRels_.value(sheet.id).target, sheet.name));
    }
    if (document_->partial) document_->notice = QStringLiteral("Partial XLSX preview: sheet, row, column or cell limit reached.");
    if (missingResults_) {
        if (!document_->notice.isEmpty()) document_->notice += QChar(' ');
        document_->notice += QStringLiteral("%1 formula result(s) unavailable; saved results are not recalculated.").arg(missingResults_);
    }
    return document_;
}
void Reader::openSheet(int index)
{
    xml_.reset();
    entry_.reset();
    const auto path = workbookRels_.value(sheets_.at(index).id).target;
    if (!entries_.contains(path)) invalid();
    entry_ = std::make_unique<ZipEntryDevice>(zip_, entries_.value(path), expanded_, XlsxReader::ExpandedLimit, [](const QString &message) { throw Stop{message}; });
    xml_ = std::make_unique<QXmlStreamReader>(entry_.get());
    activeSheet_ = index;
    position_ = previous_ = row_ = column_ = depth_ = 0;
    held_ = false;
}
QJsonObject Reader::portion(const QString &token)
{
    if (!deferredError_.isEmpty()) throw Stop{deferredError_};
    clock_.restart();
    input_->remaining = XlsxReader::SourceReadLimit;
    input_->exceeded = false;
    expanded_ = 0;
    int index = 0, requested = 0;
    if (token != QStringLiteral("0")) {
        const auto parts = token.split(':');
        bool sheetOk = false, rowOk = false;
        if (parts.size() != 3 || parts[0] != QLatin1String("f")) invalid();
        index = parts[1].toInt(&sheetOk);
        requested = parts[2].toInt(&rowOk);
        if (!sheetOk || !rowOk || index < 0 || index >= sheets_.size() || requested < 0 || requested > 1048576
            || QStringLiteral("%1:%2:%3").arg(parts[0]).arg(index).arg(requested) != token) invalid();
    }
    if (activeSheet_ != index || position_ != requested) openSheet(index);
    TabularSheet batch;
    int first = position_, previous = previous_;
    for (;;) {
        check();
        first = position_;
        previous = previous_;
        characters_ = cells_ = missingResults_ = 0;
        batch = worksheetXml(*xml_, sheets_.at(index).name, first);
        position_ += batch.rowCount;
        previous_ = first;
        if (first == requested) break;
        if (position_ <= first || position_ > requested || !held_) invalid();
    }
    QJsonArray rows;
    for (int row = 0; row < batch.rowCount; ++row) {
        QJsonArray values;
        for (int col = 0; col < batch.columnCount; ++col) values.append(batch.value(row, col).toString());
        rows.append(values);
    }
    QJsonArray names, sheetTokens;
    for (int i = 0; i < sheets_.size(); ++i) {
        names.append(sheets_.at(i).name);
        sheetTokens.append(QStringLiteral("f:%1:0").arg(i));
    }
    const auto forward = [index](int row) { return QStringLiteral("f:%1:%2").arg(index).arg(row); };
    QString notice;
    if (document_->partial) notice = QStringLiteral("Partial XLSX preview: only the first 32 worksheets and 256 columns are shown.");
    if (missingResults_) notice += QStringLiteral(" %1 formula result(s) unavailable; saved results are not recalculated.").arg(missingResults_);
    return {{"previous", first ? forward(previous) : QString()},
        {"next", held_ ? forward(position_) : QString()},
        {"content", QJsonObject{{"kind", "table"}, {"sheet", batch.name}, {"sheets", names}, {"sheetTokens", sheetTokens},
            {"rowOffset", first}, {"rows", rows}, {"columns", batch.columnCount}, {"notice", notice}}}};
}

}
XlsxResult XlsxReader::read(QFile &file, const Cancellation &cancel)
{
    try { return {Reader(cancel).read(file), {}, false}; }
    catch (const Stop &stop) { return {{}, stop.error, stop.cancelled}; }
}
struct XlsxPortionReader::State {
    QFile file;
    QString error;
    qint64 size = 0;
    QDateTime modified;
    Reader reader{Cancellation{}};
    bool initialized = false;
    explicit State(const QString &path) {
        FileResource resource;
        resource.path = path;
        if (!openPreviewFile(resource, file, error)) return;
        size = file.size();
        modified = file.fileTime(QFileDevice::FileModificationTime);
    }
};
XlsxPortionReader::XlsxPortionReader(const QString &path) : state_(std::make_unique<State>(path)) {}
XlsxPortionReader::~XlsxPortionReader() = default;
QJsonObject XlsxPortionReader::read(const QString &token)
{
    auto &state = *state_;
    if (!state.error.isEmpty()) return {{"error", state.error}};
    if (state.file.size() != state.size || state.file.fileTime(QFileDevice::FileModificationTime) != state.modified)
        return {{"error", QStringLiteral("The source changed. Reopen the preview to continue.")}};
    try {
        if (!state.initialized) {
            state.reader.initialize(state.file);
            state.initialized = true;
        }
        auto response = state.reader.portion(token);
        if (state.file.size() != state.size || state.file.fileTime(QFileDevice::FileModificationTime) != state.modified)
            return {{"error", QStringLiteral("The source changed. Reopen the preview to continue.")}};
        return response;
    } catch (const Stop &stop) {
        state.error = stop.error;
        return {{"error", state.error}};
    }
}

}

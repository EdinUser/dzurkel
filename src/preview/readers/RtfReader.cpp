#include "DocumentReaderInternal.h"
#include <QHash>
#include <iconv.h>
#include <cerrno>

namespace dzurkel::document {
QString decode(const QByteArray &bytes, const QByteArray &encoding)
{
    if (bytes.isEmpty()) return {};
    const auto sourceEncoding = encoding == "CP65001" ? QByteArray("UTF-8") : encoding;
    auto converter = iconv_open("UTF-8", sourceEncoding.constData());
    if (converter == reinterpret_cast<iconv_t>(-1)) return QString::fromLatin1(bytes);
    QByteArray result;
    char *input = const_cast<char *>(bytes.constData());
    size_t remaining = size_t(bytes.size());
    while (remaining) {
        char buffer[4096];
        char *output = buffer;
        size_t available = sizeof(buffer);
        const auto converted = iconv(converter, &input, &remaining, &output, &available);
        result.append(buffer, qsizetype(sizeof(buffer) - available));
        if (converted == size_t(-1) && errno != E2BIG) {
            result.append("\xef\xbf\xbd");
            ++input;
            --remaining;
        }
    }
    iconv_close(converter);
    return QString::fromUtf8(result);
}
void readRtf(Context &c, const QByteArray &bytes)
{
    if (!bytes.startsWith("{\\rtf")) c.invalid();
    c.markup(QStringLiteral("<p>"));
    struct State {
        bool skip = false;
        bool fontTable = false;
        bool bold = false, italic = false, underline = false, strike = false, hidden = false;
        int fallback = 1;
        int font = 0;
        int codepage = 1252;
    } state;
    QList<State> stack;
    QHash<int, int> fontPages;
    QByteArray pending;
    int fallback = 0;
    bool closed = false;
    int defaultFont = 0;
    const auto encoding = [&] { return QByteArray("CP") + QByteArray::number(fontPages.value(state.font, state.codepage)); };
    ushort pendingHigh = 0;
    auto emitText = [&](QString text) {
        if (state.skip || state.hidden || text.isEmpty()) return;
        if (pendingHigh) {
            text.prepend(text.front().isLowSurrogate() ? QChar(pendingHigh) : QChar::ReplacementCharacter);
            pendingHigh = 0;
        }
        if (text.back().isHighSurrogate()) { pendingHigh = text.back().unicode(); text.chop(1); }
        if (text.isEmpty()) return;
        if (state.bold) c.markup(QStringLiteral("<b>"));
        if (state.italic) c.markup(QStringLiteral("<i>"));
        if (state.underline) c.markup(QStringLiteral("<u>"));
        if (state.strike) c.markup(QStringLiteral("<s>"));
        c.text(text);
        if (state.strike) c.markup(QStringLiteral("</s>"));
        if (state.underline) c.markup(QStringLiteral("</u>"));
        if (state.italic) c.markup(QStringLiteral("</i>"));
        if (state.bold) c.markup(QStringLiteral("</b>"));
    };
    auto flush = [&] { if (!pending.isEmpty()) { emitText(decode(pending, encoding())); pending.clear(); } };
    auto literal = [&](char value) {
        if (fallback > 0) { --fallback; return; }
        if (!state.skip && !state.hidden) pending += value;
        if (pending.size() >= 4096) flush();
    };
    const QList<QByteArray> destinations = {"fonttbl", "colortbl", "stylesheet", "info", "pict", "object", "objdata", "fldinst",
        "header", "headerl", "headerr", "footer", "footerl", "footerr", "generator", "listtable", "listoverridetable",
        "datastore", "xmlnstbl", "themedata", "colorschememapping", "datafield", "filetbl", "revtbl", "rsidtbl"};
    for (qsizetype i = 0; i < bytes.size();) {
        c.check();
        const char ch = bytes.at(i++);
        if (closed) { if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t') c.invalid(); continue; }
        if (ch == '{') {
            flush();
            if (stack.size() >= 64) c.limit();
            stack.append(state);
        } else if (ch == '}') {
            flush();
            if (stack.isEmpty()) c.invalid();
            state = stack.takeLast();
            if (stack.isEmpty()) closed = true;
        } else if (ch == '\\') {
            if (i >= bytes.size()) c.invalid();
            const char symbol = bytes.at(i++);
            if (symbol == '\\' || symbol == '{' || symbol == '}') { literal(symbol); continue; }
            if (symbol == '\'') {
                if (i + 2 > bytes.size()) c.invalid();
                const auto pair = bytes.mid(i, 2);
                bool ok;
                const auto value = pair.toUInt(&ok, 16);
                if (!ok || pair.size() != 2) c.invalid();
                literal(char(value)); i += 2; continue;
            }
            flush();
            if (symbol == '*') { state.skip = true; continue; }
            if (symbol == '~' || symbol == '_' || symbol == '-') {
                if (fallback) --fallback;
                else emitText(symbol == '~' ? QString(QChar(0xa0)) : symbol == '_' ? QString(QChar(0x2011)) : QString(QChar(0xad)));
                continue;
            }
            if (!((symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z'))) continue;
            const auto start = i - 1;
            while (i < bytes.size() && ((bytes.at(i) >= 'a' && bytes.at(i) <= 'z') || (bytes.at(i) >= 'A' && bytes.at(i) <= 'Z'))) ++i;
            if (i - start > 64) c.invalid();
            const auto control = bytes.mid(start, i - start);
            const auto numberStart = i;
            if (i < bytes.size() && bytes.at(i) == '-') ++i;
            while (i < bytes.size() && bytes.at(i) >= '0' && bytes.at(i) <= '9') ++i;
            const bool hasNumber = i > numberStart;
            bool ok = true;
            const int value = hasNumber ? bytes.mid(numberStart, i - numberStart).toInt(&ok) : 1;
            if (!ok) c.invalid();
            if (i < bytes.size() && bytes.at(i) == ' ') ++i;
            if (control == "bin") {
                if (!hasNumber || value < 0 || value > bytes.size() - i) c.invalid();
                i += value; continue;
            }
            if (control == "fonttbl") state.fontTable = true;
            if (destinations.contains(control)) {
                if (control == "pict") emitText(QStringLiteral(" [Image] "));
                if (control == "object") emitText(QStringLiteral(" [Embedded object] "));
                state.skip = true;
            }
            if (control == "ansicpg" && value > 0 && value < 65536) state.codepage = value;
            if (control == "deff") { defaultFont = value; state.font = value; }
            if (control == "f") state.font = value;
            if (control == "fcharset" && state.fontTable) {
                const QHash<int, int> pages{{0,1252},{128,932},{129,949},{134,936},{136,950},{161,1253},{162,1254},{163,1258},{177,1255},{178,1256},{186,1257},{204,1251},{222,874},{238,1250}};
                if (pages.contains(value)) fontPages.insert(state.font, pages.value(value));
            }
            if (control == "uc") { if (value < 0 || value > 16) c.invalid(); state.fallback = value; }
            if (control == "u") {
                if (value < -32768 || value > 65535) c.invalid();
                emitText(QString(QChar(ushort(value)))); fallback = state.fallback;
            } else if (control == "b") state.bold = value != 0;
            else if (control == "i") state.italic = value != 0;
            else if (control == "ul") state.underline = value != 0;
            else if (control == "ulnone") state.underline = false;
            else if (control == "strike") state.strike = value != 0;
            else if (control == "v") state.hidden = value != 0;
            else if (control == "deleted") { state.hidden = value != 0; c.notice(QStringLiteral("Revisions present; showing current text.")); }
            else if (control == "plain") { state.bold = state.italic = state.underline = state.strike = state.hidden = false; state.font = defaultFont; }
            else if (!state.skip && !state.hidden) {
                if (control == "par" || control == "row") { c.markup(QStringLiteral("</p><p>")); c.paragraph(); }
                else if (control == "line") { c.markup(QStringLiteral("<br/>")); c.paragraph(); }
                else if (control == "tab" || control == "cell") emitText(QStringLiteral("\t"));
                else if (control == "emdash") emitText(QString(QChar(0x2014)));
                else if (control == "endash") emitText(QString(QChar(0x2013)));
                else if (control == "bullet") emitText(QString(QChar(0x2022)));
                else if (control == "lquote") emitText(QString(QChar(0x2018)));
                else if (control == "rquote") emitText(QString(QChar(0x2019)));
                else if (control == "ldblquote") emitText(QString(QChar(0x201c)));
                else if (control == "rdblquote") emitText(QString(QChar(0x201d)));
            }
        } else if (ch != '\r' && ch != '\n') literal(ch);
    }
    flush();
    if (pendingHigh) { pendingHigh = 0; emitText(QString(QChar::ReplacementCharacter)); }
    if (!closed || !stack.isEmpty()) c.invalid();
    c.markup(QStringLiteral("</p>"));
    c.notice(QStringLiteral("Document layout simplified."));
}
}

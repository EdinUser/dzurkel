#include "DocumentReaderInternal.h"
#include "DocumentLines.h"
#include "preview/document/MarkdownLayout.h"
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextFrame>

namespace dzurkel::document {
namespace {
constexpr auto Features = QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML;
class MarkdownDocument final : public QTextDocument {
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
void preflight(Context &c, const QString &text)
{
    int lines = 1, pipes = 0, maximum = 0;
    for (const auto ch : text) {
        if (ch == QChar('|')) ++pipes;
        if (ch == QChar('\n')) { ++lines; maximum = qMax(maximum, pipes); pipes = 0; }
    }
    maximum = qMax(maximum, pipes);
    if (maximum > 256 || (maximum && qint64(maximum + 1) * lines > 100000))
        throw Stop{QStringLiteral("Markdown shown as plain text; table layout exceeds preview limits.")};
    if (lines > 5000 || text.size() > DocumentReader::CharacterLimit) c.limit();
}
// This is a source-boundary adapter, not a Markdown renderer. Ask the existing
// Qt importer whether a candidate blank-line boundary is outside code, lists,
// quotes and tables. Keep an entire ambiguous construct rather than cutting it.
class MarkdownSource {
    Context &c;
    DocumentLines lines;
    std::optional<QString> held;
    QString deferredError;
public:
    MarkdownSource(Context &context, QIODevice &input) : c(context), lines(context, input) {}
    std::optional<QString> next() {
        if (!deferredError.isEmpty()) throw Stop{deferredError};
        QString source;
        bool blank = false;
        for (;;) {
            std::optional<QString> line;
            try { line = held ? std::exchange(held, {}) : lines.next(); }
            catch (const Stop &stop) {
                if (stop.cancelled || source.trimmed().isEmpty()) throw;
                deferredError = stop.error;
                return source;
            }
            if (!line) return source.isEmpty() ? std::nullopt : std::optional<QString>(source);
            if (blank && source.size() >= 8192 && !line->isEmpty() && !line->front().isSpace()) {
                preflight(c, source);
                MarkdownDocument probe;
                QString marker = QStringLiteral("DzurkelBoundaryProbe");
                while (source.contains(marker)) marker += QChar('x');
                probe.setMarkdown(source + QChar('\n') + marker + QChar('\n'), Features);
                const auto found = probe.find(marker);
                if (!found.isNull() && found.block().text() == marker && !found.block().textList()
                    && !found.blockFormat().hasProperty(QTextFormat::BlockCodeFence)
                    && !found.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)
                    && !found.blockFormat().intProperty(QTextFormat::BlockQuoteLevel)
                    && found.currentFrame() == probe.rootFrame()) {
                    auto previous = found.block().previous();
                    while (previous.isValid() && previous.text().isEmpty()) previous = previous.previous();
                    MarkdownDocument following; following.setMarkdown(*line, Features);
                    auto first = following.begin();
                    const bool continuesList = previous.isValid() && previous.textList() && first.textList();
                    const bool continuesQuote = previous.isValid() && previous.blockFormat().intProperty(QTextFormat::BlockQuoteLevel)
                        && first.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
                    if (!continuesList && !continuesQuote) { held = std::move(line); return source; }
                }
            }
            source += *line + QChar('\n');
            blank = line->trimmed().isEmpty();
            preflight(c, source);
        }
    }
};
class MarkdownParser final : public PortionParser {
    Context &c;
    QFile &file;
    MarkdownSource source;
    QString definitions;
    bool referencesScanned = false;
    qint64 heading = 0;
    void references() {
        // Qt resolves definitions only within an import. Collect a bounded
        // dictionary by asking that same importer to resolve candidate labels;
        // definitions inside code and duplicate definitions retain Qt semantics.
        const auto position = file.pos();
        const auto restore = qScopeGuard([&] { file.seek(position); });
        if (!file.seek(0)) c.invalid();
        DocumentInput input(c, file);
        MarkdownSource scan(c, input);
        QSet<QString> seen;
        static const QRegularExpression definition(QStringLiteral(R"(\[((?:\\.|[^\[\]\\]){1,999})\]:)"));
        while (auto part = scan.next()) {
            c.check();
            auto matches = definition.globalMatch(*part);
            while (matches.hasNext()) {
                const auto label = matches.next().captured(1);
                const auto normalized = label.simplified().toCaseFolded();
                if (seen.contains(normalized)) continue;
                MarkdownDocument probe;
                QString marker = QStringLiteral("DzurkelReferenceProbe");
                while (part->contains(marker)) marker += QChar('x');
                probe.setMarkdown(*part + QStringLiteral("\n\n[%1][%2]\n").arg(marker, label), Features);
                const auto found = probe.find(marker);
                if (found.isNull() || !found.charFormat().isAnchor() || found.charFormat().anchorHref().isEmpty()) continue;
                auto href = found.charFormat().anchorHref();
                href.replace(QChar('<'), QStringLiteral("%3C")); href.replace(QChar('>'), QStringLiteral("%3E"));
                auto tooltip = found.charFormat().toolTip();
                tooltip.replace(QChar('\\'), QStringLiteral("\\\\")); tooltip.replace(QChar('"'), QStringLiteral("\\\""));
                tooltip.replace(QChar('\n'), QChar(' ')); tooltip.replace(QChar('\r'), QChar(' '));
                definitions += QStringLiteral("[%1]: <%2>%3\n").arg(label, href,
                    tooltip.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(tooltip));
                seen.insert(normalized);
                if (definitions.size() > 65536 || seen.size() > 2048) c.limit();
            }
        }
        referencesScanned = true;
    }
public:
    MarkdownParser(Context &context, QIODevice &input, QFile &sourceFile) : c(context), file(sourceFile), source(context, input) {}
    bool next() override {
        const auto part = source.next();
        if (!part) return false;
        if (!referencesScanned && part->contains(QChar('['))) references();
        MarkdownDocument rendered;
        rendered.setMarkdown(*part + QChar('\n') + definitions, Features);
        normalizeMarkdownTables(&rendered);
        for (auto block = rendered.begin(); block.isValid(); block = block.next()) {
            const auto level = block.blockFormat().headingLevel();
            if (!level || block.text().trimmed().isEmpty()) continue;
            const auto anchor = level == 1 && !heading ? QStringLiteral("dzurkel-title-markdown") : QStringLiteral("markdown-%1").arg(heading);
            ++heading;
            auto cursor = QTextCursor(block);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            auto format = cursor.charFormat(); format.setAnchor(true); format.setAnchorNames({anchor}); cursor.setCharFormat(format);
            if (c.doc->sections.size() < 64) c.doc->sections.append({block.text().left(100), anchor});
            if (level == 1 && c.doc->title.isEmpty()) c.doc->title = block.text().left(500);
        }
        const auto text = rendered.toPlainText();
        if (c.doc->plainText.size() + text.size() > DocumentReader::CharacterLimit) c.limit();
        c.doc->plainText += text;
        if (!c.doc->plainText.endsWith(QChar('\n'))) c.doc->plainText += QChar('\n');
        c.markup(rendered.toHtml());
        return true;
    }
};
}
std::unique_ptr<PortionParser> markdownParser(Context &c, QIODevice &input, QFile &file) { return std::make_unique<MarkdownParser>(c, input, file); }
}

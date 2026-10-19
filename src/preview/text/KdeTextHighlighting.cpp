#include "TextHighlightingBackend.h"
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#include <QEvent>
#include <QPalette>
#include <QPlainTextEdit>

namespace dzurkel {
namespace {
class HighlightedDocument : public QObject {
public:
    HighlightedDocument(QPlainTextEdit *editor, KSyntaxHighlighting::Repository &repository,
                        const KSyntaxHighlighting::Definition &definition)
        : QObject(editor), editor_(editor), repository_(repository),
          highlighter_(new KSyntaxHighlighting::SyntaxHighlighter(editor->document()))
    {
        highlighter_->setObjectName(QStringLiteral("textSyntaxHighlighter"));
        highlighter_->setDefinition(definition);
        updateTheme();
        editor_->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
            updateTheme();
        return false;
    }
private:
    void updateTheme()
    {
        const bool dark = editor_->palette().color(QPalette::Base).lightness() < 128;
        highlighter_->setTheme(repository_.defaultTheme(dark
            ? KSyntaxHighlighting::Repository::DarkTheme : KSyntaxHighlighting::Repository::LightTheme));
        highlighter_->rehighlight();
    }
    QPlainTextEdit *editor_;
    KSyntaxHighlighting::Repository &repository_;
    KSyntaxHighlighting::SyntaxHighlighter *highlighter_;
};
}

class KdeTextHighlighting : public QObject, public TextHighlightingBackend {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DZURKEL_TEXT_HIGHLIGHTING_IID)
    Q_INTERFACES(dzurkel::TextHighlightingBackend)
public:
    bool attach(QPlainTextEdit *editor, const QString &fileName, const QString &mimeType) override
    {
        auto definition = repository_.definitionForFileName(fileName);
        if (!definition.isValid()) definition = repository_.definitionForMimeType(mimeType);
        if (!definition.isValid()) return false;
        new HighlightedDocument(editor, repository_, definition);
        return true;
    }
private:
    KSyntaxHighlighting::Repository repository_;
};

} // namespace dzurkel

#include "KdeTextHighlighting.moc"

#include "preview/providers/TextPreviewProvider.h"
#include "preview/text/TextHighlighting.h"
#include "ui/ZoomPreview.h"
#include <QApplication>
#include <QPalette>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSyntaxHighlighter>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

using namespace dzurkel;

class TextHighlightingTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir configuration_;
private slots:
    void initTestCase()
    {
        QVERIFY(configuration_.isValid());
        qputenv("XDG_CONFIG_HOME", configuration_.path().toUtf8());
    }
    void preferenceDisablesHighlightingWithoutChangingSource()
    {
        QString error;
        QVERIFY(setHighlightingEnabled(false, error));
        QVERIFY(!highlightingEnabled());
        QPlainTextEdit editor;
        const auto source = QStringLiteral("{\"answer\": 42}");
        editor.setPlainText(source);
        QVERIFY(!highlightText(&editor, QStringLiteral("file.json"), QStringLiteral("application/json")));
        QVERIFY(!hasFormatting(editor));
        QCOMPARE(editor.toPlainText(), source);
        QVERIFY(setHighlightingEnabled(true, error));
        QCOMPARE(highlightText(&editor, QStringLiteral("file.json"), QStringLiteral("application/json")), expectsBackend());
        QCOMPARE(editor.toPlainText(), source);
    }
    void recheckFindsNewlyInstalledAdapter()
    {
        const auto source = qEnvironmentVariable("DZURKEL_TEST_RECHECK_PLUGIN");
        if (source.isEmpty()) QSKIP("Only run in the isolated copied-binary harness.");
        QVERIFY(!highlightingAvailability().available);
        const QDir application(QCoreApplication::applicationDirPath());
        QVERIFY(application.mkpath(QStringLiteral("dzurkel-plugins")));
        const auto destination = application.filePath(QStringLiteral("dzurkel-plugins/libdzurkel_highlighting.so"));
        QVERIFY(QFile::copy(source, destination));
        QVERIFY(!highlightingAvailability().available);
        const auto state = highlightingAvailability(true);
        QVERIFY2(state.available, qPrintable(state.explanation));
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("{\"answer\": 42}"));
        QVERIFY(highlightText(&editor, QStringLiteral("file.json"), QStringLiteral("application/json")));
        QVERIFY(hasFormatting(editor));
        QVERIFY(QFile::remove(destination));
    }
    void availabilityDistinguishesMissingAndUnloadableAdapters()
    {
        const auto state = highlightingAvailability();
        QCOMPARE(state.available, expectsBackend());
        if (expectsBackend()) QVERIFY(state.adapterFound);
        const auto expected = qgetenv("DZURKEL_TEST_ADAPTER_PRESENT");
        if (!expected.isEmpty()) QCOMPARE(state.adapterFound, expected == "1");
        QVERIFY(!state.explanation.isEmpty());
    }
    void sourceFormatsReachTheTextProvider()
    {
        const QMimeDatabase database;
        for (const auto &name : {"example.json", "example.sql", "example.php"}) {
            const auto mime = database.mimeTypeForFile(QString::fromLatin1(name), QMimeDatabase::MatchExtension);
            QVERIFY2(TextPreviewProvider().supports(mime), qPrintable(mime.name()));
        }
    }
    void highlightsSourceWithoutChangingText_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("mime");
        QTest::addColumn<QString>("text");
        QTest::newRow("json") << "example.json" << "text/plain" << "{\"name\": \"Dzurkel\", \"value\": 42}";
        QTest::newRow("sql") << "example.sql" << "text/plain" << "SELECT name FROM files WHERE id = 42; -- comment";
        QTest::newRow("php") << "example.php" << "text/plain" << "<?php\n// comment\necho \"Dzurkel\";\n";
        QTest::newRow("mime-only") << "example.unknown-dzurkel-format" << "application/json" << "{\"value\": true}";
    }
    void highlightsSourceWithoutChangingText()
    {
        QFETCH(QString, name);
        QFETCH(QString, mime);
        QFETCH(QString, text);
        for (int i = 0; i < 3; ++i) {
            QPointer<QSyntaxHighlighter> highlighter;
            {
                QPlainTextEdit editor;
                editor.setPlainText(text);
                QCOMPARE(highlightText(&editor, name, mime), expectsBackend());
                QCOMPARE(editor.toPlainText(), text);
                QCOMPARE(hasFormatting(editor), expectsBackend());
                highlighter = editor.document()->findChild<QSyntaxHighlighter *>();
                QCOMPARE(!highlighter.isNull(), expectsBackend());
            }
            QVERIFY(highlighter.isNull());
        }
    }
    void unsupportedAndLargeTextStayPlain()
    {
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("literal <script>text</script>"));
        QVERIFY(!highlightText(&editor, QStringLiteral("file.unknown-dzurkel-format"), QStringLiteral("text/plain")));
        QVERIFY(!hasFormatting(editor));
        editor.setPlainText(QString(4096, QLatin1Char('x')));
        QVERIFY(!highlightText(&editor, QStringLiteral("file.json"), QStringLiteral("application/json")));
        QVERIFY(!hasFormatting(editor));
        QString large;
        while (large.size() < 256 * 1024) large += QStringLiteral("SELECT 42;\n");
        editor.setPlainText(large);
        QVERIFY(!highlightText(&editor, QStringLiteral("file.sql"), QStringLiteral("text/plain")));
        QCOMPARE(editor.toPlainText(), large);
        QVERIFY(!hasFormatting(editor));
    }
    void providerUsesOriginalRemoteNameAndPreservesZoomAndPalette()
    {
        PreviewData data;
        data.kind = PreviewKind::Text;
        data.text = QStringLiteral("SELECT 42; -- source\n");
        data.mimeType = QStringLiteral("text/plain");
        data.resource.url = QUrl(QStringLiteral("sftp://example.invalid/source/query.sql"));
        data.resource.backingPath = QStringLiteral("/tmp/snapshot.txt");
        auto widget = std::unique_ptr<QWidget>(TextPreviewProvider().createWidget(data, nullptr));
        auto *editor = widget->findChild<QPlainTextEdit *>();
        QVERIFY(editor);
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::NoWrap);
        QCOMPARE(hasFormatting(*editor), expectsBackend());
        auto *zoom = qobject_cast<ZoomPreview *>(widget.get());
        QVERIFY(zoom);
        const auto originalSize = editor->font().pointSizeF();
        zoom->zoomBy(1);
        QVERIFY(editor->font().pointSizeF() > originalSize);
        zoom->fit();
        QCOMPARE(editor->font().pointSizeF(), originalSize);
        for (const auto &base : {Qt::black, Qt::white}) {
            auto palette = editor->palette();
            palette.setColor(QPalette::Base, base);
            palette.setColor(QPalette::Text, base == Qt::black ? Qt::white : Qt::black);
            editor->setPalette(palette);
            QCOMPARE(hasFormatting(*editor), expectsBackend());
            QCOMPARE(editor->toPlainText(), data.text);
        }
    }
private:
    static bool expectsBackend()
    {
#ifdef DZURKEL_TEST_HIGHLIGHTING
        return !qEnvironmentVariableIsSet("DZURKEL_TEST_NO_HIGHLIGHTING");
#else
        return false;
#endif
    }
    static bool hasFormatting(const QPlainTextEdit &editor)
    {
        for (auto block = editor.document()->begin(); block.isValid(); block = block.next()) {
            if (!block.layout()->formats().isEmpty()) return true;
        }
        return false;
    }
};

QTEST_MAIN(TextHighlightingTest)
#include "TextHighlightingTest.moc"

#include "controller/PreviewController.h"
#include "preview/PreviewLibrary.h"
#include "preview/media/MediaBackend.h"
#include "ui/SettingsDialog.h"
#include "../support/OffscreenWarnings.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace dzurkel;
class OptionalReadersTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(directory_.isValid());
        qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
        warnings_ = std::make_unique<OffscreenWarnings>();
    }
    void cleanupTestCase() { warnings_.reset(); }
    void catalogDoesNotDependOnInstalledReaders()
    {
        QVERIFY(PreviewLibrary::capability(QStringLiteral("audio")));
        QVERIFY(PreviewLibrary::capability(QStringLiteral("video")));
        QVERIFY(PreviewLibrary::capability(QStringLiteral("doc")));
        QCOMPARE(mediaAvailability().available, qEnvironmentVariableIntValue("DZURKEL_TEST_AUDIO_AVAILABLE") == 1);
    }
    void missingAudioExplainsRequirementAndOpensLibraries_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::newRow("audio") << QStringLiteral("mp3");
        QTest::newRow("video") << QStringLiteral("mkv");
    }
    void mismatchedAdapterExplainsRebuild()
    {
        if (!qEnvironmentVariableIsSet("DZURKEL_TEST_MEDIA_INCOMPATIBLE")) QSKIP("Isolated incompatible-adapter case.");
        const auto availability = mediaAvailability();
        QVERIFY(!availability.available);
        QVERIFY(availability.adapterFound);
        QVERIFY2(availability.explanation.contains(QStringLiteral("does not match")), qPrintable(availability.explanation));
        QVERIFY(availability.explanation.contains(QStringLiteral("Rebuild and reinstall")));
        QVERIFY(availability.explanation.contains(QStringLiteral("libdzurkel_audio.so")));
        QVERIFY(!availability.explanation.contains(QStringLiteral("Unknown error")));
    }
    void missingAudioExplainsRequirementAndOpensLibraries()
    {
        QFETCH(QString, suffix);
        if (mediaAvailability().available) QSKIP("Missing adapter is exercised by the isolated fallback harness.");
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(write(QStringLiteral("missing.") + suffix, "ID3fixture"), error));
        QTRY_VERIFY(!controller.loading());
        QCOMPARE(controller.previewKind(), QStringLiteral("information"));
        auto *button = controller.window()->findChild<QPushButton *>(QStringLiteral("openLibrarySettings"));
        QVERIFY(button);
        QCOMPARE(button->property("libraryId").toString(), QStringLiteral("multimedia"));
        button->click();
        auto *settings = controller.window()->findChild<SettingsDialog *>();
        QVERIFY(settings && settings->isVisible());
        QCOMPARE(settings->findChild<QTabWidget *>(QStringLiteral("settingsTabs"))->currentIndex(), 2);
        settings->hide();
        controller.hide();
    }
    void missingAntiwordHasDistinctRecoveryAction()
    {
        const auto oldPath = qgetenv("PATH");
        qputenv("PATH", directory_.path().toUtf8());
        const auto restore = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(write(QStringLiteral("missing.doc"), QByteArray::fromHex("d0cf11e0a1b11ae1") + QByteArray(512, '\0')), error));
        QTRY_VERIFY(!controller.loading());
        QCOMPARE(controller.previewKind(), QStringLiteral("information"));
        auto *button = controller.window()->findChild<QPushButton *>(QStringLiteral("openLibrarySettings"));
        QVERIFY(button);
        QCOMPARE(button->property("libraryId").toString(), QStringLiteral("antiword"));
    }
    void baselineRemainsUsable()
    {
        const auto image = directory_.filePath(QStringLiteral("image.png"));
        QImage pixels(40, 30, QImage::Format_RGB32);
        pixels.fill(Qt::red);
        QVERIFY(pixels.save(image));
        const auto pdf = directory_.filePath(QStringLiteral("document.pdf"));
        { QPdfWriter writer(pdf); QPainter painter(&writer); painter.fillRect(QRect(20, 20, 100, 100), Qt::blue); }
        const QList<QPair<QString, QString>> previews = {
            {write(QStringLiteral("text.txt"), "Plain text"), QStringLiteral("text")},
            {write(QStringLiteral("table.csv"), "one,two\n1,2\n"), QStringLiteral("table")},
            {write(QStringLiteral("document.md"), "# Heading\nText"), QStringLiteral("document")},
            {image, QStringLiteral("image")}, {pdf, QStringLiteral("pdf")}
        };
        PreviewController controller;
        for (const auto &[path, kind] : previews) {
            QString error;
            QVERIFY(controller.show(path, error));
            QTRY_VERIFY(!controller.loading());
            QCOMPARE(controller.previewKind(), kind);
            controller.hide();
        }
    }
    void refreshCanLoadAnAddedAdapter()
    {
        const auto source = qEnvironmentVariable("DZURKEL_TEST_ADD_AUDIO_ADAPTER");
        if (source.isEmpty()) QSKIP("Only runs in the isolated copied-binary harness.");
        QVERIFY(!mediaAvailability().available);
        const QDir application(QCoreApplication::applicationDirPath());
        QVERIFY(application.mkpath(QStringLiteral("dzurkel-plugins")));
        const auto destination = application.filePath(QStringLiteral("dzurkel-plugins/libdzurkel_audio.so"));
        QVERIFY(QFile::copy(source, destination));
        QVERIFY(!mediaAvailability().available);
        const auto state = mediaAvailability(true);
        QVERIFY2(state.available, qPrintable(state.explanation));
        PreviewData data;
        data.kind = PreviewKind::Audio;
        std::unique_ptr<QWidget> view(createMediaPreview(data, nullptr));
        QVERIFY(qobject_cast<MediaControls *>(view.get()));
        // Delete before queued playback startup; this checks loading, not audio.
        view.reset();
        QVERIFY(QFile::remove(destination));
    }
private:
    QString write(const QString &name, const QByteArray &bytes)
    {
        QFile file(directory_.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
        return file.fileName();
    }
    QTemporaryDir directory_;
    std::unique_ptr<OffscreenWarnings> warnings_;
};
QTEST_MAIN(OptionalReadersTest)
#include "OptionalReadersTest.moc"

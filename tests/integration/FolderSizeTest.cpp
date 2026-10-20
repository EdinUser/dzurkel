#include "controller/PreviewController.h"
#include "desktop/FolderSizeBackend.h"
#include "preview/providers/FileInfoProvider.h"
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <sys/stat.h>
#include <unistd.h>

using namespace dzurkel;

class FakeFolderSize final : public FolderSizeBackend {
public:
    QList<FolderSizeCallback> callbacks;
    QPointer<QObject> job;
    QUrl url;
    QObject *start(const QUrl &source, FolderSizeCallback callback) override
    {
        url = source;
        callbacks.append(std::move(callback));
        job = new QObject;
        return job;
    }
};

class FolderSizeTest : public QObject {
    Q_OBJECT
private:
    static PreviewData load(const FileResource &resource, const QSize &, const Cancellation &)
    {
        auto data = FileInfoProvider::describe(resource);
        data.resource = resource;
        return data;
    }
    static void write(const QString &path, int count)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(count, 'x')), count);
    }
private slots:
    void directoryMetadataIsNeverPresentedAsContentsSize()
    {
        QTemporaryDir dir;
        FileResource resource;
        QString error;
        QVERIFY(resolveFileResource(dir.path(), resource, error));
        const auto data = FileInfoProvider::describe(resource);
        QCOMPARE(data.size, -1);
        std::unique_ptr<QWidget> widget(FileInfoProvider::createWidget(data, nullptr));
        QCOMPARE(widget->findChild<QLabel *>("folderSize")->text(), QStringLiteral("Contents size: Unavailable"));
        resource.metadata = {{"mime", "inode/directory"}, {"size", 4096}};
        QCOMPARE(FileInfoProvider::describe(resource).size, -1);
    }
    void progressStopAndStaleCallbacksPreserveTheCurrentPreview()
    {
        QTemporaryDir first, second;
        FakeFolderSize backend;
        PreviewController controller(false, load, &backend);
        QString error;
        QVERIFY(controller.show(first.path(), error));
        QTRY_COMPARE(backend.callbacks.size(), 1);
        const auto label = [&] { return controller.window()->findChild<QLabel *>("folderSize"); };
        QVERIFY(label());
        QCOMPARE(label()->text(), QStringLiteral("Contents size: Calculating…"));
        FolderSize progress{FolderSizeState::Calculating, 123, 2, 1, {}};
        backend.callbacks[0](progress);
        QVERIFY(label()->text().contains("123 bytes"));
        QVERIFY(label()->text().contains("counted so far"));
        auto *stop = controller.window()->findChild<QPushButton *>("stopFolderSize");
        QVERIFY(stop && stop->isVisible());
        QTest::mouseClick(stop, Qt::LeftButton);
        QVERIFY(backend.job.isNull());
        QVERIFY(label()->text().contains("stopped"));
        auto complete = progress;
        complete.state = FolderSizeState::Complete;
        backend.callbacks[0](complete);
        QVERIFY(label()->text().contains("stopped"));

        QVERIFY(controller.show(second.path(), error));
        QTRY_COMPARE(backend.callbacks.size(), 2);
        backend.callbacks[0](complete);
        QCOMPARE(label()->text(), QStringLiteral("Contents size: Calculating…"));
        complete.bytes = 456;
        backend.callbacks[1](complete);
        QVERIFY(label()->text().contains("456 bytes"));
        QVERIFY(!controller.window()->findChild<QPushButton *>("stopFolderSize")->isVisible());
        auto *info = controller.window()->findChild<QPushButton *>("fileInfoButton");
        QVERIFY(info->toolTip().contains("456 bytes"));
        QTest::mouseClick(info, Qt::LeftButton);
        auto *dialogText = controller.window()->findChild<QLabel *>("fileInformationText");
        QVERIFY(dialogText && dialogText->text().contains("456 bytes"));
        controller.hide();
        QVERIFY(backend.job.isNull());
        backend.callbacks[1](progress); // A late notification after dismissal is harmless.
    }
    void controllerDestructionCancelsTheJobAndIgnoresLateUpdates()
    {
        QTemporaryDir dir;
        FakeFolderSize backend;
        auto controller = std::make_unique<PreviewController>(false, load, &backend);
        QString error;
        QVERIFY(controller->show(dir.path(), error));
        QTRY_COMPARE(backend.callbacks.size(), 1);
        controller.reset();
        QVERIFY(backend.job.isNull());
        backend.callbacks[0]({FolderSizeState::Complete, 123, 1, 0, {}});
    }
    void partialAndUnavailableResultsStayExplicit()
    {
        QTemporaryDir dir;
        FakeFolderSize backend;
        PreviewController controller(false, load, &backend);
        QString error;
        QVERIFY(controller.show(dir.path(), error));
        QTRY_COMPARE(backend.callbacks.size(), 1);
        QTest::mouseClick(controller.window()->findChild<QPushButton *>("fileInfoButton"), Qt::LeftButton);
        auto *dialogText = controller.window()->findChild<QLabel *>("fileInformationText");
        QVERIFY(dialogText && dialogText->text().contains("Calculating"));
        backend.callbacks[0]({FolderSizeState::Partial, 17, 1, 0, "Some entries unavailable"});
        QVERIFY(dialogText->text().contains("17 bytes"));
        QVERIFY(dialogText->text().contains("partial"));
        auto *label = controller.window()->findChild<QLabel *>("folderSize");
        QVERIFY(label->text().contains("partial"));
        QVERIFY(controller.window()->findChild<QPushButton *>("fileInfoButton")->toolTip().contains("Some entries unavailable"));
        controller.hide();
    }
    void resolvedRemotePathIsUsedWithoutChangingIdentity()
    {
        QTemporaryDir dir;
        FakeFolderSize backend;
        PreviewController controller(false,
            [&dir](const FileResource &resource, const QSize &, const Cancellation &) {
                PreviewData data;
                data.resource = resource;
                data.resource.metadata = {{"directoryPath", dir.path()}};
                data.mimeType = "inode/directory";
                return data;
            }, &backend);
        QString error;
        QVERIFY(controller.show("smb://example.invalid/share/folder", error));
        QTRY_COMPARE(backend.callbacks.size(), 1);
        QCOMPARE(backend.url, QUrl::fromLocalFile(dir.path()));
        QCOMPARE(controller.fileUrl(), QStringLiteral("smb://example.invalid/share/folder"));
        controller.hide();
        QVERIFY(backend.job.isNull());
    }
    void missingDesktopCalculatorKeepsFolderUsable()
    {
        if (folderSizeBackend()) QSKIP("This check requires an unavailable desktop adapter.");
        QTemporaryDir dir;
        PreviewController controller(false, load);
        QString error;
        QVERIFY(controller.show(dir.path(), error));
        QTRY_VERIFY(controller.window()->findChild<QLabel *>("folderSize"));
        QCOMPARE(controller.window()->findChild<QLabel *>("folderSize")->text(), QStringLiteral("Contents size: Unavailable"));
        QCOMPARE(controller.previewKind(), QStringLiteral("information"));
        controller.hide();
    }
    void realFolderPreviewUpdatesUiAndPopup()
    {
#ifndef DZURKEL_TEST_FOLDER_SIZE
        QSKIP("Optional desktop calculator was not built.");
#else
        QTemporaryDir dir;
        write(dir.filePath("one"), 12345);
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(dir.path(), error));
        QTRY_VERIFY_WITH_TIMEOUT(controller.window()->findChild<QLabel *>("folderSize"), 10000);
        auto *label = controller.window()->findChild<QLabel *>("folderSize");
        QTRY_VERIFY_WITH_TIMEOUT(label->text().contains("12345 bytes") && !label->text().contains("calculating"), 10000);
        QVERIFY(controller.window()->findChild<QLabel *>("folderCounts")->text().contains("1 file"));
        auto *info = controller.window()->findChild<QPushButton *>("fileInfoButton");
        QVERIFY(info->toolTip().contains("12345 bytes"));
        if (const auto screenshot = qgetenv("DZURKEL_TEST_FOLDER_SCREENSHOT"); !screenshot.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(const_cast<PreviewWindow *>(controller.window())->grab().save(QString::fromLocal8Bit(screenshot)));
        }
        controller.hide();
#endif
    }
    void realDesktopCalculatorHandlesNestedAndEmptyFolders()
    {
#ifndef DZURKEL_TEST_FOLDER_SIZE
        QSKIP("Optional desktop calculator was not built.");
#else
        auto *backend = folderSizeBackend();
        QVERIFY(backend);
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("nested"));
        write(dir.filePath("one"), 3);
        write(dir.filePath(".hidden"), 7);
        write(dir.filePath("nested/two"), 9);
        FolderSize result;
        std::unique_ptr<QObject> job(backend->start(QUrl::fromLocalFile(dir.path()),
            [&](const FolderSize &size) { result = size; }));
        QTRY_COMPARE_WITH_TIMEOUT(result.state, FolderSizeState::Complete, 10000);
        QCOMPARE(result.bytes, quint64(19));
        QCOMPARE(result.files, quint64(3));
        QCOMPARE(result.folders, quint64(1));
        job.reset();
        QTemporaryDir aliases;
        QVERIFY(::symlink(QFile::encodeName(dir.path()), QFile::encodeName(aliases.filePath("selected-link"))) == 0);
        result = {};
        job.reset(backend->start(QUrl::fromLocalFile(aliases.filePath("selected-link")), [&](const FolderSize &size) { result = size; }));
        QTRY_COMPARE_WITH_TIMEOUT(result.state, FolderSizeState::Complete, 10000);
        QCOMPARE(result.bytes, quint64(19));
        job.reset();
        QTemporaryDir empty;
        result = {};
        job.reset(backend->start(QUrl::fromLocalFile(empty.path()), [&](const FolderSize &size) { result = size; }));
        QTRY_COMPARE_WITH_TIMEOUT(result.state, FolderSizeState::Complete, 10000);
        QCOMPARE(result.bytes, quint64(0));
        QCOMPARE(result.files, quint64(0));
#endif
    }
    void realDesktopCalculatorUsesItsLinkAndSparseFileSemantics()
    {
#ifndef DZURKEL_TEST_FOLDER_SIZE
        QSKIP("Optional desktop calculator was not built.");
#else
        auto *backend = folderSizeBackend();
        QVERIFY(backend);
        QTemporaryDir dir, external;
        write(dir.filePath("one"), 3);
        write(external.filePath("outside"), 200);
        QVERIFY(::link(QFile::encodeName(dir.filePath("one")), QFile::encodeName(dir.filePath("hard"))) == 0);
        QVERIFY(::symlink(QFile::encodeName(external.path()), QFile::encodeName(dir.filePath("link"))) == 0);
        QVERIFY(::mkfifo(QFile::encodeName(dir.filePath("fifo")), 0600) == 0);
        QFile sparse(dir.filePath("sparse"));
        QVERIFY(sparse.open(QIODevice::WriteOnly));
        QVERIFY(sparse.resize(1024 * 1024));
        sparse.close();
        FolderSize result;
        std::unique_ptr<QObject> job(backend->start(QUrl::fromLocalFile(dir.path()),
            [&](const FolderSize &size) { result = size; }));
        QTRY_COMPARE_WITH_TIMEOUT(result.state, FolderSizeState::Complete, 10000);
        quint64 expected = 1024 * 1024 + 3;
#ifdef DZURKEL_TEST_GIO_FOLDER_SIZE
        // GIO counts the symlink's own text, but never follows its target.
        // KIO omits those bytes and deduplicates hard links; GIO counts each
        // directory entry. Both use logical file lengths, not sparse allocation.
        if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':').contains(QStringLiteral("KDE"), Qt::CaseInsensitive))
            expected += QFile::encodeName(external.path()).size() + 3;
#endif
        QCOMPARE(result.bytes, expected);
#endif
    }
    void realDesktopErrorsAndCancellationDoNotBecomeSuccessfulZero()
    {
#ifndef DZURKEL_TEST_FOLDER_SIZE
        QSKIP("Optional desktop calculator was not built.");
#else
        auto *backend = folderSizeBackend();
        QVERIFY(backend);
        QTemporaryDir dir;
        FolderSize result;
        std::unique_ptr<QObject> job(backend->start(QUrl::fromLocalFile(dir.filePath("missing")),
            [&](const FolderSize &size) { result = size; }));
        QTRY_COMPARE_WITH_TIMEOUT(result.state, FolderSizeState::Partial, 10000);
        QVERIFY(!result.explanation.isEmpty());
        job.reset();
        int callbacks = 0;
        for (int i = 0; i < 20; ++i)
            delete backend->start(QUrl::fromLocalFile(dir.path()), [&](const FolderSize &) { ++callbacks; });
        QTest::qWait(100);
        QCOMPARE(callbacks, 0);
        // Start the asynchronous desktop request, then cancel at the next queued
        // turn before its worker can return. Cancellation must stay silent.
        QPointer<QObject> running = backend->start(QUrl::fromLocalFile(dir.path()),
            [&](const FolderSize &) { ++callbacks; });
        QTimer::singleShot(0, this, [running] { delete running.data(); });
        QTest::qWait(100);
        QVERIFY(running.isNull());
        QCOMPARE(callbacks, 0);
#endif
    }
    void unresolvedRemoteUrlIsNotSubmittedToAnotherAuthenticationRoute()
    {
#ifndef DZURKEL_TEST_FOLDER_SIZE
        QSKIP("Optional desktop calculator was not built.");
#else
        FolderSize result;
        auto *backend = folderSizeBackend();
        QVERIFY(backend);
        bool called = false;
        std::unique_ptr<QObject> job(backend->start(QUrl("smb://example.invalid/share"),
            [&](const FolderSize &size) { result = size; called = true; }));
        QTRY_VERIFY(called);
        QCOMPARE(result.state, FolderSizeState::Unavailable);
        QVERIFY(!result.explanation.isEmpty());
#endif
    }
};
QTEST_MAIN(FolderSizeTest)
#include "FolderSizeTest.moc"

#include "resources/FileResource.h"
#include "application/CommandLine.h"
#include "controller/PreviewController.h"
#include "ui/PreviewWindow.h"
#include "ui/SettingsDialog.h"
#include "desktop/DolphinShortcuts.h"

#include <QFile>
#include <QLabel>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QScreen>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QPushButton>
#include <QStandardPaths>
#include <QDialog>
#include <QScrollArea>
#include <QScrollBar>

class DzurkelTest : public QObject {
    Q_OBJECT
private slots:
    void preservesSpecialCharacters();
    void parsesCommands();
    void controllerReusesWindow();
    void resolvesRelativePaths();
    void rejectsInvalidResources_data();
    void rejectsInvalidResources();
    void describesDirectories();
    void rejectsUnreadableFiles();
    void dismissesWithKeyboard_data();
    void dismissesWithKeyboard();
    void windowStartsLargeAndRetainsResizing();
    void shiftSpaceDismissesButControlSpaceDoesNot();
    void followsLightAndDarkPalettes();
    void shortcutSetupPreservesAndRestores();
    void shortcutSetupRejectsMalformedAndNewerChanges();
    void shortcutSetupRejectsLinksAndConcurrentChanges();
    void settingsLinkIsReusedAndKeyboardIndependent();
    void informationDialogStaysCompactWithLongPathsAndThemes();
};

void DzurkelTest::informationDialogStaysCompactWithLongPathsAndThemes()
{
    const auto original = QApplication::palette();
    const auto restore = qScopeGuard([original] { QApplication::setPalette(original); });
    const QString name = QStringLiteral("example <sample>.txt");
    const QString path = QStringLiteral("/example/") + QStringLiteral("long-directory-name/").repeated(30) + name;
    dzurkel::FileResource resource;
    resource.path = path;
    resource.url = QUrl::fromLocalFile(path);
    dzurkel::PreviewData data;
    data.resource = resource;
    data.kind = dzurkel::PreviewKind::Text;
    data.mimeType = QStringLiteral("text/plain");
    data.description = QStringLiteral("Plain text");
    data.detail = QStringLiteral("UTF-8");
    data.textLineCount = 2;
    data.size = 100;
    for (bool dark : {false, true}) {
        QPalette palette = original;
        const QColor background = dark ? QColor(35, 35, 35) : QColor(245, 245, 245);
        const QColor foreground = dark ? QColor(240, 240, 240) : QColor(20, 20, 20);
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::Base, background);
        palette.setColor(QPalette::Button, background);
        palette.setColor(QPalette::WindowText, foreground);
        palette.setColor(QPalette::Text, foreground);
        palette.setColor(QPalette::ButtonText, foreground);
        QApplication::setPalette(palette);
        dzurkel::PreviewWindow window(resource);
        window.setContent(new QPlainTextEdit(QStringLiteral("First line\nSecond line")));
        window.setPreviewData(data);
        window.show();
        window.resize(400, 400);
        QCoreApplication::processEvents();
        QCOMPARE(window.windowTitle(), name);
        auto *info = window.findChild<QPushButton *>(QStringLiteral("fileInfoButton"));
        QVERIFY(info->accessibleDescription().contains(QStringLiteral("Name: ") + name));
        QVERIFY(info->accessibleDescription().contains(path));
        const auto *settings = window.findChild<QPushButton *>(QStringLiteral("settingsLink"));
        QVERIFY(settings->isVisible());
        QVERIFY(window.rect().contains(settings->geometry()));
        QVERIFY(window.grab().save(dark ? QStringLiteral("visual-footer-dark.png") : QStringLiteral("visual-footer-light.png")));
        QTest::mouseClick(info, Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>(QStringLiteral("fileInformationDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(dialog->width() <= 480);
        QVERIFY(dialog->height() <= 360);
        const auto *text = dialog->findChild<QLabel *>(QStringLiteral("fileInformationText"));
        QVERIFY(text->text().contains(name.toHtmlEscaped()));
        QVERIFY(text->text().contains(path.toHtmlEscaped()));
        QCOMPARE(text->palette().color(QPalette::WindowText), foreground);
        QVERIFY(text->height() >= text->heightForWidth(text->width()));
        const auto *scroll = dialog->findChild<QScrollArea *>();
        if (text->height() > scroll->viewport()->height())
            QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(dialog->grab().save(dark ? QStringLiteral("visual-information-dark.png") : QStringLiteral("visual-information-light.png")));
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(window.isVisible());
    }
}

void DzurkelTest::shortcutSetupPreservesAndRestores()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("dolphinui.rc"));
    const QString backup = directory.filePath(QStringLiteral("backup.json"));
    auto put = [&](const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    auto get = [&] { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    QVERIFY(put("<gui name=\"dolphin\" version=\"60\"><!--Keep comment--><ToolBar name=\"main\">Keep me</ToolBar>"
        "<ActionProperties><Action name=\"custom\" shortcut=\"Space; Ctrl+J\" icon=\"folder\"/>"
        "<Action name=\"servicemenu_dzurkel.desktop::dzurkel\" shortcut=\"Ctrl+P\"/>"
        "<Action name=\"unrelated\" shortcut=\"Ctrl+K\"/></ActionProperties></gui>"));
    dzurkel::DolphinShortcuts shortcuts(path, backup);
    QString error;
    QVERIFY(shortcuts.setupSummary(error).contains(QStringLiteral("custom")));
    QVERIFY(error.isEmpty());
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY2(shortcuts.useSpace(error), qPrintable(error));
    QVERIFY(shortcuts.usesSpace());
    QVERIFY(shortcuts.hasBackup());
    const auto configured = get();
    QVERIFY(configured.contains("shortcut=\"Ctrl+J\""));
    QVERIFY(configured.contains("shortcut=\"Ctrl+K\""));
    QVERIFY(configured.contains("shortcut=\"Space; Ctrl+P\""));
    QVERIFY(configured.contains("Keep comment"));
    QVERIFY(configured.contains("icon=\"folder\""));
    QVERIFY(!shortcuts.useSpace(error));
    QCOMPARE(get(), configured);
    QVERIFY(put(QByteArray(configured).replace("Keep me", "New toolbar text").replace("Ctrl+K", "Ctrl+L")));
    QVERIFY2(shortcuts.restore(error), qPrintable(error));
    QVERIFY(!shortcuts.usesSpace());
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY(get().contains("New toolbar text"));
    QVERIFY(get().contains("shortcut=\"Space; Ctrl+J\""));
    QVERIFY(get().contains("shortcut=\"Ctrl+L\""));
    QVERIFY(get().contains("shortcut=\"Ctrl+P\""));
}

void DzurkelTest::shortcutSetupRejectsMalformedAndNewerChanges()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("dolphinui.rc"));
    dzurkel::DolphinShortcuts shortcuts(path, directory.filePath(QStringLiteral("backup.json")));
    auto put = [&](const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    auto get = [&] { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    QString error;
    QVERIFY(put("<gui name=\"dolphin\"><ActionProperties scheme=\"Custom\"/></gui>"));
    QVERIFY(!shortcuts.useSpace(error));
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY(put("<gui broken"));
    QVERIFY(!shortcuts.useSpace(error));
    QCOMPARE(get(), QByteArray("<gui broken"));
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY(QFile::remove(path));
    QVERIFY2(shortcuts.useSpace(error), qPrintable(error));
    QVERIFY(shortcuts.usesSpace());
    // A later user shortcut edit must not be silently replaced by Restore.
    QVERIFY(put(get().replace("shortcut=\"Space\"", "shortcut=\"Ctrl+P\"")));
    const auto changed = get();
    QVERIFY(!shortcuts.restore(error));
    QCOMPARE(get(), changed);
    QVERIFY(shortcuts.hasBackup());
}

void DzurkelTest::shortcutSetupRejectsLinksAndConcurrentChanges()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("dolphinui.rc"));
    const QString backup = directory.filePath(QStringLiteral("backup.json"));
    const QString original = directory.filePath(QStringLiteral("original.rc"));
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents = "<gui name=\"dolphin\"/>";
    QCOMPARE(file.write(contents), contents.size());
    file.close();
    QVERIFY(QFile::link(original, path));
    dzurkel::DolphinShortcuts shortcuts(path, backup);
    QString error;
    QVERIFY(!shortcuts.useSpace(error));
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY(shortcuts.setupSummary(error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(QFile::remove(path));
    QLockFile lock(backup + QStringLiteral(".lock"));
    QVERIFY(lock.tryLock());
    QVERIFY(!shortcuts.useSpace(error));
    QVERIFY(!QFile::exists(path));
    QVERIFY(!shortcuts.hasBackup());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), contents);
}
void DzurkelTest::settingsLinkIsReusedAndKeyboardIndependent()
{
    QStandardPaths::setTestModeEnabled(true);
    const auto restorePaths = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
    dzurkel::PreviewWindow window;
    window.show();
    auto *link = window.findChild<QPushButton *>(QStringLiteral("settingsLink"));
    QVERIFY(link);
    link->setFocus();
    QTest::keyClick(link, Qt::Key_Space);
    auto *dialog = window.findChild<dzurkel::SettingsDialog *>();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(window.isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(!dialog->isVisible());
    QVERIFY(window.isVisible());
    QTest::mouseClick(link, Qt::LeftButton);
    QCOMPARE(window.findChildren<dzurkel::SettingsDialog *>().size(), 1);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->grab().save(QStringLiteral("settings-dialog.png")));
    window.hide();
    QVERIFY(!dialog->isVisible());
    window.showSettings();
    QVERIFY(dialog->isVisible());
    QVERIFY(!window.isVisible());
    QCOMPARE(window.findChildren<dzurkel::SettingsDialog *>().size(), 1);
    dialog->close();
}

void DzurkelTest::shiftSpaceDismissesButControlSpaceDoesNot()
{
    dzurkel::PreviewWindow window;
    auto *editor = new QPlainTextEdit;
    window.setContent(editor);
    window.show();
    QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
    QVERIFY(window.isVisible());
    QTest::keyClick(editor, Qt::Key_Space, Qt::ShiftModifier);
    QVERIFY(!window.isVisible());
}

void DzurkelTest::followsLightAndDarkPalettes()
{
    const QPalette original = QApplication::palette();
    // Restore the global palette even if a test assertion fails.
    const auto restore = qScopeGuard([original] { QApplication::setPalette(original); });
    dzurkel::PreviewWindow window;
    auto *editor = new QPlainTextEdit(QStringLiteral("Peek-a-Dzurkel!"));
    window.setContent(editor);
    window.show();
    for (const bool dark : {false, true}) {
        QPalette palette = original;
        const QColor background = dark ? QColor(35, 35, 35) : QColor(245, 245, 245);
        const QColor foreground = dark ? QColor(240, 240, 240) : QColor(20, 20, 20);
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::Base, background);
        palette.setColor(QPalette::WindowText, foreground);
        palette.setColor(QPalette::Text, foreground);
        QApplication::setPalette(palette);
        QCoreApplication::processEvents();
        QCOMPARE(window.palette().color(QPalette::Window), background);
        QCOMPARE(editor->palette().color(QPalette::Base), background);
        QCOMPARE(editor->palette().color(QPalette::Text), foreground);
        QVERIFY(window.grab().save(dark ? QStringLiteral("desktop-dark.png") : QStringLiteral("desktop-light.png")));
    }
}

void DzurkelTest::windowStartsLargeAndRetainsResizing()
{
    dzurkel::PreviewWindow window;
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
    window.show();
    QCoreApplication::processEvents();
    const QSize area = window.screen()->availableGeometry().size();
    QCOMPARE(window.size(), QSize(area.width() * 85 / 100, area.height() * 85 / 100));

    // The initial percentage is not a maximum: the user can grow the window.
    const QSize larger(area.width() * 95 / 100, area.height() * 95 / 100);
    window.resize(larger);
    QCoreApplication::processEvents();
    QCOMPARE(window.size(), larger);
    window.hide();
    window.show();
    QCOMPARE(window.size(), larger);
    window.resize(window.size() - QSize(30, 30));
    QCOMPARE(window.size(), larger - QSize(30, 30));
}

void DzurkelTest::preservesSpecialCharacters()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QString::fromUtf8("пример space # % ' \" $ ; ` .txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("unchanged"), qint64(9));
    file.close();

    QUrl localhost = QUrl::fromLocalFile(path);
    localhost.setHost(QStringLiteral("localhost"));
    for (const auto &input : {path, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded),
                             localhost.toString(QUrl::FullyEncoded)}) {
        dzurkel::FileResource resource;
        QString error;
        QVERIFY2(dzurkel::resolveFileResource(input, resource, error), qPrintable(error));
        QCOMPARE(resource.path, path);
        QCOMPARE(resource.url.toLocalFile(), path);
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("unchanged"));
}

void DzurkelTest::parsesCommands()
{
    dzurkel::Request request;
    QString error;
    QVERIFY(dzurkel::parseCommandLine({"dzurkel", "--toggle", "missing.txt"}, request, error));
    QCOMPARE(request.command, dzurkel::Command::Toggle);
    QCOMPARE(request.url, QUrl::fromLocalFile(QDir::current().absoluteFilePath("missing.txt")).toString(QUrl::FullyEncoded));
    QVERIFY(dzurkel::parseCommandLine({"dzurkel", "--", "-file.txt"}, request, error));
    QCOMPARE(request.command, dzurkel::Command::Show);
    QVERIFY(dzurkel::parseCommandLine({"dzurkel", "--hide"}, request, error));
    QCOMPARE(request.command, dzurkel::Command::Hide);
    QVERIFY(!dzurkel::parseCommandLine({"dzurkel", "--hide", "--quit"}, request, error));
    QVERIFY(!dzurkel::parseCommandLine({"dzurkel", "--quit", "file.txt"}, request, error));
    QVERIFY(!dzurkel::parseCommandLine({"dzurkel", "--toggle"}, request, error));
}

void DzurkelTest::controllerReusesWindow()
{
    QTemporaryDir directory;
    const QString first = directory.filePath(QStringLiteral("first.txt"));
    const QString second = directory.filePath(QStringLiteral("second.txt"));
    for (const auto &path : {first, second}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    dzurkel::PreviewController controller;
    const auto *window = controller.window();
    QString error;
    QVERIFY(!controller.isVisible());
    QVERIFY(controller.show(QUrl::fromLocalFile(first).toString(), error));
    QVERIFY(controller.isVisible());
    QVERIFY(controller.show(QUrl::fromLocalFile(second).toString(), error));
    QCOMPARE(controller.window(), window);
    QCOMPARE(window->windowTitle(), QStringLiteral("second.txt"));
    QVERIFY(!controller.show(QStringLiteral("https://example.com/unsupported"), error));
    QVERIFY(controller.isVisible());
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded));
    QVERIFY(controller.toggle("unused invalid path", error));
    QVERIFY(!controller.isVisible());
    QVERIFY(controller.toggle(QUrl::fromLocalFile(first).toString(), error));
    QCOMPARE(controller.window(), window);
    auto *child = window->findChild<QPushButton *>("fileInfoButton");
    child->window()->activateWindow();
    QTRY_VERIFY(window->isActiveWindow());
    QTest::keyClick(child->window(), Qt::Key_Escape);
    QTRY_VERIFY(!controller.isVisible());
    QVERIFY(controller.show(QUrl::fromLocalFile(second).toString(), error));
    QCOMPARE(controller.window(), window);
    controller.hide();
}

void DzurkelTest::resolvesRelativePaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("-sample.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    const QString previous = QDir::currentPath();
    QVERIFY(QDir::setCurrent(directory.path()));
    dzurkel::FileResource resource;
    QString error;
    const bool resolved = dzurkel::resolveFileResource(QStringLiteral("-sample.txt"), resource, error);
    const bool restored = QDir::setCurrent(previous);
    QVERIFY(restored);
    QVERIFY2(resolved, qPrintable(error));
    QCOMPARE(resource.path, file.fileName());
}

void DzurkelTest::rejectsInvalidResources_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("message");
    QTest::newRow("empty") << "" << "required";
    QTest::newRow("https") << "https://example.com/test" << "Unsupported URL scheme";
    QTest::newRow("smb") << "smb://server/test" << "asynchronous preparation";
    QTest::newRow("remote-file") << "file://server/test" << "Remote file URLs";
    QTest::newRow("malformed") << "file:///tmp/bad%ZZ" << "Malformed URL";
    QTest::newRow("query") << "file:///tmp/test?query" << "query or fragment";
    QTest::newRow("fragment") << "file:///tmp/test#fragment" << "query or fragment";
    QTest::newRow("relative-file-url") << "file:relative.txt" << "absolute local path";
    QTest::newRow("missing") << "/dzurkel-test/nonexistent/file" << "does not exist";
}

void DzurkelTest::rejectsInvalidResources()
{
    QFETCH(QString, input);
    QFETCH(QString, message);
    dzurkel::FileResource resource;
    QString error;
    QVERIFY(!dzurkel::resolveFileResource(input, resource, error));
    QVERIFY2(error.contains(message), qPrintable(error));
    QVERIFY(resource.path.isEmpty());
}

void DzurkelTest::describesDirectories()
{
    QTemporaryDir directory;
    dzurkel::FileResource resource;
    QString error;
    QVERIFY(dzurkel::resolveFileResource(directory.path(), resource, error));
    QVERIFY(resource.informationOnly);
}

void DzurkelTest::rejectsUnreadableFiles()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("unreadable.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::WriteOwner));
    dzurkel::FileResource resource;
    QString error;
    QVERIFY(!dzurkel::resolveFileResource(file.fileName(), resource, error));
    QVERIFY(error.contains(QStringLiteral("not readable")));
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
}

void DzurkelTest::dismissesWithKeyboard_data()
{
    QTest::addColumn<int>("key");
    QTest::newRow("space") << int(Qt::Key_Space);
    QTest::newRow("escape") << int(Qt::Key_Escape);
}

void DzurkelTest::dismissesWithKeyboard()
{
    QFETCH(int, key);
    const QString path = QString::fromUtf8("/tmp/пример <b>name</b>.txt");
    dzurkel::FileResource resource;
    resource.path = path;
    resource.url = QUrl::fromLocalFile(path);
    dzurkel::PreviewWindow window(resource);
    auto *label = window.findChild<QLabel *>(QStringLiteral("previewSummary"));
    QVERIFY(label);
    const auto *details = window.findChild<QPushButton *>(QStringLiteral("fileInfoButton"));
    QVERIFY(details);
    QCOMPARE(details->accessibleDescription(), path);
    QVERIFY(details->toolTip().contains(path.toHtmlEscaped()));
    QCOMPARE(label->textFormat(), Qt::PlainText);
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    // Exercise the window shortcut with focus inside a child widget.
    label->setFocusPolicy(Qt::StrongFocus);
    label->setFocus();
    QTest::keyClick(label, static_cast<Qt::Key>(key));
    QTRY_VERIFY(!window.isVisible());
}

QTEST_MAIN(DzurkelTest)
#include "DzurkelTest.moc"

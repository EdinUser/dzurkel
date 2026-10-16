#include "resources/FileResource.h"
#include "application/CommandLine.h"
#include "controller/PreviewController.h"
#include "ui/PreviewWindow.h"

#include <QFile>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>

class DzurkelTest : public QObject {
    Q_OBJECT
private slots:
    void preservesSpecialCharacters();
    void parsesCommands();
    void controllerReusesWindow();
    void resolvesRelativePaths();
    void rejectsInvalidResources_data();
    void rejectsInvalidResources();
    void rejectsDirectories();
    void rejectsUnreadableFiles();
    void dismissesWithKeyboard_data();
    void dismissesWithKeyboard();
};

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
    QCOMPARE(window->findChild<QLabel *>("fileName")->text(), QStringLiteral("second.txt"));
    QVERIFY(!controller.show(directory.filePath("missing"), error));
    QVERIFY(controller.isVisible());
    QCOMPARE(controller.fileUrl(), QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded));
    QVERIFY(controller.toggle("unused invalid path", error));
    QVERIFY(!controller.isVisible());
    QVERIFY(controller.toggle(QUrl::fromLocalFile(first).toString(), error));
    QCOMPARE(controller.window(), window);
    auto *child = window->findChild<QLabel *>("filePath");
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
    QTest::newRow("smb") << "smb://server/test" << "Unsupported URL scheme";
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

void DzurkelTest::rejectsDirectories()
{
    QTemporaryDir directory;
    dzurkel::FileResource resource;
    QString error;
    QVERIFY(!dzurkel::resolveFileResource(directory.path(), resource, error));
    QVERIFY(error.contains(QStringLiteral("regular file")));
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
    dzurkel::PreviewWindow window({path, QUrl::fromLocalFile(path)});
    auto *label = window.findChild<QLabel *>(QStringLiteral("filePath"));
    QVERIFY(label);
    QCOMPARE(label->text(), path);
    QCOMPARE(label->textFormat(), Qt::PlainText);
    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
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

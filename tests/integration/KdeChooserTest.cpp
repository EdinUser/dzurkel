#include "controller/PreviewController.h"
#include "ipc/PreviewService.h"
#include "desktop/ChooserIntegration.h"
#include <KDirOperator>
#include <KFileWidget>
#include <QAbstractItemView>
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

using namespace dzurkel;
namespace {
struct Fixture {
    QTemporaryDir directory;
    QDialog dialog;
    KFileWidget *chooser;
    Fixture() {
        for (const auto *name : {"one #%.txt", "two.txt"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture\n") != 8) qFatal("Cannot create fixture");
        }
        chooser = new KFileWidget(QUrl::fromLocalFile(directory.path() + '/'), &dialog);
        chooser->setOperationMode(KFileWidget::Opening);
        chooser->setMode(KFile::Files | KFile::ExistingOnly);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(chooser);
        dialog.resize(800, 600);
        dialog.show();
    }
    QUrl url(const QString &name = QStringLiteral("one #%.txt")) const { return QUrl::fromLocalFile(directory.filePath(name)); }
    void select() { chooser->dirOperator()->setCurrentItems({url()}); }
    QAbstractItemView *view() const { return chooser->dirOperator()->view(); }
};
QDBusMessage message(const QString &method, const QVariantList &arguments = {})
{
    auto result = QDBusMessage::createMethodCall(QString::fromLatin1(ServiceName), QString::fromLatin1(ObjectPath),
        QString::fromLatin1(InterfaceName), method);
    result.setArguments(arguments);
    return result;
}
}
class KdeChooserTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void startsResidentAndCancelsBeforeFileAcceptance();
    void showsDismissesAndPreservesNewOwner();
    void keepsTextModifiersAndMultipleSelectionInChooser();
    void managedSessionOptInPreservesEnvironment();
    void nativeDialogFocusAndCancellation();
};
void KdeChooserTest::initTestCase()
{
    QVERIFY(qEnvironmentVariable("QT_QPA_GENERIC_PLUGINS").contains(QStringLiteral("dzurkelchooser")));
    QVERIFY(QDBusConnection::sessionBus().isConnected());
    QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(ServiceName)).value());
}
void KdeChooserTest::startsResidentAndCancelsBeforeFileAcceptance()
{
    Fixture f;
    QTRY_VERIFY_WITH_TIMEOUT(f.view()->model()->rowCount() >= 2, 5000);
    f.select();
    QTRY_COMPARE(f.chooser->dirOperator()->selectedItems().size(), 1);
    QSignalSpy accepted(f.chooser, &KFileWidget::accepted);
    QTest::keyClick(f.view(), Qt::Key_Space);
    auto bus = QDBusConnection::sessionBus();
    const auto stop = qScopeGuard([&] { bus.call(message(QStringLiteral("Quit")), QDBus::Block, 1000); });
    QTRY_VERIFY_WITH_TIMEOUT(bus.interface()->isServiceRegistered(QString::fromLatin1(ServiceName)).value(), 6000);
    auto visible = [&] {
        auto get = QDBusMessage::createMethodCall(QString::fromLatin1(ServiceName), QString::fromLatin1(ObjectPath),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        get << QString::fromLatin1(InterfaceName) << QStringLiteral("Visible");
        const QDBusReply<QDBusVariant> reply = bus.call(get);
        return reply.isValid() && reply.value().variant().toBool();
    };
    QTRY_VERIFY_WITH_TIMEOUT(visible(), 5000);
    QCOMPARE(accepted.size(), 0);
    QVERIFY(f.dialog.isVisible());
    f.dialog.reject();
    QTRY_VERIFY_WITH_TIMEOUT(!visible(), 3000);
}
void KdeChooserTest::showsDismissesAndPreservesNewOwner()
{
    auto bus = QDBusConnection::sessionBus();
    QTRY_VERIFY_WITH_TIMEOUT(!bus.interface()->isServiceRegistered(QString::fromLatin1(ServiceName)).value(), 3000);
    PreviewController controller;
    PreviewService service(controller);
    QVERIFY(bus.registerObject(QString::fromLatin1(ObjectPath), &service,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals));
    QVERIFY(bus.registerService(QString::fromLatin1(ServiceName)));
    const auto remove = qScopeGuard([&] {
        bus.unregisterService(QString::fromLatin1(ServiceName)); bus.unregisterObject(QString::fromLatin1(ObjectPath));
    });
    Fixture f;
    QTRY_VERIFY_WITH_TIMEOUT(f.view()->model()->rowCount() >= 2, 5000);
    f.select();
    QTest::keyClick(f.view(), Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(controller.isVisible() && !controller.loading(), 5000);
    QCOMPARE(controller.fileUrl(), f.url().toString(QUrl::FullyEncoded));
    QVERIFY(!controller.chooserOwner().isEmpty());
    QTest::keyClick(const_cast<PreviewWindow *>(controller.window()), Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(controller.fileUrl(), f.url().toString(QUrl::FullyEncoded));
    QTest::keyClick(const_cast<PreviewWindow *>(controller.window()), Qt::Key_Escape);
    QTRY_VERIFY(!controller.isVisible());
    QVERIFY(f.dialog.isVisible());
    QTest::keyClick(f.view(), Qt::Key_Space);
    QTRY_VERIFY(controller.isVisible());
    QString error;
    const auto replacement = f.url(QStringLiteral("two.txt")).toString(QUrl::FullyEncoded);
    QVERIFY(controller.show(replacement, error)); // Dolphin/CLI takes ownership.
    f.dialog.reject();
    QTest::qWait(100);
    QVERIFY(controller.isVisible());
    QCOMPARE(controller.fileUrl(), replacement);
}
void KdeChooserTest::keepsTextModifiersAndMultipleSelectionInChooser()
{
    Fixture f;
    QTRY_VERIFY_WITH_TIMEOUT(f.view()->model()->rowCount() >= 2, 5000);
    f.select();
    const auto editors = f.chooser->findChildren<QLineEdit *>();
    QVERIFY(!editors.isEmpty());
    auto *editor = editors.first();
    editor->setText(QStringLiteral("a"));
    editor->setCursorPosition(1);
    QTest::keyClick(editor, Qt::Key_Space);
    QCOMPARE(editor->text(), QStringLiteral("a "));
    QTest::keyClick(f.view(), Qt::Key_Space, Qt::ControlModifier);
    f.chooser->dirOperator()->setCurrentItems({f.url(), f.url(QStringLiteral("two.txt"))});
    QTRY_COMPARE(f.chooser->dirOperator()->selectedItems().size(), 2);
    QTest::keyClick(f.view(), Qt::Key_Space);
    f.select();
    f.chooser->setOperationMode(KFileWidget::Saving);
    QTest::keyClick(f.view(), Qt::Key_Space);
    QTest::qWait(100);
    QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(ServiceName)).value());
}
void KdeChooserTest::managedSessionOptInPreservesEnvironment()
{
    QTemporaryDir directory;
    const auto configuration = directory.filePath(QStringLiteral("env/chooser.sh"));
    ChooserIntegration integration(configuration, QStringLiteral(CHOOSER_PLUGIN_ROOT), QStringLiteral(DZURKEL_EXECUTABLE));
    QString error;
    QVERIFY2(integration.setEnabled(true, error), qPrintable(error));
    QVERIFY(integration.enabled());
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CHOOSER_SCRIPT"), configuration);
    environment.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/existing/plugins"));
    environment.insert(QStringLiteral("QT_QPA_GENERIC_PLUGINS"), QStringLiteral("otherdriver"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
        QStringLiteral(". \"$CHOOSER_SCRIPT\"; . \"$CHOOSER_SCRIPT\"; printf '%s\\n%s\\n' \"$QT_PLUGIN_PATH\" \"$QT_QPA_GENERIC_PLUGINS\"")});
    QVERIFY(process.waitForFinished(3000));
    QCOMPARE(process.exitCode(), 0);
    const auto result = QString::fromUtf8(process.readAllStandardOutput()).split(QChar('\n'));
    QCOMPARE(result.at(0), QStringLiteral(CHOOSER_PLUGIN_ROOT) + QStringLiteral(":/existing/plugins"));
    QCOMPARE(result.at(1), QStringLiteral("otherdriver,dzurkelchooser:") + QStringLiteral(DZURKEL_EXECUTABLE));
    QVERIFY(integration.setEnabled(false, error));
    QVERIFY(!integration.enabled());
    environment.insert(QStringLiteral("QT_PLUGIN_PATH"), result.at(0));
    environment.insert(QStringLiteral("QT_QPA_GENERIC_PLUGINS"), result.at(1));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
        QStringLiteral(". \"$CHOOSER_SCRIPT\"; printf '%s\\n%s\\n' \"$QT_PLUGIN_PATH\" \"$QT_QPA_GENERIC_PLUGINS\"")});
    QVERIFY(process.waitForFinished(3000));
    QCOMPARE(process.exitCode(), 0);
    QCOMPARE(process.readAllStandardOutput(), QByteArray("/existing/plugins\notherdriver\n"));
    QVERIFY(QDir().mkpath(QFileInfo(configuration).absolutePath()));
    QFile other(configuration);
    QVERIFY(other.open(QIODevice::WriteOnly)); other.write("# user content\n"); other.close();
    QVERIFY(!integration.setEnabled(true, error));
    QVERIFY(!integration.setEnabled(false, error));
}
void KdeChooserTest::nativeDialogFocusAndCancellation()
{
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        QSKIP("Native Wayland acceptance check; run explicitly on KDE.");
    auto bus = QDBusConnection::sessionBus();
    PreviewController controller;
    PreviewService service(controller);
    QVERIFY(bus.registerObject(QString::fromLatin1(ObjectPath), &service,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals));
    QVERIFY(bus.registerService(QString::fromLatin1(ServiceName)));
    const auto remove = qScopeGuard([&] {
        bus.unregisterService(QString::fromLatin1(ServiceName)); bus.unregisterObject(QString::fromLatin1(ObjectPath));
    });
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("native #%.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Native chooser fixture\n"); file.close();
    const auto url = QUrl::fromLocalFile(file.fileName());
    QFileDialog dialog(nullptr, QStringLiteral("Dzurkel KDE chooser integration check"), directory.path());
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    dialog.setWindowModality(Qt::NonModal);
    dialog.show();
    KFileWidget *native = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (auto *widget : QApplication::allWidgets()) {
            if (auto *candidate = qobject_cast<KFileWidget *>(widget); candidate && candidate->isVisible()) {
                native = candidate; return true;
            }
        }
        return false;
    })(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(native->dirOperator()->view()->model()->rowCount() == 1, 5000);
    native->dirOperator()->setCurrentItems({url});
    QTRY_COMPARE(native->dirOperator()->selectedItems().size(), 1);
    auto *view = native->dirOperator()->view();
    view->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(native->window()->isActiveWindow(), 3000);
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(controller.isVisible() && !controller.loading(), 5000);
    QCOMPARE(accepted.size(), 0);
    QCOMPARE(rejected.size(), 0);
    QCOMPARE(controller.fileUrl(), url.toString(QUrl::FullyEncoded));
    QTRY_VERIFY_WITH_TIMEOUT(controller.window()->isActiveWindow(), 3000);
    QTest::keyClick(const_cast<PreviewWindow *>(controller.window()), Qt::Key_Escape);
    QTRY_VERIFY(!controller.isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(native->window()->isActiveWindow(), 3000);
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_VERIFY(controller.isVisible());
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_COMPARE(rejected.size(), 1);
    QTRY_VERIFY(!controller.isVisible());
    QCOMPARE(accepted.size(), 0);
}
QTEST_MAIN(KdeChooserTest)
#include "KdeChooserTest.moc"

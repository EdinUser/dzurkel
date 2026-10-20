// Test-only input/observation driver. Never installed with the application.
#include <KDirOperator>
#include <KFileWidget>
#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGenericPlugin>
#include <QTest>
#include <QTimer>

class ChooserDriver : public QObject {
public:
    explicit ChooserDriver(QObject *parent) : QObject(parent), root_(qEnvironmentVariable("DZURKEL_CHOOSER_DRIVER_ROOT"))
    {
        if (root_.isEmpty()) return;
        elapsed_.start();
        connect(&timer_, &QTimer::timeout, this, [this] { step(); });
        timer_.start(50);
    }
private:
    void mark(const QString &name) {
        QFile file(QDir(root_).filePath(name));
        if (file.open(QIODevice::WriteOnly)) file.write("ok\n");
    }
    void step() {
        if (!qobject_cast<QApplication *>(QCoreApplication::instance())) return;
        const bool preview = QCoreApplication::applicationName() == QLatin1String("dzurkel");
        if (preview) {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (!widget->inherits("dzurkel::PreviewWindow") || !widget->isVisible()) continue;
                if (!widget->isActiveWindow()) break;
                mark(QStringLiteral("preview-active"));
                widget->grab().save(QDir(root_).filePath(QStringLiteral("preview.png")));
                QTest::keyClick(widget, Qt::Key_Escape);
                if (!widget->isVisible()) mark(QStringLiteral("preview-dismissed"));
                timer_.stop();
                return;
            }
        } else if (QCoreApplication::applicationFilePath().endsWith(QLatin1String("/xdg-desktop-portal-kde"))
            || QCoreApplication::applicationFilePath().endsWith(QLatin1String("/kdialog"))) {
            for (auto *widget : QApplication::allWidgets()) {
                auto *chooser = qobject_cast<KFileWidget *>(widget);
                if (!chooser || !chooser->isVisible()) continue;
                auto *view = chooser->dirOperator()->view();
                if (!view || view->model()->rowCount() != 1) continue;
                if (!sent_) {
                    chooser->dirOperator()->setCurrentItems({QUrl::fromLocalFile(QDir(root_).filePath(QStringLiteral("files/native #%.txt")))});
                    if (chooser->dirOperator()->selectedItems().size() != 1) continue;
                    view->setFocus();
                    if (!chooser->window()->isActiveWindow()) continue;
                    chooser->window()->grab().save(QDir(root_).filePath(QStringLiteral("chooser.png")));
                    mark(QStringLiteral("chooser-active"));
                    sent_ = true;
                    QTest::keyClick(view, Qt::Key_Space);
                } else if (QFileInfo::exists(QDir(root_).filePath(QStringLiteral("preview-dismissed")))
                    && chooser->window()->isActiveWindow()) {
                    mark(QStringLiteral("chooser-focus-returned"));
                    QTest::keyClick(view, Qt::Key_Escape);
                    mark(QStringLiteral("chooser-cancelled"));
                    timer_.stop();
                }
                return;
            }
        } else { timer_.stop(); return; }
        if (elapsed_.elapsed() > 12000) {
            mark(preview ? QStringLiteral("error-preview-focus") : QStringLiteral("error-chooser-focus"));
            timer_.stop();
            for (auto *widget : QApplication::topLevelWidgets()) if (widget->isVisible()) widget->close();
        }
    }
    QString root_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool sent_ = false;
};
class KdeChooserDriver : public QGenericPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "chooser-driver.json")
public:
    QObject *create(const QString &, const QString &) override { return new ChooserDriver(nullptr); }
};
#include "KdeChooserDriver.moc"

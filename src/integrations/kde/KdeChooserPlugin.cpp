#include <KDirOperator>
#include <KFileWidget>
#include <KWaylandExtras>
#include <KWindowSystem>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QGenericPlugin>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>
#include <QWindow>

namespace {
const QString service = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer");
const QString objectPath = QStringLiteral("/io/github/EdinUser/Dzurkel/Previewer");
const QString interface = QStringLiteral("io.github.EdinUser.Dzurkel.Previewer1");

class ChooserSession : public QObject {
    Q_OBJECT
public:
    ChooserSession(KFileWidget *chooser, QString executable)
        : QObject(chooser), chooser_(chooser), executable_(std::move(executable)),
          session_(QUuid::createUuid().toString(QUuid::WithoutBraces)), bus_(QDBusConnection::sessionBus())
    {
        chooser->installEventFilter(this);
        chooser->window()->installEventFilter(this);
        connect(qApp, &QCoreApplication::aboutToQuit, this, &ChooserSession::cancel);
    }
    ~ChooserSession() override { cancel(); }
    void preview(const QUrl &url)
    {
        const auto generation = ++generation_;
        session_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        url_ = url;
        active_ = true;
        attempts_ = 0;
        auto *window = chooser_->window()->windowHandle();
        if (window && KWindowSystem::isPlatformWayland()) {
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation] {
                if (current(generation)) resolveOwner(generation, watcher->result());
                watcher->deleteLater();
            });
            watcher->setFuture(KWaylandExtras::xdgActivationToken(window, QStringLiteral("dzurkel")));
        } else resolveOwner(generation, {});
    }
private slots:
    void serverClosed(const QString &session, const QString &reason, const QString &token)
    {
        leases_.remove(session);
        if (session != session_ || !active_) return;
        active_ = false;
        ++generation_;
        if (reason != QLatin1String("dismissed") || !chooser_ || !chooser_->isVisible()) return;
        auto *window = chooser_->window()->windowHandle();
        if (!window) return;
        if (KWindowSystem::isPlatformWayland()) {
            if (token.isEmpty()) return; // Never force focus without a compositor grant.
            KWindowSystem::setCurrentXdgActivationToken(token);
            KWindowSystem::activateWindow(window);
        } else window->requestActivate();
    }
private:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close) cancel();
        return false;
    }
    bool current(quint64 generation) const
    {
        if (!active_ || generation != generation_ || !chooser_ || !chooser_->isVisible()) return false;
        const auto items = chooser_->dirOperator()->selectedItems();
        return items.size() == 1 && items.first().url() == url_;
    }
    void cancel()
    {
        ++generation_;
        active_ = false;
        if (!owner_.isEmpty()) {
            for (const auto &session : std::as_const(leases_)) {
                auto message = QDBusMessage::createMethodCall(owner_, objectPath, interface, QStringLiteral("HideFromChooser"));
                message.setAutoStartService(false);
                message << session;
                bus_.asyncCall(message, 1000);
            }
        }
        leases_.clear();
    }
    void resolveOwner(quint64 generation, const QString &token)
    {
        if (!current(generation)) return;
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
        message << service;
        auto *watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message, 1000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, token] {
            const QDBusPendingReply<QString> reply = *watcher;
            watcher->deleteLater();
            if (!current(generation)) return;
            if (reply.isError()) {
                if (reply.error().name() != QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")) return;
                if (++attempts_ > 40) { active_ = false; return; }
                if (attempts_ == 1) {
                    static QElapsedTimer startupThrottle;
                    const bool start = !startupThrottle.isValid() || startupThrottle.elapsed() >= 5000;
                    if (start) {
                        startupThrottle.start();
                        QProcess process;
                        auto environment = QProcessEnvironment::systemEnvironment();
                        // Do not load the chooser module in the preview application.
                        auto plugins = environment.value(QStringLiteral("QT_QPA_GENERIC_PLUGINS")).split(QChar(','), Qt::SkipEmptyParts);
                        plugins.removeIf([](const QString &plugin) { return plugin == QLatin1String("dzurkelchooser") || plugin.startsWith(QLatin1String("dzurkelchooser:")); });
                        if (plugins.isEmpty()) environment.remove(QStringLiteral("QT_QPA_GENERIC_PLUGINS"));
                        else environment.insert(QStringLiteral("QT_QPA_GENERIC_PLUGINS"), plugins.join(QChar(',')));
                        environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
                        environment.remove(QStringLiteral("DESKTOP_STARTUP_ID"));
                        process.setProcessEnvironment(environment);
                        process.setProgram(executable_);
                        process.setArguments({QStringLiteral("--serve")});
                        if (!process.startDetached()) { active_ = false; return; }
                    }
                }
                QTimer::singleShot(100, this, [this, generation, token] { resolveOwner(generation, token); });
                return;
            }
            if (owner_ != reply.value()) {
                if (!owner_.isEmpty()) bus_.disconnect(owner_, objectPath, interface, QStringLiteral("ChooserClosed"),
                    this, SLOT(serverClosed(QString,QString,QString)));
                owner_ = reply.value();
                bus_.connect(owner_, objectPath, interface, QStringLiteral("ChooserClosed"),
                    this, SLOT(serverClosed(QString,QString,QString)));
            }
            auto show = QDBusMessage::createMethodCall(owner_, objectPath, interface, QStringLiteral("ShowFromChooser"));
            if (leases_.size() >= 4) return;
            show.setAutoStartService(false);
            show << session_ << url_.toString(QUrl::FullyEncoded)
                 << QVariantMap{{QStringLiteral("activation-token"), token}};
            leases_.insert(session_);
            const auto submittedSession = session_;
            auto *request = new QDBusPendingCallWatcher(bus_.asyncCall(show, 3000), this);
            connect(request, &QDBusPendingCallWatcher::finished, this, [this, request, generation, submittedSession] {
                const QDBusPendingReply<> result = *request;
                request->deleteLater();
                if (result.isError()) leases_.remove(submittedSession);
                if (generation == generation_ && result.isError()) {
                    active_ = false;
                    qWarning("Dzurkel chooser preview unavailable; ordinary file selection remains usable.");
                }
            });
        });
    }
    QPointer<KFileWidget> chooser_;
    QString executable_, session_, owner_;
    QSet<QString> leases_;
    QDBusConnection bus_;
    QUrl url_;
    quint64 generation_ = 0;
    int attempts_ = 0;
    bool active_ = false;
};

class ChooserFilter : public QObject {
public:
    explicit ChooserFilter(QString executable, QObject *parent)
        : QObject(parent), executable_(std::move(executable)) { qApp->installEventFilter(this); }
private:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Space || key->modifiers() != Qt::NoModifier) return false;
        auto *target = qobject_cast<QWidget *>(object);
        if (!target || !target->isVisible()) return false;
        KFileWidget *chooser = nullptr;
        for (auto *widget = target; widget; widget = widget->parentWidget()) {
            if (qobject_cast<QLineEdit *>(widget) || qobject_cast<QComboBox *>(widget)
                || qobject_cast<QTextEdit *>(widget) || qobject_cast<QPlainTextEdit *>(widget)) return false;
            if ((chooser = qobject_cast<KFileWidget *>(widget))) break;
        }
        if (!chooser || chooser->operationMode() != KFileWidget::Opening) return false;
        auto *view = chooser->dirOperator()->view();
        if (!view || (target != view && !view->isAncestorOf(target))) return false;
        const auto items = chooser->dirOperator()->selectedItems();
        if (items.size() != 1) return false;
        const auto url = items.first().url();
        if (!url.isValid() || !(url.isLocalFile() || url.scheme() == QLatin1String("smb")
            || url.scheme() == QLatin1String("sftp"))) return false;
        key->accept();
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            auto *session = chooser->findChild<ChooserSession *>(QString(), Qt::FindDirectChildrenOnly);
            if (!session) session = new ChooserSession(chooser, executable_);
            session->preview(url);
        }
        return true;
    }
    QString executable_;
};
}

class KdeChooserPlugin : public QGenericPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "chooser.json")
public:
    QObject *create(const QString &key, const QString &specification) override
    {
        if (key.compare(QLatin1String("dzurkelchooser"), Qt::CaseInsensitive)) return nullptr;
        auto *lifetime = new QObject;
        const auto executable = specification.isEmpty() ? QStringLiteral(DZURKEL_CHOOSER_EXECUTABLE) : specification;
        // Generic plugins are loaded inside QGuiApplication construction.
        QTimer::singleShot(0, lifetime, [lifetime, executable] {
            if (qobject_cast<QApplication *>(QCoreApplication::instance())
                && QCoreApplication::applicationName() != QLatin1String("dzurkel"))
                new ChooserFilter(executable, lifetime);
        });
        return lifetime;
    }
};
#include "KdeChooserPlugin.moc"

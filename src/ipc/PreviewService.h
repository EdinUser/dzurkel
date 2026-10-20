#pragma once
#include <QDBusContext>
#include <QObject>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusServiceWatcher>

namespace dzurkel {
class PreviewController;
class NautilusPreviewService;
// Provisional project-owned identifiers; finalize the namespace before release.
inline constexpr auto ServiceName = "io.github.EdinUser.Dzurkel.Previewer";
inline constexpr auto ObjectPath = "/io/github/EdinUser/Dzurkel/Previewer";
inline constexpr auto InterfaceName = "io.github.EdinUser.Dzurkel.Previewer1";

class PreviewService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.EdinUser.Dzurkel.Previewer1")
    Q_PROPERTY(bool Visible READ isVisible)
    Q_PROPERTY(QString FileUrl READ fileUrl)
    Q_PROPERTY(QString PreviewKind READ previewKind)
    Q_PROPERTY(bool Loading READ loading)
public:
    explicit PreviewService(PreviewController &controller, NautilusPreviewService *nautilus = nullptr);
    bool isVisible() const;
    QString fileUrl() const;
    QString previewKind() const;
    bool loading() const;
public slots:
    void Show(const QString &url);
    void ShowWithOptions(const QString &url, const QVariantMap &options);
    void Hide();
    void Toggle(const QString &url);
    void ToggleWithOptions(const QString &url, const QVariantMap &options);
    void Quit();
    void ShowSettings(const QVariantMap &options);
    void StartNautilusPreviewer();
    void ShowFromChooser(const QString &session, const QString &url, const QVariantMap &options);
    void HideFromChooser(const QString &session);
signals:
    // Sent only to the owning caller, never broadcast with activation tokens.
    void ChooserClosed(const QString &session, const QString &reason, const QString &activationToken);
private:
    void open(const QString &url, bool toggle, const QVariantMap &options = {});
    void finishChooser(const QString &reason, bool notify = true);
    QString chooserKey() const;
    QDBusServiceWatcher chooserWatcher_;
    QDBusConnection chooserConnection_{QString()};
    QString chooserCaller_, chooserSession_;
    bool changingChooser_ = false, returningFocus_ = false;
    PreviewController &controller_;
    NautilusPreviewService *nautilus_;
};
} // namespace dzurkel

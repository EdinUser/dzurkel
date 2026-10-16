#pragma once
#include <QDBusContext>
#include <QObject>

namespace dzurkel {
class PreviewController;
// Provisional project-owned identifiers; finalize the namespace before release.
inline constexpr auto ServiceName = "io.github.EdinUser.Dzurkel.Previewer";
inline constexpr auto ObjectPath = "/io/github/EdinUser/Dzurkel/Previewer";
inline constexpr auto InterfaceName = "io.github.EdinUser.Dzurkel.Previewer1";

class PreviewService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.EdinUser.Dzurkel.Previewer1")
    Q_PROPERTY(bool Visible READ isVisible)
    Q_PROPERTY(QString FileUrl READ fileUrl)
public:
    explicit PreviewService(PreviewController &controller);
    bool isVisible() const;
    QString fileUrl() const;
public slots:
    void Show(const QString &url);
    void Hide();
    void Toggle(const QString &url);
    void Quit();
private:
    void open(const QString &url, bool toggle);
    PreviewController &controller_;
};
} // namespace dzurkel

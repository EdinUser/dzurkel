#include "PreviewWindow.h"
#include "resources/FileResource.h"

#include <QCursor>
#include <QTextStream>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>

namespace dzurkel {

PreviewWindow::PreviewWindow(bool diagnostics)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), diagnostics_(diagnostics)
{
    setWindowTitle(QStringLiteral("Dzurkel"));
    setAccessibleName(windowTitle());
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto addLabel = [this, layout](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(label);
        return label;
    };
    auto *name = addLabel(QString());
    name->setObjectName(QStringLiteral("fileName"));
    QFont font = name->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.3);
    name->setFont(font);
    auto *path = addLabel(QString());
    path->setObjectName(QStringLiteral("filePath"));
    layout->addStretch();
    addLabel(QStringLiteral("Space or Escape to close"));

    for (const auto key : {Qt::Key_Space, Qt::Key_Escape}) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, this, &QWidget::close);
    }

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect area = screen->availableGeometry();
        const QSize limit(std::max(1, area.width() * 4 / 5),
                          std::max(1, area.height() * 4 / 5));
        setMaximumSize(limit);
        resize(QSize(560, 240).boundedTo(limit));
        // Wayland compositors may choose the final top-level position.
        move(area.center() - rect().center());
    }
}

PreviewWindow::PreviewWindow(const FileResource &resource, bool diagnostics)
    : PreviewWindow(diagnostics)
{
    setResource(resource);
}

void PreviewWindow::setResource(const FileResource &resource)
{
    const QString name = QFileInfo(resource.path).fileName();
    setWindowTitle(QStringLiteral("Dzurkel — %1").arg(name));
    setAccessibleName(windowTitle());
    findChild<QLabel *>(QStringLiteral("fileName"))->setText(name);
    findChild<QLabel *>(QStringLiteral("filePath"))->setText(resource.path);
}

void PreviewWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (diagnostics_ && event->type() == QEvent::ActivationChange) {
        QTextStream(stderr) << "Dzurkel window active: " << isActiveWindow() << '\n';
    }
}

} // namespace dzurkel

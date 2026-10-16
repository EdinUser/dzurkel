#include "PreviewWindow.h"
#include "desktop/DesktopIntegration.h"
#include "resources/FileResource.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QShowEvent>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>

namespace dzurkel {
PreviewWindow::PreviewWindow(bool diagnostics)
    : QWidget(nullptr, Qt::Window), diagnostics_(diagnostics)
{
    setWindowTitle(QStringLiteral("Dzurkel"));
    setAccessibleName(windowTitle());
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    const auto addLabel = [this, layout](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(label);
        return label;
    };
    auto *name = addLabel({});
    name->setObjectName(QStringLiteral("fileName"));
    auto font = name->font();
    font.setBold(true);
    name->setFont(font);
    addLabel({})->setObjectName(QStringLiteral("filePath"));
    contentHost_ = new QWidget(this);
    contentHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout_ = new QVBoxLayout(contentHost_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(contentHost_, 1);
    status_ = addLabel({});
    status_->setObjectName(QStringLiteral("previewStatus"));
    status_->hide();
    addLabel(QStringLiteral("Space or Escape to close"));
    qApp->installEventFilter(this);
}
PreviewWindow::PreviewWindow(const FileResource &resource, bool diagnostics) : PreviewWindow(diagnostics)
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
void PreviewWindow::present(const QString &activationToken)
{
    show();
    activatePreview(windowHandle(), activationToken, diagnostics_);
}
void PreviewWindow::setContent(QWidget *content)
{
    delete content_;
    content_ = content;
    if (content_) {
        content_->setParent(contentHost_);
        contentLayout_->addWidget(content_);
        if (content_->focusProxy() || content_->focusPolicy() != Qt::NoFocus)
            content_->setFocus(Qt::OtherFocusReason);
        else setFocus(Qt::OtherFocusReason);
    }
}
void PreviewWindow::setStatus(const QString &status)
{
    status_->setText(status);
    status_->setVisible(!status.isEmpty());
}
QSize PreviewWindow::previewSize() const
{
    return (contentHost_->size() * devicePixelRatioF()).boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
}
void PreviewWindow::showEvent(QShowEvent *event)
{
    if (!event->spontaneous()) {
        QScreen *target = QGuiApplication::screenAt(QCursor::pos());
        if (!target) target = screen();
        if (!target) target = QGuiApplication::primaryScreen();
        if (target) {
            setScreen(target);
            const QRect area = target->availableGeometry();
            // Logical screen coordinates already account for display scaling.
            const QSize initial(std::max(1, area.width() * 85 / 100),
                                std::max(1, area.height() * 85 / 100));
            // Keep a user's size across hide/reopen; fit a smaller new monitor.
            if (!shown_) resize(initial);
            else if (!isMaximized()) resize(size().boundedTo(area.size()));
            if (!isMaximized()) move(area.center() - rect().center());
            shown_ = true;
            watchScreen(target);
        }
    }
    QWidget::showEvent(event);
    if (!trackingScreen_ && windowHandle()) {
        trackingScreen_ = true;
        connect(windowHandle(), &QWindow::screenChanged, this, [this](QScreen *target) {
            watchScreen(target);
            fitScreen(target);
        });
    }
}
void PreviewWindow::watchScreen(QScreen *target)
{
    disconnect(screenGeometryConnection_);
    if (!target) return;
    screenGeometryConnection_ = connect(target, &QScreen::availableGeometryChanged,
        this, [this, target] { fitScreen(target); });
}
void PreviewWindow::fitScreen(QScreen *target)
{
    if (!target || isMaximized() || isFullScreen()) return;
    const QRect area = target->availableGeometry();
    if (area.isEmpty()) return;
    const QSize decoration = frameGeometry().size() - size();
    const QSize limit = (area.size() - decoration).expandedTo(QSize(1, 1));
    resize(size().boundedTo(limit));
    const QRect frame = frameGeometry();
    const QPoint position(std::clamp(frame.x(), area.left(), std::max(area.left(), area.right() - frame.width() + 1)),
                          std::clamp(frame.y(), area.top(), std::max(area.top(), area.bottom() - frame.height() + 1)));
    // The compositor decides final placement on Wayland.
    move(pos() + position - frame.topLeft());
}
bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *target = qobject_cast<QWidget *>(watched);
    if (isVisible() && target && target->window() == this
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool dismiss = (key->key() == Qt::Key_Space
            && (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier))
            || (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier);
        if (dismiss) {
            key->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) close();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void PreviewWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit dismissed();
}
void PreviewWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (diagnostics_ && event->type() == QEvent::ActivationChange)
        QTextStream(stderr) << "Dzurkel window active: " << isActiveWindow() << '\n';
}
} // namespace dzurkel

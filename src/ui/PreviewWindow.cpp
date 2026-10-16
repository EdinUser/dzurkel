#include "PreviewWindow.h"
#include "SettingsDialog.h"
#include "desktop/DesktopIntegration.h"
#include "resources/FileResource.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QScreen>
#include <QShowEvent>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWindow>
#include <QResizeEvent>
#include <QTimer>
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
    auto *footer = new QHBoxLayout;
    footer->addWidget(new QLabel(QStringLiteral("Space or Escape to close"), this));
    footer->addStretch();
    auto *settings = new QPushButton(QStringLiteral("Settings"), this);
    settings->setObjectName(QStringLiteral("settingsLink"));
    settings->setFlat(true);
    settings->setCursor(Qt::PointingHandCursor);
    auto linkFont = settings->font();
    linkFont.setUnderline(true);
    settings->setFont(linkFont);
    settings->setAccessibleDescription(QStringLiteral("Open Dzurkel desktop and preview settings"));
    footer->addWidget(settings);
    layout->addLayout(footer);
    connect(settings, &QPushButton::clicked, this, [this] {
        if (!settings_) settings_ = new SettingsDialog(this);
        settings_->show();
        settings_->raise();
        settings_->activateWindow();
    });
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
        if (settings_ && settings_->isVisible()) return;
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
    // A compact fallback must not reduce the next image's decode resolution.
    QSize target = informationMode_ ? previewContentSize_ : contentHost_->size();
    if (screen()) target = target.boundedTo(screen()->availableGeometry().size());
    return (target * devicePixelRatioF()).boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
}
void PreviewWindow::setInformationMode(bool information)
{
    if (!information && !informationMode_) return;
    const QPoint center = geometry().center();
    const auto minimized = windowState() & Qt::WindowMinimized;
    if (information) {
        if (!informationMode_) {
            previewWindowState_ = windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
            previewWindowSize_ = previewWindowState_ ? normalGeometry().size() : size();
            previewContentSize_ = contentHost_->size();
        }
        informationMode_ = true;
        informationRestorePending_ = isMaximized() || isFullScreen();
        // Unmaximize temporarily; do not request activation when loading ends.
        setWindowState(minimized);
        fitInformation();
    } else {
        informationMode_ = false;
        informationRestorePending_ = false;
        resize(previewWindowSize_);
        setWindowState(previewWindowState_ | minimized);
    }
    if (!isMaximized() && !isFullScreen()) {
        move(center - rect().center());
        fitScreen(screen());
    }
}
void PreviewWindow::fitInformation()
{
    layout()->invalidate();
    const int width = std::clamp(content_ ? content_->sizeHint().width() + 40 : 480, 380, 560);
    const int height = std::max(layout()->totalHeightForWidth(width), layout()->minimumSize().height());
    resize(width, height);
    fitScreen(screen());
}
void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (informationRestorePending_ && event->spontaneous() && !isMaximized() && !isFullScreen()) {
        informationRestorePending_ = false;
        // Wayland's restore configure can supersede the initial resize. Fit
        // again after that native resize, then leave manual resizing alone.
        QTimer::singleShot(0, this, [this] {
            if (informationMode_ && isVisible() && !isMaximized() && !isFullScreen()) fitInformation();
        });
    }
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
        // Space on the Settings link activates the focused button.
        if (key->key() == Qt::Key_Space && target->objectName() == QStringLiteral("settingsLink"))
            return QWidget::eventFilter(watched, event);
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
    if (settings_) settings_->hide();
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

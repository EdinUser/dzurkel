#include "PreviewWindow.h"
#include "SettingsDialog.h"
#include "ZoomPreview.h"
#include <QWheelEvent>
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
    auto *controls = new QHBoxLayout;
    auto button = [this](const QString &text, const QString &name, const QString &tip) {
        auto *result = new QPushButton(text, this);
        result->setObjectName(name);
        result->setToolTip(tip);
        result->setAccessibleName(tip);
        result->setAutoDefault(false);
        return result;
    };
    previous_ = button(QStringLiteral("Previous"), QStringLiteral("previousFile"), QStringLiteral("Previous file (Alt+Left)"));
    next_ = button(QStringLiteral("Next"), QStringLiteral("nextFile"), QStringLiteral("Next file (Alt+Right)"));
    controls->addWidget(previous_);
    controls->addWidget(next_);
    navigationStatus_ = new QLabel(this);
    navigationStatus_->setObjectName(QStringLiteral("navigationStatus"));
    navigationStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    controls->addWidget(navigationStatus_, 1);
    connect(previous_, &QPushButton::clicked, this, [this] { emit navigateRequested(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { emit navigateRequested(1); });
    zoomControls_ = new QWidget(this);
    auto *zoomLayout = new QHBoxLayout(zoomControls_);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    auto *out = button(QStringLiteral("−"), QStringLiteral("zoomOut"), QStringLiteral("Zoom out (- or Ctrl+wheel)"));
    auto *in = button(QStringLiteral("+"), QStringLiteral("zoomIn"), QStringLiteral("Zoom in (+ or Ctrl+wheel)"));
    auto *fit = button(QStringLiteral("Fit"), QStringLiteral("zoomFit"), QStringLiteral("Fit preview (0)"));
    zoomStatus_ = new QLabel(zoomControls_);
    zoomStatus_->setObjectName(QStringLiteral("zoomStatus"));
    zoomStatus_->setMinimumWidth(80);
    zoomLayout->addWidget(out);
    zoomLayout->addWidget(zoomStatus_);
    zoomLayout->addWidget(in);
    zoomLayout->addWidget(fit);
    controls->addWidget(zoomControls_);
    connect(out, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->zoomBy(-1); });
    connect(in, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->zoomBy(1); });
    connect(fit, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->fit(); });
    layout->addLayout(controls);
    setNavigation(false, false, {});
    zoomControls_->hide();
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
    findChild<QLabel *>(QStringLiteral("filePath"))->setText(resource.url.isLocalFile()
        ? resource.path : resource.url.toDisplayString());
}
void PreviewWindow::present(const QString &activationToken)
{
    // Wayland places a newly mapped surface using its initial dimensions.
    // Restore before mapping: growing an already placed compact window leaves
    // its top-left corner fixed, and clients cannot reposition it afterward.
    if (!isVisible() && informationMode_ && shown_) {
        resize(previewWindowSize_);
        setWindowState(previewWindowState_);
        // Keep information mode until the provider completes so a late compact
        // configure cannot be mistaken for a new user-chosen preview size.
    }
    show();
    activatePreview(windowHandle(), activationToken, diagnostics_);
}
void PreviewWindow::setContent(QWidget *content)
{
    wheelRemainder_ = 0;
    delete content_;
    content_ = content;
    if (auto *zoom = qobject_cast<ZoomPreview *>(content_))
        connect(zoom, &ZoomPreview::zoomChanged, this, &PreviewWindow::updateZoomControls);
    updateZoomControls();
    if (content_) {
        content_->setParent(contentHost_);
        contentLayout_->addWidget(content_);
        content_->show();
        if (settings_ && settings_->isVisible()) return;
        if (content_->focusProxy() || content_->focusPolicy() != Qt::NoFocus)
            content_->setFocus(Qt::OtherFocusReason);
        else setFocus(Qt::OtherFocusReason);
    }
}
void PreviewWindow::setNavigation(bool previous, bool next, const QString &status)
{
    previous_->setEnabled(previous);
    next_->setEnabled(next);
    navigationStatus_->setText(status);
    navigationStatus_->setToolTip(status);
    navigationStatus_->setAccessibleName(status);
}
void PreviewWindow::updateZoomControls()
{
    auto *zoom = qobject_cast<ZoomPreview *>(content_);
    zoomControls_->setVisible(zoom != nullptr);
    if (zoom) {
        zoomStatus_->setText(QStringLiteral("%1%2%")
            .arg(zoom->isFit() ? QStringLiteral("Fit · ") : QString()).arg(qRound(zoom->zoomFactor() * 100)));
        zoomStatus_->setToolTip(zoom->zoomHint());
        findChild<QPushButton *>(QStringLiteral("zoomIn"))->setEnabled(zoom->zoomFactor() < zoom->maximumZoom() - .00001);
        findChild<QPushButton *>(QStringLiteral("zoomOut"))->setEnabled(zoom->zoomFactor() > std::min(.1, zoom->maximumZoom()) + .00001);
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
        requestedWindowSize_ = previewWindowSize_;
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
    contentLayout_->activate();
    layout()->invalidate();
    layout()->activate();
    const int width = std::clamp(content_ ? content_->sizeHint().width() + 40 : 480, 380, 560);
    const int height = std::max(layout()->totalHeightForWidth(width), layout()->minimumSize().height());
    resize(width, height);
    fitScreen(screen());
}
void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mappingResizePending_ && event->spontaneous()) {
        mappingResizePending_ = false;
        if (event->size() == mappedWindowSize_) {
            // A late initial configure can restore the size used when mapping,
            // superseding a provider's subsequent compact/restore request.
            QTimer::singleShot(0, this, [this] {
                if (!isVisible() || isMaximized() || isFullScreen()) return;
                if (informationMode_) fitInformation();
                else {
                    resize(requestedWindowSize_);
                    fitScreen(screen());
                }
            });
        }
    }
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
        if (!presentationPrepared_) preparePresentation();
        presentationPrepared_ = false;
        shown_ = true;
        mappedWindowSize_ = size();
        requestedWindowSize_ = size();
        mappingResizePending_ = true;
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
void PreviewWindow::preparePresentation()
{
    if (!presentationPrepared_) {
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
            watchScreen(target);
            presentationPrepared_ = true;
        }
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
    if (isVisible() && target && target->window() == this && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() == Qt::ControlModifier) {
            if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) {
                // Angle deltas support high-resolution wheels without making
                // every tiny touchpad event a full zoom step.
                if (wheel->phase() == Qt::ScrollBegin) wheelRemainder_ = 0;
                wheelRemainder_ += wheel->angleDelta().isNull()
                    ? wheel->pixelDelta().y() * 3 : wheel->angleDelta().y();
                const int steps = wheelRemainder_ / 120;
                wheelRemainder_ %= 120;
                if (steps) zoom->zoomBy(std::clamp(steps, -8, 8));
                wheel->accept();
                return true;
            }
        }
    }
    if (isVisible() && target && target->window() == this
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool keyPress = event->type() == QEvent::KeyPress;
        const auto modifiers = key->modifiers();
        int direction = 0;
        auto *zoom = qobject_cast<ZoomPreview *>(content_);
        const bool verticalBrowsing = arrowNavigation_ && (!zoom || zoom->isFit());
        if (modifiers == Qt::AltModifier || (arrowNavigation_ && modifiers == Qt::NoModifier)) {
            if (key->key() == Qt::Key_Left) direction = -1;
            if (key->key() == Qt::Key_Right) direction = 1;
        }
        if (modifiers == Qt::AltModifier || (verticalBrowsing && modifiers == Qt::NoModifier)) {
            if (key->key() == Qt::Key_Up) direction = -1;
            if (key->key() == Qt::Key_Down) direction = 1;
        }
        if (modifiers == Qt::NoModifier && arrowNavigation_ && zoom && !zoom->isFit()
            && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
            key->accept();
            if (keyPress) zoom->scrollVertically(key->key() == Qt::Key_Up ? -1 : 1);
            return true;
        }
        if (direction) {
            key->accept();
            if (keyPress && !key->isAutoRepeat() && (direction < 0 ? previous_->isEnabled() : next_->isEnabled()))
                emit navigateRequested(direction);
            return true;
        }
        if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) {
            const bool zoomKey = (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
                && (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal
                    || key->key() == Qt::Key_Minus || key->key() == Qt::Key_0);
            if (zoomKey) {
                key->accept();
                if (keyPress) {
                    if (key->key() == Qt::Key_0) zoom->fit();
                    else zoom->zoomBy(key->key() == Qt::Key_Minus ? -1 : 1);
                }
                return true;
            }
        }
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
    mappingResizePending_ = false;
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

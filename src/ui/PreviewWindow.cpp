#include "PreviewWindow.h"
#include "SettingsDialog.h"
#include "ZoomPreview.h"
#include "preview/PreviewProvider.h"
#include "preview/table/TabularDocument.h"
#include <QWheelEvent>
#include "desktop/DesktopIntegration.h"
#include "desktop/WindowPlacement.h"
#include "resources/FileResource.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLocale>
#include <QMimeDatabase>
#include <QStyle>
#include <QTabWidget>
#include <QScrollArea>
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
    placement_ = new WindowPlacement(this);
    setWindowTitle(QStringLiteral("Dzurkel"));
    setAccessibleName(windowTitle());
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 6);
    layout->setSpacing(6);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(6);
    auto button = [this](const QString &theme, QStyle::StandardPixmap fallback,
                         const QString &name, const QString &tip) {
        auto *result = new QPushButton(this);
        result->setIcon(QIcon::fromTheme(theme, style()->standardIcon(fallback)));
        result->setObjectName(name);
        result->setToolTip(tip);
        result->setAccessibleName(tip);
        result->setAutoDefault(false);
        result->setFixedSize(30, 30);
        return result;
    };
    previous_ = button(QStringLiteral("go-previous"), QStyle::SP_ArrowBack,
        QStringLiteral("previousFile"), QStringLiteral("Previous file (Alt+Left)"));
    next_ = button(QStringLiteral("go-next"), QStyle::SP_ArrowForward,
        QStringLiteral("nextFile"), QStringLiteral("Next file (Alt+Right)"));
    controls->addWidget(previous_);
    navigationStatus_ = new QLabel(this);
    navigationStatus_->setObjectName(QStringLiteral("navigationStatus"));
    navigationStatus_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    navigationStatus_->setMinimumWidth(44);
    navigationStatus_->setAlignment(Qt::AlignCenter);
    controls->addWidget(navigationStatus_);
    controls->addWidget(next_);
    controls->addStretch();
    connect(previous_, &QPushButton::clicked, this, [this] { emit navigateRequested(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { emit navigateRequested(1); });
    zoomControls_ = new QWidget(this);
    auto *zoomLayout = new QHBoxLayout(zoomControls_);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(4);
    auto *out = button(QStringLiteral("zoom-out"), QStyle::SP_ArrowDown,
        QStringLiteral("zoomOut"), QStringLiteral("Zoom out (- or Ctrl+wheel)"));
    auto *in = button(QStringLiteral("zoom-in"), QStyle::SP_ArrowUp,
        QStringLiteral("zoomIn"), QStringLiteral("Zoom in (+ or Ctrl+wheel)"));
    auto *fit = button(QStringLiteral("zoom-fit-best"), QStyle::SP_TitleBarMaxButton,
        QStringLiteral("zoomFit"), QStringLiteral("Fit preview (0)"));
    out->setIcon(QIcon());
    out->setText(QStringLiteral("−"));
    in->setIcon(QIcon());
    in->setText(QStringLiteral("+"));
    zoomStatus_ = new QLabel(zoomControls_);
    zoomStatus_->setObjectName(QStringLiteral("zoomStatus"));
    zoomStatus_->setAlignment(Qt::AlignCenter);
    zoomStatus_->setMinimumWidth(42);
    zoomLayout->addWidget(out);
    zoomLayout->addWidget(zoomStatus_);
    zoomLayout->addWidget(in);
    zoomLayout->addWidget(fit);
    controls->addWidget(zoomControls_);
    connect(out, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->zoomBy(-1); });
    connect(in, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->zoomBy(1); });
    connect(fit, &QPushButton::clicked, this, [this] { if (auto *zoom = qobject_cast<ZoomPreview *>(content_)) zoom->fit(); });
    fileInfoButton_ = button(QStringLiteral("dialog-information"), QStyle::SP_MessageBoxInformation,
        QStringLiteral("fileInfoButton"), QStringLiteral("File information"));
    fileInfoButton_->setAccessibleName(QStringLiteral("File information"));
    controls->addWidget(fileInfoButton_);
    connect(fileInfoButton_, &QPushButton::clicked, this, &PreviewWindow::showFileInformation);
    layout->addLayout(controls);
    setNavigation(false, false, {});
    zoomControls_->hide();
    contentHost_ = new QWidget(this);
    contentHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout_ = new QVBoxLayout(contentHost_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(contentHost_, 1);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("previewSummary"));
    summary_->setTextFormat(Qt::PlainText);
    summary_->setForegroundRole(QPalette::PlaceholderText);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(summary_, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("previewStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->setMinimumWidth(0);
    status_->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    status_->hide();
    footer->addWidget(status_, 1);
    auto *hint = new QLabel(QStringLiteral("Space / Esc to close"), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    footer->addWidget(hint);
    auto *settings = button(QStringLiteral("configure"), QStyle::SP_FileDialogDetailedView,
        QStringLiteral("settingsLink"), QStringLiteral("Settings"));
    settings->setFlat(true);
    settings->setAccessibleDescription(QStringLiteral("Open Dzurkel desktop and preview settings"));
    footer->addWidget(settings);
    layout->addLayout(footer);
    connect(settings, &QPushButton::clicked, this, [this] {
        showSettings();
    });
    qApp->installEventFilter(this);
}
void PreviewWindow::showSettings(const QString &activationToken)
{
    if (!settings_) settings_ = new SettingsDialog(this);
    settings_->show();
    settings_->raise();
    activatePreview(settings_->windowHandle(), activationToken, diagnostics_);
}
PreviewWindow::PreviewWindow(const FileResource &resource, bool diagnostics) : PreviewWindow(diagnostics)
{
    setResource(resource);
}
void PreviewWindow::setResource(const FileResource &resource)
{
    placement_->cancel();
    ++resizeRevision_;
    const QString name = QFileInfo(resource.path).fileName();
    // The desktop backend appends applicationDisplayName to the native title.
    setWindowTitle(name);
    setAccessibleName(windowTitle());
    location_ = resource.url.isLocalFile() ? resource.path : resource.url.toDisplayString();
    fileInformation_ = QStringLiteral("<qt><table cellspacing='0' cellpadding='3'>"
        "<tr><td valign='top'><small>Name</small></td><td>%1</td></tr>"
        "<tr><td valign='top'><small>Location</small></td><td>%2</td></tr></table></qt>")
        .arg(name.toHtmlEscaped(), location_.toHtmlEscaped());
    fileInfoButton_->setToolTip(fileInformation_);
    fileInfoButton_->setAccessibleDescription(location_);
    summary_->clear();
    if (informationDialog_) informationDialog_->hide();
}
void PreviewWindow::present(const QString &activationToken)
{
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    if (!isVisible()) {
        // A late Wayland configure can leave QWindow maximized after QWidget
        // has returned to a normal preview size. Reconcile before mapping:
        // Qt otherwise requests that stale native state for the new surface.
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")) && windowHandle())
            windowHandle()->setWindowStates(windowState() & ~Qt::WindowActive);
        show();
    }
    if (!isActiveWindow() || !activationToken.isEmpty())
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
    const auto position = status.section(QStringLiteral(" · "), 0, 0);
    const int width = std::clamp(navigationStatus_->fontMetrics().horizontalAdvance(position) + 8, 44, 100);
    navigationStatus_->setFixedWidth(width);
    navigationStatus_->setText(navigationStatus_->fontMetrics().elidedText(position, Qt::ElideRight, width));
    navigationStatus_->setToolTip(status);
    navigationStatus_->setAccessibleName(status);
    layout()->activate();
}
void PreviewWindow::updateZoomControls()
{
    auto *zoom = qobject_cast<ZoomPreview *>(content_);
    zoomControls_->setVisible(zoom != nullptr);
    if (zoom) {
        zoomStatus_->setText(QStringLiteral("%1%").arg(qRound(zoom->zoomFactor() * 100)));
        zoomStatus_->setToolTip(zoom->zoomHint());
        findChild<QPushButton *>(QStringLiteral("zoomIn"))->setEnabled(zoom->zoomFactor() < zoom->maximumZoom() - .00001);
        findChild<QPushButton *>(QStringLiteral("zoomOut"))->setEnabled(zoom->zoomFactor() > std::min(.1, zoom->maximumZoom()) + .00001);
    }
}
void PreviewWindow::setStatus(const QString &status)
{
    QString badge = status;
    const bool partial = status.contains(QStringLiteral("Partial"), Qt::CaseInsensitive);
    const bool missing = status.contains(QStringLiteral("formula result(s) unavailable"));
    if (partial || missing) badge = partial && missing ? QStringLiteral("Partial · Results unavailable")
        : partial ? QStringLiteral("Partial preview") : QStringLiteral("Results unavailable");
    else if (badge.size() > 42) badge = badge.left(39) + QChar(0x2026);
    status_->setText(badge);
    status_->setToolTip(status);
    status_->setAccessibleName(status);
    status_->setVisible(!status.isEmpty());
}
void PreviewWindow::setPreviewData(const PreviewData &data)
{
    if (data.kind != PreviewKind::Image && imageMode_) {
        imageMode_ = false;
        imageRestorePending_ = false;
        if (data.kind != PreviewKind::Information) restoreDocumentSize();
        else {
            // Keep the reading size saved before entering the image preview.
            informationMode_ = true;
        }
    }
    if (data.kind == PreviewKind::Image) enterImageMode();
    else setInformationMode(data.kind == PreviewKind::Information);
    QStringList plain;
    QString rows;
    const auto add = [&](const QString &name, const QString &value) {
        if (value.isEmpty()) return;
        rows += QStringLiteral("<tr><td valign='top'><small>%1</small></td><td>%2</td></tr>")
            .arg(name.toHtmlEscaped(), value.toHtmlEscaped().replace(QChar('\n'), QStringLiteral("<br>")));
        plain.append(name + QStringLiteral(": ") + value);
    };
    add(QStringLiteral("Name"), windowTitle());
    add(QStringLiteral("Location"), location_);
    add(QStringLiteral("Type"), data.description.isEmpty() ? data.mimeType : data.description);
    add(QStringLiteral("Size"), QStringLiteral("%1 (%2 bytes)").arg(QLocale().formattedDataSize(data.size)).arg(data.size));
    if (!data.imageSize.isEmpty()) add(QStringLiteral("Dimensions"), QStringLiteral("%1 × %2 pixels").arg(data.imageSize.width()).arg(data.imageSize.height()));
    if (data.modified.isValid()) add(QStringLiteral("Modified"), QLocale().toString(data.modified, QLocale::ShortFormat));
    add(QStringLiteral("Target"), data.linkTarget);
    if (data.kind == PreviewKind::Text) {
        add(QStringLiteral("Encoding"), data.detail);
        add(data.truncated ? QStringLiteral("Lines shown") : QStringLiteral("Lines"), QString::number(data.textLineCount));
    } else add(QStringLiteral("Preview"), data.detail);
    add(QStringLiteral("Notice"), data.message);
    fileInformation_ = QStringLiteral("<qt><table cellspacing='0' cellpadding='3'>%1</table></qt>").arg(rows);
    fileInfoButton_->setToolTip(fileInformation_);
    fileInfoButton_->setAccessibleDescription(plain.join(QChar('\n')));
    const auto mime = QMimeDatabase().mimeTypeForName(data.mimeType);
    const auto icon = QIcon::fromTheme(mime.iconName(), style()->standardIcon(QStyle::SP_FileIcon));
    if (windowIcon().isNull() || windowIcon().name() != icon.name()) setWindowIcon(icon);
    if (data.kind == PreviewKind::Image) {
        summary_->setText(QStringLiteral("%1 × %2 px").arg(data.imageSize.width()).arg(data.imageSize.height()));
    } else if (data.kind == PreviewKind::Table && data.table) {
        const auto document = data.table;
        const bool delimited = data.readerId == QStringLiteral("csv");
        const auto update = [this, document, delimited](int index) {
            if (index < 0 || index >= document->sheets.size()) return;
            const auto &sheet = document->sheets.at(index);
            if (delimited)
                summary_->setText(QStringLiteral("%1 %2 × %3 %4")
                    .arg(sheet.rowCount).arg(sheet.rowCount == 1 ? QStringLiteral("row") : QStringLiteral("rows"))
                    .arg(sheet.columnCount).arg(sheet.columnCount == 1 ? QStringLiteral("column") : QStringLiteral("columns")));
            else
                summary_->setText(QStringLiteral("%1 %2 · %3 × %4")
                    .arg(document->sheets.size()).arg(document->sheets.size() == 1 ? QStringLiteral("sheet") : QStringLiteral("sheets"))
                    .arg(sheet.rowCount).arg(sheet.columnCount));
        };
        if (auto *tabs = content_->findChild<QTabWidget *>()) {
            connect(tabs, &QTabWidget::currentChanged, this, update);
            update(tabs->currentIndex());
        }
    } else if (data.kind == PreviewKind::Pdf) {
        summary_->setText(QStringLiteral("%1 %2").arg(data.pdfPageSizes.size())
            .arg(data.pdfPageSizes.size() == 1 ? QStringLiteral("page") : QStringLiteral("pages")));
    } else if (data.kind == PreviewKind::Text) {
        summary_->setText(QStringLiteral("%1 · %2 %3").arg(data.detail).arg(data.textLineCount)
            .arg(data.textLineCount == 1 ? QStringLiteral("line") : QStringLiteral("lines")));
    } else summary_->setText(data.description);
    summary_->setToolTip(fileInformation_);
    setStatus(data.kind == PreviewKind::Information ? QString() : data.message);
    if (data.kind == PreviewKind::Table && data.readerId == QStringLiteral("xlsx") && data.message.isEmpty())
        setStatus(QStringLiteral("Saved results may be stale"));
    if (imageMode_) fitImage();
}
void PreviewWindow::showFileInformation()
{
    if (!informationDialog_) {
        auto *dialog = new QDialog(this);
        informationDialog_ = dialog;
        dialog->setObjectName(QStringLiteral("fileInformationDialog"));
        dialog->setWindowTitle(QStringLiteral("File information"));
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        auto *text = new QLabel(dialog);
        text->setObjectName(QStringLiteral("fileInformationText"));
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        text->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        auto *scroll = new QScrollArea(dialog);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(text);
        layout->addWidget(scroll);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::hide);
        layout->addWidget(buttons);
    }
    auto *text = informationDialog_->findChild<QLabel *>(QStringLiteral("fileInformationText"));
    text->setText(fileInformation_);
    const QSize limit = (screen()->availableGeometry().size() - QSize(32, 32)).expandedTo(QSize(1, 1));
    const int dialogWidth = std::min(480, limit.width());
    const int textWidth = std::max(1, dialogWidth - 24 - style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    const int buttonsHeight = informationDialog_->findChild<QDialogButtonBox *>()->sizeHint().height();
    const int dialogHeight = std::clamp(text->heightForWidth(textWidth) + buttonsHeight + 32, 160, 360);
    informationDialog_->setMaximumSize(limit);
    informationDialog_->resize(QSize(dialogWidth, dialogHeight).boundedTo(limit));
    informationDialog_->show();
    informationDialog_->raise();
    informationDialog_->activateWindow();
}
QSize PreviewWindow::previewSize() const
{
    // A compact fallback must not reduce the next image's decode resolution.
    QSize target = informationMode_ || imageMode_ ? previewContentSize_ : contentHost_->size();
    if (screen()) target = target.boundedTo(screen()->availableGeometry().size());
    return (target * devicePixelRatioF()).boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
}
void PreviewWindow::setInformationMode(bool information)
{
    if (!information && !informationMode_) return;
    const QPoint center = geometry().center();
    const auto minimized = windowState() & Qt::WindowMinimized;
    if (information) {
        if (!informationMode_ && !imageMode_) {
            previewWindowState_ = windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
            previewWindowSize_ = previewWindowState_ ? normalGeometry().size() : size();
            previewContentSize_ = contentHost_->size();
        }
        informationMode_ = true;
        imageMode_ = false;
        setMinimumSize(0, 0);
        informationRestorePending_ = isMaximized() || isFullScreen();
        // Unmaximize temporarily; do not request activation when loading ends.
        setWindowState(minimized);
        fitInformation();
    } else {
        informationMode_ = false;
        informationRestorePending_ = false;
        restoreDocumentSize();
    }
    if (!isMaximized() && !isFullScreen()) {
        if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            move(center - rect().center());
        fitScreen(screen());
    }
}
void PreviewWindow::restoreDocumentSize()
{
    setMinimumSize(0, 0);
    requestedWindowSize_ = previewWindowSize_;
    resizePreview(previewWindowSize_);
    setWindowState(previewWindowState_ | (windowState() & Qt::WindowMinimized));
    fitScreen(screen());
}
void PreviewWindow::enterImageMode()
{
    if (imageMode_) return;
    if (informationMode_) {
        // Both are temporary sizes. Keep the saved reading geometry and avoid
        // an intermediate asynchronous restore through the document size.
        informationMode_ = false;
        informationRestorePending_ = false;
        setMinimumSize(0, 0);
        // Information and image previews both borrow the document geometry.
        // Keep the compact state instead of maximizing between these modes.
    } else {
        previewWindowState_ = windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
        previewWindowSize_ = previewWindowState_ ? normalGeometry().size() : size();
        previewContentSize_ = contentHost_->size();
    }
    imageMode_ = true;
    // A large document may have been auto-maximized by Mutter. Images borrow
    // a natural size, just like information previews, then restore the saved
    // document state on return. Maximizing an image itself is still respected.
    imageRestorePending_ = isMaximized() && !isFullScreen();
    if (imageRestorePending_) setWindowState(windowState() & ~Qt::WindowMaximized);
}
void PreviewWindow::prepareImage(const QSize &imageSize)
{
    if (imageSize.isEmpty()) return;
    enterImageMode();
    fitImage(imageSize);
}
void PreviewWindow::fitImage(const QSize &sourceSize)
{
    auto *zoom = qobject_cast<ZoomPreview *>(content_);
    const QSize natural = sourceSize.isEmpty() && zoom ? zoom->naturalSize() : sourceSize;
    if (natural.isEmpty() || isMaximized() || isFullScreen()) return;
    layout()->invalidate();
    layout()->activate();
    const QSize chrome = size() - contentHost_->size();
    const QSize decoration = frameGeometry().size() - size();
    const QSize limit = (screen()->availableGeometry().size() - decoration - QSize(32, 32)).expandedTo(QSize(1, 1));
    setMinimumSize(QSize(400, 400).boundedTo(limit));
    const auto available = (limit - chrome).expandedTo(QSize(1, 1));
    const qreal factor = std::min({1.0, qreal(available.width()) / natural.width(), qreal(available.height()) / natural.height()});
    const QSize fitted(qRound(natural.width() * factor), qRound(natural.height() * factor));
    requestedWindowSize_ = (fitted + chrome).expandedTo(minimumSize()).boundedTo(limit);
    resizePreview(requestedWindowSize_, natural, chrome);
    fitScreen(screen());
}
void PreviewWindow::resizePreview(const QSize &target, const QSize &imageSize, const QSize &chrome)
{
    const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    ++resizeRevision_;
    if (isVisible() && wayland && placement_->available()) {
        if (!isMaximized() && !isFullScreen() && !isMinimized())
            placement_->request(windowTitle(), target, imageSize, chrome);
        return;
    }
    if (isVisible() && wayland) {
        // Let Qt finish the current content/layout/configure event before
        // resizing on compositors without KWin's geometry transaction.
        const auto revision = resizeRevision_;
        QTimer::singleShot(0, this, [this, target, revision] {
            if (revision != resizeRevision_ || !isVisible()
                || isMaximized() || isFullScreen() || isMinimized()) return;
            resize(target);
            fitScreen(screen());
        });
        return;
    }
    if (target == size()) return;
    const QPoint center = geometry().center();
    resize(target);
    if (!wayland) move(center - rect().center());
}
void PreviewWindow::fitInformation()
{
    contentLayout_->activate();
    layout()->invalidate();
    layout()->activate();
    const int width = std::clamp(content_ ? content_->sizeHint().width() + 16 : 480, 380, 560);
    const int height = std::max(layout()->totalHeightForWidth(width), layout()->minimumSize().height());
    resizePreview(QSize(width, height));
    fitScreen(screen());
}
void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (diagnostics_)
        qInfo() << "Dzurkel window resize:" << event->oldSize() << "->" << event->size()
                << "native" << event->spontaneous() << "state" << windowState();
    if (imageRestorePending_ && event->spontaneous()) {
        // The compositor's unmaximize configure can supersede the first fit.
        // Wait until Qt has applied both its geometry and window-state events.
        QTimer::singleShot(0, this, [this] {
            if (!imageRestorePending_ || !imageMode_ || !isVisible()
                || isMaximized() || isFullScreen() || isMinimized()) return;
            imageRestorePending_ = false;
            fitImage();
        });
    }
    if (mappingResizePending_ && event->spontaneous()) {
        mappingResizePending_ = false;
        if (event->size() == mappedWindowSize_
            && !QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
            // A Wayland configure is the compositor's authoritative placement
            // and work-area constraint. Never expand past it to a Qt estimate.
            // A late initial configure can restore the size used when mapping,
            // superseding a provider's subsequent compact/restore request.
            QTimer::singleShot(0, this, [this] {
                if (!isVisible() || isMaximized() || isFullScreen()) return;
                if (informationMode_) fitInformation();
                else if (imageMode_) fitImage();
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
    if (diagnostics_)
        qInfo() << "Dzurkel window show:" << size() << "state" << windowState()
                << "native state" << (windowHandle() ? windowHandle()->windowStates() : Qt::WindowStates{});
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
        if (!isVisible() && shown_ && (informationMode_ || imageMode_)) {
            const auto imageState = windowState();
            restoreDocumentSize();
            // Restore the reading dimensions for decoding, but keep an image's
            // own normal/maximized state until the next provider is known.
            if (imageMode_) setWindowState(imageState);
        }
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
            if (!isMaximized() && !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
                move(area.center() - rect().center());
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
    if (imageMode_) setMinimumSize(QSize(400, 400).boundedTo(limit));
    resize(size().boundedTo(limit));
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) return;
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
        if (key->key() == Qt::Key_Space && (target->objectName() == QStringLiteral("settingsLink")
            || target->objectName() == QStringLiteral("fileInfoButton")))
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
    ++resizeRevision_;
    placement_->cancel();
    if (settings_) settings_->hide();
    if (informationDialog_) informationDialog_->hide();
    QWidget::hideEvent(event);
    emit dismissed();
}
void PreviewWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (diagnostics_ && event->type() == QEvent::WindowStateChange)
        qInfo() << "Dzurkel window state:" << windowState() << "size" << size();
    if (event->type() == QEvent::WindowStateChange && (isMaximized() || isFullScreen() || isMinimized()))
        placement_->cancel();
    if (diagnostics_ && event->type() == QEvent::ActivationChange)
        QTextStream(stderr) << "Dzurkel window active: " << isActiveWindow() << '\n';
}
} // namespace dzurkel

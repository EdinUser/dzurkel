#include "PreviewWindow.h"
#include "resources/FileResource.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QTextStream>
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
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry();
        const QSize limit(std::max(1, area.width() * 4 / 5), std::max(1, area.height() * 4 / 5));
        setMaximumSize(limit);
        resize(QSize(800, 640).boundedTo(limit));
        move(area.center() - rect().center());
    }
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
void PreviewWindow::setContent(QWidget *content)
{
    delete content_;
    content_ = content;
    if (content_) {
        content_->setParent(contentHost_);
        contentLayout_->addWidget(content_);
        content_->setFocus(Qt::OtherFocusReason);
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
bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *target = qobject_cast<QWidget *>(watched);
    if (isVisible() && target && target->window() == this
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_Space || key->key() == Qt::Key_Escape)) {
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

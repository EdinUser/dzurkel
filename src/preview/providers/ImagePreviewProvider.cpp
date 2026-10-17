#include "ImagePreviewProvider.h"
#include <QFile>
#include <QImageIOHandler>
#include <QImageReader>
#include <QPainter>
#include <QWidget>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QHideEvent>
#include "ui/ZoomPreview.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

namespace dzurkel {
namespace {
class ImageCanvas : public QWidget {
public:
    ImageCanvas(QImage image, QWidget *parent) : QWidget(parent), image_(std::move(image))
    { setObjectName(QStringLiteral("imageCanvas")); }
    void setImage(const QImage &image) { image_ = image; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(rect(), image_);
    }
private:
    QImage image_;
};
class ImageWidget : public ZoomPreview {
public:
    ImageWidget(const PreviewData &data, QWidget *parent)
        : ZoomPreview(parent), source_(data.imageSize.isEmpty() ? data.image.size() : data.imageSize),
          reduced_(data.image.size() != source_), frames_(data.animationFrames),
          delays_(data.animationDelays), loops_(data.animationLoops), remainingLoops_(data.animationLoops)
    {
        setObjectName(QStringLiteral("imagePreview"));
        setAccessibleName(QStringLiteral("Image preview"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        scroll_ = new QScrollArea(this);
        scroll_->setAlignment(Qt::AlignCenter);
        scroll_->setFrameShape(QFrame::NoFrame);
        canvas_ = new ImageCanvas(data.image, nullptr);
        scroll_->setWidget(canvas_);
        layout->addWidget(scroll_);
        setFocusProxy(scroll_);
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] { advanceFrame(); });
        if (frames_.size() > 1) {
            playback_ = new QPushButton(QStringLiteral("Pause"), this);
            playback_->setObjectName(QStringLiteral("animationToggle"));
            playback_->setToolTip(QStringLiteral("Pause or play animation (P)"));
            playback_->setShortcut(QKeySequence(Qt::Key_P));
            playback_->setAutoDefault(false);
            layout->addWidget(playback_, 0, Qt::AlignHCenter);
            connect(playback_, &QPushButton::clicked, this, [this] {
                playing_ = !playing_;
                if (playing_) {
                    if (finished_) {
                        frame_ = 0;
                        remainingLoops_ = loops_;
                        finished_ = false;
                        canvas_->setImage(frames_.first());
                    }
                    if (isVisible()) timer_.start(delays_.at(frame_));
                } else timer_.stop();
                updatePlaybackButton();
            });
            updatePlaybackButton();
        }
    }
    qreal zoomFactor() const override { return scale_; }
    bool isFit() const override { return fit_; }
    QSize naturalSize() const override
    {
        return source_ + QSize(0, playback_ ? playback_->sizeHint().height() + layout()->spacing() : 0);
    }
    qreal maximumZoom() const override
    {
        return std::min(8.0, qreal(QWIDGETSIZE_MAX - 1) / std::max(source_.width(), source_.height()));
    }
    QString zoomHint() const override
    {
        return reduced_ ? QStringLiteral("Image detail is limited to a 4096 × 4096 decode within 64 MiB; larger zoom magnifies those pixels.") : QString();
    }
    void fit() override { fit_ = true; updateSize(); }
    void scrollVertically(int direction) override
    {
        scroll_->verticalScrollBar()->triggerAction(direction < 0 ? QAbstractSlider::SliderSingleStepSub : QAbstractSlider::SliderSingleStepAdd);
    }
    void zoomBy(int steps) override
    {
        const auto factor = steppedZoom(steps);
        if (qAbs(factor - scale_) < .0000001) return;
        const auto center = QPointF(scroll_->horizontalScrollBar()->value() + std::min(scroll_->viewport()->width(), canvas_->width()) / 2.0,
                                    scroll_->verticalScrollBar()->value() + std::min(scroll_->viewport()->height(), canvas_->height()) / 2.0) / scale_;
        fit_ = false;
        scale_ = factor;
        updateSize();
        scroll_->horizontalScrollBar()->setValue(qRound(center.x() * scale_ - scroll_->viewport()->width() / 2.0));
        scroll_->verticalScrollBar()->setValue(qRound(center.y() * scale_ - scroll_->viewport()->height() / 2.0));
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        ZoomPreview::resizeEvent(event);
        if (fit_) updateSize();
    }
    void showEvent(QShowEvent *event) override
    {
        ZoomPreview::showEvent(event);
        if (fit_) updateSize();
        if (playing_ && frames_.size() > 1) timer_.start(delays_.at(frame_));
    }
    void hideEvent(QHideEvent *event) override
    {
        timer_.stop();
        ZoomPreview::hideEvent(event);
    }
private:
    void updatePlaybackButton()
    {
        const auto text = playing_ ? QStringLiteral("Pause") : (finished_ ? QStringLiteral("Replay") : QStringLiteral("Play"));
        playback_->setText(text);
        playback_->setAccessibleName(text + QStringLiteral(" animation"));
    }
    void advanceFrame()
    {
        if (!playing_ || !isVisible()) return;
        if (frame_ + 1 == frames_.size()) {
            if (!remainingLoops_) {
                playing_ = false;
                finished_ = true;
                updatePlaybackButton();
                return;
            }
            if (remainingLoops_ > 0) --remainingLoops_;
            frame_ = 0;
        } else ++frame_;
        canvas_->setImage(frames_.at(frame_));
        timer_.start(delays_.at(frame_));
    }
    void updateSize()
    {
        if (fit_) {
            // Reserve no scrollbars in fit mode; avoid alternating fit sizes.
            const auto available = scroll_->maximumViewportSize().expandedTo(QSize(1, 1));
            scale_ = std::min({1.0, qreal(available.width()) / source_.width(), qreal(available.height()) / source_.height()});
        }
        // Painting scales the bounded source image; never allocate a zoom-sized bitmap.
        const auto dimensions = QSize(qRound(std::clamp(source_.width() * scale_, 1.0, double(QWIDGETSIZE_MAX))),
                                      qRound(std::clamp(source_.height() * scale_, 1.0, double(QWIDGETSIZE_MAX))));
        canvas_->setFixedSize(dimensions);
        emit zoomChanged();
    }
    QSize source_;
    bool reduced_;
    QList<QImage> frames_;
    QList<int> delays_;
    int loops_;
    int remainingLoops_;
    int frame_ = 0;
    bool playing_ = true;
    bool finished_ = false;
    QTimer timer_;
    QPushButton *playback_ = nullptr;
    QScrollArea *scroll_;
    ImageCanvas *canvas_;
    qreal scale_ = 1;
    bool fit_ = true;
};
}

bool ImagePreviewProvider::supports(const QMimeType &mime) const
{
    return mime.name().startsWith(QStringLiteral("image/"));
}
PreviewData ImagePreviewProvider::load(std::unique_ptr<QFile> file, PreviewData data,
                                     const QSize &target, const Cancellation &cancel) const
{
    return loadWithMetadata(std::move(file), std::move(data), target, cancel, {});
}
PreviewData ImagePreviewProvider::loadWithMetadata(std::unique_ptr<QFile> file, PreviewData data,
                                     const QSize &, const Cancellation &cancel,
                                     const std::function<void(const QSize &)> &metadataReady) const
{
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    QElapsedTimer work;
    work.start();
    QImageReader reader(file.get());
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        data.message = QStringLiteral("No installed Qt image reader can decode this file.");
        return data;
    }
    const QSize original = reader.size();
    if (!original.isValid() || original.isEmpty()) {
        data.message = QStringLiteral("The image has invalid or unavailable dimensions.");
        return data;
    }
    // Readers without native scaling may allocate the original before shrinking.
    const qint64 pixels = qint64(original.width()) * original.height();
    if (!reader.supportsOption(QImageIOHandler::ScaledSize)
        && pixels > qint64(AllocationLimitMiB) * 1024 * 1024 / 4) {
        data.message = QStringLiteral("Image exceeds the 64 MiB decoding budget for this reader.");
        return data;
    }
    // Retain useful detail for zoom without rereading the source on the GUI thread.
    QSize bounds(4096, 4096);
    data.imageSize = original;
    if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) data.imageSize.transpose();
    if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) bounds.transpose();
    reader.setScaledSize(original.scaled(bounds.boundedTo(original), Qt::KeepAspectRatio));
    // Query format before decoding: format() can look ahead to the next GIF
    // frame and advance delay metadata after a read.
    const bool animatedGif = reader.format() == QByteArrayLiteral("gif") && reader.supportsAnimation();
    // This callback runs on the decoder worker, before allocating image pixels.
    if (!cancel->load() && metadataReady) metadataReady(data.imageSize);
    if (cancel->load()) { data.kind = PreviewKind::Cancelled; return data; }
    data.image = reader.read();
    const int firstDelay = qBound(20, reader.nextImageDelay(), 60000);
    if (cancel->load()) {
        data.image = {};
        data.kind = PreviewKind::Cancelled;
    } else if (data.image.isNull()) {
        data.message = QStringLiteral("Image decoding failed: %1").arg(reader.errorString());
    } else if (data.image.sizeInBytes() > qint64(AllocationLimitMiB) * 1024 * 1024) {
        data.image = {};
        data.message = QStringLiteral("Decoded image exceeds the 64 MiB budget.");
    } else {
        data.kind = PreviewKind::Image;
        data.message.clear();
        data.detail = QStringLiteral("%1 × %2 pixels").arg(original.width()).arg(original.height());
        if (animatedGif) {
            data.animationFrames.append(data.image);
            data.animationDelays.append(firstDelay);
            data.animationLoops = reader.loopCount();
            qint64 retained = data.image.sizeInBytes();
            while (reader.canRead()) {
                if (cancel->load()) {
                    data.image = {};
                    data.animationFrames.clear();
                    data.animationDelays.clear();
                    data.kind = PreviewKind::Cancelled;
                    return data;
                }
                if (data.animationFrames.size() >= AnimationFrameLimit || work.elapsed() >= AnimationWorkMs) {
                    data.message = QStringLiteral("Animation exceeds the 300-frame or 5-second decoding-work limit; showing the first frame.");
                    break;
                }
                auto frame = reader.read();
                const int delay = qBound(20, reader.nextImageDelay(), 60000);
                if (frame.isNull() || frame.sizeInBytes() > qint64(AllocationLimitMiB) * 1024 * 1024 - retained) {
                    data.message = frame.isNull() ? QStringLiteral("Animation decoding failed; showing the first frame.")
                        : QStringLiteral("Animation exceeds the 64 MiB retained-frame budget; showing the first frame.");
                    break;
                }
                retained += frame.sizeInBytes();
                data.animationFrames.append(std::move(frame));
                data.animationDelays.append(delay);
            }
            if (!data.message.isEmpty() || data.animationFrames.size() < 2) {
                data.animationFrames.clear();
                data.animationDelays.clear();
            } else {
                data.detail += QStringLiteral(" · Animated GIF · %1 frames").arg(data.animationFrames.size());
            }
        }
    }
    return data;
}
QWidget *ImagePreviewProvider::createWidget(const PreviewData &data, QWidget *parent) const
{
    return new ImageWidget(data, parent);
}
} // namespace dzurkel

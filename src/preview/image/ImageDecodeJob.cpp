#include "ImageDecodeJob.h"
#include "preview/providers/ImagePreviewProvider.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace dzurkel {
namespace {
QSet<QProcess *> &helpers()
{
    static QSet<QProcess *> processes;
    return processes;
}
void initializeReaper()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        for (auto *process : helpers()) {
            if (process->state() == QProcess::NotRunning) continue;
            process->kill();
            process->setParent(nullptr); // Never invoke a waiting destructor at exit.
            process->disconnect();
        }
    });
}
}
int ImageDecodeJob::runningHelpers() { return helpers().size(); }
ImageDecodeJob::ImageDecodeJob(const PreviewData &data, QObject *parent, const QString &program, int timeoutMs)
    : QObject(parent), result_(data)
{
    initializeReaper();
    result_.kind = PreviewKind::Information;
    timeout_.setSingleShot(true);
    timeout_.setInterval(timeoutMs);
    connect(&timeout_, &QTimer::timeout, this, [this] { finish(QStringLiteral("Image preview timed out.")); });
    if (helpers().size() >= 2) {
        QTimer::singleShot(0, this, [this] { finish(QStringLiteral("Previous image preview is still stopping. Try again shortly.")); });
        return;
    }
    auto *process = new QProcess(qApp);
    process_ = process;
    helpers().insert(process);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(process, &QProcess::finished, process, [process, store = data.resource.backingStore] {
        Q_UNUSED(store);
        helpers().remove(process);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { helpers().remove(process); process->deleteLater(); }
    });
    connect(process, &QProcess::readyReadStandardOutput, this, &ImageDecodeJob::read);
    connect(process, &QProcess::finished, this, [this] { finish(QStringLiteral("Image preview stopped before decoding completed.")); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Cannot start the image preview helper."));
    });
    const auto executable = program.isEmpty() ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dzurkel")) : program;
    process->start(executable, {QStringLiteral("--internal-image"),
        data.resource.backingPath.isEmpty() ? data.resource.path : data.resource.backingPath, data.mimeType});
    timeout_.start();
}
ImageDecodeJob::~ImageDecodeJob() { stop(); }
void ImageDecodeJob::stop()
{
    stopped_ = true;
    timeout_.stop();
    frames_.clear();
    if (process_) {
        disconnect(process_, nullptr, this, nullptr);
        if (process_->state() != QProcess::NotRunning) process_->kill();
        process_ = nullptr;
    }
}
void ImageDecodeJob::finish(const QString &error)
{
    if (stopped_) return;
    if (!error.isEmpty()) {
        result_.kind = PreviewKind::Information;
        result_.message = error;
        result_.image = {};
        result_.animationFrames.clear();
        result_.animationDelays.clear();
    } else if (process_) {
        // Acknowledge receipt; the normal helper exits and is reaped asynchronously.
        disconnect(process_, nullptr, this, nullptr);
        process_->closeWriteChannel();
        QTimer::singleShot(1000, process_, [process = process_] {
            if (process && process->state() != QProcess::NotRunning) process->kill();
        });
        process_ = nullptr;
    }
    stop();
    emit completed(result_);
}
bool ImageDecodeJob::header()
{
    const auto object = QJsonDocument::fromJson(headerBytes_).object();
    headerBytes_.clear();
    auto invalid = [this] { finish(QStringLiteral("Invalid image preview response.")); return false; };
    const auto event = object.value("event").toString();
    const QSize size(object.value("width").toInt(), object.value("height").toInt());
    if (event == QStringLiteral("metadata")) {
        if (metadataSeen_ || !size.isValid() || size.isEmpty()) return invalid();
        metadataSeen_ = true;
        result_.imageSize = size;
        emit metadataReady(size);
        return true;
    }
    if (event != QStringLiteral("result")) return invalid();
    result_.message = object.value("message").toString().left(4096);
    result_.detail = object.value("detail").toString().left(4096);
    result_.missingLibrary = object.value("missingLibrary").toString();
    if (!result_.missingLibrary.isEmpty() && result_.missingLibrary != QStringLiteral("imageformats")) return invalid();
    if (!object.value("image").toBool()) { finish(); return false; }
    if (!size.isValid() || size.isEmpty() || (metadataSeen_ && size != result_.imageSize)) return invalid();
    result_.imageSize = size;
    const auto descriptors = object.value("frames").toArray();
    const auto delays = object.value("delays").toArray();
    if (descriptors.isEmpty() || descriptors.size() > ImagePreviewProvider::AnimationFrameLimit
        || (descriptors.size() > 1 && delays.size() != descriptors.size()) || (descriptors.size() == 1 && !delays.isEmpty())) return invalid();
    result_.animationLoops = object.value("loops").toInt();
    if (result_.animationLoops < -1) return invalid();
    qint64 total = 0;
    for (int i = 0; i < descriptors.size(); ++i) {
        const auto descriptor = descriptors.at(i).toObject();
        const int width = descriptor.value("width").toInt(), height = descriptor.value("height").toInt();
        const int format = descriptor.value("format").toInt();
        const auto bytes = descriptor.value("bytes").toInteger(-1);
        if (width < 1 || height < 1 || width > 4096 || height > 4096 || format <= QImage::Format_Invalid
            || format >= QImage::NImageFormats || bytes < 1 || bytes > ImagePixelBudget - total) return invalid();
        QImage image(width, height, QImage::Format(format));
        if (image.isNull() || image.sizeInBytes() != bytes) return invalid();
        total += bytes;
        const auto profile = descriptor.value("profile").toString().toLatin1();
        if (profile.size() > (ImageProfileBudget + 2) / 3 * 4) return invalid();
        const auto icc = QByteArray::fromBase64(profile, QByteArray::AbortOnBase64DecodingErrors);
        if (icc.size() > ImageProfileBudget || (!profile.isEmpty() && icc.isEmpty())) return invalid();
        if (!icc.isEmpty()) {
            const auto colorSpace = QColorSpace::fromIccProfile(icc);
            if (!colorSpace.isValid()) return invalid();
            image.setColorSpace(colorSpace);
        }
        const auto palette = descriptor.value("palette").toArray();
        if (palette.size() > 256) return invalid();
        QList<QRgb> colors;
        for (const auto &entry : palette) {
            const auto color = entry.toInteger(-1);
            if (color < 0 || color > 0xffffffffLL) return invalid();
            colors.append(QRgb(color));
        }
        if (!colors.isEmpty()) image.setColorTable(colors);
        const auto dpr = descriptor.value("dpr").toDouble(1);
        if (!std::isfinite(dpr) || dpr <= 0 || dpr > 64) return invalid();
        image.setDevicePixelRatio(dpr);
        image.setDotsPerMeterX(descriptor.value("dpmX").toInt());
        image.setDotsPerMeterY(descriptor.value("dpmY").toInt());
        frames_.append(std::move(image));
        if (descriptors.size() > 1) {
            const int delay = delays.at(i).toInt();
            if (delay < 20 || delay > 60000) return invalid();
            result_.animationDelays.append(delay);
        }
    }
    result_.kind = PreviewKind::Image;
    body_ = true;
    return true;
}
void ImageDecodeJob::read()
{
    if (stopped_ || !process_) return;
    qint64 budget = 512 * 1024;
    while (process_ && process_->bytesAvailable() && budget > 0) {
        if (!body_) {
            if (budget < 2) break; // QIODevice::readLine reserves a terminator.
            const auto chunk = process_->readLine(std::min({qint64(64 * 1024), budget, qint64(ImageHeaderBudget + 1 - headerBytes_.size())}));
            if (chunk.isEmpty()) return;
            headerBytes_ += chunk;
            budget -= chunk.size();
            if (headerBytes_.size() > ImageHeaderBudget
                || (headerBytes_.size() == ImageHeaderBudget && !headerBytes_.endsWith('\n'))) {
                finish(QStringLiteral("Image preview header exceeds its budget.")); return;
            }
            if (!headerBytes_.endsWith('\n')) continue;
            if (!header()) return;
            continue;
        }
        auto &image = frames_[frame_];
        const auto count = process_->read(reinterpret_cast<char *>(image.bits()) + received_,
            std::min({qint64(64 * 1024), budget, image.sizeInBytes() - received_}));
        if (count <= 0) return;
        received_ += count;
        budget -= count;
        if (received_ == image.sizeInBytes()) {
            received_ = 0;
            if (++frame_ == frames_.size()) {
                result_.image = frames_.first();
                if (frames_.size() > 1) result_.animationFrames = frames_;
                finish();
                return;
            }
        }
    }
    if (process_ && process_->bytesAvailable()) QTimer::singleShot(0, this, &ImageDecodeJob::read);
}
}

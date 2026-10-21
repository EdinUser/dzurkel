#include "controller/PreviewController.h"
#include "preview/PreviewRegistry.h"
#include "preview/image/ImageDecodeJob.h"
#include "preview/providers/ImagePreviewProvider.h"
#include "resources/ResourcePreparation.h"
#include "../support/OffscreenWarnings.h"
#include <QBuffer>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImageReader>
#include <QMimeDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace dzurkel;
class ImageTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    std::unique_ptr<OffscreenWarnings> warnings_;
    PreviewData decode(const QString &path)
    {
        FileResource resource;
        QString error;
        if (!resolveFileResource(path, resource, error)) { PreviewData data; data.message = error; return data; }
        PreviewData base;
        base.resource = resource;
        base.mimeType = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
        ImageDecodeJob job(base);
        PreviewData result;
        result.message = QStringLiteral("Test timed out.");
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&job, &ImageDecodeJob::completed, &loop, [&](const PreviewData &data) { result = data; loop.quit(); });
        timer.start(10000);
        loop.exec();
        return result;
    }
    QString script(const QByteArray &body)
    {
        const auto path = directory_.filePath(QStringLiteral("helper"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!/usr/bin/python3\nimport sys,time\n" + body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
private slots:
    void initTestCase()
    {
        warnings_ = std::make_unique<OffscreenWarnings>();
        QVERIFY(directory_.isValid());
        qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
        QImageReader::setAllocationLimit(ImagePreviewProvider::AllocationLimitMiB);
        qRegisterMetaType<PreviewData>();
    }
    void cleanup() { QTRY_COMPARE(ImageDecodeJob::runningHelpers(), 0); }
    void opensSparseSourceBeyondOldCopyLimit()
    {
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::green);
        QByteArray bmp;
        QBuffer buffer(&bmp);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "BMP"));
        const auto oldOffset = qFromLittleEndian<quint32>(bmp.constData() + 10);
        constexpr quint32 newOffset = 128 * 1024 * 1024;
        auto header = bmp.left(oldOffset);
        qToLittleEndian<quint32>(newOffset + bmp.size() - oldOffset, header.data() + 2);
        qToLittleEndian<quint32>(newOffset, header.data() + 10);
        const auto path = directory_.filePath(QStringLiteral("large.bmp"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(header), header.size());
        QVERIFY(file.seek(newOffset));
        QCOMPARE(file.write(bmp.mid(oldOffset)), bmp.size() - oldOffset);
        file.close();
        FileResource original;
        QString error;
        QVERIFY(parseFileResource(path, original, error));
        ResourcePreparation preparation;
        FileResource prepared;
        bool ready = false;
        connect(&preparation, &ResourcePreparation::ready, this, [&](const FileResource &resource) { prepared = resource; ready = true; });
        preparation.start(original);
        QTRY_VERIFY(ready);
        QVERIFY(!prepared.informationOnly);
        QVERIFY(prepared.seekableSource);
        QVERIFY(!prepared.backingStore);
        QCOMPARE(prepared.backingPath, path);
        QCOMPARE(prepared.metadata.value(QStringLiteral("imageWidth")).toInt(), 2);
        QCOMPARE(prepared.metadata.value(QStringLiteral("imageHeight")).toInt(), 2);
        auto candidate = PreviewRegistry::load(prepared, {}, std::make_shared<std::atomic_bool>(false));
        QCOMPARE(candidate.kind, PreviewKind::Image);
        QVERIFY(candidate.image.isNull());
        ImageDecodeJob job(candidate);
        PreviewData result;
        bool decoded = false;
        connect(&job, &ImageDecodeJob::completed, this, [&](const PreviewData &data) { result = data; decoded = true; });
        QTRY_VERIFY(decoded);
        QCOMPARE(result.kind, PreviewKind::Image);
        QCOMPARE(result.imageSize, QSize(2, 2));
        QCOMPARE(result.image.pixelColor(0, 0), QColor(Qt::green));
        QTRY_COMPARE(ImageDecodeJob::runningHelpers(), 0);
        QVERIFY(QFile::remove(path));
        // The decoder worker must not reopen a prepared direct source.
        candidate = PreviewRegistry::load(prepared, {}, std::make_shared<std::atomic_bool>(false));
        QCOMPARE(candidate.kind, PreviewKind::Image);
        ImageDecodeJob missing(candidate);
        bool missingDone = false;
        connect(&missing, &ImageDecodeJob::completed, this, [&](const PreviewData &data) {
            QCOMPARE(data.kind, PreviewKind::Information);
            QVERIFY(!data.message.isEmpty());
            missingDone = true;
        });
        QTRY_VERIFY(missingDone);
    }
    void preservesPixelsAlphaBitDepthAndColourProfile()
    {
        QImage image(32, 20, QImage::Format_RGBA64);
        image.fill(QColor::fromRgbF(.12, .43, .76, .37));
        image.setColorSpace(QColorSpace::DisplayP3);
        const auto path = directory_.filePath(QStringLiteral("alpha16.png"));
        QVERIFY(image.save(path));
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto expected = reader.read();
        QVERIFY(!expected.isNull());
        const auto result = decode(path);
        QCOMPARE(result.kind, PreviewKind::Image);
        QCOMPARE(result.image, expected);
        QCOMPARE(result.image.format(), expected.format());
        QCOMPARE(result.image.colorSpace(), expected.colorSpace());
    }
    void preservesJpegOrientationAndMetadata()
    {
        QImage image(120, 60, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "JPEG"));
        jpeg.insert(2, QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
        const auto path = directory_.filePath(QStringLiteral("oriented.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(jpeg), jpeg.size());
        file.close();
        const auto result = decode(path);
        QCOMPARE(result.kind, PreviewKind::Image);
        QCOMPARE(result.imageSize, QSize(60, 120));
        QCOMPARE(result.image.size(), result.imageSize);
        FileResource original;
        QString error;
        QVERIFY(parseFileResource(path, original, error));
        ResourcePreparation preparation;
        bool ready = false;
        QSize earlySize;
        connect(&preparation, &ResourcePreparation::ready, this, [&](const FileResource &resource) {
            earlySize = QSize(resource.metadata.value(QStringLiteral("imageWidth")).toInt(),
                              resource.metadata.value(QStringLiteral("imageHeight")).toInt());
            ready = true;
        });
        preparation.start(original);
        QTRY_VERIFY(ready);
        QCOMPARE(earlySize, QSize(60, 120));
    }
    void supportsFontUsingImageReaders()
    {
        if (!QImageReader::supportedImageFormats().contains("svg")) QSKIP("Qt SVG image reader is not installed");
        const auto path = directory_.filePath(QStringLiteral("text.svg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='80'><rect width='200' height='80' fill='white'/><text x='10' y='40' font-size='20'>Preview</text></svg>");
        file.close();
        const auto result = decode(path);
        QCOMPARE(result.kind, PreviewKind::Image);
        QCOMPARE(result.imageSize, QSize(200, 80));
        QVERIFY(!result.image.isNull());
    }
    void preservesAnimationAndBudgetFallbacks()
    {
        const auto result = decode(QStringLiteral(DZURKEL_FIXTURES "/animated.gif"));
        QCOMPARE(result.kind, PreviewKind::Image);
        QCOMPARE(result.animationFrames.size(), 3);
        QCOMPARE(result.animationDelays, QList<int>({80, 120, 160}));
        QCOMPARE(result.animationLoops, -1);
        for (const auto &name : {"animation-frame-limit.gif", "animation-memory-limit.gif"}) {
            const auto limited = decode(QStringLiteral(DZURKEL_FIXTURES "/") + QString::fromLatin1(name));
            QCOMPARE(limited.kind, PreviewKind::Image);
            QVERIFY(!limited.image.isNull());
            QVERIFY(limited.animationFrames.isEmpty());
            QVERIFY(limited.message.contains(QStringLiteral("showing the first frame")));
        }
    }
    void handlesMalformedOrTruncatedResults_data()
    {
        QTest::addColumn<QByteArray>("output");
        QTest::newRow("oversized-header") << QByteArray(ImageHeaderBudget + 1, 'x');
        QTest::newRow("invalid-json") << QByteArray("bad\n");
        QTest::newRow("invalid-frame") << QByteArray("{\"event\":\"result\",\"image\":true,\"width\":100,\"height\":100,\"frames\":[{\"width\":5000,\"height\":5000,\"format\":5,\"bytes\":100000000}]}\n");
        QTest::newRow("truncated-frame") << QByteArray("{\"event\":\"result\",\"image\":true,\"width\":100,\"height\":100,\"frames\":[{\"width\":100,\"height\":100,\"format\":5,\"bytes\":40000}]}\nshort");
    }
    void handlesMalformedOrTruncatedResults()
    {
        QFETCH(QByteArray, output);
        const auto program = script("sys.stdout.buffer.write(bytes.fromhex('" + output.toHex() + "'))\nsys.stdout.flush()\n");
        PreviewData base;
        ImageDecodeJob job(base, nullptr, program);
        bool done = false;
        connect(&job, &ImageDecodeJob::completed, this, [&](const PreviewData &data) {
            QCOMPARE(data.kind, PreviewKind::Information);
            QVERIFY(!data.message.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void cancelsStalledHelpersAndBoundsConcurrency()
    {
        const auto program = script("time.sleep(10)\n");
        PreviewData base;
        auto first = std::make_unique<ImageDecodeJob>(base, nullptr, program);
        ImageDecodeJob second(base, nullptr, program, 150);
        ImageDecodeJob third(base, nullptr, program);
        QSignalSpy secondDone(&second, &ImageDecodeJob::completed);
        QSignalSpy thirdDone(&third, &ImageDecodeJob::completed);
        QCOMPARE(ImageDecodeJob::runningHelpers(), 2);
        QTRY_COMPARE(thirdDone.size(), 1);
        QElapsedTimer elapsed;
        elapsed.start();
        first.reset();
        QVERIFY(elapsed.elapsed() < 100);
        QTRY_COMPARE(secondDone.size(), 1);
    }
};
QTEST_MAIN(ImageTest)
#include "ImageTest.moc"

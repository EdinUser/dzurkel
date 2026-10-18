#include "resources/media/StreamingMediaSource.h"
#include <QAudioOutput>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QMediaPlayer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dzurkel;
class MediaPlaybackTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void repeatedPlaybackSeeksAndStopsWithoutRetainingSources();
    void cancellationDuringDecoderReadDoesNotWaitForSource();
private:
    QTemporaryDir directory_;
    QString path_;
};
void MediaPlaybackTest::initTestCase()
{
    QVERIFY(directory_.isValid());
    path_ = directory_.filePath(QStringLiteral("silence.wav"));
    QFile file(path_);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 90 * 48000 * 2 * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(bytes + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
           << quint32(48000 * 4) << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    QCOMPARE(file.pos(), 44);
    QVERIFY(file.resize(bytes + 44));
}
void MediaPlaybackTest::repeatedPlaybackSeeksAndStopsWithoutRetainingSources()
{
    qint64 worstStop = 0;
    for (int iteration = 0; iteration < 8; ++iteration) {
        StreamingMediaSource source;
        QAudioOutput output;
        output.setMuted(true);
        QMediaPlayer player;
        player.setAudioOutput(&output);
        QSignalSpy ready(&source, &StreamingMediaSource::ready);
        QSignalSpy failed(&source, &StreamingMediaSource::failed);
        source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), path_);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 3000);
        QVERIFY2(failed.isEmpty(), qPrintable(source.errorString()));
        player.setSourceDevice(&source);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0 || player.error() != QMediaPlayer::NoError, 3000);
        QCOMPARE(player.error(), QMediaPlayer::NoError);
        QVERIFY(player.isSeekable());
        // The 17 MB fixture has not been read in full to start playback.
        QVERIFY(source.bytesTransferred() < source.size());
        player.setPosition(30000);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 30100, 2000);
        player.setPosition(1000);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 1100 && player.position() < 5000, 2000);
        QCOMPARE(source.ownerThreadReads(), 0);
        QVERIFY(source.peakReadBytes() <= StreamingMediaSource::ChunkBytes);
        QElapsedTimer stop;
        stop.start();
        output.setMuted(true);
        source.cancel();
        player.setSource(QUrl());
        worstStop = qMax(worstStop, stop.elapsed());
        QVERIFY(stop.elapsed() < 500);
        QTRY_COMPARE_WITH_TIMEOUT(StreamingMediaSource::activeHelpers(), 0, 1000);
    }
    qInfo() << "Eight playback/replacement cycles; worst stop (ms):" << worstStop;
}
void MediaPlaybackTest::cancellationDuringDecoderReadDoesNotWaitForSource()
{
    StreamingMediaSource source(nullptr, 10000);
    QAudioOutput output;
    output.setMuted(true);
    QMediaPlayer player;
    player.setAudioOutput(&output);
    QSignalSpy ready(&source, &StreamingMediaSource::ready);
    source.start(QString::fromUtf8(DZURKEL_MEDIA_HELPER), path_, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
    player.setSourceDevice(&source);
    player.play();
    QTRY_COMPARE_WITH_TIMEOUT(source.readRequests(), 1, 1000);
    int heartbeats = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start(5);
    QTest::qWait(100);
    QVERIFY(heartbeats > 5);
    QCOMPARE(source.bytesTransferred(), 0);
    QElapsedTimer stop;
    stop.start();
    source.cancel();
    player.setSource(QUrl());
    QVERIFY(stop.elapsed() < 500);
    QTRY_COMPARE_WITH_TIMEOUT(StreamingMediaSource::activeHelpers(), 0, 1000);
    QCOMPARE(source.ownerThreadReads(), 0);
}
QTEST_GUILESS_MAIN(MediaPlaybackTest)
#include "MediaPlaybackTest.moc"

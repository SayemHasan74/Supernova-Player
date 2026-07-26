#include "PlayerCore/PlayerCore.h"
#include "Mpv/MpvCore.h"

#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <mpv/client.h>

namespace {
bool createSilentWaveFile(const QString &path)
{
    constexpr quint32 sampleRate = 8'000;
    constexpr quint16 channels = 1;
    constexpr quint16 bitsPerSample = 16;
    constexpr quint32 durationSeconds = 2;
    constexpr quint32 bytesPerSample =
        channels * bitsPerSample / 8;
    constexpr quint32 dataSize =
        sampleRate * durationSeconds * bytesPerSample;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream output(&file);
    output.setByteOrder(QDataStream::LittleEndian);
    file.write("RIFF", 4);
    output << quint32(36 + dataSize);
    file.write("WAVEfmt ", 8);
    output << quint32(16) << quint16(1) << channels
           << sampleRate << quint32(sampleRate * bytesPerSample)
           << quint16(bytesPerSample) << bitsPerSample;
    file.write("data", 4);
    output << dataSize;
    return file.write(
               QByteArray(static_cast<qsizetype>(dataSize), '\0'))
        == dataSize;
}
}

class PlayerCoreLifecycleTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void replacementStopKeepsTheNewLoadActive();
    void eofKeepsLoadedMediaRestartable();
    void idleIsReservedForUnloadedMedia();
    void bufferingMetricsRemainOrthogonalToPlaybackState();
    void loadErrorsAreReportedAfterIdle();
    void realMediaReachesEofAndRestarts();
    void nativePlaylistBuildsAuthoritativeQueueState();
    void realPlaylistSupportsPlayRemoveAndClear();
    void singleLocalFileAutoloadsSiblingsAndNextPlaysThem();
};

void PlayerCoreLifecycleTests::initTestCase()
{
    qRegisterMetaType<PlayerState>();
    qRegisterMetaType<BufferingInfo>();
    qRegisterMetaType<MpvEndFileInfo>();
    qRegisterMetaType<PlaylistState>();
}

void PlayerCoreLifecycleTests::replacementStopKeepsTheNewLoadActive()
{
    PlayerCore core;
    QSignalSpy endedSpy(&core, &PlayerCore::mediaEnded);
    core.m_info.state = PlayerState::Loading;

    MpvEndFileInfo end;
    end.reason = MPV_END_FILE_REASON_STOP;
    core.onMpvFileEnded(end);

    QCOMPARE(core.info().state, PlayerState::Loading);
    QCOMPARE(endedSpy.count(), 1);
}

void PlayerCoreLifecycleTests::eofKeepsLoadedMediaRestartable()
{
    PlayerCore core;
    QSignalSpy eofSpy(&core, &PlayerCore::eofChanged);
    QSignalSpy positionSpy(&core, &PlayerCore::positionChanged);
    core.m_info.state = PlayerState::Playing;
    core.m_info.videoDurationSec = 42.5;
    core.m_info.videoPositionSec = 42.0;

    core.onMpvPropertyChanged(
        QStringLiteral("eof-reached"), true);

    QCOMPARE(core.info().state, PlayerState::Paused);
    QVERIFY(core.info().eofReached);
    QCOMPARE(core.info().videoPositionSec, 42.5);
    QCOMPARE(eofSpy.count(), 1);
    QCOMPARE(positionSpy.count(), 1);
}

void PlayerCoreLifecycleTests::idleIsReservedForUnloadedMedia()
{
    PlayerCore core;
    QSignalSpy stoppedSpy(&core, &PlayerCore::playbackStopped);
    core.m_info.state = PlayerState::Paused;
    core.m_info.eofReached = true;
    core.m_info.hasVideo = true;
    core.m_info.videoWidth = 1920;
    core.m_info.videoHeight = 1080;
    core.m_info.buffering.active = true;

    core.onMpvPropertyChanged(
        QStringLiteral("idle-active"), true);

    QCOMPARE(core.info().state, PlayerState::Idle);
    QVERIFY(!core.info().eofReached);
    QVERIFY(!core.info().hasVideo);
    QVERIFY(!core.info().buffering.active);
    QCOMPARE(core.info().videoWidth, 0);
    QCOMPARE(core.info().videoHeight, 0);
    QCOMPARE(stoppedSpy.count(), 1);
}

void PlayerCoreLifecycleTests::
    bufferingMetricsRemainOrthogonalToPlaybackState()
{
    PlayerCore core;
    QSignalSpy bufferingSpy(
        &core, &PlayerCore::bufferingChanged);
    core.m_info.state = PlayerState::Playing;

    core.onMpvPropertyChanged(
        QStringLiteral("paused-for-cache"), true);
    core.onMpvPropertyChanged(
        QStringLiteral("cache-buffering-state"), qint64(63));
    core.onMpvPropertyChanged(
        QStringLiteral("cache-speed"), qint64(2'000'000));
    core.onMpvPropertyChanged(
        QStringLiteral("demuxer-cache-duration"), 8.25);
    core.onMpvPropertyChanged(
        QStringLiteral("demuxer-cache-state"),
        QVariantMap{
            {QStringLiteral("fw-bytes"), qint64(4'000'000)},
            {QStringLiteral("raw-input-rate"), qint64(3'000'000)}});

    QCOMPARE(core.info().state, PlayerState::Playing);
    QVERIFY(core.info().buffering.active);
    QCOMPARE(core.info().buffering.percent, 63);
    QCOMPARE(
        core.info().buffering.cacheUsedBytes, qint64(4'000'000));
    QCOMPARE(
        core.info().buffering.cacheSpeedBytesPerSecond,
        qint64(3'000'000));
    QCOMPARE(core.info().buffering.cacheDurationSec, 8.25);
    QVERIFY(bufferingSpy.count() >= 5);

    core.onMpvPropertyChanged(
        QStringLiteral("paused-for-cache"), false);
    QCOMPARE(core.info().state, PlayerState::Playing);
    QVERIFY(!core.info().buffering.active);
}

void PlayerCoreLifecycleTests::loadErrorsAreReportedAfterIdle()
{
    PlayerCore core;
    QSignalSpy errorSpy(&core, &PlayerCore::playbackError);
    core.m_info.state = PlayerState::Starting;
    core.m_info.currentUrl =
        QUrl::fromLocalFile(QStringLiteral("Z:/missing.mkv"));

    MpvEndFileInfo end;
    end.reason = MPV_END_FILE_REASON_ERROR;
    end.errorCode = MPV_ERROR_LOADING_FAILED;
    end.errorMessage = QStringLiteral("loading failed");
    core.onMpvFileEnded(end);

    QCOMPARE(core.info().state, PlayerState::Starting);
    QCOMPARE(errorSpy.count(), 0);

    core.onMpvPropertyChanged(
        QStringLiteral("idle-active"), true);
    QCOMPARE(core.info().state, PlayerState::Idle);
    QCOMPARE(errorSpy.count(), 1);
}

void PlayerCoreLifecycleTests::realMediaReachesEofAndRestarts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath =
        directory.filePath(QStringLiteral("silent.wav"));
    QVERIFY(createSilentWaveFile(mediaPath));

    PlayerCore core;
    core.m_mpv->setString(
        QStringLiteral("ao"), QStringLiteral("null"));
    core.m_mpv->setFlag(QStringLiteral("mute"), true);
    QTRY_VERIFY_WITH_TIMEOUT(
        core.m_mpv->getFlag(QStringLiteral("mute")), 5000);

    QSignalSpy loadedSpy(&core, &PlayerCore::mediaLoaded);
    QSignalSpy eofSpy(&core, &PlayerCore::eofChanged);
    core.openUrl(QUrl::fromLocalFile(mediaPath));

    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        core.info().state, PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(core.info().eofReached, 7000);
    QCOMPARE(core.info().state, PlayerState::Paused);
    QCOMPARE(
        core.info().videoPositionSec,
        core.info().videoDurationSec);

    core.resume();
    QTRY_VERIFY_WITH_TIMEOUT(!core.info().eofReached, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        core.info().state, PlayerState::Playing, 5000);
    QVERIFY(eofSpy.count() >= 2);
}

void PlayerCoreLifecycleTests::
    nativePlaylistBuildsAuthoritativeQueueState()
{
    PlayerCore core;
    QSignalSpy playlistSpy(&core, &PlayerCore::playlistChanged);
    const QVariantList native{
        QVariantMap{
            {QStringLiteral("id"), qint64(41)},
            {QStringLiteral("filename"), QStringLiteral("C:/Media/one.mkv")},
            {QStringLiteral("current"), true},
            {QStringLiteral("playing"), true}},
        QVariantMap{
            {QStringLiteral("id"), qint64(42)},
            {QStringLiteral("filename"), QStringLiteral("https://example.com/two")},
            {QStringLiteral("title"), QStringLiteral("Remote Two")}}};

    core.onMpvPropertyChanged(QStringLiteral("playlist"), native);

    QCOMPARE(core.info().playlist.size(), 2);
    QCOMPARE(core.info().playlist.currentIndex, 0);
    QCOMPARE(core.info().playlist.items[0].id, qint64(41));
    QCOMPARE(
        core.info().playlist.items[0].displayName,
        QStringLiteral("one.mkv"));
    QVERIFY(core.info().playlist.items[0].current);
    QVERIFY(core.info().playlist.items[1].networkResource);
    QCOMPARE(
        core.info().playlist.items[1].displayName,
        QStringLiteral("Remote Two"));
    QCOMPARE(playlistSpy.count(), 1);
}

void PlayerCoreLifecycleTests::realPlaylistSupportsPlayRemoveAndClear()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QList<QUrl> urls;
    for (int index = 1; index <= 3; ++index) {
        const QString path = directory.filePath(
            QStringLiteral("track%1.wav").arg(index));
        QVERIFY(createSilentWaveFile(path));
        urls.append(QUrl::fromLocalFile(path));
    }

    PlayerCore core;
    core.m_mpv->setString(
        QStringLiteral("ao"), QStringLiteral("null"));
    core.m_mpv->setFlag(QStringLiteral("mute"), true);
    QSignalSpy playlistSpy(&core, &PlayerCore::playlistChanged);
    core.openUrls(urls);

    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.size(), 3, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.currentIndex, 0, 5000);

    core.playPlaylistIndex(2);
    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.currentIndex, 2, 5000);

    core.removePlaylistItems({0});
    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.size(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.currentIndex, 1, 5000);

    core.clearPlaylist();
    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.size(), 1, 5000);
    QCOMPARE(core.info().playlist.currentIndex, 0);
    QVERIFY(playlistSpy.count() >= 3);
}

void PlayerCoreLifecycleTests::
    singleLocalFileAutoloadsSiblingsAndNextPlaysThem()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString track2 =
        directory.filePath(QStringLiteral("episode2.wav"));
    const QString track10 =
        directory.filePath(QStringLiteral("episode10.wav"));
    QVERIFY(createSilentWaveFile(track2));
    QVERIFY(createSilentWaveFile(track10));

    PlayerCore core;
    core.m_mpv->setString(
        QStringLiteral("ao"), QStringLiteral("null"));
    core.m_mpv->setFlag(QStringLiteral("mute"), true);
    core.openUrl(QUrl::fromLocalFile(track2));

    QTRY_COMPARE_WITH_TIMEOUT(core.info().playlist.size(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        core.info().playlist.currentIndex, 0, 5000);
    QCOMPARE(
        core.info().playlist.items[0].url.toLocalFile(), track2);
    QCOMPARE(
        core.info().playlist.items[1].url.toLocalFile(), track10);

    core.navigateInPlaylist(true);
    QTRY_COMPARE_WITH_TIMEOUT(
        core.info().playlist.currentIndex, 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        core.info().currentUrl.toLocalFile(), track10, 5000);
}

QTEST_GUILESS_MAIN(PlayerCoreLifecycleTests)

#include "PlayerCoreLifecycleTests.moc"

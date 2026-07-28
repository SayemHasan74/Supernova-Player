#include "PlayerCore/MediaTrack.h"
#include "PlayerCore/QuickSettings.h"

#include <QTest>

class MediaTrackTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesAuthoritativeMpvTrackList();
    void formatsIinaStyleTrackTitles();
    void keepsPrimaryAndSecondarySelectionsIndependent();
    void parsesAudioOutputDevices();
    void parsesAndClassifiesMpvFilters();
};

void MediaTrackTests::parsesAuthoritativeMpvTrackList()
{
    const QVariantList node{
        QVariantMap{
            {QStringLiteral("id"), 1},
            {QStringLiteral("type"), QStringLiteral("video")},
            {QStringLiteral("default"), true},
            {QStringLiteral("forced"), false},
            {QStringLiteral("image"), false},
            {QStringLiteral("selected"), true},
            {QStringLiteral("external"), false},
            {QStringLiteral("codec"), QStringLiteral("hevc")},
            {QStringLiteral("demux-w"), 1920},
            {QStringLiteral("demux-h"), 1080},
            {QStringLiteral("demux-fps"), 23.976}},
        QVariantMap{
            {QStringLiteral("id"), 3},
            {QStringLiteral("type"), QStringLiteral("sub")},
            {QStringLiteral("default"), false},
            {QStringLiteral("forced"), true},
            {QStringLiteral("image"), false},
            {QStringLiteral("selected"), false},
            {QStringLiteral("external"), true},
            {QStringLiteral("title"), QStringLiteral("Signs")},
            {QStringLiteral("lang"), QStringLiteral("en")},
            {QStringLiteral("codec"), QStringLiteral("ass")},
            {QStringLiteral("external-filename"),
             QStringLiteral("C:/media/signs.ass")}}};

    const QList<MediaTrack> tracks = MediaTrack::fromMpvNode(node);

    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks[0].type, MediaTrackType::Video);
    QCOMPARE(tracks[0].width, 1920);
    QCOMPARE(tracks[0].height, 1080);
    QVERIFY(tracks[0].isDefault);
    QCOMPARE(tracks[1].type, MediaTrackType::Subtitle);
    QVERIFY(tracks[1].isExternal);
    QVERIFY(tracks[1].isForced);
    QVERIFY(tracks[1].isAssSubtitle());
    QCOMPARE(
        tracks[1].externalFilename,
        QStringLiteral("C:/media/signs.ass"));
}

void MediaTrackTests::formatsIinaStyleTrackTitles()
{
    MediaTrack audio;
    audio.type = MediaTrackType::Audio;
    audio.title = QStringLiteral("Surround");
    audio.language = QStringLiteral("en");
    audio.codec = QStringLiteral("eac3");
    audio.channelCount = 6;
    audio.sampleRate = 48000;
    audio.isDefault = true;

    const QString title = audio.readableTitle();
    QVERIFY(title.contains(QStringLiteral("Surround")));
    QVERIFY(title.contains(QStringLiteral("eac3")));
    QVERIFY(title.contains(QStringLiteral("6ch")));
    QVERIFY(title.contains(QStringLiteral("48kHz")));
    QVERIFY(title.contains(QStringLiteral("Default")));
}

void MediaTrackTests::keepsPrimaryAndSecondarySelectionsIndependent()
{
    MediaTrackState state;
    MediaTrack first;
    first.id = 2;
    first.type = MediaTrackType::Subtitle;
    MediaTrack second = first;
    second.id = 4;
    state.subtitleTracks = {first, second};
    state.selectedSubtitleId = 2;
    state.selectedSecondarySubtitleId = 4;

    QVERIFY(state.selectedSubtitle(true));
    QVERIFY(state.selectedSubtitle(false));
    QCOMPARE(state.selectedSubtitle(true)->id, 2);
    QCOMPARE(state.selectedSubtitle(false)->id, 4);
}

void MediaTrackTests::parsesAudioOutputDevices()
{
    const QVariantList node{
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("auto")},
            {QStringLiteral("description"), QStringLiteral("Auto-select")}},
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("wasapi/Speakers")},
            {QStringLiteral("description"), QStringLiteral("Speakers")}},
        QVariantMap{
            {QStringLiteral("description"), QStringLiteral("Invalid")}}};

    const QList<AudioOutputDevice> devices =
        AudioOutputDevice::fromMpvNode(node);

    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].name, QStringLiteral("auto"));
    QCOMPARE(
        devices[1].displayName(),
        QStringLiteral("[Speakers] wasapi/Speakers"));
}

void MediaTrackTests::parsesAndClassifiesMpvFilters()
{
    const QVariantList node{
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("crop")},
            {QStringLiteral("label"), QStringLiteral("supernova_crop")},
            {QStringLiteral("params"),
             QVariantMap{
                 {QStringLiteral("w"), QStringLiteral("1920")},
                 {QStringLiteral("h"), QStringLiteral("800")}}}},
        QVariantMap{
            {QStringLiteral("name"), QStringLiteral("deband")},
            {QStringLiteral("label"), QStringLiteral("external_filter")}}};

    const QList<MediaFilterInfo> filters =
        mediaFiltersFromMpvNode(node);

    QCOMPARE(filters.size(), 2);
    QVERIFY(filters[0].managed);
    QVERIFY(filters[0].description.startsWith(
        QStringLiteral("@supernova_crop:crop=")));
    QVERIFY(filters[0].description.contains(QStringLiteral("w=1920")));
    QVERIFY(!filters[1].managed);
}

QTEST_APPLESS_MAIN(MediaTrackTests)

#include "MediaTrackTests.moc"

#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

enum class MediaTrackType {
    Video,
    Audio,
    Subtitle,
};

struct MediaTrack {
    int id = 0;
    MediaTrackType type = MediaTrackType::Video;
    int sourceId = -1;
    QString title;
    QString language;
    QString codec;
    QString externalFilename;
    QString decoderDescription;
    QString channelLayout;
    int width = 0;
    int height = 0;
    int channelCount = 0;
    int sampleRate = 0;
    double frameRate = 0.0;
    bool isDefault = false;
    bool isForced = false;
    bool isImage = false;
    bool isSelected = false;
    bool isExternal = false;
    bool isAlbumArt = false;

    [[nodiscard]] QString readableLanguage() const;
    [[nodiscard]] QString readableTitle(
        bool includeLanguage = true) const;
    [[nodiscard]] bool isImageSubtitle() const noexcept;
    [[nodiscard]] bool isAssSubtitle() const noexcept;

    [[nodiscard]] static QList<MediaTrack> fromMpvNode(
        const QVariant &node);

    bool operator==(const MediaTrack &) const = default;
};

struct MediaTrackState {
    QList<MediaTrack> videoTracks;
    QList<MediaTrack> audioTracks;
    QList<MediaTrack> subtitleTracks;
    int selectedVideoId = 0;
    int selectedAudioId = 0;
    int selectedSubtitleId = 0;
    int selectedSecondarySubtitleId = 0;

    [[nodiscard]] const MediaTrack *selectedSubtitle(
        bool primary) const noexcept;

    bool operator==(const MediaTrackState &) const = default;
};

struct SubtitleSettings {
    bool primaryVisible = true;
    bool secondaryVisible = true;
    double primaryDelay = 0.0;
    double secondaryDelay = 0.0;
    int primaryPosition = 100;
    int secondaryPosition = 100;
    double scale = 1.0;
    QString font;
    double fontSize = 55.0;
    QString textColor;
    QString backgroundColor;
    QString borderColor;
    double borderSize = 3.0;
    QString assOverride;

    bool operator==(const SubtitleSettings &) const = default;
};

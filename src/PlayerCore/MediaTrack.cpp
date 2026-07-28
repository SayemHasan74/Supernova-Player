#include "PlayerCore/MediaTrack.h"

#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {
QString prettyNumber(double value)
{
    if (std::abs(value - std::round(value)) < 0.01) {
        return QString::number(qRound(value));
    }
    return QString::number(value, 'f', 2)
        .remove(QRegularExpression(QStringLiteral("0+$")))
        .remove(QRegularExpression(QStringLiteral("\\.$")));
}

bool nodeFlag(const QVariantMap &map, const char *name)
{
    return map.value(QString::fromLatin1(name)).toBool();
}

int nodeInt(const QVariantMap &map, const char *name, int fallback = 0)
{
    const QVariant value = map.value(QString::fromLatin1(name));
    return value.isValid() ? value.toInt() : fallback;
}
}

QString MediaTrack::readableLanguage() const
{
    if (language.isEmpty() || language == QStringLiteral("und")) {
        return {};
    }
    const QLocale locale(language);
    const QString nativeName = locale.nativeLanguageName();
    if (!nativeName.isEmpty()) {
        QString result = nativeName;
        result[0] = result[0].toUpper();
        return result;
    }
    return language;
}

QString MediaTrack::readableTitle(bool includeLanguage) const
{
    QStringList details;
    if (!codec.isEmpty()) {
        details.append(codec);
    }
    if (type == MediaTrackType::Video) {
        if (width > 0 && height > 0) {
            details.append(
                QStringLiteral("%1×%2").arg(width).arg(height));
        }
        if (frameRate > 0.0) {
            details.append(
                QStringLiteral("%1fps").arg(prettyNumber(frameRate)));
        }
    } else if (type == MediaTrackType::Audio) {
        if (channelCount > 0) {
            details.append(
                QStringLiteral("%1ch").arg(channelCount));
        }
        if (sampleRate > 0) {
            details.append(QStringLiteral("%1kHz").arg(
                prettyNumber(static_cast<double>(sampleRate) / 1000.0)));
        }
    }
    if (isDefault) {
        details.append(QObject::tr("Default"));
    }
    if (isForced) {
        details.append(QObject::tr("Forced"));
    }
    if (isExternal) {
        details.append(QObject::tr("External"));
    }

    QStringList components;
    const QString localizedLanguage = readableLanguage();
    if (includeLanguage && !localizedLanguage.isEmpty()) {
        components.append(
            QStringLiteral("[%1]").arg(localizedLanguage));
    }
    components.append(
        title.isEmpty() ? QObject::tr("No Title") : title);
    if (!details.isEmpty()) {
        components.append(
            QStringLiteral("(%1)").arg(details.join(QStringLiteral(", "))));
    }
    return components.join(QLatin1Char(' '));
}

bool MediaTrack::isImageSubtitle() const noexcept
{
    return type == MediaTrackType::Subtitle
        && (codec == QStringLiteral("hdmv_pgs_subtitle")
            || codec == QStringLiteral("dvb_subtitle"));
}

bool MediaTrack::isAssSubtitle() const noexcept
{
    return type == MediaTrackType::Subtitle
        && codec == QStringLiteral("ass");
}

QList<MediaTrack> MediaTrack::fromMpvNode(const QVariant &node)
{
    QList<MediaTrack> tracks;
    const QVariantList entries = node.toList();
    tracks.reserve(entries.size());
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        const QString rawType = map.value(QStringLiteral("type")).toString();
        MediaTrackType type;
        if (rawType == QStringLiteral("video")) {
            type = MediaTrackType::Video;
        } else if (rawType == QStringLiteral("audio")) {
            type = MediaTrackType::Audio;
        } else if (rawType == QStringLiteral("sub")) {
            type = MediaTrackType::Subtitle;
        } else {
            continue;
        }
        if (!map.contains(QStringLiteral("id"))) {
            continue;
        }

        MediaTrack track;
        track.id = nodeInt(map, "id");
        track.type = type;
        track.sourceId = nodeInt(map, "src-id", -1);
        track.title = map.value(QStringLiteral("title")).toString();
        track.language = map.value(QStringLiteral("lang")).toString();
        track.codec = map.value(QStringLiteral("codec")).toString();
        track.externalFilename =
            map.value(QStringLiteral("external-filename")).toString();
        track.decoderDescription =
            map.value(QStringLiteral("decoder-desc")).toString();
        track.channelLayout =
            map.value(QStringLiteral("demux-channels")).toString();
        track.width = nodeInt(map, "demux-w");
        track.height = nodeInt(map, "demux-h");
        track.channelCount = nodeInt(map, "demux-channel-count");
        track.sampleRate = nodeInt(map, "demux-samplerate");
        track.frameRate =
            map.value(QStringLiteral("demux-fps")).toDouble();
        track.isDefault = nodeFlag(map, "default");
        track.isForced = nodeFlag(map, "forced");
        track.isImage = nodeFlag(map, "image");
        track.isSelected = nodeFlag(map, "selected");
        track.isExternal = nodeFlag(map, "external");
        track.isAlbumArt = nodeFlag(map, "albumart");
        tracks.append(std::move(track));
    }
    return tracks;
}

const MediaTrack *MediaTrackState::selectedSubtitle(
    bool primary) const noexcept
{
    const int selectedId =
        primary ? selectedSubtitleId : selectedSecondarySubtitleId;
    const auto found = std::find_if(
        subtitleTracks.cbegin(), subtitleTracks.cend(),
        [selectedId](const MediaTrack &track) {
            return track.id == selectedId;
        });
    return found == subtitleTracks.cend() ? nullptr : &*found;
}

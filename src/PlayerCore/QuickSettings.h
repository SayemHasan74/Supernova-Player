#pragma once

#include <QList>
#include <QString>
#include <QVariant>

#include <array>

struct MediaFilterInfo {
    QString name;
    QString label;
    QString description;
    bool managed = false;

    bool operator==(const MediaFilterInfo &) const = default;
};

struct VideoQuickSettings {
    QString aspectRatio = QStringLiteral("Default");
    QString crop = QStringLiteral("None");
    int rotation = 0;
    bool hardwareDecoding = true;
    bool deinterlace = false;
    bool flipped = false;
    bool mirrored = false;
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int gamma = 0;
    int hue = 0;
    QList<MediaFilterInfo> filters;

    bool operator==(const VideoQuickSettings &) const = default;
};

struct AudioOutputDevice {
    QString name;
    QString description;

    [[nodiscard]] QString displayName() const;
    [[nodiscard]] static QList<AudioOutputDevice> fromMpvNode(
        const QVariant &node);

    bool operator==(const AudioOutputDevice &) const = default;
};

struct AudioQuickSettings {
    QList<AudioOutputDevice> devices;
    QString selectedDevice = QStringLiteral("auto");
    QString channels = QStringLiteral("auto-safe");
    double delay = 0.0;
    std::array<double, 10> equalizer{};
    QList<MediaFilterInfo> filters;

    bool operator==(const AudioQuickSettings &) const = default;
};

[[nodiscard]] QList<MediaFilterInfo> mediaFiltersFromMpvNode(
    const QVariant &node);

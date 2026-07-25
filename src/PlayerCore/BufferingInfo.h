#pragma once

#include <QMetaType>

struct BufferingInfo {
    bool active = false;
    int percent = 0;
    qint64 cacheUsedBytes = 0;
    qint64 cacheSpeedBytesPerSecond = 0;
    double cacheDurationSec = 0.0;
    bool cacheIdle = false;

    friend bool operator==(
        const BufferingInfo &, const BufferingInfo &) = default;
};

Q_DECLARE_METATYPE(BufferingInfo)

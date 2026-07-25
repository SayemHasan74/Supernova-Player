#pragma once

#include "PlayerCore/PlayerState.h"

#include <QString>
#include <QUrl>

struct PlaybackInfo {
    PlayerState state = PlayerState::Idle;

    QUrl currentUrl;
    bool isNetworkResource = false;

    int videoWidth = 0;
    int videoHeight = 0;

    double videoPositionSec = 0.0;
    double videoDurationSec = 0.0;

    double volume = 100.0;
    bool isMuted = false;
    double playSpeed = 1.0;

    bool justOpenedFile = false;
};

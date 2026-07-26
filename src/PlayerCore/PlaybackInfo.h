#pragma once

#include "PlayerCore/BufferingInfo.h"
#include "PlayerCore/PlayerState.h"
#include "PlayerCore/PlaylistState.h"

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
    bool eofReached = false;
    bool isSeeking = false;

    double volume = 100.0;
    bool isMuted = false;
    double playSpeed = 1.0;

    bool hasVideo = false;
    bool hasAudio = false;
    BufferingInfo buffering;
    PlaylistState playlist;

    bool justOpenedFile = false;
};

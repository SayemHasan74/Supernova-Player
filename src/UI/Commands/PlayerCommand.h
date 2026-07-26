#pragma once

#include <QList>
#include <QString>
#include <QStringList>

enum class PlayerCommand {
    OpenFile,
    OpenFolder,
    CloseWindow,
    QuitApplication,
    TogglePause,
    Stop,
    PreviousMedia,
    NextMedia,
    SeekBackward,
    SeekForward,
    JumpToBeginning,
    FrameBackward,
    FrameForward,
    VolumeDown,
    VolumeUp,
    ToggleMute,
    SpeedDown,
    SpeedUp,
    ResetSpeed,
    TakeScreenshot,
    ToggleFullScreen,
    ToggleAlwaysOnTop,
    ToggleProgressMode,
    TogglePlaylist,
    PauseAndMinimize,
};

enum class PlayerMenu {
    File,
    Playback,
    Video,
    Audio,
    Window,
};

struct PlayerCommandDefinition {
    PlayerCommand command;
    PlayerMenu menu;
    QString title;
    QStringList shortcuts;
    bool requiresMedia = false;
    bool checkable = false;
};

[[nodiscard]] const QList<PlayerCommandDefinition> &
playerCommandDefinitions();

[[nodiscard]] const PlayerCommandDefinition *
playerCommandDefinition(PlayerCommand command);

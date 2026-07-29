#pragma once

#include <QList>
#include <QString>
#include <QStringList>

enum class PlayerCommand {
    OpenFile,
    OpenUrl,
    OpenFolder,
    ImportPlaylist,
    SavePlaylist,
    NewPlayerWindow,
    CloseWindow,
    QuitApplication,
    TogglePause,
    Stop,
    PreviousMedia,
    NextMedia,
    PreviousChapter,
    NextChapter,
    ToggleAbLoop,
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
    OpenScreenshotFolder,
    FindOnlineSubtitles,
    ToggleFullScreen,
    ToggleAlwaysOnTop,
    ToggleMusicMode,
    TogglePictureInPicture,
    ToggleProgressMode,
    TogglePlaylist,
    ToggleMediaSettings,
    ShowPlaybackHistory,
    ShowMediaInspector,
    ShowPreferences,
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

[[nodiscard]] QString playerCommandIdentifier(PlayerCommand command);
[[nodiscard]] bool playerCommandFromIdentifier(
    const QString &identifier, PlayerCommand *command);

#include "UI/Commands/PlayerCommand.h"

const QList<PlayerCommandDefinition> &playerCommandDefinitions()
{
    static const QList<PlayerCommandDefinition> definitions{
        {PlayerCommand::OpenFile, PlayerMenu::File, QStringLiteral("Open File…"),
         {QStringLiteral("Ctrl+O")}},
        {PlayerCommand::OpenFolder, PlayerMenu::File, QStringLiteral("Open Folder…"),
         {QStringLiteral("Ctrl+Shift+O")}},
        {PlayerCommand::ImportPlaylist, PlayerMenu::File,
         QStringLiteral("Import Playlist…"),
         {QStringLiteral("Ctrl+Shift+I")}},
        {PlayerCommand::SavePlaylist, PlayerMenu::File,
         QStringLiteral("Save Playlist…"),
         {QStringLiteral("Ctrl+Shift+S")}, true},
        {PlayerCommand::CloseWindow, PlayerMenu::File, QStringLiteral("Close Window"),
         {QStringLiteral("Ctrl+W")}},
        {PlayerCommand::QuitApplication, PlayerMenu::File, QStringLiteral("Quit"),
         {QStringLiteral("Ctrl+Q")}},

        {PlayerCommand::TogglePause, PlayerMenu::Playback,
         QStringLiteral("Play / Pause"), {QStringLiteral("Space")}, true},
        {PlayerCommand::Stop, PlayerMenu::Playback, QStringLiteral("Stop"),
         {QStringLiteral("Ctrl+.")}, true},
        {PlayerCommand::PreviousMedia, PlayerMenu::Playback,
         QStringLiteral("Previous Media"), {QStringLiteral("PgUp")}, true},
        {PlayerCommand::NextMedia, PlayerMenu::Playback,
         QStringLiteral("Next Media"), {QStringLiteral("PgDown")}, true},
        {PlayerCommand::PreviousChapter, PlayerMenu::Playback,
         QStringLiteral("Previous Chapter"),
         {QStringLiteral("Ctrl+PgUp")}, true},
        {PlayerCommand::NextChapter, PlayerMenu::Playback,
         QStringLiteral("Next Chapter"),
         {QStringLiteral("Ctrl+PgDown")}, true},
        {PlayerCommand::ToggleAbLoop, PlayerMenu::Playback,
         QStringLiteral("A–B Loop"), {QStringLiteral("L")}, true, true},
        {PlayerCommand::SeekBackward, PlayerMenu::Playback,
         QStringLiteral("Seek Backward 5 Seconds"),
         {QStringLiteral("Left")}, true},
        {PlayerCommand::SeekForward, PlayerMenu::Playback,
         QStringLiteral("Seek Forward 5 Seconds"),
         {QStringLiteral("Right")}, true},
        {PlayerCommand::JumpToBeginning, PlayerMenu::Playback,
         QStringLiteral("Jump to Beginning"), {QStringLiteral("Home")}, true},
        {PlayerCommand::FrameBackward, PlayerMenu::Playback,
         QStringLiteral("Previous Frame"), {QStringLiteral(",")}, true},
        {PlayerCommand::FrameForward, PlayerMenu::Playback,
         QStringLiteral("Next Frame"), {QStringLiteral(".")}, true},
        {PlayerCommand::SpeedDown, PlayerMenu::Playback,
         QStringLiteral("Decrease Speed"), {QStringLiteral("[")}, true},
        {PlayerCommand::SpeedUp, PlayerMenu::Playback,
         QStringLiteral("Increase Speed"), {QStringLiteral("]")}, true},
        {PlayerCommand::ResetSpeed, PlayerMenu::Playback,
         QStringLiteral("Reset Speed"), {QStringLiteral("Backspace")}, true},

        {PlayerCommand::TakeScreenshot, PlayerMenu::Video,
         QStringLiteral("Take Screenshot"), {QStringLiteral("S")}, true},
        {PlayerCommand::ToggleFullScreen, PlayerMenu::Video,
         QStringLiteral("Enter Full Screen"),
         {QStringLiteral("F"), QStringLiteral("F11"),
          QStringLiteral("Alt+Return")},
         false, true},

        {PlayerCommand::VolumeDown, PlayerMenu::Audio,
         QStringLiteral("Decrease Volume"), {QStringLiteral("Down")}, true},
        {PlayerCommand::VolumeUp, PlayerMenu::Audio,
         QStringLiteral("Increase Volume"), {QStringLiteral("Up")}, true},
        {PlayerCommand::ToggleMute, PlayerMenu::Audio, QStringLiteral("Mute"),
         {QStringLiteral("M")}, true, true},

        {PlayerCommand::ToggleAlwaysOnTop, PlayerMenu::Window,
         QStringLiteral("Always on Top"),
         {QStringLiteral("Ctrl+T")}, false, true},
        {PlayerCommand::ToggleProgressMode, PlayerMenu::Window,
         QStringLiteral("Progress-Only Mode"), {}, false, true},
        {PlayerCommand::TogglePlaylist, PlayerMenu::Window,
         QStringLiteral("Playlist"),
         {QStringLiteral("Ctrl+Shift+P")}, false, true},
        {PlayerCommand::ToggleMediaSettings, PlayerMenu::Window,
         QStringLiteral("Quick Settings"), {}, true, true},
        {PlayerCommand::PauseAndMinimize, PlayerMenu::Window,
         QStringLiteral("Pause and Minimize"),
         {QStringLiteral("Escape")}},
    };
    return definitions;
}

const PlayerCommandDefinition *playerCommandDefinition(
    PlayerCommand command)
{
    const auto &definitions = playerCommandDefinitions();
    for (const auto &definition : definitions) {
        if (definition.command == command) {
            return &definition;
        }
    }
    return nullptr;
}

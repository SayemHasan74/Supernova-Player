#include "UI/Commands/PlayerCommand.h"

const QList<PlayerCommandDefinition> &playerCommandDefinitions()
{
    static const QList<PlayerCommandDefinition> definitions{
        {PlayerCommand::OpenFile, PlayerMenu::File, QStringLiteral("Open File…"),
         {QKeySequence::Open}},
        {PlayerCommand::OpenFolder, PlayerMenu::File, QStringLiteral("Open Folder…"),
         {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)}},
        {PlayerCommand::CloseWindow, PlayerMenu::File, QStringLiteral("Close Window"),
         {QKeySequence(Qt::CTRL | Qt::Key_W)}},
        {PlayerCommand::QuitApplication, PlayerMenu::File, QStringLiteral("Quit"),
         {QKeySequence(Qt::CTRL | Qt::Key_Q)}},

        {PlayerCommand::TogglePause, PlayerMenu::Playback,
         QStringLiteral("Play / Pause"), {QKeySequence(Qt::Key_Space)}, true},
        {PlayerCommand::Stop, PlayerMenu::Playback, QStringLiteral("Stop"),
         {QKeySequence(Qt::CTRL | Qt::Key_Period)}, true},
        {PlayerCommand::PreviousMedia, PlayerMenu::Playback,
         QStringLiteral("Previous Media"), {QKeySequence(Qt::Key_PageUp)}, true},
        {PlayerCommand::NextMedia, PlayerMenu::Playback,
         QStringLiteral("Next Media"), {QKeySequence(Qt::Key_PageDown)}, true},
        {PlayerCommand::SeekBackward, PlayerMenu::Playback,
         QStringLiteral("Seek Backward 5 Seconds"),
         {QKeySequence(Qt::Key_Left)}, true},
        {PlayerCommand::SeekForward, PlayerMenu::Playback,
         QStringLiteral("Seek Forward 5 Seconds"),
         {QKeySequence(Qt::Key_Right)}, true},
        {PlayerCommand::JumpToBeginning, PlayerMenu::Playback,
         QStringLiteral("Jump to Beginning"), {QKeySequence(Qt::Key_Home)}, true},
        {PlayerCommand::FrameBackward, PlayerMenu::Playback,
         QStringLiteral("Previous Frame"), {QKeySequence(Qt::Key_Comma)}, true},
        {PlayerCommand::FrameForward, PlayerMenu::Playback,
         QStringLiteral("Next Frame"), {QKeySequence(Qt::Key_Period)}, true},
        {PlayerCommand::SpeedDown, PlayerMenu::Playback,
         QStringLiteral("Decrease Speed"), {QKeySequence(Qt::Key_BracketLeft)}, true},
        {PlayerCommand::SpeedUp, PlayerMenu::Playback,
         QStringLiteral("Increase Speed"), {QKeySequence(Qt::Key_BracketRight)}, true},
        {PlayerCommand::ResetSpeed, PlayerMenu::Playback,
         QStringLiteral("Reset Speed"), {QKeySequence(Qt::Key_Backspace)}, true},

        {PlayerCommand::TakeScreenshot, PlayerMenu::Video,
         QStringLiteral("Take Screenshot"), {QKeySequence(Qt::Key_S)}, true},
        {PlayerCommand::ToggleFullScreen, PlayerMenu::Video,
         QStringLiteral("Enter Full Screen"),
         {QKeySequence(Qt::Key_F), QKeySequence(Qt::Key_F11),
          QKeySequence(Qt::ALT | Qt::Key_Return)},
         false, true},

        {PlayerCommand::VolumeDown, PlayerMenu::Audio,
         QStringLiteral("Decrease Volume"), {QKeySequence(Qt::Key_Down)}, true},
        {PlayerCommand::VolumeUp, PlayerMenu::Audio,
         QStringLiteral("Increase Volume"), {QKeySequence(Qt::Key_Up)}, true},
        {PlayerCommand::ToggleMute, PlayerMenu::Audio, QStringLiteral("Mute"),
         {QKeySequence(Qt::Key_M)}, true, true},

        {PlayerCommand::ToggleAlwaysOnTop, PlayerMenu::Window,
         QStringLiteral("Always on Top"),
         {QKeySequence(Qt::CTRL | Qt::Key_T)}, false, true},
        {PlayerCommand::ToggleProgressMode, PlayerMenu::Window,
         QStringLiteral("Progress-Only Mode"), {}, false, true},
        {PlayerCommand::PauseAndMinimize, PlayerMenu::Window,
         QStringLiteral("Pause and Minimize"),
         {QKeySequence(Qt::Key_Escape)}},
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

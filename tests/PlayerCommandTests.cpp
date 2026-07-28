#include "UI/Commands/PlayerCommand.h"

#include <QSet>
#include <QTest>

class PlayerCommandTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesCompleteUniqueCommandSet();
    void keepsShortcutsUnambiguous();
    void classifiesMediaDependentCommands();
};

void PlayerCommandTests::exposesCompleteUniqueCommandSet()
{
    const auto &definitions = playerCommandDefinitions();
    QCOMPARE(definitions.size(), 31);

    QSet<int> commands;
    QSet<int> menus;
    for (const auto &definition : definitions) {
        QVERIFY(!definition.title.trimmed().isEmpty());
        const int command = static_cast<int>(definition.command);
        QVERIFY(!commands.contains(command));
        commands.insert(command);
        menus.insert(static_cast<int>(definition.menu));
        QCOMPARE(
            playerCommandDefinition(definition.command),
            &definition);
    }
    QCOMPARE(menus.size(), 5);
}

void PlayerCommandTests::keepsShortcutsUnambiguous()
{
    QSet<QString> shortcuts;
    for (const auto &definition : playerCommandDefinitions()) {
        for (const auto &shortcut : definition.shortcuts) {
            const QString portable = shortcut;
            QVERIFY2(!portable.isEmpty(), qPrintable(definition.title));
            QVERIFY2(
                !shortcuts.contains(portable),
                qPrintable(QStringLiteral("Duplicate shortcut: %1")
                               .arg(portable)));
            shortcuts.insert(portable);
        }
    }
}

void PlayerCommandTests::classifiesMediaDependentCommands()
{
    const auto *seek =
        playerCommandDefinition(PlayerCommand::SeekForward);
    const auto *volume =
        playerCommandDefinition(PlayerCommand::VolumeUp);
    const auto *open =
        playerCommandDefinition(PlayerCommand::OpenFile);
    const auto *fullScreen =
        playerCommandDefinition(PlayerCommand::ToggleFullScreen);

    QVERIFY(seek && seek->requiresMedia);
    QVERIFY(volume && volume->requiresMedia);
    QVERIFY(open && !open->requiresMedia);
    QVERIFY(fullScreen && !fullScreen->requiresMedia);
}

QTEST_APPLESS_MAIN(PlayerCommandTests)

#include "PlayerCommandTests.moc"

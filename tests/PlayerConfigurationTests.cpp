#include "Preferences/PlayerConfiguration.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class PlayerConfigurationTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesApplicationAndMpvBindings();
    void discoversNativeMpvProfiles();
    void builtInBindingsCoverPlayerLevelCommands();
};

void PlayerConfigurationTests::parsesApplicationAndMpvBindings()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path =
        temporary.filePath(QStringLiteral("input.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "# normal comment\n"
        "#@supernova SPACE TogglePause\n"
        "Ctrl+r cycle-values video-rotate 0 90 180 270 # rotate\n"
        "broken\n");
    file.close();

    bool ok = false;
    const QList<ConfiguredKeyBinding> bindings =
        PlayerConfiguration::parseInputConf(path, &ok);

    QVERIFY(ok);
    QCOMPARE(bindings.size(), 2);
    QVERIFY(bindings[0].applicationCommand);
    QCOMPARE(bindings[0].action, QStringLiteral("TogglePause"));
    QVERIFY(!bindings[1].applicationCommand);
    QCOMPARE(
        bindings[1].action,
        QStringLiteral("cycle-values video-rotate 0 90 180 270"));
    QCOMPARE(bindings[1].comment, QStringLiteral("rotate"));
}

void PlayerConfigurationTests::discoversNativeMpvProfiles()
{
    const QString config = QStringLiteral(
        "hwdec=auto\n"
        "[cinema]\n"
        "profile-desc=Cinema\n"
        "[default]\n"
        "[network] # comment\n"
        "[cinema]\n");

    QCOMPARE(
        PlayerConfiguration::mpvProfiles(config),
        QStringList({QStringLiteral("cinema"),
                     QStringLiteral("network")}));
}

void PlayerConfigurationTests::builtInBindingsCoverPlayerLevelCommands()
{
    const QList<ConfiguredKeyBinding> bindings =
        PlayerConfiguration::defaultKeyBindings();
    const auto containsAction =
        [&bindings](const QString &action) {
            return std::any_of(
                bindings.cbegin(), bindings.cend(),
                [&action](const ConfiguredKeyBinding &binding) {
                    return binding.applicationCommand
                        && binding.action == action;
                });
        };

    QVERIFY(containsAction(QStringLiteral("PauseAndMinimize")));
    QVERIFY(containsAction(QStringLiteral("ShowPlaybackHistory")));
    QVERIFY(containsAction(QStringLiteral("ShowPreferences")));
}

QTEST_APPLESS_MAIN(PlayerConfigurationTests)

#include "PlayerConfigurationTests.moc"

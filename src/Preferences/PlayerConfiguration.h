#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct ConfiguredKeyBinding {
    QString key;
    QString action;
    QString comment;
    bool applicationCommand = false;

    bool operator==(const ConfiguredKeyBinding &) const = default;
};

struct ConfiguredMpvOption {
    QString name;
    QString value;

    bool operator==(const ConfiguredMpvOption &) const = default;
};

class PlayerConfiguration final {
public:
    [[nodiscard]] static QString inputConfigDirectory();
    [[nodiscard]] static QString mpvConfigDirectory();
    [[nodiscard]] static QString mpvConfigFilePath();
    [[nodiscard]] static QString currentInputConfigName();
    static void setCurrentInputConfigName(const QString &name);

    [[nodiscard]] static QStringList inputConfigNames();
    [[nodiscard]] static bool isBuiltInInputConfig(const QString &name);
    [[nodiscard]] static QString inputConfigPath(const QString &name);
    [[nodiscard]] static QList<ConfiguredKeyBinding> defaultKeyBindings();
    [[nodiscard]] static QList<ConfiguredKeyBinding> parseInputConf(
        const QString &path, bool *ok = nullptr);
    [[nodiscard]] static QList<ConfiguredKeyBinding> currentKeyBindings();
    static bool saveInputConfig(
        const QString &name,
        const QList<ConfiguredKeyBinding> &bindings);
    static bool createInputConfig(
        const QString &name,
        const QList<ConfiguredKeyBinding> &bindings);
    static bool deleteInputConfig(const QString &name);
    static bool importInputConfig(
        const QString &sourcePath, QString *importedName = nullptr);

    [[nodiscard]] static QList<ConfiguredMpvOption> advancedOptions();
    static void setAdvancedOptions(
        const QList<ConfiguredMpvOption> &options);
    [[nodiscard]] static bool advancedSettingsEnabled();
    [[nodiscard]] static bool useCustomConfigDirectory();
    [[nodiscard]] static QString customConfigDirectory();

    [[nodiscard]] static QStringList mpvProfiles(
        const QString &configurationText);
    [[nodiscard]] static QStringList mpvProfilesFromDisk();
    [[nodiscard]] static QString readMpvConfig();
    static bool writeMpvConfig(const QString &text);

private:
    static void ensureConfigurationFiles();
    [[nodiscard]] static QString sanitizedConfigName(const QString &name);
};


#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

enum class PluginPermission {
    NetworkRequest,
    ShowOsd,
    ShowAlert,
    VideoOverlay,
    FileSystem,
};

struct PluginAuthor {
    QString name;
    QString email;
    QString url;
};

struct PluginManifest {
    QString name;
    PluginAuthor author;
    QString identifier;
    QString version;
    QString entry;
    QString globalEntry;
    QString description;
    QString preferencesPage;
    QString helpPage;
    QString githubRepo;
    int githubVersion = -1;
    QString sidebarTabName;
    QStringList allowedDomains;
    QList<PluginPermission> permissions;
    QJsonObject preferenceDefaults;
    QJsonObject localized;
    QJsonObject source;

    [[nodiscard]] bool hasPermission(PluginPermission permission) const;
    [[nodiscard]] static std::optional<PluginManifest> parse(
        const QByteArray &json, QString *error = nullptr);
};

[[nodiscard]] QString pluginPermissionName(PluginPermission permission);
[[nodiscard]] std::optional<PluginPermission> pluginPermissionFromName(
    const QString &name);
[[nodiscard]] bool pluginPermissionIsDangerous(
    PluginPermission permission);


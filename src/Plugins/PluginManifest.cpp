#include "Plugins/PluginManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {
void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QString requiredString(
    const QJsonObject &object, const QString &key, QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        fail(error, QStringLiteral("Info.json requires a non-empty “%1”.")
                        .arg(key));
        return {};
    }
    return value.toString().trimmed();
}
}

QString pluginPermissionName(PluginPermission permission)
{
    switch (permission) {
    case PluginPermission::NetworkRequest:
        return QStringLiteral("network-request");
    case PluginPermission::ShowOsd:
        return QStringLiteral("show-osd");
    case PluginPermission::ShowAlert:
        return QStringLiteral("show-alert");
    case PluginPermission::VideoOverlay:
        return QStringLiteral("video-overlay");
    case PluginPermission::FileSystem:
        return QStringLiteral("file-system");
    }
    return {};
}

std::optional<PluginPermission> pluginPermissionFromName(
    const QString &name)
{
    for (const PluginPermission permission : {
             PluginPermission::NetworkRequest,
             PluginPermission::ShowOsd,
             PluginPermission::ShowAlert,
             PluginPermission::VideoOverlay,
             PluginPermission::FileSystem}) {
        if (pluginPermissionName(permission) == name) {
            return permission;
        }
    }
    return std::nullopt;
}

bool pluginPermissionIsDangerous(PluginPermission permission)
{
    return permission == PluginPermission::NetworkRequest
        || permission == PluginPermission::FileSystem;
}

bool PluginManifest::hasPermission(PluginPermission permission) const
{
    return permissions.contains(permission);
}

std::optional<PluginManifest> PluginManifest::parse(
    const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        fail(error, QStringLiteral("Invalid Info.json: %1")
                        .arg(parseError.errorString()));
        return std::nullopt;
    }

    PluginManifest manifest;
    const QJsonObject object = document.object();
    manifest.source = object;
    manifest.name = requiredString(object, QStringLiteral("name"), error);
    manifest.identifier =
        requiredString(object, QStringLiteral("identifier"), error);
    manifest.version =
        requiredString(object, QStringLiteral("version"), error);
    manifest.entry =
        requiredString(object, QStringLiteral("entry"), error);
    if (manifest.name.isEmpty() || manifest.identifier.isEmpty()
        || manifest.version.isEmpty() || manifest.entry.isEmpty()) {
        return std::nullopt;
    }

    const QJsonObject author = object.value(
        QStringLiteral("author")).toObject();
    manifest.author.name =
        requiredString(author, QStringLiteral("name"), error);
    if (manifest.author.name.isEmpty()) {
        return std::nullopt;
    }
    manifest.author.email =
        author.value(QStringLiteral("email")).toString();
    manifest.author.url = author.value(QStringLiteral("url")).toString();

    static const QRegularExpression identifierPattern(
        QStringLiteral(R"(^([\w_-]+\.)+[\w_-]+$)"));
    if (!identifierPattern.match(manifest.identifier).hasMatch()) {
        fail(error, QStringLiteral(
                        "Plugin identifier must use reverse-domain notation."));
        return std::nullopt;
    }

    manifest.globalEntry =
        object.value(QStringLiteral("globalEntry")).toString();
    manifest.description =
        object.value(QStringLiteral("description")).toString();
    manifest.preferencesPage =
        object.value(QStringLiteral("preferencesPage")).toString();
    manifest.helpPage =
        object.value(QStringLiteral("helpPage")).toString();
    manifest.githubRepo =
        object.value(QStringLiteral("ghRepo")).toString();
    manifest.githubVersion =
        object.value(QStringLiteral("ghVersion")).toInt(-1);
    manifest.preferenceDefaults =
        object.value(QStringLiteral("preferenceDefaults")).toObject();
    manifest.localized =
        object.value(QStringLiteral("localized")).toObject();
    manifest.sidebarTabName =
        object.value(QStringLiteral("sidebarTab")).toObject()
            .value(QStringLiteral("name")).toString();

    const QJsonArray domains =
        object.value(QStringLiteral("allowedDomains")).toArray();
    for (const QJsonValue &domain : domains) {
        if (!domain.isString() || domain.toString().trimmed().isEmpty()) {
            fail(error, QStringLiteral(
                            "allowedDomains must contain only domain names."));
            return std::nullopt;
        }
        manifest.allowedDomains.append(
            domain.toString().trimmed().toLower());
    }

    QSet<QString> seenPermissions;
    const QJsonArray permissions =
        object.value(QStringLiteral("permissions")).toArray();
    for (const QJsonValue &value : permissions) {
        const QString name = value.toString();
        const auto permission = pluginPermissionFromName(name);
        if (!permission) {
            fail(error, QStringLiteral("Unknown plugin permission “%1”.")
                            .arg(name));
            return std::nullopt;
        }
        if (!seenPermissions.contains(name)) {
            manifest.permissions.append(*permission);
            seenPermissions.insert(name);
        }
    }
    return manifest;
}


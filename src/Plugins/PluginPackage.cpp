#include "Plugins/PluginPackage.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
bool isContained(const QString &root, const QString &candidate)
{
    const QString canonicalRoot =
        QFileInfo(root).canonicalFilePath().replace(
            QLatin1Char('/'), QLatin1Char('\\'));
    const QString canonicalCandidate =
        QFileInfo(candidate).canonicalFilePath().replace(
            QLatin1Char('/'), QLatin1Char('\\'));
    return !canonicalRoot.isEmpty() && !canonicalCandidate.isEmpty()
        && (canonicalCandidate.compare(
                canonicalRoot, Qt::CaseInsensitive) == 0
            || canonicalCandidate.startsWith(
                canonicalRoot + QLatin1Char('\\'),
                Qt::CaseInsensitive));
}
}

QString PluginPackage::pluginsRoot()
{
    return QStandardPaths::writableLocation(
               QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/plugins");
}

QString PluginPackage::entryPath(bool global) const
{
    return QDir(rootPath).filePath(
        global ? manifest.globalEntry : manifest.entry);
}

QString PluginPackage::dataPath() const
{
    return QDir(pluginsRoot()).filePath(
        QStringLiteral(".data/") + manifest.identifier);
}

QString PluginPackage::temporaryPath() const
{
    return QDir(QStandardPaths::writableLocation(
                    QStandardPaths::TempLocation))
        .filePath(QStringLiteral("supernova-") + manifest.identifier);
}

bool PluginPackage::containsPath(const QString &path) const
{
    return isContained(rootPath, path);
}

std::optional<PluginPackage> PluginPackage::load(
    const QString &path, QString *error)
{
    QFileInfo root(path);
    if (!root.exists() || !root.isDir()) {
        if (error) {
            *error = QStringLiteral("Plugin package does not exist.");
        }
        return std::nullopt;
    }

    QFile manifestFile(QDir(path).filePath(QStringLiteral("Info.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Plugin package has no readable Info.json.");
        }
        return std::nullopt;
    }
    auto manifest = PluginManifest::parse(manifestFile.readAll(), error);
    if (!manifest) {
        return std::nullopt;
    }

    PluginPackage package;
    package.manifest = std::move(*manifest);
    package.rootPath = root.canonicalFilePath();
    package.development =
        root.fileName().endsWith(QStringLiteral(".iinaplugin-dev"),
                                 Qt::CaseInsensitive);

    for (const QString &relative : {
             package.manifest.entry,
             package.manifest.globalEntry,
             package.manifest.preferencesPage}) {
        if (relative.isEmpty()) {
            continue;
        }
        const QString resolved = QDir(package.rootPath).filePath(relative);
        if (!isContained(package.rootPath, resolved)
            || !QFileInfo::exists(resolved)) {
            if (error) {
                *error = QStringLiteral(
                    "Plugin resource is missing or outside the package: %1")
                             .arg(relative);
            }
            return std::nullopt;
        }
    }

    QSettings settings;
    package.enabled = settings.value(
        QStringLiteral("plugins/enabled/") + package.manifest.identifier,
        true).toBool();
    QDir().mkpath(package.dataPath());
    QDir().mkpath(package.temporaryPath());
    return package;
}


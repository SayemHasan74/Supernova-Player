#pragma once

#include "Plugins/PluginManifest.h"

#include <QDir>
#include <QString>

#include <optional>

struct PluginPackage {
    PluginManifest manifest;
    QString rootPath;
    bool development = false;
    bool enabled = true;

    [[nodiscard]] QString entryPath(bool global = false) const;
    [[nodiscard]] QString dataPath() const;
    [[nodiscard]] QString temporaryPath() const;
    [[nodiscard]] bool containsPath(const QString &path) const;

    [[nodiscard]] static std::optional<PluginPackage> load(
        const QString &rootPath, QString *error = nullptr);
    [[nodiscard]] static QString pluginsRoot();
};


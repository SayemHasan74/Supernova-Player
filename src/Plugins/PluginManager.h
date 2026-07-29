#pragma once

#include "Plugins/PluginPackage.h"

#include <QHash>
#include <QList>
#include <QObject>

class MainWindow;
class PlayerCore;
class PluginRuntime;

class PluginManager final : public QObject {
    Q_OBJECT

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    [[nodiscard]] const QList<PluginPackage> &packages() const
    {
        return m_packages;
    }
    void reload();
    void attachPlayer(PlayerCore *player, MainWindow *window);
    void detachPlayer(MainWindow *window);
    void setEnabled(const QString &identifier, bool enabled);
    [[nodiscard]] QStringList errors() const { return m_errors; }
    [[nodiscard]] static PluginManager *instance();

signals:
    void packagesChanged();

private:
    void loadGlobalInstances();
    void loadPlayerInstances(PlayerCore *player, MainWindow *window);
    void unloadInstances();

    static PluginManager *s_instance;
    QList<PluginPackage> m_packages;
    QList<PluginRuntime *> m_globalInstances;
    QHash<MainWindow *, QList<PluginRuntime *>> m_playerInstances;
    QHash<MainWindow *, PlayerCore *> m_players;
    QStringList m_errors;
};

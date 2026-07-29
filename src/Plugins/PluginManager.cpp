#include "Plugins/PluginManager.h"

#include "Core/Logger.h"
#include "Plugins/PluginRuntime.h"
#include "UI/MainWindow/MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

PluginManager *PluginManager::s_instance = nullptr;

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    reload();
}

PluginManager::~PluginManager()
{
    unloadInstances();
    if (s_instance == this) s_instance = nullptr;
}

PluginManager *PluginManager::instance() { return s_instance; }

void PluginManager::reload()
{
    unloadInstances();
    m_packages.clear();
    m_errors.clear();
    const QString root = PluginPackage::pluginsRoot();
    QDir().mkpath(root);
    const QFileInfoList entries = QDir(root).entryInfoList(
        {QStringLiteral("*.iinaplugin"),
         QStringLiteral("*.iinaplugin-dev")},
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QSet<QString> identifiers;
    for (const QFileInfo &entry : entries) {
        QString error;
        auto package = PluginPackage::load(entry.absoluteFilePath(), &error);
        if (!package) {
            m_errors.append(
                QStringLiteral("%1: %2").arg(entry.fileName(), error));
            continue;
        }
        if (identifiers.contains(package->manifest.identifier)) {
            m_errors.append(QStringLiteral(
                "%1: duplicate identifier %2")
                    .arg(entry.fileName(), package->manifest.identifier));
            continue;
        }
        identifiers.insert(package->manifest.identifier);
        m_packages.append(std::move(*package));
    }
    const QStringList order =
        QSettings().value(QStringLiteral("plugins/order")).toStringList();
    std::stable_sort(
        m_packages.begin(), m_packages.end(),
        [&order](const PluginPackage &left, const PluginPackage &right) {
            const int li = order.indexOf(left.manifest.identifier);
            const int ri = order.indexOf(right.manifest.identifier);
            return (li < 0 ? INT_MAX : li)
                < (ri < 0 ? INT_MAX : ri);
        });
    loadGlobalInstances();
    const auto players = m_players;
    for (auto it = players.begin(); it != players.end(); ++it)
        loadPlayerInstances(it.value(), it.key());
    emit packagesChanged();
}

void PluginManager::loadGlobalInstances()
{
    for (const PluginPackage &package : std::as_const(m_packages)) {
        if (!package.enabled || package.manifest.globalEntry.isEmpty())
            continue;
        auto *runtime =
            new PluginRuntime(package, nullptr, nullptr, this);
        m_globalInstances.append(runtime);
    }
}

void PluginManager::attachPlayer(
    PlayerCore *player, MainWindow *window)
{
    detachPlayer(window);
    m_players.insert(window, player);
    loadPlayerInstances(player, window);
    connect(window, &QObject::destroyed, this,
            [this, window] {
                m_playerInstances.remove(window);
                m_players.remove(window);
            });
}

void PluginManager::loadPlayerInstances(
    PlayerCore *player, MainWindow *window)
{
    QList<PluginRuntime *> instances;
    for (const PluginPackage &package : std::as_const(m_packages)) {
        if (!package.enabled) continue;
        auto *runtime =
            new PluginRuntime(package, player, window, window);
        connect(runtime, &PluginRuntime::osdRequested, window,
                [window, package](const QString &message) {
            Logger::info(QStringLiteral("Plugin OSD [%1]: %2")
                             .arg(package.manifest.name, message));
            window->showPluginOsd(message);
        });
        instances.append(runtime);
    }
    m_playerInstances.insert(window, instances);
}

void PluginManager::detachPlayer(MainWindow *window)
{
    const QList<PluginRuntime *> instances =
        m_playerInstances.take(window);
    for (PluginRuntime *runtime : instances) delete runtime;
    m_players.remove(window);
}

void PluginManager::setEnabled(
    const QString &identifier, bool enabled)
{
    QSettings().setValue(
        QStringLiteral("plugins/enabled/") + identifier, enabled);
    reload();
}

void PluginManager::unloadInstances()
{
    for (auto it = m_playerInstances.begin();
         it != m_playerInstances.end(); ++it) {
        for (PluginRuntime *runtime : it.value()) delete runtime;
    }
    m_playerInstances.clear();
    qDeleteAll(m_globalInstances);
    m_globalInstances.clear();
}

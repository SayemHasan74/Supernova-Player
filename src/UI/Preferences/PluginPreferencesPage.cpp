#include "UI/Preferences/PluginPreferencesPage.h"

#include "Plugins/PluginManager.h"
#include "Plugins/PluginPackage.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

PluginPreferencesPage::PluginPreferencesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    auto *explanation = new QLabel(
        tr("Plugins use IINA-compatible Info.json manifests. Each enabled "
           "plugin receives a separate JavaScript context for every player."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        {tr("Enabled"), tr("Plugin"), tr("Version"), tr("Permissions")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
    m_errors = new QLabel(this);
    m_errors->setWordWrap(true);
    m_errors->setStyleSheet(QStringLiteral("color: #e58a8a;"));
    layout->addWidget(m_errors);
    auto *buttons = new QHBoxLayout;
    auto *folder = new QPushButton(tr("Open Plugins Folder"), this);
    auto *reload = new QPushButton(tr("Reload Plugins"), this);
    buttons->addWidget(folder);
    buttons->addWidget(reload);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(folder, &QPushButton::clicked, this, [] {
        QDir().mkpath(PluginPackage::pluginsRoot());
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(PluginPackage::pluginsRoot()));
    });
    connect(reload, &QPushButton::clicked, this, [this] {
        if (PluginManager::instance()) PluginManager::instance()->reload();
        refresh();
    });
    if (PluginManager::instance()) {
        connect(PluginManager::instance(),
                &PluginManager::packagesChanged,
                this, &PluginPreferencesPage::refresh);
    }
    refresh();
}

void PluginPreferencesPage::refresh()
{
    m_table->setRowCount(0);
    PluginManager *manager = PluginManager::instance();
    if (!manager) return;
    for (const PluginPackage &package : manager->packages()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *enabled = new QCheckBox(m_table);
        enabled->setChecked(package.enabled);
        enabled->setToolTip(
            package.enabled ? tr("Disable plugin") : tr("Enable plugin"));
        connect(enabled, &QCheckBox::toggled, this,
                [this, enabled, package](bool value) {
            if (value && !package.manifest.permissions.isEmpty()) {
                QStringList permissions;
                for (PluginPermission permission :
                     package.manifest.permissions) {
                    permissions.append(
                        QStringLiteral("• %1%2")
                            .arg(pluginPermissionName(permission),
                                 pluginPermissionIsDangerous(permission)
                                     ? tr(" (dangerous)") : QString()));
                }
                const auto answer = QMessageBox::question(
                    this, tr("Enable Plugin"),
                    tr("Allow “%1” to use these capabilities?\n\n%2")
                        .arg(package.manifest.name,
                             permissions.join(QLatin1Char('\n'))));
                if (answer != QMessageBox::Yes) {
                    const QSignalBlocker blocker(enabled);
                    enabled->setChecked(false);
                    return;
                }
            }
            if (PluginManager::instance())
                PluginManager::instance()->setEnabled(
                    package.manifest.identifier, value);
        });
        auto *cell = new QWidget(m_table);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(8, 0, 0, 0);
        cellLayout->addWidget(enabled);
        m_table->setCellWidget(row, 0, cell);
        m_table->setItem(
            row, 1, new QTableWidgetItem(package.manifest.name));
        m_table->setItem(
            row, 2, new QTableWidgetItem(package.manifest.version));
        QStringList permissions;
        for (PluginPermission permission :
             package.manifest.permissions) {
            permissions.append(pluginPermissionName(permission));
        }
        m_table->setItem(
            row, 3, new QTableWidgetItem(permissions.join(
                        QStringLiteral(", "))));
    }
    m_errors->setText(manager->errors().join(QLatin1Char('\n')));
    m_errors->setVisible(!manager->errors().isEmpty());
}

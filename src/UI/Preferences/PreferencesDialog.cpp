#include "UI/Preferences/PreferencesDialog.h"

#include "PlayerCore/PlayerCore.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QPushButton *smallButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setMinimumWidth(74);
    return button;
}

QTableWidgetItem *editableItem(const QString &text = {})
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(
        item->flags() | Qt::ItemIsEditable);
    return item;
}
}

PreferencesDialog::PreferencesDialog(
    PlayerCore *playerCore, QWidget *parent)
    : QDialog(parent),
      m_playerCore(playerCore)
{
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowTitle(tr("Preferences"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(760, 570);
    setMinimumSize(620, 430);

    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(), tr("General"));
    tabs->addTab(createMatchingPage(), tr("Matching"));
    tabs->addTab(createProfilesPage(), tr("Profiles"));
    tabs->addTab(createKeyBindingsPage(), tr("Key Bindings"));
    tabs->addTab(createAdvancedPage(), tr("Advanced"));
    root->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(buttons->button(QDialogButtonBox::Apply),
            &QPushButton::clicked,
            this, &PreferencesDialog::applyPreferences);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::hide);
    root->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "#preferencesDialog { background: rgb(29,30,34); color: rgb(239,239,244); }"
        "QTabWidget::pane { border: 1px solid rgba(255,255,255,28); }"
        "QTabBar::tab { color: rgb(225,225,232); padding: 7px 14px;"
        " background: rgb(38,39,44); }"
        "QTabBar::tab:selected { background: rgb(55,57,64); }"
        "QLineEdit, QComboBox, QTextEdit, QTableWidget {"
        " background: rgb(22,23,27); color: rgb(239,239,244);"
        " border: 1px solid rgba(255,255,255,30); border-radius: 4px; }"
        "QHeaderView::section { background: rgb(43,44,50); color: rgb(225,225,232);"
        " border: 0; padding: 5px; }"
        "QPushButton { color: rgb(239,239,244); padding: 5px 11px;"
        " border: 1px solid rgba(255,255,255,34); border-radius: 5px;"
        " background: rgb(48,49,55); }"
        "QPushButton:hover { background: rgb(61,62,69); }"));
}

QWidget *PreferencesDialog::createGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    const QSettings settings;
    m_recordHistory = new QCheckBox(
        tr("Record playback history"), page);
    m_recordHistory->setChecked(settings.value(
        QStringLiteral("history/recordPlaybackHistory"), true).toBool());
    m_resumePlayback = new QCheckBox(
        tr("Resume media from the last playback position"), page);
    m_resumePlayback->setChecked(settings.value(
        QStringLiteral("history/resumePlayback"), true).toBool());
    m_recordRecentMedia = new QCheckBox(
        tr("Remember recently opened media"), page);
    m_recordRecentMedia->setChecked(settings.value(
        QStringLiteral("history/recordRecentMedia"), true).toBool());
    m_trackPlaylistFilesAsRecent = new QCheckBox(
        tr("Include files reached through the playlist in Recent Media"),
        page);
    m_trackPlaylistFilesAsRecent->setChecked(settings.value(
        QStringLiteral("history/trackPlaylistFilesAsRecent"), true)
        .toBool());
    layout->addWidget(m_recordHistory);
    layout->addWidget(m_resumePlayback);
    layout->addWidget(m_recordRecentMedia);
    layout->addWidget(m_trackPlaylistFilesAsRecent);

    auto *explanation = new QLabel(
        tr("History metadata and mpv watch-later state are stored separately. "
           "Disabling history stops new history entries; disabling resume stops "
           "restoring and writing watch-later positions."),
        page);
    explanation->setWordWrap(true);
    explanation->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,160);"));
    layout->addWidget(explanation);

    auto *pathTitle = new QLabel(tr("History storage"), page);
    QFont titleFont = pathTitle->font();
    titleFont.setBold(true);
    pathTitle->setFont(titleFont);
    layout->addSpacing(10);
    layout->addWidget(pathTitle);
    m_historyPath = new QLabel(
        m_playerCore ? m_playerCore->historyFilePath() : QString(), page);
    m_historyPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_historyPath->setWordWrap(true);
    m_historyPath->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,160);"));
    layout->addWidget(m_historyPath);

    auto *clear = smallButton(tr("Clear Playback History…"), page);
    auto *clearRecent = smallButton(tr("Clear Recent Media…"), page);
    auto *clearLine = new QHBoxLayout;
    clearLine->addWidget(clear);
    clearLine->addWidget(clearRecent);
    clearLine->addStretch();
    layout->addLayout(clearLine);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (m_playerCore
            && QMessageBox::question(
                   this, tr("Clear Playback History"),
                   tr("Remove every playback-history entry?"))
                == QMessageBox::Yes) {
            m_playerCore->clearHistory();
        }
    });
    connect(clearRecent, &QPushButton::clicked, this, [this] {
        if (m_playerCore
            && QMessageBox::question(
                   this, tr("Clear Recent Media"),
                   tr("Remove every item from Recent Media?"))
                == QMessageBox::Yes) {
            m_playerCore->clearRecentMedia();
        }
    });
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::createMatchingPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    const QSettings settings;

    m_playlistAutoAdd = new QCheckBox(
        tr("Automatically add playable files in the same folder"),
        page);
    m_playlistAutoAdd->setChecked(settings.value(
        QStringLiteral("matching/playlistAutoAdd"), true).toBool());
    layout->addWidget(m_playlistAutoAdd);

    auto *form = new QFormLayout;
    m_subtitleMode = new QComboBox(page);
    m_subtitleMode->addItem(
        tr("Disabled"), static_cast<int>(SubtitleAutoLoadMode::Disabled));
    m_subtitleMode->addItem(
        tr("Filename containment"),
        static_cast<int>(SubtitleAutoLoadMode::Filename));
    m_subtitleMode->addItem(
        tr("Smart series matching"),
        static_cast<int>(SubtitleAutoLoadMode::Smart));
    const int mode = settings.value(
        QStringLiteral("matching/subtitleMode"),
        static_cast<int>(SubtitleAutoLoadMode::Smart)).toInt();
    m_subtitleMode->setCurrentIndex(
        std::max(0, m_subtitleMode->findData(mode)));
    m_subtitleSearchPaths = new QLineEdit(settings.value(
        QStringLiteral("matching/subtitleSearchPaths"),
        QStringLiteral("./*")).toString(), page);
    m_subtitlePriorityStrings = new QLineEdit(settings.value(
        QStringLiteral("matching/subtitlePriorityStrings")).toString(),
        page);
    form->addRow(tr("Subtitle matching"), m_subtitleMode);
    form->addRow(tr("Subtitle search paths"), m_subtitleSearchPaths);
    form->addRow(tr("Priority strings"), m_subtitlePriorityStrings);
    layout->addLayout(form);

    auto *hint = new QLabel(
        tr("Separate subtitle paths with semicolons. Relative paths are "
           "resolved beside the media; “./*” searches each immediate "
           "subfolder. Separate priority strings with commas."),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,160);"));
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::createProfilesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("mpv Profile"), page));
    m_profile = new QComboBox(page);
    top->addWidget(m_profile, 1);
    auto *apply = smallButton(tr("Apply"), page);
    auto *add = smallButton(tr("New…"), page);
    auto *remove = smallButton(tr("Delete"), page);
    top->addWidget(apply);
    top->addWidget(add);
    top->addWidget(remove);
    layout->addLayout(top);

    auto *hint = new QLabel(
        tr("Profiles are native mpv.conf sections. Top-level options and "
           "conditional profiles are preserved exactly as written."),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,160);"));
    layout->addWidget(hint);

    m_mpvConfig = new QTextEdit(page);
    m_mpvConfig->setAcceptRichText(false);
    m_mpvConfig->setLineWrapMode(QTextEdit::NoWrap);
    m_mpvConfig->setFontFamily(QStringLiteral("Consolas"));
    layout->addWidget(m_mpvConfig, 1);

    auto *bottom = new QHBoxLayout;
    auto *reload = smallButton(tr("Reload from Disk"), page);
    auto *save = smallButton(tr("Save mpv.conf"), page);
    bottom->addWidget(reload);
    bottom->addStretch();
    bottom->addWidget(save);
    layout->addLayout(bottom);

    connect(apply, &QPushButton::clicked, this, [this] {
        if (m_playerCore && !m_profile->currentText().isEmpty()) {
            saveMpvConfiguration();
            m_playerCore->applyMpvProfile(m_profile->currentText());
        }
    });
    connect(add, &QPushButton::clicked,
            this, &PreferencesDialog::addProfile);
    connect(remove, &QPushButton::clicked,
            this, &PreferencesDialog::removeProfile);
    connect(reload, &QPushButton::clicked,
            this, &PreferencesDialog::refreshProfiles);
    connect(save, &QPushButton::clicked,
            this, &PreferencesDialog::saveMpvConfiguration);
    refreshProfiles();
    return page;
}

QWidget *PreferencesDialog::createKeyBindingsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto *configs = new QHBoxLayout;
    configs->addWidget(new QLabel(tr("Configuration"), page));
    m_inputConfig = new QComboBox(page);
    configs->addWidget(m_inputConfig, 1);
    auto *create = smallButton(tr("New…"), page);
    auto *duplicate = smallButton(tr("Duplicate…"), page);
    auto *import = smallButton(tr("Import…"), page);
    m_deleteInputConfig = smallButton(tr("Delete"), page);
    configs->addWidget(create);
    configs->addWidget(duplicate);
    configs->addWidget(import);
    configs->addWidget(m_deleteInputConfig);
    layout->addLayout(configs);

    m_bindingSearch = new QLineEdit(page);
    m_bindingSearch->setClearButtonEnabled(true);
    m_bindingSearch->setPlaceholderText(
        tr("Search keys and commands"));
    layout->addWidget(m_bindingSearch);

    m_bindings = new QTableWidget(0, 3, page);
    m_bindings->setHorizontalHeaderLabels(
        {tr("Key"), tr("Action"), tr("Type")});
    m_bindings->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_bindings->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_bindings->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_bindings->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindings->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_bindings, 1);

    auto *buttons = new QHBoxLayout;
    auto *add = smallButton(tr("Add"), page);
    auto *remove = smallButton(tr("Remove"), page);
    m_saveBindings = smallButton(tr("Save and Apply"), page);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(m_saveBindings);
    layout->addLayout(buttons);

    connect(m_inputConfig, &QComboBox::currentIndexChanged,
            this, &PreferencesDialog::loadSelectedBindings);
    connect(m_bindingSearch, &QLineEdit::textChanged,
            this, [this](const QString &text) {
                for (int row = 0; row < m_bindings->rowCount(); ++row) {
                    const QString key = m_bindings->item(row, 0)
                        ? m_bindings->item(row, 0)->text() : QString();
                    const QString action = m_bindings->item(row, 1)
                        ? m_bindings->item(row, 1)->text() : QString();
                    m_bindings->setRowHidden(
                        row, !text.isEmpty()
                            && !key.contains(text, Qt::CaseInsensitive)
                            && !action.contains(text, Qt::CaseInsensitive));
                }
            });
    connect(add, &QPushButton::clicked, this, [this] {
        QDialog editor(this);
        editor.setWindowTitle(tr("Add Key Binding"));
        auto *form = new QFormLayout(&editor);
        auto *key = new QKeySequenceEdit(&editor);
        key->setMaximumSequenceLength(1);
        auto *action = new QLineEdit(&editor);
        action->setPlaceholderText(
            tr("mpv command or application command identifier"));
        auto *type = new QComboBox(&editor);
        type->addItems({tr("mpv Command"), tr("Application Command")});
        form->addRow(tr("Key"), key);
        form->addRow(tr("Action"), action);
        form->addRow(tr("Type"), type);
        auto *dialogButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
        form->addRow(dialogButtons);
        connect(dialogButtons, &QDialogButtonBox::accepted,
                &editor, &QDialog::accept);
        connect(dialogButtons, &QDialogButtonBox::rejected,
                &editor, &QDialog::reject);
        if (editor.exec() != QDialog::Accepted
            || key->keySequence().isEmpty()
            || action->text().trimmed().isEmpty()) {
            return;
        }
        const int row = m_bindings->rowCount();
        m_bindings->insertRow(row);
        m_bindings->setItem(
            row, 0, editableItem(key->keySequence().toString(
                        QKeySequence::PortableText)));
        m_bindings->setItem(
            row, 1, editableItem(action->text().trimmed()));
        auto *storedType = new QComboBox(m_bindings);
        storedType->addItems(
            {tr("mpv Command"), tr("Application Command")});
        storedType->setCurrentIndex(type->currentIndex());
        m_bindings->setCellWidget(row, 2, storedType);
        m_bindings->setCurrentCell(row, 0);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index :
             m_bindings->selectionModel()->selectedRows()) {
            rows.append(index.row());
        }
        std::sort(rows.begin(), rows.end(), std::greater<>());
        for (int row : std::as_const(rows)) {
            m_bindings->removeRow(row);
        }
    });
    connect(m_saveBindings, &QPushButton::clicked,
            this, &PreferencesDialog::saveSelectedBindings);
    connect(create, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, tr("New Key Binding Configuration"),
            tr("Name:"), QLineEdit::Normal, QString(), &accepted);
        if (accepted && PlayerConfiguration::createInputConfig(name, {})) {
            reloadInputConfigNames(name.trimmed());
        }
    });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, tr("Duplicate Key Bindings"),
            tr("New name:"), QLineEdit::Normal,
            m_inputConfig->currentText() + tr(" Copy"), &accepted);
        if (accepted && PlayerConfiguration::createInputConfig(
                name, tableBindings())) {
            reloadInputConfigNames(name.trimmed());
        }
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import input.conf"), QString(),
            tr("mpv Input Configuration (*.conf)"));
        QString name;
        if (!path.isEmpty()
            && PlayerConfiguration::importInputConfig(path, &name)) {
            reloadInputConfigNames(name);
        }
    });
    connect(m_deleteInputConfig, &QPushButton::clicked, this, [this] {
        const QString name = m_inputConfig->currentText();
        if (!PlayerConfiguration::isBuiltInInputConfig(name)
            && QMessageBox::question(
                   this, tr("Delete Configuration"),
                   tr("Delete “%1”?").arg(name))
                == QMessageBox::Yes
            && PlayerConfiguration::deleteInputConfig(name)) {
            reloadInputConfigNames();
        }
    });
    reloadInputConfigNames(PlayerConfiguration::currentInputConfigName());
    return page;
}

QWidget *PreferencesDialog::createAdvancedPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    const QSettings settings;

    m_enableAdvanced = new QCheckBox(
        tr("Enable advanced mpv options"), page);
    m_enableAdvanced->setChecked(
        PlayerConfiguration::advancedSettingsEnabled());
    layout->addWidget(m_enableAdvanced);

    m_useConfigDirectory = new QCheckBox(
        tr("Use another mpv configuration directory"), page);
    m_useConfigDirectory->setChecked(
        PlayerConfiguration::useCustomConfigDirectory());
    layout->addWidget(m_useConfigDirectory);
    auto *directory = new QHBoxLayout;
    m_configDirectory = new QLineEdit(
        settings.value(
            QStringLiteral("advanced/configDirectory"),
            PlayerConfiguration::mpvConfigDirectory()).toString(),
        page);
    m_chooseConfigDirectory = smallButton(tr("Choose…"), page);
    directory->addWidget(m_configDirectory, 1);
    directory->addWidget(m_chooseConfigDirectory);
    layout->addLayout(directory);

    auto *warning = new QLabel(
        tr("Options are applied in order during player startup. Invalid "
           "options are logged and skipped. Changes to this page require "
           "restarting the player."),
        page);
    warning->setWordWrap(true);
    warning->setStyleSheet(
        QStringLiteral("color: rgba(235,185,90,200);"));
    layout->addWidget(warning);

    m_options = new QTableWidget(0, 2, page);
    m_options->setHorizontalHeaderLabels({tr("Option"), tr("Value")});
    m_options->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_options->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_options->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_options, 1);

    auto *buttons = new QHBoxLayout;
    auto *add = smallButton(tr("Add"), page);
    auto *remove = smallButton(tr("Remove"), page);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto updateDirectoryEnabled = [this] {
        const bool enabled = m_useConfigDirectory->isChecked();
        m_configDirectory->setEnabled(enabled);
        m_chooseConfigDirectory->setEnabled(enabled);
    };
    connect(m_useConfigDirectory, &QCheckBox::toggled,
            this, updateDirectoryEnabled);
    updateDirectoryEnabled();
    connect(m_chooseConfigDirectory, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Choose mpv Configuration Directory"),
            m_configDirectory->text());
        if (!directory.isEmpty()) {
            m_configDirectory->setText(directory);
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const int row = m_options->rowCount();
        m_options->insertRow(row);
        m_options->setItem(row, 0, editableItem(QStringLiteral("option")));
        m_options->setItem(row, 1, editableItem(QStringLiteral("value")));
        m_options->setCurrentCell(row, 0);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index :
             m_options->selectionModel()->selectedRows()) {
            rows.append(index.row());
        }
        std::sort(rows.begin(), rows.end(), std::greater<>());
        for (int row : std::as_const(rows)) {
            m_options->removeRow(row);
        }
    });
    loadAdvancedOptions();
    return page;
}

void PreferencesDialog::applyPreferences()
{
    QSettings settings;
    settings.setValue(
        QStringLiteral("history/recordPlaybackHistory"),
        m_recordHistory->isChecked());
    settings.setValue(
        QStringLiteral("history/resumePlayback"),
        m_resumePlayback->isChecked());
    settings.setValue(
        QStringLiteral("history/recordRecentMedia"),
        m_recordRecentMedia->isChecked());
    settings.setValue(
        QStringLiteral("history/trackPlaylistFilesAsRecent"),
        m_trackPlaylistFilesAsRecent->isChecked());
    settings.setValue(
        QStringLiteral("matching/playlistAutoAdd"),
        m_playlistAutoAdd->isChecked());
    settings.setValue(
        QStringLiteral("matching/subtitleMode"),
        m_subtitleMode->currentData().toInt());
    settings.setValue(
        QStringLiteral("matching/subtitleSearchPaths"),
        m_subtitleSearchPaths->text().trimmed());
    settings.setValue(
        QStringLiteral("matching/subtitlePriorityStrings"),
        m_subtitlePriorityStrings->text().trimmed());
    settings.setValue(
        QStringLiteral("advanced/enabled"),
        m_enableAdvanced->isChecked());
    settings.setValue(
        QStringLiteral("advanced/useConfigDirectory"),
        m_useConfigDirectory->isChecked());
    settings.setValue(
        QStringLiteral("advanced/configDirectory"),
        m_configDirectory->text().trimmed());
    PlayerConfiguration::setAdvancedOptions(tableOptions());
    settings.sync();

    if (m_playerCore) {
        m_playerCore->setHistoryRecordingEnabled(
            m_recordHistory->isChecked());
        m_playerCore->setResumePlaybackEnabled(
            m_resumePlayback->isChecked());
        m_playerCore->setRecentMediaRecordingEnabled(
            m_recordRecentMedia->isChecked());
        m_playerCore->setTrackPlaylistFilesAsRecent(
            m_trackPlaylistFilesAsRecent->isChecked());
    }
    refreshProfiles();
}

void PreferencesDialog::reloadInputConfigNames(const QString &select)
{
    const QString target = select.isEmpty()
        ? PlayerConfiguration::currentInputConfigName() : select;
    const QSignalBlocker blocker(m_inputConfig);
    m_inputConfig->clear();
    m_inputConfig->addItems(PlayerConfiguration::inputConfigNames());
    const int index = m_inputConfig->findText(target);
    m_inputConfig->setCurrentIndex(index >= 0 ? index : 0);
    loadSelectedBindings();
}

void PreferencesDialog::loadSelectedBindings()
{
    const QString name = m_inputConfig->currentText();
    bool ok = false;
    const QList<ConfiguredKeyBinding> bindings =
        PlayerConfiguration::parseInputConf(
            PlayerConfiguration::inputConfigPath(name), &ok);
    setTableBindings(ok ? bindings
                        : PlayerConfiguration::defaultKeyBindings());
    const bool editable =
        !PlayerConfiguration::isBuiltInInputConfig(name);
    m_bindings->setEditTriggers(
        editable ? QAbstractItemView::DoubleClicked
                       | QAbstractItemView::EditKeyPressed
                 : QAbstractItemView::NoEditTriggers);
    m_saveBindings->setEnabled(editable);
    m_deleteInputConfig->setEnabled(editable);
    PlayerConfiguration::setCurrentInputConfigName(name);
    emit keyBindingsChanged();
}

void PreferencesDialog::saveSelectedBindings()
{
    const QString name = m_inputConfig->currentText();
    if (PlayerConfiguration::saveInputConfig(name, tableBindings())) {
        PlayerConfiguration::setCurrentInputConfigName(name);
        emit keyBindingsChanged();
    }
}

QList<ConfiguredKeyBinding> PreferencesDialog::tableBindings() const
{
    QList<ConfiguredKeyBinding> bindings;
    for (int row = 0; row < m_bindings->rowCount(); ++row) {
        const QTableWidgetItem *keyItem = m_bindings->item(row, 0);
        const QTableWidgetItem *actionItem = m_bindings->item(row, 1);
        const auto *type =
            qobject_cast<QComboBox *>(m_bindings->cellWidget(row, 2));
        if (keyItem && actionItem
            && !keyItem->text().trimmed().isEmpty()
            && !actionItem->text().trimmed().isEmpty()) {
            bindings.append({
                keyItem->text().trimmed(),
                actionItem->text().trimmed(),
                {},
                type && type->currentIndex() == 1});
        }
    }
    return bindings;
}

void PreferencesDialog::setTableBindings(
    const QList<ConfiguredKeyBinding> &bindings)
{
    m_bindings->setRowCount(0);
    for (const ConfiguredKeyBinding &binding : bindings) {
        const int row = m_bindings->rowCount();
        m_bindings->insertRow(row);
        m_bindings->setItem(row, 0, editableItem(binding.key));
        m_bindings->setItem(row, 1, editableItem(binding.action));
        auto *type = new QComboBox(m_bindings);
        type->addItems({tr("mpv Command"), tr("Application Command")});
        type->setCurrentIndex(binding.applicationCommand ? 1 : 0);
        m_bindings->setCellWidget(row, 2, type);
    }
}

void PreferencesDialog::refreshProfiles()
{
    m_mpvConfig->setPlainText(PlayerConfiguration::readMpvConfig());
    const QSignalBlocker blocker(m_profile);
    m_profile->clear();
    m_profile->addItems(
        PlayerConfiguration::mpvProfiles(m_mpvConfig->toPlainText()));
}

void PreferencesDialog::saveMpvConfiguration()
{
    if (!PlayerConfiguration::writeMpvConfig(
            m_mpvConfig->toPlainText())) {
        QMessageBox::warning(
            this, tr("mpv Configuration"),
            tr("The mpv configuration could not be saved."));
        return;
    }
    const QString selected = m_profile->currentText();
    m_profile->clear();
    m_profile->addItems(
        PlayerConfiguration::mpvProfiles(m_mpvConfig->toPlainText()));
    const int index = m_profile->findText(selected);
    if (index >= 0) {
        m_profile->setCurrentIndex(index);
    }
}

void PreferencesDialog::addProfile()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("New mpv Profile"), tr("Profile name:"),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()
        || name.contains(QLatin1Char('['))
        || name.contains(QLatin1Char(']'))) {
        return;
    }
    if (PlayerConfiguration::mpvProfiles(
            m_mpvConfig->toPlainText()).contains(name)) {
        return;
    }
    QString text = m_mpvConfig->toPlainText();
    if (!text.endsWith(QLatin1Char('\n'))) {
        text += QLatin1Char('\n');
    }
    text += QStringLiteral("\n[%1]\nprofile-desc=%1\n").arg(name);
    m_mpvConfig->setPlainText(text);
    saveMpvConfiguration();
    m_profile->setCurrentText(name);
}

void PreferencesDialog::removeProfile()
{
    const QString name = m_profile->currentText();
    if (name.isEmpty()
        || QMessageBox::question(
               this, tr("Delete mpv Profile"),
               tr("Delete profile “%1” from mpv.conf?").arg(name))
            != QMessageBox::Yes) {
        return;
    }
    QStringList lines =
        m_mpvConfig->toPlainText().split(QLatin1Char('\n'));
    QStringList kept;
    bool removing = false;
    const QRegularExpression section(
        QStringLiteral("^\\s*\\[([^\\]]+)\\]\\s*(?:#.*)?$"));
    for (const QString &line : std::as_const(lines)) {
        const QRegularExpressionMatch match = section.match(line);
        if (match.hasMatch()) {
            removing = match.captured(1).trimmed() == name;
        }
        if (!removing) {
            kept.append(line);
        }
    }
    m_mpvConfig->setPlainText(kept.join(QLatin1Char('\n')));
    saveMpvConfiguration();
}

void PreferencesDialog::loadAdvancedOptions()
{
    m_options->setRowCount(0);
    for (const ConfiguredMpvOption &option :
         PlayerConfiguration::advancedOptions()) {
        const int row = m_options->rowCount();
        m_options->insertRow(row);
        m_options->setItem(row, 0, editableItem(option.name));
        m_options->setItem(row, 1, editableItem(option.value));
    }
}

QList<ConfiguredMpvOption> PreferencesDialog::tableOptions() const
{
    QList<ConfiguredMpvOption> options;
    for (int row = 0; row < m_options->rowCount(); ++row) {
        const QTableWidgetItem *name = m_options->item(row, 0);
        const QTableWidgetItem *value = m_options->item(row, 1);
        if (name && !name->text().trimmed().isEmpty()) {
            options.append({
                name->text().trimmed(),
                value ? value->text() : QString()});
        }
    }
    return options;
}

#pragma once

#include "Preferences/PlayerConfiguration.h"

#include <QDialog>

class PlayerCore;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

class PreferencesDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(
        PlayerCore *playerCore, QWidget *parent = nullptr);

signals:
    void keyBindingsChanged();

private:
    QWidget *createGeneralPage();
    QWidget *createMatchingPage();
    QWidget *createProfilesPage();
    QWidget *createKeyBindingsPage();
    QWidget *createAdvancedPage();
    void applyPreferences();

    void reloadInputConfigNames(const QString &select = {});
    void loadSelectedBindings();
    void saveSelectedBindings();
    [[nodiscard]] QList<ConfiguredKeyBinding> tableBindings() const;
    void setTableBindings(const QList<ConfiguredKeyBinding> &bindings);

    void refreshProfiles();
    void saveMpvConfiguration();
    void addProfile();
    void removeProfile();

    void loadAdvancedOptions();
    [[nodiscard]] QList<ConfiguredMpvOption> tableOptions() const;

    PlayerCore *m_playerCore = nullptr;
    QCheckBox *m_recordHistory = nullptr;
    QCheckBox *m_resumePlayback = nullptr;
    QCheckBox *m_recordRecentMedia = nullptr;
    QCheckBox *m_trackPlaylistFilesAsRecent = nullptr;
    QLabel *m_historyPath = nullptr;
    QCheckBox *m_playlistAutoAdd = nullptr;
    QComboBox *m_subtitleMode = nullptr;
    QLineEdit *m_subtitleSearchPaths = nullptr;
    QLineEdit *m_subtitlePriorityStrings = nullptr;

    QComboBox *m_profile = nullptr;
    QTextEdit *m_mpvConfig = nullptr;

    QComboBox *m_inputConfig = nullptr;
    QLineEdit *m_bindingSearch = nullptr;
    QTableWidget *m_bindings = nullptr;
    QPushButton *m_deleteInputConfig = nullptr;
    QPushButton *m_saveBindings = nullptr;

    QCheckBox *m_enableAdvanced = nullptr;
    QCheckBox *m_useConfigDirectory = nullptr;
    QLineEdit *m_configDirectory = nullptr;
    QPushButton *m_chooseConfigDirectory = nullptr;
    QTableWidget *m_options = nullptr;
};

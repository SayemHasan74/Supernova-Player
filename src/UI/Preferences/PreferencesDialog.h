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
class QSpinBox;

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
    QWidget *createMediaToolsPage();
    QWidget *createNetworkPage();
    QWidget *createOnlineSubtitlesPage();
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
    QCheckBox *m_systemMediaControls = nullptr;
    QCheckBox *m_preventSleep = nullptr;
    QCheckBox *m_allowDisplaySleepForAudio = nullptr;
    QCheckBox *m_jumpList = nullptr;
    QLabel *m_fileAssociationStatus = nullptr;
    QLabel *m_historyPath = nullptr;
    QCheckBox *m_playlistAutoAdd = nullptr;
    QComboBox *m_subtitleMode = nullptr;
    QLineEdit *m_subtitleSearchPaths = nullptr;
    QLineEdit *m_subtitlePriorityStrings = nullptr;
    QCheckBox *m_thumbnailEnabled = nullptr;
    QSpinBox *m_thumbnailWidth = nullptr;
    QSpinBox *m_thumbnailCacheSize = nullptr;
    QCheckBox *m_screenshotSave = nullptr;
    QCheckBox *m_screenshotClipboard = nullptr;
    QCheckBox *m_screenshotSubtitles = nullptr;
    QCheckBox *m_screenshotPreview = nullptr;
    QLineEdit *m_screenshotFolder = nullptr;
    QComboBox *m_screenshotFormat = nullptr;

    QCheckBox *m_cacheEnabled = nullptr;
    QSpinBox *m_cacheSeconds = nullptr;
    QSpinBox *m_cacheMemory = nullptr;
    QCheckBox *m_cacheOnDisk = nullptr;
    QSpinBox *m_networkTimeout = nullptr;
    QLineEdit *m_proxy = nullptr;
    QLineEdit *m_userAgent = nullptr;
    QLineEdit *m_referrer = nullptr;
    QLineEdit *m_cookiesFile = nullptr;
    QCheckBox *m_ytdlEnabled = nullptr;
    QLineEdit *m_ytdlPath = nullptr;
    QLineEdit *m_javascriptRuntime = nullptr;
    QLineEdit *m_ytdlFormat = nullptr;
    QLineEdit *m_ytdlRawOptions = nullptr;
    QCheckBox *m_tryYtdlFirst = nullptr;
    QCheckBox *m_ytdlSubtitles = nullptr;
    QCheckBox *m_ytdlAutomaticSubtitles = nullptr;

    QLineEdit *m_openSubtitlesApiKey = nullptr;
    QLineEdit *m_openSubtitlesToken = nullptr;
    QLineEdit *m_openSubtitlesUsername = nullptr;
    QLineEdit *m_openSubtitlesPassword = nullptr;
    QLineEdit *m_assrtToken = nullptr;
    QLineEdit *m_onlineSubtitleLanguages = nullptr;

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

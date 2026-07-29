#include "UI/Preferences/PreferencesDialog.h"

#include "PlayerCore/PlayerCore.h"
#include "Network/SecureCredentialStore.h"
#include "Platform/Windows/WindowsShellIntegration.h"
#include "UI/Preferences/PluginPreferencesPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
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
    tabs->addTab(createMediaToolsPage(), tr("Media Tools"));
    tabs->addTab(createNetworkPage(), tr("Network"));
    tabs->addTab(
        createOnlineSubtitlesPage(), tr("Online Subtitles"));
    tabs->addTab(createProfilesPage(), tr("Profiles"));
    tabs->addTab(createKeyBindingsPage(), tr("Key Bindings"));
    tabs->addTab(createAdvancedPage(), tr("Advanced"));
    tabs->addTab(new PluginPreferencesPage(tabs), tr("Plugins"));
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

#ifdef Q_OS_WIN
    layout->addSpacing(10);
    auto *windowsTitle =
        new QLabel(tr("Windows integration"), page);
    windowsTitle->setFont(titleFont);
    layout->addWidget(windowsTitle);

    m_systemMediaControls = new QCheckBox(
        tr("Use Windows media controls and media keys"), page);
    m_systemMediaControls->setChecked(settings.value(
        QStringLiteral("windows/systemMediaControls"), true).toBool());
    m_preventSleep = new QCheckBox(
        tr("Prevent sleep while media is playing"), page);
    m_preventSleep->setChecked(settings.value(
        QStringLiteral("windows/preventSleep"), true).toBool());
    m_allowDisplaySleepForAudio = new QCheckBox(
        tr("Allow the display to sleep during audio-only playback"), page);
    m_allowDisplaySleepForAudio->setChecked(settings.value(
        QStringLiteral("windows/allowDisplaySleepForAudio"), true)
                                                    .toBool());
    m_jumpList = new QCheckBox(
        tr("Show recent media and player tasks in the Jump List"), page);
    m_jumpList->setChecked(settings.value(
        QStringLiteral("windows/jumpList"), true).toBool());
    layout->addWidget(m_systemMediaControls);
    layout->addWidget(m_preventSleep);
    layout->addWidget(m_allowDisplaySleepForAudio);
    layout->addWidget(m_jumpList);

    m_fileAssociationStatus = new QLabel(page);
    const auto refreshAssociationStatus = [this] {
        m_fileAssociationStatus->setText(
            WindowsFileAssociations::isRegistered()
                ? tr("Supernova is registered as an available media handler.")
                : tr("Supernova is not registered with Windows file types."));
    };
    m_fileAssociationStatus->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,160);"));
    refreshAssociationStatus();
    layout->addWidget(m_fileAssociationStatus);

    auto *registerAssociations =
        smallButton(tr("Register File Types"), page);
    auto *removeAssociations =
        smallButton(tr("Remove Registration"), page);
    auto *defaultApps =
        smallButton(tr("Open Default Apps"), page);
    auto *associationActions = new QHBoxLayout;
    associationActions->addWidget(registerAssociations);
    associationActions->addWidget(removeAssociations);
    associationActions->addWidget(defaultApps);
    associationActions->addStretch();
    layout->addLayout(associationActions);
    connect(registerAssociations, &QPushButton::clicked,
            this, [this, refreshAssociationStatus] {
                QString error;
                if (!WindowsFileAssociations::registerCurrentExecutable(
                        &error)) {
                    QMessageBox::warning(
                        this, tr("File Associations"), error);
                    return;
                }
                refreshAssociationStatus();
            });
    connect(removeAssociations, &QPushButton::clicked,
            this, [this, refreshAssociationStatus] {
                QString error;
                if (!WindowsFileAssociations::unregisterCurrentUser(
                        &error)) {
                    QMessageBox::warning(
                        this, tr("File Associations"), error);
                    return;
                }
                refreshAssociationStatus();
            });
    connect(defaultApps, &QPushButton::clicked,
            this, [] {
                WindowsFileAssociations::openDefaultAppsSettings();
            });
#endif
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

QWidget *PreferencesDialog::createMediaToolsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    const QSettings settings;

    auto *thumbnailTitle = new QLabel(tr("Timeline Thumbnails"), page);
    QFont titleFont = thumbnailTitle->font();
    titleFont.setBold(true);
    thumbnailTitle->setFont(titleFont);
    layout->addWidget(thumbnailTitle);
    m_thumbnailEnabled = new QCheckBox(
        tr("Enable timeline thumbnail previews"), page);
    m_thumbnailEnabled->setChecked(settings.value(
        QStringLiteral("thumbnails/enabled"), true).toBool());
    layout->addWidget(m_thumbnailEnabled);
    auto *thumbnailForm = new QFormLayout;
    m_thumbnailWidth = new QSpinBox(page);
    m_thumbnailWidth->setRange(80, 300);
    m_thumbnailWidth->setSuffix(tr(" px"));
    m_thumbnailWidth->setValue(settings.value(
        QStringLiteral("thumbnails/width"), 120).toInt());
    m_thumbnailCacheSize = new QSpinBox(page);
    m_thumbnailCacheSize->setRange(0, 4096);
    m_thumbnailCacheSize->setSuffix(tr(" MiB"));
    m_thumbnailCacheSize->setSpecialValueText(tr("Disabled"));
    m_thumbnailCacheSize->setValue(settings.value(
        QStringLiteral("thumbnails/maxCacheMiB"), 500).toInt());
    thumbnailForm->addRow(tr("Preview width"), m_thumbnailWidth);
    thumbnailForm->addRow(tr("Maximum cache"), m_thumbnailCacheSize);
    layout->addLayout(thumbnailForm);
    auto *clearCache = smallButton(tr("Clear Thumbnail Cache…"), page);
    layout->addWidget(clearCache, 0, Qt::AlignLeft);
    connect(clearCache, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(
                this, tr("Clear Thumbnail Cache"),
                tr("Delete every cached timeline thumbnail?"))
            == QMessageBox::Yes) {
            ThumbnailProvider::clearCache();
        }
    });

    auto *screenshotTitle = new QLabel(tr("Screenshots"), page);
    screenshotTitle->setFont(titleFont);
    layout->addSpacing(10);
    layout->addWidget(screenshotTitle);
    m_screenshotSave = new QCheckBox(tr("Save screenshots to file"), page);
    m_screenshotSave->setChecked(settings.value(
        QStringLiteral("screenshots/saveToFile"), true).toBool());
    m_screenshotClipboard = new QCheckBox(
        tr("Copy screenshots to the clipboard"), page);
    m_screenshotClipboard->setChecked(settings.value(
        QStringLiteral("screenshots/copyToClipboard"), false).toBool());
    m_screenshotSubtitles = new QCheckBox(
        tr("Include subtitles"), page);
    m_screenshotSubtitles->setChecked(settings.value(
        QStringLiteral("screenshots/includeSubtitles"), true).toBool());
    m_screenshotPreview = new QCheckBox(
        tr("Show preview after capture"), page);
    m_screenshotPreview->setChecked(settings.value(
        QStringLiteral("screenshots/showPreview"), true).toBool());
    layout->addWidget(m_screenshotSave);
    layout->addWidget(m_screenshotClipboard);
    layout->addWidget(m_screenshotSubtitles);
    layout->addWidget(m_screenshotPreview);
    auto *screenshotForm = new QFormLayout;
    auto *folderLine = new QHBoxLayout;
    const QString defaultFolder =
        QDir(QStandardPaths::writableLocation(
                 QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("Screenshots"));
    m_screenshotFolder = new QLineEdit(settings.value(
        QStringLiteral("screenshots/folder"), defaultFolder).toString(),
        page);
    auto *chooseFolder = smallButton(tr("Choose…"), page);
    folderLine->addWidget(m_screenshotFolder, 1);
    folderLine->addWidget(chooseFolder);
    connect(chooseFolder, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("Screenshot Folder"),
            m_screenshotFolder->text());
        if (!folder.isEmpty()) {
            m_screenshotFolder->setText(QDir::toNativeSeparators(folder));
        }
    });
    m_screenshotFormat = new QComboBox(page);
    m_screenshotFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_screenshotFormat->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    m_screenshotFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    m_screenshotFormat->setCurrentIndex(std::max(
        0, m_screenshotFormat->findData(settings.value(
            QStringLiteral("screenshots/format"),
            QStringLiteral("png")).toString())));
    screenshotForm->addRow(tr("Save to"), folderLine);
    screenshotForm->addRow(tr("Format"), m_screenshotFormat);
    layout->addLayout(screenshotForm);
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::createNetworkPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(9);
    const QSettings settings;

    auto *cacheTitle = new QLabel(tr("Streaming and Cache"), page);
    QFont titleFont = cacheTitle->font();
    titleFont.setBold(true);
    cacheTitle->setFont(titleFont);
    layout->addWidget(cacheTitle);
    m_cacheEnabled = new QCheckBox(
        tr("Enable cache for network media"), page);
    m_cacheEnabled->setChecked(settings.value(
        QStringLiteral("network/cacheEnabled"), true).toBool());
    m_cacheOnDisk = new QCheckBox(
        tr("Store stream cache in a temporary disk file"), page);
    m_cacheOnDisk->setChecked(settings.value(
        QStringLiteral("network/cacheOnDisk"), false).toBool());
    layout->addWidget(m_cacheEnabled);
    layout->addWidget(m_cacheOnDisk);
    auto *cacheForm = new QFormLayout;
    m_cacheSeconds = new QSpinBox(page);
    m_cacheSeconds->setRange(0, 86'400);
    m_cacheSeconds->setSuffix(tr(" s"));
    m_cacheSeconds->setValue(settings.value(
        QStringLiteral("network/cacheSeconds"), 60).toInt());
    m_cacheMemory = new QSpinBox(page);
    m_cacheMemory->setRange(16, 4096);
    m_cacheMemory->setSuffix(tr(" MiB"));
    m_cacheMemory->setValue(settings.value(
        QStringLiteral("network/cacheMemoryMiB"), 150).toInt());
    m_networkTimeout = new QSpinBox(page);
    m_networkTimeout->setRange(1, 3600);
    m_networkTimeout->setSuffix(tr(" s"));
    m_networkTimeout->setValue(settings.value(
        QStringLiteral("network/timeoutSeconds"), 60).toInt());
    cacheForm->addRow(tr("Read ahead"), m_cacheSeconds);
    cacheForm->addRow(tr("Maximum memory"), m_cacheMemory);
    cacheForm->addRow(tr("Network timeout"), m_networkTimeout);
    layout->addLayout(cacheForm);

    auto *requestTitle = new QLabel(tr("HTTP Requests"), page);
    requestTitle->setFont(titleFont);
    layout->addWidget(requestTitle);
    auto *requestForm = new QFormLayout;
    m_proxy = new QLineEdit(settings.value(
        QStringLiteral("network/proxy")).toString(), page);
    m_proxy->setPlaceholderText(
        QStringLiteral("http://127.0.0.1:8080"));
    m_userAgent = new QLineEdit(settings.value(
        QStringLiteral("network/userAgent")).toString(), page);
    m_referrer = new QLineEdit(settings.value(
        QStringLiteral("network/referrer")).toString(), page);
    m_cookiesFile = new QLineEdit(settings.value(
        QStringLiteral("network/cookiesFile")).toString(), page);
    auto *cookiesRow = new QHBoxLayout;
    cookiesRow->addWidget(m_cookiesFile, 1);
    auto *chooseCookies = smallButton(tr("Choose…"), page);
    cookiesRow->addWidget(chooseCookies);
    requestForm->addRow(tr("Proxy"), m_proxy);
    requestForm->addRow(tr("User agent"), m_userAgent);
    requestForm->addRow(tr("Referrer"), m_referrer);
    requestForm->addRow(tr("Netscape cookies file"), cookiesRow);
    layout->addLayout(requestForm);
    connect(chooseCookies, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose Cookies File"), m_cookiesFile->text(),
            tr("Cookies (*.txt);;All Files (*)"));
        if (!path.isEmpty()) m_cookiesFile->setText(path);
    });

    auto *ytdlTitle = new QLabel(tr("yt-dlp"), page);
    ytdlTitle->setFont(titleFont);
    layout->addWidget(ytdlTitle);
    m_ytdlEnabled = new QCheckBox(
        tr("Enable yt-dlp for supported websites"), page);
    m_ytdlEnabled->setChecked(settings.value(
        QStringLiteral("network/ytdlEnabled"), true).toBool());
    m_tryYtdlFirst = new QCheckBox(
        tr("Try yt-dlp before direct network playback"), page);
    m_tryYtdlFirst->setChecked(settings.value(
        QStringLiteral("network/tryYtdlFirst"), false).toBool());
    m_ytdlSubtitles = new QCheckBox(
        tr("Load subtitles exposed by yt-dlp"), page);
    m_ytdlSubtitles->setChecked(settings.value(
        QStringLiteral("network/includeSubtitles"), true).toBool());
    m_ytdlAutomaticSubtitles = new QCheckBox(
        tr("Include automatically generated subtitles"), page);
    m_ytdlAutomaticSubtitles->setChecked(settings.value(
        QStringLiteral("network/includeAutomaticSubtitles"), false).toBool());
    layout->addWidget(m_ytdlEnabled);
    layout->addWidget(m_tryYtdlFirst);
    layout->addWidget(m_ytdlSubtitles);
    layout->addWidget(m_ytdlAutomaticSubtitles);
    auto *ytdlForm = new QFormLayout;
    m_ytdlPath = new QLineEdit(settings.value(
        QStringLiteral("network/ytdlPath")).toString(), page);
    m_ytdlPath->setPlaceholderText(
        tr("Empty: search PATH for yt-dlp"));
    auto *ytdlPathRow = new QHBoxLayout;
    ytdlPathRow->addWidget(m_ytdlPath, 1);
    auto *chooseYtdl = smallButton(tr("Choose…"), page);
    auto *checkYtdl = smallButton(tr("Check"), page);
    auto *updateYtdl = smallButton(tr("Download / Update"), page);
    ytdlPathRow->addWidget(chooseYtdl);
    ytdlPathRow->addWidget(checkYtdl);
    ytdlPathRow->addWidget(updateYtdl);
    m_javascriptRuntime = new QLineEdit(settings.value(
        QStringLiteral("network/javascriptRuntime")).toString(), page);
    m_javascriptRuntime->setPlaceholderText(
        tr("Optional Deno, Node, QuickJS, or Bun path"));
    m_ytdlFormat = new QLineEdit(settings.value(
        QStringLiteral("network/ytdlFormat"),
        QStringLiteral("bestvideo+bestaudio/best")).toString(), page);
    m_ytdlRawOptions = new QLineEdit(settings.value(
        QStringLiteral("network/ytdlRawOptions")).toString(), page);
    m_ytdlRawOptions->setPlaceholderText(
        tr("mpv key=value list"));
    ytdlForm->addRow(tr("Executable"), ytdlPathRow);
    ytdlForm->addRow(tr("JavaScript runtime"), m_javascriptRuntime);
    ytdlForm->addRow(tr("Format selection"), m_ytdlFormat);
    ytdlForm->addRow(tr("Raw options"), m_ytdlRawOptions);
    layout->addLayout(ytdlForm);
    connect(chooseYtdl, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose yt-dlp"), m_ytdlPath->text(),
            tr("Executables (*.exe);;All Files (*)"));
        if (!path.isEmpty()) m_ytdlPath->setText(path);
    });
    connect(checkYtdl, &QPushButton::clicked, this, [this] {
        QString executable = m_ytdlPath->text().trimmed();
        if (executable.isEmpty()) {
            executable = QStandardPaths::findExecutable(
                QStringLiteral("yt-dlp"));
        }
        if (executable.isEmpty()) {
            QMessageBox::warning(
                this, tr("yt-dlp"),
                tr("yt-dlp was not found. Choose it or use "
                   "Download / Update."));
            return;
        }
        QProcess process;
        process.start(executable, {QStringLiteral("--version")});
        if (!process.waitForStarted(5'000)
            || !process.waitForFinished(10'000)
            || process.exitCode() != 0) {
            QMessageBox::warning(
                this, tr("yt-dlp"),
                tr("yt-dlp could not be started:\n%1")
                    .arg(QString::fromUtf8(
                        process.readAllStandardError()).trimmed()));
            return;
        }
        QMessageBox::information(
            this, tr("yt-dlp"),
            tr("yt-dlp %1 is ready.")
                .arg(QString::fromUtf8(
                    process.readAllStandardOutput()).trimmed()));
    });
    connect(updateYtdl, &QPushButton::clicked, this,
            [this, updateYtdl] {
        updateYtdl->setEnabled(false);
        updateYtdl->setText(tr("Downloading…"));
        auto *network = new QNetworkAccessManager(this);
        QNetworkRequest sumsRequest(QUrl(QStringLiteral(
            "https://github.com/yt-dlp/yt-dlp/releases/latest/"
            "download/SHA2-256SUMS")));
        sumsRequest.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
        sumsRequest.setTransferTimeout(30'000);
        QNetworkReply *sumsReply = network->get(sumsRequest);
        connect(sumsReply, &QNetworkReply::finished, this,
                [this, network, sumsReply, updateYtdl] {
            const auto finish = [network, updateYtdl] {
                updateYtdl->setEnabled(true);
                updateYtdl->setText(
                    QObject::tr("Download / Update"));
                network->deleteLater();
            };
            if (sumsReply->error() != QNetworkReply::NoError) {
                const QString error = sumsReply->errorString();
                sumsReply->deleteLater();
                finish();
                QMessageBox::warning(
                    this, tr("yt-dlp"),
                    tr("Could not download the official checksum: %1")
                        .arg(error));
                return;
            }
            const QString sums =
                QString::fromUtf8(sumsReply->readAll());
            sumsReply->deleteLater();
            const QRegularExpression expression(
                QStringLiteral(
                    R"((?im)^([0-9a-f]{64})\s+\*?yt-dlp\.exe\s*$)"));
            const QRegularExpressionMatch match =
                expression.match(sums);
            if (!match.hasMatch()) {
                finish();
                QMessageBox::warning(
                    this, tr("yt-dlp"),
                    tr("The official release did not contain a checksum "
                       "for yt-dlp.exe."));
                return;
            }
            const QByteArray expected =
                match.captured(1).toLatin1().toLower();
            QNetworkRequest binaryRequest(QUrl(QStringLiteral(
                "https://github.com/yt-dlp/yt-dlp/releases/latest/"
                "download/yt-dlp.exe")));
            binaryRequest.setAttribute(
                QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::NoLessSafeRedirectPolicy);
            binaryRequest.setTransferTimeout(120'000);
            QNetworkReply *binaryReply =
                network->get(binaryRequest);
            connect(binaryReply, &QNetworkReply::finished, this,
                    [this, network, binaryReply, updateYtdl,
                     expected] {
                const auto finishBinary =
                    [network, updateYtdl] {
                        updateYtdl->setEnabled(true);
                        updateYtdl->setText(
                            QObject::tr("Download / Update"));
                        network->deleteLater();
                    };
                if (binaryReply->error()
                    != QNetworkReply::NoError) {
                    const QString error =
                        binaryReply->errorString();
                    binaryReply->deleteLater();
                    finishBinary();
                    QMessageBox::warning(
                        this, tr("yt-dlp"),
                        tr("Could not download yt-dlp: %1")
                            .arg(error));
                    return;
                }
                const QByteArray binary = binaryReply->readAll();
                binaryReply->deleteLater();
                const QByteArray actual =
                    QCryptographicHash::hash(
                        binary, QCryptographicHash::Sha256).toHex();
                if (binary.isEmpty() || binary.size() > 100 * 1024 * 1024
                    || actual != expected) {
                    finishBinary();
                    QMessageBox::critical(
                        this, tr("yt-dlp"),
                        tr("The downloaded executable failed its official "
                           "SHA-256 verification and was not saved."));
                    return;
                }
                const QString folder = QDir(
                    QStandardPaths::writableLocation(
                        QStandardPaths::AppLocalDataLocation))
                    .filePath(QStringLiteral("yt-dlp"));
                QDir().mkpath(folder);
                const QString path =
                    QDir(folder).filePath(QStringLiteral("yt-dlp.exe"));
                QSaveFile output(path);
                if (!output.open(QIODevice::WriteOnly)
                    || output.write(binary) != binary.size()
                    || !output.commit()) {
                    finishBinary();
                    QMessageBox::warning(
                        this, tr("yt-dlp"),
                        tr("Could not save yt-dlp to %1.").arg(path));
                    return;
                }
                m_ytdlPath->setText(path);
                finishBinary();
                QMessageBox::information(
                    this, tr("yt-dlp"),
                    tr("The verified yt-dlp executable was installed. "
                       "Apply preferences and restart the player."));
            });
        });
    });
    auto *restart = new QLabel(
        tr("Network and yt-dlp changes apply after restarting the player."),
        page);
    restart->setWordWrap(true);
    restart->setStyleSheet(
        QStringLiteral("color: rgba(235,185,90,200);"));
    layout->addWidget(restart);
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::createOnlineSubtitlesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    const QSettings settings;

    auto *explanation = new QLabel(
        tr("Search is available from the Video menu and the player "
           "right-click menu. Provider credentials belong to you and are "
           "never written to logs."),
        page);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *form = new QFormLayout;
    m_onlineSubtitleLanguages = new QLineEdit(settings.value(
        QStringLiteral("onlineSubtitles/languages"),
        QStringLiteral("en")).toString(), page);
    m_onlineSubtitleLanguages->setPlaceholderText(
        QStringLiteral("en,fr,de"));
    m_openSubtitlesApiKey = new QLineEdit(
        SecureCredentialStore::read(
            QStringLiteral("openSubtitlesApiKey")), page);
    m_openSubtitlesApiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_openSubtitlesUsername = new QLineEdit(settings.value(
        QStringLiteral("onlineSubtitles/openSubtitlesUsername"))
        .toString(), page);
    m_openSubtitlesPassword = new QLineEdit(page);
    m_openSubtitlesPassword->setEchoMode(QLineEdit::Password);
    auto *loginRow = new QHBoxLayout;
    loginRow->addWidget(m_openSubtitlesPassword, 1);
    auto *login = smallButton(tr("Log In"), page);
    loginRow->addWidget(login);
    m_openSubtitlesToken = new QLineEdit(
        SecureCredentialStore::read(
            QStringLiteral("openSubtitlesToken")), page);
    m_openSubtitlesToken->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_assrtToken = new QLineEdit(
        SecureCredentialStore::read(
            QStringLiteral("assrtToken")), page);
    m_assrtToken->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    form->addRow(tr("Preferred languages"), m_onlineSubtitleLanguages);
    form->addRow(tr("OpenSubtitles API key"), m_openSubtitlesApiKey);
    form->addRow(
        tr("OpenSubtitles username"), m_openSubtitlesUsername);
    form->addRow(
        tr("OpenSubtitles password"), loginRow);
    form->addRow(
        tr("OpenSubtitles login token (optional)"),
        m_openSubtitlesToken);
    form->addRow(tr("Assrt API token"), m_assrtToken);
    layout->addLayout(form);
    connect(login, &QPushButton::clicked, this, [this, login] {
        const QString apiKey =
            m_openSubtitlesApiKey->text().trimmed();
        const QString username =
            m_openSubtitlesUsername->text().trimmed();
        const QString password =
            m_openSubtitlesPassword->text();
        if (apiKey.isEmpty() || username.isEmpty()
            || password.isEmpty()) {
            QMessageBox::warning(
                this, tr("OpenSubtitles Login"),
                tr("Enter the API key, username, and password."));
            return;
        }
        login->setEnabled(false);
        auto *network = new QNetworkAccessManager(this);
        QNetworkRequest request(QUrl(QStringLiteral(
            "https://api.opensubtitles.com/api/v1/login")));
        request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30'000);
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("Content-Type", "application/json");
        request.setRawHeader("Api-Key", apiKey.toUtf8());
        request.setRawHeader("User-Agent", "Supernova Player v0.1");
        const QJsonObject body{
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), password}};
        QNetworkReply *reply = network->post(
            request, QJsonDocument(body).toJson(
                         QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this,
                [this, login, network, reply] {
            login->setEnabled(true);
            if (reply->error() != QNetworkReply::NoError) {
                const QString error = reply->errorString();
                reply->deleteLater();
                network->deleteLater();
                QMessageBox::warning(
                    this, tr("OpenSubtitles Login"),
                    tr("Login failed: %1").arg(error));
                return;
            }
            const QJsonObject response =
                QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            network->deleteLater();
            const QString token =
                response.value(QStringLiteral("token")).toString();
            if (token.isEmpty()) {
                QMessageBox::warning(
                    this, tr("OpenSubtitles Login"),
                    tr("Login succeeded but no session token was returned."));
                return;
            }
            m_openSubtitlesToken->setText(token);
            m_openSubtitlesPassword->clear();
            QMessageBox::information(
                this, tr("OpenSubtitles Login"),
                tr("Login succeeded. Apply preferences to save the "
                   "protected session token."));
        });
    });
    auto *hint = new QLabel(
        tr("OpenSubtitles requires an application API key. A login token "
           "raises account download limits but is optional. Assrt requires "
           "its 32-character API token. Shooter uses a local-file hash and "
           "does not need credentials."),
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
#ifdef Q_OS_WIN
    settings.setValue(
        QStringLiteral("windows/systemMediaControls"),
        m_systemMediaControls->isChecked());
    settings.setValue(
        QStringLiteral("windows/preventSleep"),
        m_preventSleep->isChecked());
    settings.setValue(
        QStringLiteral("windows/allowDisplaySleepForAudio"),
        m_allowDisplaySleepForAudio->isChecked());
    settings.setValue(
        QStringLiteral("windows/jumpList"),
        m_jumpList->isChecked());
#endif
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
        QStringLiteral("thumbnails/enabled"),
        m_thumbnailEnabled->isChecked());
    settings.setValue(
        QStringLiteral("thumbnails/width"), m_thumbnailWidth->value());
    settings.setValue(
        QStringLiteral("thumbnails/maxCacheMiB"),
        m_thumbnailCacheSize->value());
    settings.setValue(
        QStringLiteral("screenshots/saveToFile"),
        m_screenshotSave->isChecked());
    settings.setValue(
        QStringLiteral("screenshots/copyToClipboard"),
        m_screenshotClipboard->isChecked());
    settings.setValue(
        QStringLiteral("screenshots/includeSubtitles"),
        m_screenshotSubtitles->isChecked());
    settings.setValue(
        QStringLiteral("screenshots/showPreview"),
        m_screenshotPreview->isChecked());
    settings.setValue(
        QStringLiteral("screenshots/folder"),
        m_screenshotFolder->text().trimmed());
    settings.setValue(
        QStringLiteral("screenshots/format"),
        m_screenshotFormat->currentData().toString());
    settings.setValue(
        QStringLiteral("network/cacheEnabled"),
        m_cacheEnabled->isChecked());
    settings.setValue(
        QStringLiteral("network/cacheSeconds"),
        m_cacheSeconds->value());
    settings.setValue(
        QStringLiteral("network/cacheMemoryMiB"),
        m_cacheMemory->value());
    settings.setValue(
        QStringLiteral("network/cacheOnDisk"),
        m_cacheOnDisk->isChecked());
    settings.setValue(
        QStringLiteral("network/timeoutSeconds"),
        m_networkTimeout->value());
    settings.setValue(
        QStringLiteral("network/proxy"), m_proxy->text().trimmed());
    settings.setValue(
        QStringLiteral("network/userAgent"),
        m_userAgent->text().trimmed());
    settings.setValue(
        QStringLiteral("network/referrer"),
        m_referrer->text().trimmed());
    settings.setValue(
        QStringLiteral("network/cookiesFile"),
        m_cookiesFile->text().trimmed());
    settings.setValue(
        QStringLiteral("network/ytdlEnabled"),
        m_ytdlEnabled->isChecked());
    settings.setValue(
        QStringLiteral("network/ytdlPath"),
        m_ytdlPath->text().trimmed());
    settings.setValue(
        QStringLiteral("network/javascriptRuntime"),
        m_javascriptRuntime->text().trimmed());
    settings.setValue(
        QStringLiteral("network/ytdlFormat"),
        m_ytdlFormat->text().trimmed());
    settings.setValue(
        QStringLiteral("network/ytdlRawOptions"),
        m_ytdlRawOptions->text().trimmed());
    settings.setValue(
        QStringLiteral("network/tryYtdlFirst"),
        m_tryYtdlFirst->isChecked());
    settings.setValue(
        QStringLiteral("network/includeSubtitles"),
        m_ytdlSubtitles->isChecked());
    settings.setValue(
        QStringLiteral("network/includeAutomaticSubtitles"),
        m_ytdlAutomaticSubtitles->isChecked());
    settings.setValue(
        QStringLiteral("onlineSubtitles/languages"),
        m_onlineSubtitleLanguages->text().trimmed());
    settings.setValue(
        QStringLiteral("onlineSubtitles/openSubtitlesUsername"),
        m_openSubtitlesUsername->text().trimmed());
    const bool credentialsSaved =
        SecureCredentialStore::write(
            QStringLiteral("openSubtitlesApiKey"),
            m_openSubtitlesApiKey->text().trimmed())
        && SecureCredentialStore::write(
            QStringLiteral("openSubtitlesToken"),
            m_openSubtitlesToken->text().trimmed())
        && SecureCredentialStore::write(
            QStringLiteral("assrtToken"),
            m_assrtToken->text().trimmed());
    if (!credentialsSaved) {
        QMessageBox::warning(
            this, tr("Preferences"),
            tr("One or more provider credentials could not be "
               "protected and were not saved."));
    }
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

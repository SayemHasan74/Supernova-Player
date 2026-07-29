#pragma once

#include "Preferences/PlayerConfiguration.h"

#include <QMainWindow>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QRect>
#include <QUrl>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QResizeEvent;
class QStackedLayout;
class QTimer;
class IinaPlayerChrome;
class TimelinePreview;
class BufferingIndicator;
class ScreenshotPreview;
class MpvVideoSurface;
class PlayerCore;
class ProgressOnlyBar;
class PlaylistPanel;
class MediaSettingsPanel;
class WelcomeView;
class HistoryWindow;
class PreferencesDialog;
class MediaInspector;
class OnlineSubtitleDialog;
class QMenu;
class QPoint;
enum class PlayerCommand;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(PlayerCore *playerCore, QWidget *parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool isRenderContextReady() const noexcept;
    [[nodiscard]] bool isFullScreenMode() const noexcept;

public slots:
    void toggleFullScreen();
    void toggleProgressMode();
    void pauseAndMinimize();
    void togglePlaylist();
    void toggleMediaSettings();
    void toggleMusicMode();
    void togglePictureInPicture();

signals:
    void renderContextReady();
    void openUrlsRequested(const QList<QUrl> &urls);
    void newPlayerRequested(const QList<QUrl> &urls);
    void fullScreenChanged(bool fullScreen);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;

private:
    enum class FullScreenState {
        Windowed,
        Entering,
        FullScreen,
        Exiting,
    };
    enum class CompactMode {
        Normal,
        Music,
        PictureInPicture,
    };

    void beginShutdown();
    void applyDarkWindowFrame();
    void completeFullScreenTransition(bool fullScreen);
    void enterFullScreen();
    void enterProgressMode();
    void exitFullScreen();
    void exitProgressMode();
    void finishEnteringProgressMode();
    void enterCompactMode(CompactMode mode);
    void exitCompactMode();
    void finishEnteringCompactMode();
    void applyAlwaysOnTop(bool enabled);
    void executeCommand(PlayerCommand command);
    bool handleConfiguredKeyPress(QKeyEvent *event);
    void reloadKeyBindings();
    void showPlaybackHistory();
    void showMediaInspector();
    void showOnlineSubtitles();
    void openScreenshotFolder();
    void showPreferences();
    [[nodiscard]] QMenu *menuForCommand(PlayerCommand command) const;
    void openFiles();
    void openUrl();
    void openFolder();
    void addFilesToPlaylist();
    void addUrlToPlaylist();
    void importPlaylist();
    void savePlaylist();
    void requestOpen(const QList<QUrl> &urls);
    void showPlaybackView();
    void showWelcomeView();
    void restoreAfterMinimize();
    void revealPlayerChrome(bool animated = true);
    void showPlaybackContextMenu(const QPoint &globalPosition);
    void positionPlayerChrome();
    void positionPlaybackFeedback();
    void syncFullScreenUi();
    void setupMenus();
    void setupWindowChrome();
    void updateCommandStates();

    PlayerCore *m_playerCore = nullptr;
    MpvVideoSurface *m_videoSurface = nullptr;
    QWidget *m_playbackPage = nullptr;
    IinaPlayerChrome *m_playerChrome = nullptr;
    TimelinePreview *m_timelinePreview = nullptr;
    ScreenshotPreview *m_screenshotPreview = nullptr;
    BufferingIndicator *m_bufferingIndicator = nullptr;
    PlaylistPanel *m_playlistPanel = nullptr;
    MediaSettingsPanel *m_mediaSettingsPanel = nullptr;
    HistoryWindow *m_historyWindow = nullptr;
    PreferencesDialog *m_preferencesDialog = nullptr;
    MediaInspector *m_mediaInspector = nullptr;
    OnlineSubtitleDialog *m_onlineSubtitleDialog = nullptr;
    WelcomeView *m_welcomeView = nullptr;
    ProgressOnlyBar *m_progressBar = nullptr;
    QStackedLayout *m_contentLayout = nullptr;
    QTimer *m_chromeAutoHideTimer = nullptr;
    QTimer *m_singleClickTimer = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QHash<PlayerCommand, QAction *> m_commandActions;
    QList<ConfiguredKeyBinding> m_keyBindings;
    QMenu *m_playbackContextMenu = nullptr;
    QString m_lastOpenDirectory;
    QByteArray m_windowedGeometry;
    QRect m_progressRestoreGeometry;
    QRect m_compactRestoreGeometry;
    Qt::WindowFlags m_standardWindowFlags;
    FullScreenState m_fullScreenState = FullScreenState::Windowed;
    CompactMode m_compactMode = CompactMode::Normal;
    CompactMode m_pendingCompactMode = CompactMode::Normal;
    bool m_windowedWasMaximized = false;
    bool m_progressMode = false;
    bool m_progressRestoreFullScreen = false;
    bool m_progressRestoreMaximized = false;
    bool m_restoreFullScreenAfterMinimize = false;
    bool m_restoreMaximizedAfterMinimize = false;
    bool m_closePending = false;
    bool m_resumeAfterWheelSeek = false;
    bool m_ignoreNextLeftRelease = false;
    bool m_playlistWasVisibleBeforeProgress = false;
    bool m_mediaSettingsWasVisibleBeforeProgress = false;
    bool m_compactRestoreFullScreen = false;
    bool m_compactRestoreMaximized = false;
    bool m_playlistWasVisibleBeforeCompact = false;
    bool m_mediaSettingsWasVisibleBeforeCompact = false;
    bool m_alwaysOnTop = false;
    bool m_applicationEventFilterInstalled = false;
};

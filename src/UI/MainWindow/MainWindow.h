#pragma once

#include <QMainWindow>
#include <QByteArray>
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
class MpvVideoSurface;
class PlayerCore;
class ProgressOnlyBar;

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

signals:
    void renderContextReady();
    void openUrlsRequested(const QList<QUrl> &urls);
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

    void beginShutdown();
    void applyDarkWindowFrame();
    void completeFullScreenTransition(bool fullScreen);
    void enterFullScreen();
    void enterProgressMode();
    void exitFullScreen();
    void exitProgressMode();
    void finishEnteringProgressMode();
    void handleKeyPress(QKeyEvent *event);
    void openFiles();
    void openFolder();
    void requestOpen(const QList<QUrl> &urls);
    void restoreAfterMinimize();
    void revealPlayerChrome(bool animated = true);
    void positionPlayerChrome();
    void syncFullScreenUi();
    void setupMenus();
    void setupWindowChrome();

    PlayerCore *m_playerCore = nullptr;
    MpvVideoSurface *m_videoSurface = nullptr;
    QWidget *m_playbackPage = nullptr;
    IinaPlayerChrome *m_playerChrome = nullptr;
    ProgressOnlyBar *m_progressBar = nullptr;
    QStackedLayout *m_contentLayout = nullptr;
    QTimer *m_chromeAutoHideTimer = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QString m_lastOpenDirectory;
    QByteArray m_windowedGeometry;
    QRect m_progressRestoreGeometry;
    Qt::WindowFlags m_standardWindowFlags;
    FullScreenState m_fullScreenState = FullScreenState::Windowed;
    bool m_windowedWasMaximized = false;
    bool m_progressMode = false;
    bool m_progressRestoreFullScreen = false;
    bool m_progressRestoreMaximized = false;
    bool m_restoreFullScreenAfterMinimize = false;
    bool m_restoreMaximizedAfterMinimize = false;
    bool m_closePending = false;
};

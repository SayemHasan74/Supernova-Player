#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QList>
#include <QUrl>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class MpvVideoSurface;
class PlayerCore;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(PlayerCore *playerCore, QWidget *parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool isRenderContextReady() const noexcept;
    [[nodiscard]] bool isFullScreenMode() const noexcept;

public slots:
    void toggleFullScreen();
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

private:
    enum class FullScreenState {
        Windowed,
        Entering,
        FullScreen,
        Exiting,
    };

    void beginShutdown();
    void completeFullScreenTransition(bool fullScreen);
    void enterFullScreen();
    void exitFullScreen();
    void handleKeyPress(QKeyEvent *event);
    void openFiles();
    void openFolder();
    void requestOpen(const QList<QUrl> &urls);
    void restoreAfterMinimize();
    void syncFullScreenUi();
    void setupMenus();
    void setupWindowChrome();

    PlayerCore *m_playerCore = nullptr;
    MpvVideoSurface *m_videoSurface = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QString m_lastOpenDirectory;
    QByteArray m_windowedGeometry;
    FullScreenState m_fullScreenState = FullScreenState::Windowed;
    bool m_windowedWasMaximized = false;
    bool m_restoreFullScreenAfterMinimize = false;
    bool m_restoreMaximizedAfterMinimize = false;
    bool m_closePending = false;
};

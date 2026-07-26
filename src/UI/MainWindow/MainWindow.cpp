#include "UI/MainWindow/MainWindow.h"

#include "App/MediaSourceResolver.h"
#include "Mpv/MpvVideoSurface.h"
#include "PlayerCore/PlayerCore.h"
#include "UI/Controls/IinaPlayerChrome.h"
#include "UI/Design/DesignTokens.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class ProgressOnlyBar final : public QWidget {
public:
    explicit ProgressOnlyBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(
            tr("Middle-click to restore the player"));
    }

    void setPlayback(double position, double duration)
    {
        m_position = std::max(0.0, position);
        m_duration = std::max(0.0, duration);
        update();
    }

    [[nodiscard]] double percentAt(int x) const noexcept
    {
        if (width() <= 1) {
            return 0.0;
        }
        return std::clamp(
            static_cast<double>(x)
                / static_cast<double>(width() - 1),
            0.0, 1.0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(10, 10, 10));

        const int lineHeight = 4;
        const QRect track(
            0, (height() - lineHeight) / 2,
            width(), lineHeight);
        painter.fillRect(track, QColor(58, 58, 58));

        const double ratio = m_duration > 0.0
            ? std::clamp(m_position / m_duration, 0.0, 1.0)
            : 0.0;
        QRect played = track;
        played.setWidth(
            qRound(static_cast<double>(track.width()) * ratio));
        painter.fillRect(played, QColor(238, 238, 238));
    }

private:
    double m_position = 0.0;
    double m_duration = 0.0;
};

MainWindow::MainWindow(PlayerCore *playerCore, QWidget *parent)
    : QMainWindow(parent),
      m_playerCore(playerCore)
{
    if (!m_playerCore) {
        throw std::invalid_argument("MainWindow requires a PlayerCore");
    }
    setupWindowChrome();
    setupMenus();
    m_progressBar->setPlayback(
        m_playerCore->info().videoPositionSec,
        m_playerCore->info().videoDurationSec);

    connect(m_playerCore, &PlayerCore::stateChanged, this,
            [this](PlayerState state) {
                if (!m_closePending) {
                    return;
                }
                if (state == PlayerState::Idle) {
                    beginShutdown();
                } else if (state == PlayerState::ShutDown) {
                    close();
                }
            });
    connect(
        m_playerCore, &PlayerCore::currentUrlChanged,
        this, [this](const QUrl &url) {
            setToolTip({});
            QString mediaName = url.fileName();
            if (mediaName.isEmpty()) {
                mediaName = url.host();
            }
            if (mediaName.isEmpty()) {
                mediaName = url.toDisplayString();
            }
            setWindowTitle(
                tr("%1 — Supernova").arg(mediaName));
        });
    connect(m_playerCore, &PlayerCore::positionChanged,
            this, [this](double position) {
                m_progressBar->setPlayback(
                    position,
                    m_playerCore->info().videoDurationSec);
            });
    connect(m_playerCore, &PlayerCore::durationChanged,
            this, [this](double duration) {
                m_progressBar->setPlayback(
                    m_playerCore->info().videoPositionSec,
                    duration);
            });
    connect(m_playerCore, &PlayerCore::playbackError,
            this, [this](const QString &message, bool recoverable) {
                setToolTip(message);
                setWindowTitle(
                    recoverable
                        ? tr("Playback Error — Supernova")
                        : tr("Fatal Playback Error — Supernova"));
            });
    connect(m_playerCore, &PlayerCore::mediaLoaded,
            this, [this] { revealPlayerChrome(false); });
}

MainWindow::~MainWindow()
{
    if (QWidget *surface = takeCentralWidget()) {
        delete surface;
    }
    m_videoSurface = nullptr;
}

bool MainWindow::isRenderContextReady() const noexcept
{
    return m_videoSurface && m_videoSurface->isRenderContextReady();
}

bool MainWindow::isFullScreenMode() const noexcept
{
    return m_fullScreenState == FullScreenState::Entering
        || m_fullScreenState == FullScreenState::FullScreen;
}

void MainWindow::toggleFullScreen()
{
    if (m_progressMode) {
        return;
    }
    switch (m_fullScreenState) {
    case FullScreenState::Windowed:
        enterFullScreen();
        return;
    case FullScreenState::FullScreen:
        exitFullScreen();
        return;
    case FullScreenState::Entering:
    case FullScreenState::Exiting:
        // IINA ignores repeated requests while the window system owns the
        // transition. Doing the same prevents contradictory state changes.
        return;
    }
}

void MainWindow::toggleProgressMode()
{
    if (m_progressMode) {
        exitProgressMode();
    } else {
        enterProgressMode();
    }
}

void MainWindow::pauseAndMinimize()
{
    m_playerCore->pause();
    m_restoreFullScreenAfterMinimize = isFullScreenMode();
    m_restoreMaximizedAfterMinimize =
        !m_restoreFullScreenAfterMinimize && isMaximized();
    showMinimized();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) {
        return;
    }
    if (isMinimized()) {
        return;
    }
    if (m_restoreFullScreenAfterMinimize
        || m_restoreMaximizedAfterMinimize) {
        QTimer::singleShot(0, this, &MainWindow::restoreAfterMinimize);
        return;
    }

    const bool fullScreen = isFullScreen();
    if (m_fullScreenState == FullScreenState::Entering && fullScreen) {
        completeFullScreenTransition(true);
    } else if (m_fullScreenState == FullScreenState::Exiting
               && !fullScreen) {
        completeFullScreenTransition(false);
    } else if (m_fullScreenState == FullScreenState::Windowed
               && fullScreen) {
        m_fullScreenState = FullScreenState::FullScreen;
        syncFullScreenUi();
        emit fullScreenChanged(true);
    } else if (m_fullScreenState == FullScreenState::FullScreen
               && !fullScreen) {
        m_fullScreenState = FullScreenState::Windowed;
        syncFullScreenUi();
        emit fullScreenChanged(false);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_playerCore->info().state == PlayerState::ShutDown) {
        event->accept();
        return;
    }

    event->ignore();
    if (m_closePending) {
        return;
    }
    m_closePending = true;

    if (m_playerCore->info().state == PlayerState::Idle) {
        beginShutdown();
    } else {
        m_playerCore->stop();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (MediaSourceResolver::canResolve(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls =
        MediaSourceResolver::fromMimeData(event->mimeData());
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }

    requestOpen(urls);
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool isPlayerSurface =
        watched == m_videoSurface || watched == m_progressBar;
    if (isPlayerSurface
        && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            toggleProgressMode();
            mouseEvent->accept();
            return true;
        }
        if (watched == m_progressBar
            && mouseEvent->button() == Qt::LeftButton) {
            m_playerCore->seekPercent(
                m_progressBar->percentAt(
                    qRound(mouseEvent->position().x()))
                    * 100.0,
                true);
            mouseEvent->accept();
            return true;
        }
    }
    if (watched == m_videoSurface
        && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleFullScreen();
            mouseEvent->accept();
            return true;
        }
    }
    if (watched == m_videoSurface
        && (event->type() == QEvent::MouseMove
            || event->type() == QEvent::Enter)) {
        revealPlayerChrome();
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(
    const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage && m_videoSurface && !m_progressMode) {
        if (nativeMessage->message == WM_ENTERSIZEMOVE) {
            m_videoSurface->setLiveResize(true);
        } else if (nativeMessage->message == WM_EXITSIZEMOVE) {
            m_videoSurface->setLiveResize(false);
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    handleKeyPress(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionPlayerChrome();
}

void MainWindow::handleKeyPress(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        m_playerCore->togglePause();
        event->accept();
        return;
    case Qt::Key_Left:
        m_playerCore->seekRelative(-5.0, true);
        event->accept();
        return;
    case Qt::Key_Right:
        m_playerCore->seekRelative(5.0, true);
        event->accept();
        return;
    default:
        QMainWindow::keyPressEvent(event);
        return;
    }
}

void MainWindow::setupWindowChrome()
{
    setWindowTitle(QStringLiteral("Supernova"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/supernova.ico")));
    setMinimumSize(480, 270);
    setAcceptDrops(true);
    menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar { background: rgb(23,23,25); color: rgb(235,235,240);"
        " border: 0; padding: 2px 5px; }"
        "QMenuBar::item { background: transparent; padding: 4px 9px;"
        " border-radius: 4px; }"
        "QMenuBar::item:selected { background: rgba(255,255,255,28); }"
        "QMenu { background: rgb(31,31,34); color: rgb(240,240,244);"
        " border: 1px solid rgb(65,65,70); padding: 5px; }"
        "QMenu::item { padding: 5px 28px 5px 22px; border-radius: 4px; }"
        "QMenu::item:selected { background: rgb(55,95,155); }"
        "QMenu::separator { height: 1px; background: rgb(68,68,72);"
        " margin: 5px 8px; }"));

    applyDarkWindowFrame();

    QWidget *contentRoot = new QWidget(this);
    m_contentLayout = new QStackedLayout(contentRoot);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setStackingMode(QStackedLayout::StackOne);

    m_playbackPage = new QWidget(contentRoot);
    auto *playbackLayout = new QVBoxLayout(m_playbackPage);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    playbackLayout->setSpacing(0);

    m_videoSurface =
        new MpvVideoSurface(m_playerCore->mpvCore(), m_playbackPage);
    m_videoSurface->setAcceptDrops(false);
    m_videoSurface->setFocusPolicy(Qt::StrongFocus);
    m_videoSurface->setMouseTracking(true);
    m_videoSurface->installEventFilter(this);
    playbackLayout->addWidget(m_videoSurface);
    connect(m_videoSurface, &MpvVideoSurface::renderContextReady,
            this, &MainWindow::renderContextReady);

    m_playerChrome =
        new IinaPlayerChrome(m_playerCore, m_playbackPage);
    m_playerChrome->raise();
    connect(m_playerChrome, &IinaPlayerChrome::activity,
            this, [this] { revealPlayerChrome(); });
    connect(m_playerChrome, &IinaPlayerChrome::fullScreenRequested,
            this, &MainWindow::toggleFullScreen);
    connect(m_playerChrome, &IinaPlayerChrome::progressModeRequested,
            this, &MainWindow::toggleProgressMode);

    m_chromeAutoHideTimer = new QTimer(this);
    m_chromeAutoHideTimer->setSingleShot(true);
    m_chromeAutoHideTimer->setInterval(
        Supernova::Ui::controlAutoHideMs);
    connect(m_chromeAutoHideTimer, &QTimer::timeout,
            m_playerChrome, [this] {
                if (!m_progressMode && !m_playerChrome->underMouse()) {
                    m_playerChrome->conceal();
                    m_videoSurface->setCursor(Qt::BlankCursor);
                } else if (!m_progressMode) {
                    revealPlayerChrome();
                }
            });

    m_progressBar = new ProgressOnlyBar(contentRoot);
    m_progressBar->installEventFilter(this);
    m_contentLayout->addWidget(m_playbackPage);
    m_contentLayout->addWidget(m_progressBar);
    m_contentLayout->setCurrentWidget(m_playbackPage);
    setCentralWidget(contentRoot);
    m_standardWindowFlags = windowFlags();
    QTimer::singleShot(0, this, [this] {
        applyDarkWindowFrame();
        positionPlayerChrome();
        revealPlayerChrome(false);
    });
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *openFilesAction =
        fileMenu->addAction(tr("&Open File…"));
    openFilesAction->setShortcuts(QKeySequence::Open);
    openFilesAction->setStatusTip(
        tr("Open one or more media files"));
    connect(openFilesAction, &QAction::triggered,
            this, &MainWindow::openFiles);

    QAction *openFolderAction =
        fileMenu->addAction(tr("Open &Folder…"));
    openFolderAction->setShortcut(
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    openFolderAction->setStatusTip(
        tr("Open all supported media in a folder"));
    connect(openFolderAction, &QAction::triggered,
            this, &MainWindow::openFolder);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    m_fullScreenAction =
        new QAction(tr("Enter Full Screen"), this);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcuts(
        {QKeySequence(Qt::Key_F),
         QKeySequence(Qt::Key_F11),
         QKeySequence(Qt::ALT | Qt::Key_Return)});
    m_fullScreenAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_fullScreenAction, &QAction::triggered,
            this, &MainWindow::toggleFullScreen);
    addAction(m_fullScreenAction);
    viewMenu->addAction(m_fullScreenAction);

    QAction *pauseAndMinimizeAction =
        new QAction(tr("Pause and Minimize"), this);
    pauseAndMinimizeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    pauseAndMinimizeAction->setShortcutContext(Qt::WindowShortcut);
    connect(pauseAndMinimizeAction, &QAction::triggered,
            this, &MainWindow::pauseAndMinimize);
    addAction(pauseAndMinimizeAction);

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::openFiles()
{
    const QString initialDirectory =
        m_lastOpenDirectory.isEmpty()
            ? QDir::homePath()
            : m_lastOpenDirectory;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        tr("Choose Media Files"),
        initialDirectory,
        MediaSourceResolver::mediaDialogFilter());
    if (paths.isEmpty()) {
        return;
    }

    m_lastOpenDirectory = QFileInfo(paths.constFirst()).absolutePath();
    requestOpen(
        MediaSourceResolver::fromUserInputs(paths));
}

void MainWindow::openFolder()
{
    const QString initialDirectory =
        m_lastOpenDirectory.isEmpty()
            ? QDir::homePath()
            : m_lastOpenDirectory;
    const QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Choose Media Folder"),
        initialDirectory,
        QFileDialog::ShowDirsOnly);
    if (path.isEmpty()) {
        return;
    }

    m_lastOpenDirectory = path;
    requestOpen(
        MediaSourceResolver::resolve(
            {QUrl::fromLocalFile(path)}));
}

void MainWindow::requestOpen(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Nothing to Open"),
            tr("The selection does not contain playable media."));
        return;
    }
    emit openUrlsRequested(urls);
}

void MainWindow::enterFullScreen()
{
    if (m_fullScreenState != FullScreenState::Windowed) {
        return;
    }

    m_windowedWasMaximized = isMaximized();
    if (!m_windowedWasMaximized) {
        m_windowedGeometry = saveGeometry();
    }
    m_fullScreenState = FullScreenState::Entering;
    syncFullScreenUi();
    showFullScreen();

    // Qt normally sends WindowStateChange synchronously. Keep a queued
    // completion path for platform plugins that report it later or not at all.
    QTimer::singleShot(0, this, [this] {
        if (m_fullScreenState == FullScreenState::Entering) {
            if (isFullScreen()) {
                completeFullScreenTransition(true);
            } else {
                m_fullScreenState = FullScreenState::Windowed;
                syncFullScreenUi();
            }
        }
    });
}

void MainWindow::enterProgressMode()
{
    if (m_progressMode
        || m_fullScreenState == FullScreenState::Entering
        || m_fullScreenState == FullScreenState::Exiting) {
        return;
    }

    m_progressMode = true;
    m_progressRestoreFullScreen = isFullScreenMode();
    m_progressRestoreMaximized =
        !m_progressRestoreFullScreen && isMaximized();
    if (!m_progressRestoreFullScreen
        && !m_progressRestoreMaximized) {
        m_progressRestoreGeometry = geometry();
    }

    if (m_progressRestoreFullScreen) {
        exitFullScreen();
        QTimer::singleShot(
            0, this, &MainWindow::finishEnteringProgressMode);
    } else {
        finishEnteringProgressMode();
    }
}

void MainWindow::finishEnteringProgressMode()
{
    if (!m_progressMode) {
        return;
    }
    if (m_fullScreenState == FullScreenState::Entering
        || m_fullScreenState == FullScreenState::Exiting) {
        QTimer::singleShot(
            0, this, &MainWindow::finishEnteringProgressMode);
        return;
    }

    const QRect previousFrame = frameGeometry();
    QScreen *targetScreen = screen();
    const QRect available = targetScreen
        ? targetScreen->availableGeometry()
        : previousFrame;
    constexpr int progressHeight = 14;
    const int progressWidth = m_progressRestoreFullScreen
        ? available.width()
        : std::clamp(previousFrame.width(), 240, available.width());
    const int progressX = m_progressRestoreFullScreen
        ? available.left()
        : std::clamp(
              previousFrame.left(), available.left(),
              available.right() - progressWidth + 1);
    const int preferredY = m_progressRestoreFullScreen
        ? available.bottom() - progressHeight + 1
        : previousFrame.bottom() - progressHeight + 1;
    const int progressY = std::clamp(
        preferredY, available.top(),
        available.bottom() - progressHeight + 1);

    // Windows ignores a normal geometry while a window is maximized. Drop to
    // the normal state before applying the compact frame, while retaining the
    // saved state above for an exact restore.
    showNormal();
    m_contentLayout->setCurrentWidget(m_progressBar);
    m_chromeAutoHideTimer->stop();
    menuBar()->hide();
    setMinimumSize(240, progressHeight);
    setMaximumHeight(progressHeight);
    setWindowFlags(
        m_standardWindowFlags
        | Qt::FramelessWindowHint
        | Qt::WindowStaysOnTopHint);
    setGeometry(
        progressX, progressY, progressWidth, progressHeight);
    show();
    raise();
    activateWindow();
    m_progressBar->setFocus(Qt::OtherFocusReason);
}

void MainWindow::exitProgressMode()
{
    if (!m_progressMode) {
        return;
    }

    const bool restoreFullScreen = m_progressRestoreFullScreen;
    const bool restoreMaximized = m_progressRestoreMaximized;
    m_progressMode = false;
    m_progressRestoreFullScreen = false;
    m_progressRestoreMaximized = false;

    setWindowFlags(m_standardWindowFlags);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(480, 270);
    m_contentLayout->setCurrentWidget(m_playbackPage);
    showNormal();

    if (restoreFullScreen) {
        if (m_windowedWasMaximized) {
            showMaximized();
        } else if (!m_windowedGeometry.isEmpty()) {
            restoreGeometry(m_windowedGeometry);
        }
        m_fullScreenState = FullScreenState::Windowed;
        enterFullScreen();
    } else if (restoreMaximized) {
        showMaximized();
        syncFullScreenUi();
    } else {
        const QRect restoreGeometry = m_progressRestoreGeometry;
        if (restoreGeometry.isValid()) {
            setGeometry(restoreGeometry);
            QTimer::singleShot(0, this, [this, restoreGeometry] {
                if (!m_progressMode
                    && m_fullScreenState == FullScreenState::Windowed
                    && !isMaximized()) {
                    setGeometry(restoreGeometry);
                }
            });
        }
        syncFullScreenUi();
    }

    m_videoSurface->setFocus(Qt::OtherFocusReason);
    m_videoSurface->setLiveResize(false);
    m_videoSurface->update();
    revealPlayerChrome(false);
}

void MainWindow::exitFullScreen()
{
    if (m_fullScreenState != FullScreenState::FullScreen) {
        return;
    }

    m_fullScreenState = FullScreenState::Exiting;
    showNormal();
    if (m_windowedWasMaximized) {
        showMaximized();
    } else if (!m_windowedGeometry.isEmpty()) {
        restoreGeometry(m_windowedGeometry);
    }

    QTimer::singleShot(0, this, [this] {
        if (m_fullScreenState == FullScreenState::Exiting) {
            if (!isFullScreen()) {
                completeFullScreenTransition(false);
            } else {
                m_fullScreenState = FullScreenState::FullScreen;
                syncFullScreenUi();
            }
        }
    });
}

void MainWindow::completeFullScreenTransition(bool fullScreen)
{
    m_fullScreenState = fullScreen
        ? FullScreenState::FullScreen
        : FullScreenState::Windowed;
    syncFullScreenUi();
    if (m_videoSurface) {
        m_videoSurface->setFocus(Qt::OtherFocusReason);
        m_videoSurface->update();
    }
    emit fullScreenChanged(fullScreen);
}

void MainWindow::restoreAfterMinimize()
{
    if (isMinimized()) {
        return;
    }

    const bool restoreFullScreen = m_restoreFullScreenAfterMinimize;
    const bool restoreMaximized = m_restoreMaximizedAfterMinimize;
    m_restoreFullScreenAfterMinimize = false;
    m_restoreMaximizedAfterMinimize = false;

    if (restoreFullScreen) {
        m_fullScreenState = FullScreenState::Entering;
        syncFullScreenUi();
        showFullScreen();
        QTimer::singleShot(0, this, [this] {
            if (isFullScreen()) {
                completeFullScreenTransition(true);
            }
        });
    } else if (restoreMaximized) {
        showMaximized();
    }
}

void MainWindow::syncFullScreenUi()
{
    const bool fullScreen = isFullScreenMode();
    menuBar()->setVisible(!fullScreen && !m_progressMode);
    if (m_fullScreenAction) {
        m_fullScreenAction->setChecked(fullScreen);
        m_fullScreenAction->setText(
            fullScreen ? tr("Exit Full Screen")
                       : tr("Enter Full Screen"));
    }
    if (m_playerChrome) {
        m_playerChrome->setFullScreen(fullScreen);
        positionPlayerChrome();
        revealPlayerChrome(false);
    }
}

void MainWindow::applyDarkWindowFrame()
{
#ifdef Q_OS_WIN
    // Preserve the native non-client frame (and therefore the proven smooth
    // drag/resize path), while matching IINA's default dark window material.
    using SetDwmAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    const HWND handle = reinterpret_cast<HWND>(winId());
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        const auto setDwmAttribute = reinterpret_cast<SetDwmAttribute>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (setDwmAttribute) {
            const BOOL enabled = TRUE;
            constexpr DWORD modernDarkMode = 20;
            constexpr DWORD legacyDarkMode = 19;
            if (FAILED(setDwmAttribute(
                    handle, modernDarkMode, &enabled, sizeof(enabled)))) {
                setDwmAttribute(
                    handle, legacyDarkMode, &enabled, sizeof(enabled));
            }
        }
        FreeLibrary(dwm);
    }
    SetWindowPos(
        handle, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
            | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

void MainWindow::revealPlayerChrome(bool animated)
{
    if (!m_playerChrome || m_progressMode) {
        return;
    }
    m_playerChrome->reveal(animated);
    m_videoSurface->unsetCursor();
    m_chromeAutoHideTimer->start();
}

void MainWindow::positionPlayerChrome()
{
    if (!m_playerChrome || !m_playbackPage || m_progressMode) {
        return;
    }
    const int availableWidth = m_playbackPage->width();
    const int availableHeight = m_playbackPage->height();
    const int width = std::clamp(
        availableWidth - 2 * Supernova::Ui::floatingControlEdgeMargin,
        Supernova::Ui::floatingControlMinWidth,
        Supernova::Ui::floatingControlWidth);
    const int x = (availableWidth - width) / 2;
    const int distanceFromBottom = qRound(
        availableHeight
        * Supernova::Ui::floatingControlVerticalPosition);
    const int y = std::clamp(
        availableHeight - distanceFromBottom
            - Supernova::Ui::floatingControlHeight,
        0,
        std::max(0, availableHeight
            - Supernova::Ui::floatingControlHeight
            - Supernova::Ui::floatingControlEdgeMargin));
    m_playerChrome->setGeometry(
        x, y, width, Supernova::Ui::floatingControlHeight);
    m_playerChrome->raise();
}

void MainWindow::beginShutdown()
{
    // libmpv requires every render context to be freed before its core is
    // destroyed. The quit command is asynchronous, so tear down the surface
    // before starting that handshake.
    if (QWidget *surface = takeCentralWidget()) {
        delete surface;
    }
    m_videoSurface = nullptr;
    m_playbackPage = nullptr;
    m_playerChrome = nullptr;
    m_progressBar = nullptr;
    m_contentLayout = nullptr;
    m_playerCore->shutdown();
}

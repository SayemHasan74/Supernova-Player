#include "UI/MainWindow/MainWindow.h"

#include "App/MediaSourceResolver.h"
#include "Mpv/MpvVideoSurface.h"
#include "PlayerCore/PlayerCore.h"

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
#include <QTimer>

#include <stdexcept>

MainWindow::MainWindow(PlayerCore *playerCore, QWidget *parent)
    : QMainWindow(parent),
      m_playerCore(playerCore)
{
    if (!m_playerCore) {
        throw std::invalid_argument("MainWindow requires a PlayerCore");
    }
    setupWindowChrome();
    setupMenus();

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
    if (watched == m_videoSurface
        && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleFullScreen();
            mouseEvent->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    handleKeyPress(event);
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

    m_videoSurface =
        new MpvVideoSurface(m_playerCore->mpvCore(), this);
    m_videoSurface->setAcceptDrops(false);
    m_videoSurface->setFocusPolicy(Qt::StrongFocus);
    m_videoSurface->installEventFilter(this);
    connect(m_videoSurface, &MpvVideoSurface::renderContextReady,
            this, &MainWindow::renderContextReady);
    setCentralWidget(m_videoSurface);
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
    menuBar()->setVisible(!fullScreen);
    if (m_fullScreenAction) {
        m_fullScreenAction->setChecked(fullScreen);
        m_fullScreenAction->setText(
            fullScreen ? tr("Exit Full Screen")
                       : tr("Enter Full Screen"));
    }
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
    m_playerCore->shutdown();
}

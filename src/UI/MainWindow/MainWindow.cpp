#include "UI/MainWindow/MainWindow.h"

#include "App/MediaSourceResolver.h"
#include "Mpv/MpvVideoSurface.h"
#include "PlayerCore/PlayerCore.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>

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

void MainWindow::keyPressEvent(QKeyEvent *event)
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

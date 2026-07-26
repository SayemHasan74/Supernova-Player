#include "UI/MainWindow/MainWindow.h"

#include "App/MediaSourceResolver.h"
#include "Mpv/MpvCore.h"
#include "Mpv/MpvVideoSurface.h"
#include "PlayerCore/PlayerCore.h"
#include "PlayerCore/PlaylistIO.h"
#include "UI/Controls/IinaPlayerChrome.h"
#include "UI/Controls/PlaybackFeedback.h"
#include "UI/Commands/PlayerCommand.h"
#include "UI/Playlist/PlaylistPanel.h"
#include "UI/Design/DesignTokens.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QStackedLayout>
#include <QTimer>
#include <QWheelEvent>
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
                updateCommandStates();
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
            this, [this] {
                updateCommandStates();
                revealPlayerChrome(false);
            });
    connect(m_playerCore, &PlayerCore::playbackStopped,
            this, &MainWindow::updateCommandStates);
    connect(m_playerCore->mpvCore(), &MpvCore::propertyChanged,
            this, [this](const QString &name, const QVariant &) {
                if (name == QStringLiteral("mute")
                    || name == QStringLiteral("volume")
                    || name == QStringLiteral("speed")) {
                    updateCommandStates();
                }
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

void MainWindow::togglePlaylist()
{
    if (!m_playlistPanel || m_progressMode) {
        return;
    }
    m_playlistPanel->setVisible(!m_playlistPanel->isVisible());
    if (m_playlistPanel->isVisible()) {
        m_playlistPanel->setPlaylist(m_playerCore->info().playlist);
        m_playlistPanel->raise();
    }
    positionPlayerChrome();
    positionPlaybackFeedback();
    updateCommandStates();
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
    QWidget *watchedWidget = qobject_cast<QWidget *>(watched);
    const bool isPlaybackUi =
        watchedWidget
        && (watchedWidget == m_playbackPage
            || watchedWidget == m_videoSurface
            || watchedWidget == m_playerChrome
            || (m_playbackPage
                && m_playbackPage->isAncestorOf(watchedWidget)));
    const bool isPlaylistUi =
        watchedWidget && m_playlistPanel
        && (watchedWidget == m_playlistPanel
            || m_playlistPanel->isAncestorOf(watchedWidget));
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
        if (watched == m_videoSurface
            && mouseEvent->button() == Qt::BackButton) {
            executeCommand(PlayerCommand::PreviousMedia);
            mouseEvent->accept();
            return true;
        }
        if (watched == m_videoSurface
            && mouseEvent->button() == Qt::ForwardButton) {
            executeCommand(PlayerCommand::NextMedia);
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
            if (m_singleClickTimer) {
                m_singleClickTimer->stop();
            }
            m_ignoreNextLeftRelease = true;
            toggleFullScreen();
            mouseEvent->accept();
            return true;
        }
    }
    if (watched == m_videoSurface
        && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && m_ignoreNextLeftRelease) {
            m_ignoreNextLeftRelease = false;
        } else if (mouseEvent->button() == Qt::LeftButton
            && m_singleClickTimer) {
            m_singleClickTimer->start(
                QApplication::doubleClickInterval());
        }
    }
    if (isPlaybackUi && !isPlaylistUi
        && event->type() == QEvent::ContextMenu) {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        updateCommandStates();
        m_playbackContextMenu->popup(contextEvent->globalPos());
        contextEvent->accept();
        return true;
    }
    if (watched == m_videoSurface
        && event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        if (!isLoaded(m_playerCore->info().state)) {
            return false;
        }

        if (wheelEvent->phase() == Qt::ScrollEnd) {
            if (m_resumeAfterWheelSeek) {
                m_playerCore->resume();
                m_resumeAfterWheelSeek = false;
            }
            wheelEvent->accept();
            return true;
        }

        const QPoint pixels = wheelEvent->pixelDelta();
        const QPoint angles = wheelEvent->angleDelta();
        const double x = !pixels.isNull()
            ? pixels.x() / 40.0 : angles.x() / 120.0;
        const double y = !pixels.isNull()
            ? pixels.y() / 40.0 : angles.y() / 120.0;
        const bool horizontal =
            wheelEvent->modifiers().testFlag(Qt::ShiftModifier)
            || std::abs(x) > std::abs(y);
        if (horizontal) {
            if (wheelEvent->phase() == Qt::ScrollBegin
                && m_playerCore->info().state == PlayerState::Playing) {
                m_resumeAfterWheelSeek = true;
                m_playerCore->pause();
            }
            double delta = std::abs(x) > std::abs(y) ? x : y;
            if (wheelEvent->inverted()) {
                delta = -delta;
            }
            if (std::abs(delta) > 0.001) {
                m_playerCore->seekRelative(delta * 3.0, false);
            }
        } else {
            double delta = y;
            if (wheelEvent->inverted()) {
                delta = -delta;
            }
            if (std::abs(delta) > 0.001) {
                m_playerCore->setVolume(
                    m_playerCore->info().volume + delta * 3.0);
            }
        }
        wheelEvent->accept();
        return true;
    }
    if (watched == m_videoSurface
        && event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        switch (gesture->gestureType()) {
        case Qt::BeginNativeGesture:
            break;
        case Qt::ZoomNativeGesture:
            if (!isFullScreenMode() && !m_progressMode) {
                const double factor =
                    std::clamp(1.0 + gesture->value(), 0.85, 1.15);
                QSize target(
                    qRound(width() * factor),
                    qRound(height() * factor));
                target = target.expandedTo(minimumSize());
                if (QScreen *targetScreen = screen()) {
                    target = target.boundedTo(
                        targetScreen->availableGeometry().size());
                }
                resize(target);
            }
            break;
        case Qt::SmartZoomNativeGesture:
            toggleFullScreen();
            break;
        case Qt::PanNativeGesture: {
            const QPointF delta = gesture->delta();
            if (std::abs(delta.x()) > std::abs(delta.y())) {
                m_playerCore->seekRelative(delta.x() * 0.15, false);
            } else {
                m_playerCore->setVolume(
                    m_playerCore->info().volume - delta.y() * 0.1);
            }
            break;
        }
        case Qt::SwipeNativeGesture: {
            const double horizontal =
                std::abs(gesture->delta().x()) > 0.001
                ? gesture->delta().x() : gesture->value();
            if (std::abs(horizontal) > 0.001) {
                m_playerCore->seekRelative(
                    horizontal > 0.0 ? 10.0 : -10.0, false);
            }
            break;
        }
        default:
            break;
        }
        gesture->accept();
        return true;
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
    QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionPlayerChrome();
    positionPlaybackFeedback();
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
    // Supernova uses IINA's player-window model: commands live in the
    // playback context menu and OSC, not in a permanently docked desktop
    // menu strip above the video.
    menuBar()->setNativeMenuBar(false);
    menuBar()->hide();

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
    connect(m_playerChrome, &IinaPlayerChrome::openFileRequested,
            this, &MainWindow::openFiles);
    connect(m_playerChrome, &IinaPlayerChrome::playlistRequested,
            this, &MainWindow::togglePlaylist);
    connect(m_playerChrome, &IinaPlayerChrome::progressModeRequested,
            this, &MainWindow::toggleProgressMode);
    connect(m_playerChrome, &IinaPlayerChrome::osdRequested,
            this, &MainWindow::showPlaybackOsd);

    m_playbackOsd = new PlaybackOsd(m_playbackPage);
    m_timelinePreview = new TimelinePreview(m_playbackPage);
    m_bufferingIndicator =
        new BufferingIndicator(m_playbackPage);
    m_playlistPanel = new PlaylistPanel(m_playbackPage);
    connect(
        m_playerChrome, &IinaPlayerChrome::previewRequested,
        this, [this](double seconds, const QPoint &globalAnchor) {
            const QPoint anchor =
                m_playbackPage->mapFromGlobal(globalAnchor);
            m_timelinePreview->showTime(
                seconds, anchor, m_playerChrome->y());
        });
    connect(m_playerChrome, &IinaPlayerChrome::previewDismissed,
            m_timelinePreview, &TimelinePreview::dismiss);
    connect(
        m_playerCore, &PlayerCore::bufferingChanged,
        this, [this](const BufferingInfo &buffering) {
            m_bufferingIndicator->updateStatus(
                buffering, m_playerCore->info().isSeeking);
        });
    connect(
        m_playerCore, &PlayerCore::seekingChanged,
        this, [this](bool seeking) {
            m_bufferingIndicator->updateStatus(
                m_playerCore->info().buffering, seeking);
        });
    connect(m_playerCore, &PlayerCore::playlistChanged,
            m_playlistPanel, &PlaylistPanel::setPlaylist);
    connect(m_playerCore, &PlayerCore::positionChanged,
            m_playlistPanel, &PlaylistPanel::setPlaybackPosition);
    connect(m_playerCore, &PlayerCore::durationChanged,
            m_playlistPanel, &PlaylistPanel::setPlaybackDuration);
    connect(m_playlistPanel, &PlaylistPanel::closeRequested,
            this, &MainWindow::togglePlaylist);
    connect(m_playlistPanel, &PlaylistPanel::addRequested,
            this, &MainWindow::addFilesToPlaylist);
    connect(m_playlistPanel, &PlaylistPanel::addUrlRequested,
            this, &MainWindow::addUrlToPlaylist);
    connect(m_playlistPanel, &PlaylistPanel::importRequested,
            this, &MainWindow::importPlaylist);
    connect(m_playlistPanel, &PlaylistPanel::exportRequested,
            this, &MainWindow::savePlaylist);
    connect(m_playlistPanel, &PlaylistPanel::removeRequested,
            m_playerCore, &PlayerCore::removePlaylistItems);
    connect(m_playlistPanel, &PlaylistPanel::clearRequested,
            m_playerCore, &PlayerCore::clearPlaylist);
    connect(m_playlistPanel, &PlaylistPanel::playRequested,
            m_playerCore, &PlayerCore::playPlaylistIndex);
    connect(m_playlistPanel, &PlaylistPanel::playNextRequested,
            m_playerCore, &PlayerCore::playPlaylistItemsNext);
    connect(m_playlistPanel, &PlaylistPanel::moveRequested,
            m_playerCore, &PlayerCore::movePlaylistItems);
    connect(m_playlistPanel, &PlaylistPanel::urlsDropped,
            m_playerCore, &PlayerCore::appendToPlaylist);
    connect(m_playlistPanel, &PlaylistPanel::loopRequested,
            m_playerCore, &PlayerCore::cyclePlaylistLoopMode);
    connect(m_playlistPanel, &PlaylistPanel::shuffleRequested,
            m_playerCore, &PlayerCore::shufflePlaylist);
    connect(m_playlistPanel, &PlaylistPanel::sortRequested,
            m_playerCore, &PlayerCore::sortPlaylist);

    // Route input from every playback overlay through the same window-level
    // handler. In fullscreen the floating chrome can be the mouse target
    // instead of the video widget; IINA likewise handles right mouse at the
    // player-window level so behavior does not depend on the current mode.
    m_playbackPage->installEventFilter(this);
    const auto playbackWidgets =
        m_playbackPage->findChildren<QWidget *>();
    for (QWidget *widget : playbackWidgets) {
        const bool belongsToPlaylist =
            widget == m_playlistPanel
            || (m_playlistPanel
                && m_playlistPanel->isAncestorOf(widget));
        if (!belongsToPlaylist) {
            widget->installEventFilter(this);
        }
    }

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
    m_singleClickTimer = new QTimer(this);
    m_singleClickTimer->setSingleShot(true);
    connect(m_singleClickTimer, &QTimer::timeout, this, [this] {
        if (m_progressMode) {
            return;
        }
        if (m_playerChrome->isConcealed()) {
            revealPlayerChrome();
        } else {
            m_chromeAutoHideTimer->stop();
            m_playerChrome->conceal();
            m_videoSurface->setCursor(Qt::BlankCursor);
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
        positionPlaybackFeedback();
        revealPlayerChrome(false);
    });
}

void MainWindow::setupMenus()
{
    QHash<PlayerMenu, QMenu *> menus;
    menus.insert(PlayerMenu::File, menuBar()->addMenu(tr("&File")));
    menus.insert(
        PlayerMenu::Playback, menuBar()->addMenu(tr("&Playback")));
    menus.insert(PlayerMenu::Video, menuBar()->addMenu(tr("&Video")));
    menus.insert(PlayerMenu::Audio, menuBar()->addMenu(tr("&Audio")));
    menus.insert(PlayerMenu::Window, menuBar()->addMenu(tr("&Window")));

    for (const auto &definition : playerCommandDefinitions()) {
        auto *action = new QAction(definition.title, this);
        action->setObjectName(
            QStringLiteral("playerCommand_%1")
                .arg(static_cast<int>(definition.command)));
        QList<QKeySequence> shortcuts;
        shortcuts.reserve(definition.shortcuts.size());
        for (const QString &shortcut : definition.shortcuts) {
            shortcuts.append(QKeySequence::fromString(
                shortcut, QKeySequence::PortableText));
        }
        action->setShortcuts(shortcuts);
        action->setShortcutContext(Qt::WindowShortcut);
        action->setShortcutVisibleInContextMenu(true);
        action->setCheckable(definition.checkable);
        connect(action, &QAction::triggered, this,
                [this, command = definition.command] {
                    executeCommand(command);
                });
        m_commandActions.insert(definition.command, action);
        menus.value(definition.menu)->addAction(action);
    }

    menus.value(PlayerMenu::File)->insertSeparator(
        m_commandActions.value(PlayerCommand::CloseWindow));
    menus.value(PlayerMenu::Playback)->insertSeparator(
        m_commandActions.value(PlayerCommand::PreviousMedia));
    menus.value(PlayerMenu::Playback)->insertSeparator(
        m_commandActions.value(PlayerCommand::SeekBackward));
    menus.value(PlayerMenu::Playback)->insertSeparator(
        m_commandActions.value(PlayerCommand::SpeedDown));
    menus.value(PlayerMenu::Window)->insertSeparator(
        m_commandActions.value(PlayerCommand::PauseAndMinimize));

    m_fullScreenAction =
        m_commandActions.value(PlayerCommand::ToggleFullScreen);

    m_playbackContextMenu = new QMenu(m_videoSurface);
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::TogglePause));
    m_playbackContextMenu->addSeparator();
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::PreviousMedia));
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::NextMedia));
    m_playbackContextMenu->addSeparator();
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::ToggleMute));
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::TogglePlaylist));
    m_playbackContextMenu->addAction(m_fullScreenAction);
    m_playbackContextMenu->addSeparator();
    for (PlayerMenu menuType :
         {PlayerMenu::File, PlayerMenu::Playback,
          PlayerMenu::Video, PlayerMenu::Audio,
          PlayerMenu::Window}) {
        QMenu *submenu = menus.value(menuType);
        if (!submenu) {
            continue;
        }
        QString title = submenu->title();
        title.remove(QLatin1Char('&'));
        submenu->setTitle(title);
        m_playbackContextMenu->addMenu(submenu);
    }
    updateCommandStates();
    menuBar()->hide();
}

void MainWindow::executeCommand(PlayerCommand command)
{
    switch (command) {
    case PlayerCommand::OpenFile:
        openFiles();
        break;
    case PlayerCommand::OpenFolder:
        openFolder();
        break;
    case PlayerCommand::ImportPlaylist:
        importPlaylist();
        break;
    case PlayerCommand::SavePlaylist:
        savePlaylist();
        break;
    case PlayerCommand::CloseWindow:
        close();
        break;
    case PlayerCommand::QuitApplication:
        qApp->closeAllWindows();
        break;
    case PlayerCommand::TogglePause:
        m_playerCore->togglePause();
        break;
    case PlayerCommand::Stop:
        m_playerCore->stop();
        break;
    case PlayerCommand::PreviousMedia:
        m_playerCore->navigateInPlaylist(false);
        break;
    case PlayerCommand::NextMedia:
        m_playerCore->navigateInPlaylist(true);
        break;
    case PlayerCommand::SeekBackward:
        m_playerCore->seekRelative(-5.0, true);
        break;
    case PlayerCommand::SeekForward:
        m_playerCore->seekRelative(5.0, true);
        break;
    case PlayerCommand::JumpToBeginning:
        m_playerCore->seekAbsolute(0.0);
        break;
    case PlayerCommand::FrameBackward:
        m_playerCore->stepFrame(true);
        break;
    case PlayerCommand::FrameForward:
        m_playerCore->stepFrame(false);
        break;
    case PlayerCommand::VolumeDown:
        m_playerCore->setVolume(m_playerCore->info().volume - 3.0);
        break;
    case PlayerCommand::VolumeUp:
        m_playerCore->setVolume(m_playerCore->info().volume + 3.0);
        break;
    case PlayerCommand::ToggleMute:
        m_playerCore->toggleMute();
        break;
    case PlayerCommand::SpeedDown:
        m_playerCore->setSpeed(
            std::max(0.05, m_playerCore->info().playSpeed / 1.1));
        break;
    case PlayerCommand::SpeedUp:
        m_playerCore->setSpeed(
            std::min(4.0, m_playerCore->info().playSpeed * 1.1));
        break;
    case PlayerCommand::ResetSpeed:
        m_playerCore->setSpeed(1.0);
        break;
    case PlayerCommand::TakeScreenshot:
        m_playerCore->takeScreenshot();
        break;
    case PlayerCommand::ToggleFullScreen:
        toggleFullScreen();
        break;
    case PlayerCommand::ToggleAlwaysOnTop: {
        QAction *action = m_commandActions.value(command);
        setWindowFlag(
            Qt::WindowStaysOnTopHint, action && action->isChecked());
        show();
        break;
    }
    case PlayerCommand::ToggleProgressMode:
        toggleProgressMode();
        break;
    case PlayerCommand::TogglePlaylist:
        togglePlaylist();
        break;
    case PlayerCommand::PauseAndMinimize:
        pauseAndMinimize();
        break;
    }
    updateCommandStates();
}

void MainWindow::updateCommandStates()
{
    const bool loaded = isLoaded(m_playerCore->info().state);
    for (const auto &definition : playerCommandDefinitions()) {
        if (QAction *action = m_commandActions.value(definition.command)) {
            action->setEnabled(
                !definition.requiresMedia || loaded);
        }
    }

    if (QAction *pause =
            m_commandActions.value(PlayerCommand::TogglePause)) {
        pause->setText(
            m_playerCore->info().state == PlayerState::Playing
                ? tr("Pause") : tr("Play"));
    }
    if (QAction *mute =
            m_commandActions.value(PlayerCommand::ToggleMute)) {
        mute->setChecked(m_playerCore->info().isMuted);
        mute->setText(
            m_playerCore->info().isMuted ? tr("Unmute") : tr("Mute"));
    }
    if (QAction *fullScreen =
            m_commandActions.value(PlayerCommand::ToggleFullScreen)) {
        fullScreen->setChecked(isFullScreenMode());
        fullScreen->setText(
            isFullScreenMode()
                ? tr("Exit Full Screen") : tr("Enter Full Screen"));
    }
    if (QAction *progress =
            m_commandActions.value(PlayerCommand::ToggleProgressMode)) {
        progress->setChecked(m_progressMode);
    }
    if (QAction *playlist =
            m_commandActions.value(PlayerCommand::TogglePlaylist)) {
        playlist->setChecked(
            m_playlistPanel && m_playlistPanel->isVisible());
        playlist->setEnabled(!m_progressMode);
    }
    if (QAction *onTop =
            m_commandActions.value(PlayerCommand::ToggleAlwaysOnTop)) {
        onTop->setChecked(windowFlags().testFlag(
            Qt::WindowStaysOnTopHint));
        onTop->setEnabled(!isFullScreenMode() && !m_progressMode);
    }
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

void MainWindow::addFilesToPlaylist()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Media to Playlist"),
        m_lastOpenDirectory.isEmpty()
            ? QDir::homePath() : m_lastOpenDirectory,
        MediaSourceResolver::mediaDialogFilter());
    if (paths.isEmpty()) {
        return;
    }
    m_lastOpenDirectory = QFileInfo(paths.constFirst()).absolutePath();
    m_playerCore->appendToPlaylist(
        MediaSourceResolver::fromUserInputs(paths));
}

void MainWindow::addUrlToPlaylist()
{
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this, tr("Add URL"), tr("Media URL:"),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || input.isEmpty()) {
        return;
    }
    const QUrl url = QUrl::fromUserInput(input);
    if (!url.isValid() || url.scheme().isEmpty()) {
        QMessageBox::warning(
            this, tr("Invalid URL"),
            tr("Enter a complete media URL."));
        return;
    }
    m_playerCore->appendToPlaylist({url});
}

void MainWindow::importPlaylist()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Playlist"),
        m_lastOpenDirectory.isEmpty()
            ? QDir::homePath() : m_lastOpenDirectory,
        tr("Playlists (*.m3u *.m3u8 *.pls);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    const PlaylistIO::ImportResult result =
        PlaylistIO::importFile(path);
    if (!result.succeeded()) {
        QMessageBox::warning(
            this, tr("Could Not Import Playlist"), result.error);
        return;
    }
    m_lastOpenDirectory = QFileInfo(path).absolutePath();
    m_playerCore->appendToPlaylist(result.urls);
}

void MainWindow::savePlaylist()
{
    if (m_playerCore->info().playlist.isEmpty()) {
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Playlist"),
        m_lastOpenDirectory.isEmpty()
            ? QDir::homePath() : m_lastOpenDirectory,
        tr("M3U8 Playlist (*.m3u8)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".m3u8");
    }
    const QString error = PlaylistIO::exportM3u8(
        path, m_playerCore->info().playlist);
    if (!error.isEmpty()) {
        QMessageBox::warning(
            this, tr("Could Not Save Playlist"), error);
        return;
    }
    m_lastOpenDirectory = QFileInfo(path).absolutePath();
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
    m_playlistWasVisibleBeforeProgress =
        m_playlistPanel && m_playlistPanel->isVisible();
    if (m_playlistPanel) {
        m_playlistPanel->hide();
    }
    updateCommandStates();
    if (m_playbackOsd) {
        m_playbackOsd->hideNow();
    }
    if (m_timelinePreview) {
        m_timelinePreview->dismiss();
    }
    if (m_bufferingIndicator) {
        m_bufferingIndicator->hide();
    }
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
    updateCommandStates();
    m_progressRestoreFullScreen = false;
    m_progressRestoreMaximized = false;

    setWindowFlags(m_standardWindowFlags);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(480, 270);
    m_contentLayout->setCurrentWidget(m_playbackPage);
    if (m_playlistWasVisibleBeforeProgress && m_playlistPanel) {
        m_playlistPanel->show();
    }
    m_playlistWasVisibleBeforeProgress = false;
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
    positionPlaybackFeedback();
    m_bufferingIndicator->updateStatus(
        m_playerCore->info().buffering,
        m_playerCore->info().isSeeking);
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
    menuBar()->hide();
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
    updateCommandStates();
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
    // IINA sidebars overlay the content. Opening one must never recenter or
    // resize the floating OSC; it remains anchored to the player window.
    const int availableWidth =
        std::max(1, m_playbackPage->width());
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
    if (m_playlistPanel && m_playlistPanel->isVisible()) {
        m_playlistPanel->raise();
    }
}

void MainWindow::positionPlaybackFeedback()
{
    if (!m_playbackPage || m_progressMode) {
        return;
    }
    if (m_playbackOsd) {
        const int x = std::max(
            12, (m_playbackPage->width() - m_playbackOsd->width()) / 2);
        const int y = std::max(
            12, qRound(m_playbackPage->height() * 0.12));
        m_playbackOsd->move(x, y);
    }
    if (m_bufferingIndicator) {
        m_bufferingIndicator->move(
            std::max(0, (m_playbackPage->width()
                         - m_bufferingIndicator->width()) / 2),
            std::max(0, (m_playbackPage->height()
                         - m_bufferingIndicator->height()) / 2));
    }
    if (m_playlistPanel) {
        const int width = std::clamp(
            m_playbackPage->width() / 3, 285, 370);
        m_playlistPanel->setGeometry(
            std::max(0, m_playbackPage->width() - width),
            0, width, m_playbackPage->height());
        if (m_playlistPanel->isVisible()) {
            m_playlistPanel->raise();
        }
    }
}

void MainWindow::showPlaybackOsd(
    const QString &title, const QString &detail, double progress)
{
    if (!m_playbackOsd || m_progressMode || title.isEmpty()) {
        return;
    }
    positionPlaybackFeedback();
    m_playbackOsd->showMessage(title, detail, progress);
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
    m_playbackOsd = nullptr;
    m_timelinePreview = nullptr;
    m_bufferingIndicator = nullptr;
    m_playlistPanel = nullptr;
    m_progressBar = nullptr;
    m_contentLayout = nullptr;
    m_playerCore->shutdown();
}

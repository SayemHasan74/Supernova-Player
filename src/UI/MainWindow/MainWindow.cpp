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
#include "UI/Media/MediaSettingsPanel.h"
#include "UI/Welcome/WelcomeView.h"
#include "UI/History/HistoryWindow.h"
#include "UI/Preferences/PreferencesDialog.h"
#include "UI/Inspector/MediaInspector.h"
#include "UI/Subtitles/OnlineSubtitleDialog.h"
#include "UI/Music/MusicModeView.h"
#include "UI/Design/DesignTokens.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QDesktopServices>
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
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
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

namespace {
bool shouldIgnorePlaybackShortcutTarget(const QWidget *widget)
{
    return qobject_cast<const QLineEdit *>(widget) != nullptr;
}

QKeySequence keySequenceForMpvKey(const QString &mpvKey)
{
    QString key = mpvKey.trimmed();
    if (key == QStringLiteral(",")) {
        return QKeySequence(Qt::Key_Comma);
    }
    if (key == QStringLiteral(".")) {
        return QKeySequence(Qt::Key_Period);
    }
    const QList<QPair<QString, QString>> names{
        {QStringLiteral("SPACE"), QStringLiteral("Space")},
        {QStringLiteral("LEFT"), QStringLiteral("Left")},
        {QStringLiteral("RIGHT"), QStringLiteral("Right")},
        {QStringLiteral("UP"), QStringLiteral("Up")},
        {QStringLiteral("DOWN"), QStringLiteral("Down")},
        {QStringLiteral("PGUP"), QStringLiteral("PgUp")},
        {QStringLiteral("PGDWN"), QStringLiteral("PgDown")},
        {QStringLiteral("HOME"), QStringLiteral("Home")},
        {QStringLiteral("END"), QStringLiteral("End")},
        {QStringLiteral("ESC"), QStringLiteral("Escape")},
        {QStringLiteral("ENTER"), QStringLiteral("Return")},
        {QStringLiteral("KP_ENTER"), QStringLiteral("Enter")},
        {QStringLiteral("BS"), QStringLiteral("Backspace")},
        {QStringLiteral("DEL"), QStringLiteral("Delete")},
        {QStringLiteral("INS"), QStringLiteral("Insert")}};
    QStringList parts = key.split(QLatin1Char('+'));
    if (!parts.isEmpty()) {
        for (const auto &[mpvName, qtName] : names) {
            if (parts.last().compare(mpvName, Qt::CaseInsensitive) == 0) {
                parts.last() = qtName;
                break;
            }
        }
        key = parts.join(QLatin1Char('+'));
    }
    return QKeySequence::fromString(key, QKeySequence::PortableText);
}
} // namespace

MainWindow::MainWindow(PlayerCore *playerCore, QWidget *parent)
    : QMainWindow(parent),
      m_playerCore(playerCore)
{
    if (!m_playerCore) {
        throw std::invalid_argument("MainWindow requires a PlayerCore");
    }
    setupWindowChrome();
    setupMenus();
    m_historyWindow = new HistoryWindow(this);
    m_historyWindow->setHistory(m_playerCore->history());
    connect(m_historyWindow, &HistoryWindow::playRequested,
            m_playerCore, &PlayerCore::openUrls);
    connect(m_historyWindow, &HistoryWindow::removeRequested,
            m_playerCore, &PlayerCore::removeHistoryEntries);
    connect(m_historyWindow, &HistoryWindow::clearRequested,
            m_playerCore, &PlayerCore::clearHistory);
    connect(m_playerCore, &PlayerCore::historyChanged,
            m_historyWindow, &HistoryWindow::setHistory);
    m_preferencesDialog =
        new PreferencesDialog(m_playerCore, this);
    m_mediaInspector = new MediaInspector(m_playerCore, this);
    m_onlineSubtitleDialog = new OnlineSubtitleDialog(this);
    connect(
        m_onlineSubtitleDialog,
        &OnlineSubtitleDialog::subtitlesReady,
        this, [this](const QStringList &paths) {
            for (const QString &path : paths) {
                m_playerCore->loadExternalSubtitle(
                    QUrl::fromLocalFile(path));
            }
        });
    connect(m_preferencesDialog,
            &PreferencesDialog::keyBindingsChanged,
            this, &MainWindow::reloadKeyBindings);
    reloadKeyBindings();
    // Keep one responder chain for every layer of the player window. This is
    // the Qt equivalent of IINA routing rendering-view input back through its
    // player-window controller, and it remains intact in fullscreen.
    qApp->installEventFilter(this);
    m_applicationEventFilterInstalled = true;
    m_progressBar->setPlayback(
        m_playerCore->info().videoPositionSec,
        m_playerCore->info().videoDurationSec);

    connect(m_playerCore, &PlayerCore::stateChanged, this,
            [this](PlayerState state) {
                updateCommandStates();
                if (state == PlayerState::Idle && !m_closePending) {
                    showWelcomeView();
                }
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
                if (recoverable
                    && isLoaded(m_playerCore->info().state)) {
                    return;
                }
                setWindowTitle(
                    recoverable
                        ? tr("Playback Error — Supernova")
                        : tr("Fatal Playback Error — Supernova"));
            });
    connect(m_playerCore, &PlayerCore::mediaLoaded,
            this, [this] {
                const QUrl url = m_playerCore->info().currentUrl;
                QString mediaName = url.fileName();
                if (mediaName.isEmpty()) {
                    mediaName = url.host();
                }
                if (!mediaName.isEmpty()) {
                    setWindowTitle(
                        tr("%1 — Supernova").arg(mediaName));
                }
                showPlaybackView();
                updateCommandStates();
                revealPlayerChrome(false);
                const bool audioOnly =
                    m_playerCore->info().hasAudio
                    && !m_playerCore->info().hasVideo;
                const bool automaticMusicMode = QSettings().value(
                    QStringLiteral("window/autoMusicMode"), true)
                                                    .toBool();
                if (automaticMusicMode && audioOnly
                    && m_compactMode == CompactMode::Normal) {
                    m_musicModeAutomatic = true;
                    enterCompactMode(CompactMode::Music);
                } else if (!audioOnly && m_musicModeAutomatic
                           && m_compactMode == CompactMode::Music) {
                    m_musicModeAutomatic = false;
                    exitCompactMode();
                }
            });
    connect(m_playerCore, &PlayerCore::playbackStopped,
            this, &MainWindow::updateCommandStates);
    connect(m_playerCore, &PlayerCore::abLoopChanged,
            this, [this](const AbLoopState &) {
                updateCommandStates();
            });
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
    if (m_applicationEventFilterInstalled && qApp) {
        qApp->removeEventFilter(this);
        m_applicationEventFilterInstalled = false;
    }
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
    if (m_compactMode != CompactMode::Normal
        && m_compactMode != CompactMode::Music) {
        exitCompactMode();
        QTimer::singleShot(0, this, &MainWindow::toggleFullScreen);
        return;
    }
    if (m_compactMode == CompactMode::Music
        && m_fullScreenState == FullScreenState::Windowed) {
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
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
    if (m_compactMode == CompactMode::Music) {
        return;
    }
    if (m_compactMode != CompactMode::Normal) {
        exitCompactMode();
    }
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
    if (m_compactMode == CompactMode::Music && m_musicModeView) {
        m_musicModeView->setPlaylistVisible(
            !m_musicModeView->isPlaylistVisible());
        m_playlistPanel->setPlaylist(m_playerCore->info().playlist);
        m_playlistPanel->setHistory(m_playerCore->history());
        m_playlistPanel->setChapters(
            m_playerCore->info().chapters,
            m_playerCore->info().currentChapter);
        if (m_musicModeView->isPlaylistVisible()) {
            m_playlistPanel->raise();
        }
        updateCommandStates();
        return;
    }
    m_playlistPanel->setVisible(!m_playlistPanel->isVisible());
    if (m_playlistPanel->isVisible()) {
        if (m_mediaSettingsPanel) {
            m_mediaSettingsPanel->hide();
        }
        m_playlistPanel->setPlaylist(m_playerCore->info().playlist);
        m_playlistPanel->setHistory(m_playerCore->history());
        m_playlistPanel->setChapters(
            m_playerCore->info().chapters,
            m_playerCore->info().currentChapter);
        m_playlistPanel->raise();
    }
    positionPlayerChrome();
    positionPlaybackFeedback();
    updateCommandStates();
}

void MainWindow::prepareForInitialMedia()
{
    showPlaybackView();
}

void MainWindow::toggleMusicMode()
{
    m_musicModeAutomatic = false;
    if (m_compactMode == CompactMode::Music) {
        if (m_playerCore->info().hasAudio
            && !m_playerCore->info().hasVideo) {
            return;
        }
        exitCompactMode();
    } else {
        enterCompactMode(CompactMode::Music);
    }
}

void MainWindow::togglePictureInPicture()
{
    if (m_compactMode == CompactMode::Music
        && m_playerCore->info().hasAudio
        && !m_playerCore->info().hasVideo) {
        return;
    }
    if (m_compactMode == CompactMode::PictureInPicture) {
        exitCompactMode();
    } else {
        enterCompactMode(CompactMode::PictureInPicture);
    }
}

void MainWindow::showPluginOsd(const QString &message)
{
    if (!m_playbackPage || message.trimmed().isEmpty()) {
        return;
    }
    auto *label = m_playbackPage->findChild<QLabel *>(
        QStringLiteral("pluginOsd"));
    if (!label) {
        label = new QLabel(m_playbackPage);
        label->setObjectName(QStringLiteral("pluginOsd"));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setMaximumWidth(520);
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: white; background: rgba(20,20,22,205);"
            " border: 1px solid rgba(255,255,255,35);"
            " border-radius: 10px; padding: 10px 16px; }"));
    }
    label->setText(message);
    label->adjustSize();
    label->move(
        (m_playbackPage->width() - label->width()) / 2,
        std::max(24, m_playbackPage->height() / 7));
    label->show();
    label->raise();
    const int generation =
        label->property("osdGeneration").toInt() + 1;
    label->setProperty("osdGeneration", generation);
    const QPointer<QLabel> safeLabel(label);
    QTimer::singleShot(2500, label, [safeLabel, generation] {
        if (safeLabel
            && safeLabel->property("osdGeneration").toInt()
                == generation) {
            safeLabel->hide();
        }
    });
}

void MainWindow::toggleMediaSettings()
{
    if (!m_mediaSettingsPanel || m_progressMode
        || !isLoaded(m_playerCore->info().state)) {
        return;
    }
    m_mediaSettingsPanel->setVisible(
        !m_mediaSettingsPanel->isVisible());
    if (m_mediaSettingsPanel->isVisible()) {
        if (m_playlistPanel) {
            m_playlistPanel->hide();
        }
        m_mediaSettingsPanel->raise();
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
    if (isLoaded(m_playerCore->info().state)
        && event->mimeData()->hasUrls()) {
        QList<QUrl> subtitles;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()
                && MediaSourceResolver::supportedSubtitleExtensions()
                       .contains(
                           QFileInfo(url.toLocalFile()).suffix().toLower())) {
                subtitles.append(url);
            }
        }
        if (!subtitles.isEmpty()) {
            for (const QUrl &subtitle : subtitles) {
                m_playerCore->loadExternalSubtitle(subtitle);
            }
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
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
            || watchedWidget == m_musicModeView
            || (m_musicModeView
                && m_musicModeView->isAncestorOf(watchedWidget))
            || (m_playbackPage
                && m_playbackPage->isAncestorOf(watchedWidget)));
    const bool isPlaylistUi =
        watchedWidget && m_playlistPanel
        && (watchedWidget == m_playlistPanel
            || m_playlistPanel->isAncestorOf(watchedWidget));
    const bool isMediaSettingsUi =
        watchedWidget && m_mediaSettingsPanel
        && (watchedWidget == m_mediaSettingsPanel
            || m_mediaSettingsPanel->isAncestorOf(watchedWidget));
    const bool isPlaybackInteractionUi =
        (isPlaybackUi && !isPlaylistUi && !isMediaSettingsUi)
        || watched == m_progressBar;
    if (isPlaybackInteractionUi
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
    if (isPlaybackUi && !isPlaylistUi
        && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            showPlaybackContextMenu(
                mouseEvent->globalPosition().toPoint());
            mouseEvent->accept();
            return true;
        }
    }
    if (m_compactMode == CompactMode::Music
        && isPlaybackInteractionUi
        && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && !m_musicModeView->isInteractiveAt(
                mouseEvent->globalPosition().toPoint())) {
            toggleFullScreen();
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
        if (!m_playbackContextMenu->isVisible()) {
            showPlaybackContextMenu(contextEvent->globalPos());
        }
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
    if (isPlaybackUi && !isPlaylistUi
        && (event->type() == QEvent::MouseMove
            || event->type() == QEvent::Enter)) {
        revealPlayerChrome();
    }
    if (isPlaybackUi
        && event->type() == QEvent::ShortcutOverride) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const QKeySequence pressed(keyEvent->keyCombination());
        bool configured = keyEvent->key() == Qt::Key_Escape;
        for (const ConfiguredKeyBinding &binding :
             std::as_const(m_keyBindings)) {
            if (keySequenceForMpvKey(binding.key).matches(pressed)
                == QKeySequence::ExactMatch) {
                configured = true;
                break;
            }
        }
        if (configured) {
            keyEvent->accept();
            return true;
        }
    }
    if (isPlaybackUi && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (!shouldIgnorePlaybackShortcutTarget(watchedWidget)
            && handleConfiguredKeyPress(keyEvent)) {
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(
    const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage && result
        && nativeMessage->message == WM_NCHITTEST
        && (m_compactMode == CompactMode::PictureInPicture
            || m_compactMode == CompactMode::Music)) {
        const int screenX =
            static_cast<short>(LOWORD(nativeMessage->lParam));
        const int screenY =
            static_cast<short>(HIWORD(nativeMessage->lParam));
        if (m_compactMode == CompactMode::Music) {
            const QPoint global(screenX, screenY);
            *result = isFullScreenMode()
                ? HTCLIENT
                : m_musicModeView
                    && m_musicModeView->isInteractiveAt(global)
                ? HTCLIENT : HTCAPTION;
            return true;
        }
        RECT frame{};
        GetWindowRect(
            reinterpret_cast<HWND>(winId()), &frame);
        constexpr int resizeBorder = 8;
        const bool left = screenX < frame.left + resizeBorder;
        const bool right = screenX >= frame.right - resizeBorder;
        const bool top = screenY < frame.top + resizeBorder;
        const bool bottom = screenY >= frame.bottom - resizeBorder;
        if (top && left) {
            *result = HTTOPLEFT;
        } else if (top && right) {
            *result = HTTOPRIGHT;
        } else if (bottom && left) {
            *result = HTBOTTOMLEFT;
        } else if (bottom && right) {
            *result = HTBOTTOMRIGHT;
        } else if (left) {
            *result = HTLEFT;
        } else if (right) {
            *result = HTRIGHT;
        } else if (top) {
            *result = HTTOP;
        } else if (bottom) {
            *result = HTBOTTOM;
        } else {
            const QPoint local = mapFromGlobal(
                QPoint(screenX, screenY));
            QWidget *child = childAt(local);
            const bool overVisibleChrome =
                m_playerChrome && !m_playerChrome->isConcealed()
                && child
                && (child == m_playerChrome
                    || m_playerChrome->isAncestorOf(child));
            *result = overVisibleChrome ? HTCLIENT : HTCAPTION;
        }
        return true;
    }
    if (nativeMessage
        && m_compactMode == CompactMode::PictureInPicture
        && nativeMessage->message == WM_NCMOUSEMOVE) {
        revealPlayerChrome();
    }
    if (nativeMessage
        && m_compactMode == CompactMode::Music
        && nativeMessage->message == WM_NCMOUSEMOVE
        && m_musicModeView) {
        m_musicModeView->nativePointerMoved();
    }
    if (nativeMessage
        && (m_compactMode == CompactMode::PictureInPicture
            || m_compactMode == CompactMode::Music)
        && nativeMessage->message == WM_NCRBUTTONUP) {
        const QPoint global(
            static_cast<short>(LOWORD(nativeMessage->lParam)),
            static_cast<short>(HIWORD(nativeMessage->lParam)));
        QTimer::singleShot(0, this, [this, global] {
            showPlaybackContextMenu(global);
        });
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage
        && (m_compactMode == CompactMode::PictureInPicture
            || m_compactMode == CompactMode::Music)
        && nativeMessage->message == WM_NCLBUTTONDBLCLK) {
        QTimer::singleShot(0, this, &MainWindow::toggleFullScreen);
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage
        && m_compactMode == CompactMode::PictureInPicture
        && nativeMessage->message == WM_NCMBUTTONUP) {
        QTimer::singleShot(0, this, &MainWindow::toggleProgressMode);
        if (result) {
            *result = 0;
        }
        return true;
    }
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
    if (!shouldIgnorePlaybackShortcutTarget(focusWidget())
        && handleConfiguredKeyPress(event)) {
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_compactMode == CompactMode::Music) {
        return;
    }
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
    connect(
        m_playerChrome,
        &IinaPlayerChrome::pictureInPictureRequested,
        this, &MainWindow::togglePictureInPicture);
    connect(m_playerChrome, &IinaPlayerChrome::openFileRequested,
            this, &MainWindow::openFiles);
    connect(m_playerChrome, &IinaPlayerChrome::playlistRequested,
            this, &MainWindow::togglePlaylist);
    connect(m_playerChrome, &IinaPlayerChrome::mediaSettingsRequested,
            this, &MainWindow::toggleMediaSettings);
    connect(m_playerChrome, &IinaPlayerChrome::progressModeRequested,
            this, &MainWindow::toggleProgressMode);
    m_timelinePreview = new TimelinePreview(m_playbackPage);
    m_screenshotPreview = new ScreenshotPreview(m_playbackPage);
    m_bufferingIndicator =
        new BufferingIndicator(m_playbackPage);
    m_playlistPanel = new PlaylistPanel(m_playbackPage);
    m_mediaSettingsPanel =
        new MediaSettingsPanel(m_playerCore, m_playbackPage);
    connect(
        m_playerChrome, &IinaPlayerChrome::previewRequested,
        this, [this](double seconds, const QPoint &globalAnchor) {
            const QPoint anchor =
                m_playbackPage->mapFromGlobal(globalAnchor);
            m_timelinePreview->showPreview(
                seconds, anchor, m_playerChrome->y(),
                m_playerCore->thumbnailAt(seconds));
        });
    connect(m_playerChrome, &IinaPlayerChrome::previewDismissed,
            m_timelinePreview, &TimelinePreview::dismiss);
    connect(
        m_playerCore, &PlayerCore::screenshotCaptured,
        this,
        [this](const QImage &image, const QUrl &fileUrl, bool) {
            const QSettings settings;
            if (settings.value(
                    QStringLiteral("screenshots/copyToClipboard"),
                    false).toBool()) {
                QApplication::clipboard()->setImage(image);
            }
            if (settings.value(
                    QStringLiteral("screenshots/showPreview"),
                    true).toBool()) {
                m_screenshotPreview->showScreenshot(image, fileUrl);
            }
        });
    connect(
        m_playerCore, &PlayerCore::bufferingChanged,
        this, [this](const BufferingInfo &buffering) {
            m_bufferingIndicator->updateStatus(buffering);
        });
    connect(m_playerCore, &PlayerCore::playlistChanged,
            m_playlistPanel, &PlaylistPanel::setPlaylist);
    connect(m_playerCore, &PlayerCore::historyChanged,
            m_playlistPanel, &PlaylistPanel::setHistory);
    connect(m_playerCore, &PlayerCore::chaptersChanged,
            this, [this](const QList<PlaybackChapter> &chapters) {
                m_playlistPanel->setChapters(
                    chapters, m_playerCore->info().currentChapter);
            });
    connect(m_playerCore, &PlayerCore::chapterChanged,
            m_playlistPanel, &PlaylistPanel::setCurrentChapter);
    connect(m_playerCore, &PlayerCore::positionChanged,
            m_playlistPanel, &PlaylistPanel::setPlaybackPosition);
    connect(m_playerCore, &PlayerCore::durationChanged,
            m_playlistPanel, &PlaylistPanel::setPlaybackDuration);
    connect(m_playlistPanel, &PlaylistPanel::closeRequested,
            this, &MainWindow::togglePlaylist);
    connect(
        m_mediaSettingsPanel, &MediaSettingsPanel::closeRequested,
        this, &MainWindow::toggleMediaSettings);
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
    connect(m_playlistPanel, &PlaylistPanel::chapterRequested,
            m_playerCore, &PlayerCore::playChapter);
    connect(m_playlistPanel, &PlaylistPanel::historyRequested,
            m_playerCore, &PlayerCore::openUrl);
    connect(m_playlistPanel, &PlaylistPanel::removeHistoryRequested,
            m_playerCore, &PlayerCore::removeHistoryEntries);
    connect(m_playlistPanel, &PlaylistPanel::clearHistoryRequested,
            m_playerCore, &PlayerCore::clearHistory);

    m_playlistPanel->setPlaylist(m_playerCore->info().playlist);
    m_playlistPanel->setHistory(m_playerCore->history());
    m_playlistPanel->setChapters(
        m_playerCore->info().chapters,
        m_playerCore->info().currentChapter);

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
    m_musicModeView = new MusicModeView(m_playerCore, contentRoot);
    connect(m_musicModeView, &MusicModeView::closeRequested,
            this, &MainWindow::close);
    connect(m_musicModeView, &MusicModeView::minimizeRequested,
            this, &QWidget::showMinimized);
    connect(m_musicModeView, &MusicModeView::playlistRequested,
            this, &MainWindow::togglePlaylist);
    connect(m_musicModeView, &MusicModeView::openFileRequested,
            this, &MainWindow::openFiles);
    connect(m_musicModeView, &MusicModeView::fullScreenRequested,
            this, &MainWindow::toggleFullScreen);
    connect(m_musicModeView, &MusicModeView::preferredSizeChanged,
            this, &MainWindow::resizeMusicModeWindow);
    m_welcomeView = new WelcomeView(contentRoot);
    m_welcomeView->setHistory(m_playerCore->history());
    m_welcomeView->setRecentMedia(m_playerCore->recentMedia());
    connect(m_welcomeView, &WelcomeView::openFileRequested,
            this, &MainWindow::openFiles);
    connect(m_welcomeView, &WelcomeView::openUrlRequested,
            this, &MainWindow::openUrl);
    connect(m_welcomeView, &WelcomeView::showHistoryRequested,
            this, &MainWindow::showPlaybackHistory);
    connect(m_welcomeView, &WelcomeView::historyRequested,
            this, [this](const QUrl &url) {
                requestOpen({url});
            });
    connect(m_playerCore, &PlayerCore::historyChanged,
            m_welcomeView, &WelcomeView::setHistory);
    connect(m_playerCore, &PlayerCore::recentMediaChanged,
            m_welcomeView, &WelcomeView::setRecentMedia);
    m_contentLayout->addWidget(m_playbackPage);
    m_contentLayout->addWidget(m_musicModeView);
    m_contentLayout->addWidget(m_progressBar);
    m_contentLayout->addWidget(m_welcomeView);
    m_contentLayout->setCurrentWidget(m_welcomeView);
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
        addAction(action);
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

    // Keep the native popup owned by the player window instead of the OpenGL
    // child so it remains above the video composition in fullscreen.
    m_playbackContextMenu = new QMenu(this);
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
    m_playbackContextMenu->addAction(
        m_commandActions.value(PlayerCommand::ToggleMediaSettings));
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
    case PlayerCommand::OpenUrl:
        openUrl();
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
    case PlayerCommand::NewPlayerWindow:
        emit newPlayerRequested({});
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
    case PlayerCommand::PreviousChapter:
        m_playerCore->navigateChapter(false);
        break;
    case PlayerCommand::NextChapter:
        m_playerCore->navigateChapter(true);
        break;
    case PlayerCommand::ToggleAbLoop:
        m_playerCore->toggleAbLoop();
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
        m_playerCore->setVolume(m_playerCore->info().volume - 5.0);
        break;
    case PlayerCommand::VolumeUp:
        m_playerCore->setVolume(m_playerCore->info().volume + 5.0);
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
    case PlayerCommand::OpenScreenshotFolder:
        openScreenshotFolder();
        break;
    case PlayerCommand::FindOnlineSubtitles:
        showOnlineSubtitles();
        break;
    case PlayerCommand::ToggleFullScreen:
        toggleFullScreen();
        break;
    case PlayerCommand::ToggleAlwaysOnTop: {
        QAction *action = m_commandActions.value(command);
        m_alwaysOnTop = action && action->isChecked();
        applyAlwaysOnTop(
            m_alwaysOnTop
            || m_compactMode == CompactMode::PictureInPicture);
        break;
    }
    case PlayerCommand::ToggleMusicMode:
        toggleMusicMode();
        break;
    case PlayerCommand::TogglePictureInPicture:
        togglePictureInPicture();
        break;
    case PlayerCommand::ToggleProgressMode:
        toggleProgressMode();
        break;
    case PlayerCommand::TogglePlaylist:
        togglePlaylist();
        break;
    case PlayerCommand::ToggleMediaSettings:
        toggleMediaSettings();
        break;
    case PlayerCommand::ShowPlaybackHistory:
        showPlaybackHistory();
        break;
    case PlayerCommand::ShowMediaInspector:
        showMediaInspector();
        break;
    case PlayerCommand::ShowPreferences:
        showPreferences();
        break;
    case PlayerCommand::PauseAndMinimize:
        pauseAndMinimize();
        break;
    }
    updateCommandStates();
}

bool MainWindow::handleConfiguredKeyPress(QKeyEvent *event)
{
    if (!event) {
        return false;
    }
    // Escape is a permanent player-level behavior requested for Supernova.
    // It remains available even if a user input profile is malformed.
    if (event->key() == Qt::Key_Escape) {
        executeCommand(PlayerCommand::PauseAndMinimize);
        event->accept();
        return true;
    }

    const QKeySequence pressed(event->keyCombination());
    for (auto it = m_keyBindings.crbegin();
         it != m_keyBindings.crend(); ++it) {
        if (keySequenceForMpvKey(it->key).matches(pressed)
            != QKeySequence::ExactMatch) {
            continue;
        }
        if (it->applicationCommand) {
            PlayerCommand command;
            if (!playerCommandFromIdentifier(it->action, &command)) {
                return true;
            }
            if (event->isAutoRepeat()
                && command != PlayerCommand::SeekBackward
                && command != PlayerCommand::SeekForward
                && command != PlayerCommand::VolumeDown
                && command != PlayerCommand::VolumeUp
                && command != PlayerCommand::FrameBackward
                && command != PlayerCommand::FrameForward) {
                event->accept();
                return true;
            }
            if (QAction *action = m_commandActions.value(command);
                !action || action->isEnabled()) {
                executeCommand(command);
            }
        } else {
            m_playerCore->executeMpvCommand(it->action);
        }
        event->accept();
        return true;
    }
    return false;
}

void MainWindow::reloadKeyBindings()
{
    m_keyBindings = PlayerConfiguration::currentKeyBindings();
    for (QAction *action : std::as_const(m_commandActions)) {
        if (action) {
            action->setShortcuts({});
        }
    }

    QHash<PlayerCommand, QList<QKeySequence>> shortcuts;
    for (auto it = m_keyBindings.crbegin();
         it != m_keyBindings.crend(); ++it) {
        if (!it->applicationCommand) {
            continue;
        }
        PlayerCommand command;
        const QKeySequence shortcut = keySequenceForMpvKey(it->key);
        if (!shortcut.isEmpty()
            && playerCommandFromIdentifier(it->action, &command)) {
            const bool alreadyAssigned = std::any_of(
                shortcuts.cbegin(), shortcuts.cend(),
                [&shortcut](const QList<QKeySequence> &sequences) {
                    return sequences.contains(shortcut);
                });
            if (!alreadyAssigned) {
                shortcuts[command].prepend(shortcut);
            }
        }
    }
    shortcuts[PlayerCommand::PauseAndMinimize].append(
        QKeySequence(Qt::Key_Escape));
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it) {
        if (QAction *action = m_commandActions.value(it.key())) {
            action->setShortcuts(it.value());
        }
    }
}

void MainWindow::showPlaybackHistory()
{
    if (!m_historyWindow) {
        return;
    }
    m_historyWindow->setHistory(m_playerCore->history());
    m_historyWindow->show();
    m_historyWindow->raise();
    m_historyWindow->activateWindow();
}

void MainWindow::showMediaInspector()
{
    if (m_mediaInspector) {
        m_mediaInspector->showInspector();
    }
}

void MainWindow::showOnlineSubtitles()
{
    if (!m_onlineSubtitleDialog) {
        return;
    }
    const QUrl url = m_playerCore->info().currentUrl;
    QString title = m_playerCore->mpvPropertyString(
        QStringLiteral("media-title"));
    if (title.isEmpty()) {
        title = url.fileName();
    }
    m_onlineSubtitleDialog->showForMedia(url, title);
}

void MainWindow::openScreenshotFolder()
{
    const QString fallback =
        QDir(QStandardPaths::writableLocation(
                 QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("Screenshots"));
    const QString folder = QSettings().value(
        QStringLiteral("screenshots/folder"), fallback).toString();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::showPreferences()
{
    if (!m_preferencesDialog) {
        return;
    }
    m_preferencesDialog->show();
    m_preferencesDialog->raise();
    m_preferencesDialog->activateWindow();
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
    if (QAction *abLoop =
            m_commandActions.value(PlayerCommand::ToggleAbLoop)) {
        const AbLoopStatus status = m_playerCore->info().abLoop.status;
        abLoop->setChecked(status != AbLoopStatus::Cleared);
        abLoop->setText(
            status == AbLoopStatus::Cleared
                ? tr("Set A–B Loop Start")
                : status == AbLoopStatus::ASet
                    ? tr("Set A–B Loop End")
                    : tr("Clear A–B Loop"));
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
    if (QAction *music =
            m_commandActions.value(PlayerCommand::ToggleMusicMode)) {
        const bool active = m_compactMode == CompactMode::Music;
        music->setChecked(active);
        music->setText(
            active ? tr("Exit Music Mode") : tr("Enter Music Mode"));
        music->setEnabled(!m_progressMode);
    }
    if (QAction *pip =
            m_commandActions.value(
                PlayerCommand::TogglePictureInPicture)) {
        const bool active =
            m_compactMode == CompactMode::PictureInPicture;
        pip->setChecked(active);
        pip->setText(
            active ? tr("Exit Picture in Picture")
                   : tr("Enter Picture in Picture"));
        pip->setEnabled(loaded && !m_progressMode);
    }
    if (QAction *playlist =
            m_commandActions.value(PlayerCommand::TogglePlaylist)) {
        playlist->setChecked(
            m_playlistPanel && m_playlistPanel->isVisible());
        playlist->setEnabled(
            !m_progressMode
            && m_compactMode != CompactMode::PictureInPicture);
    }
    if (QAction *settings =
            m_commandActions.value(PlayerCommand::ToggleMediaSettings)) {
        settings->setChecked(
            m_mediaSettingsPanel
            && m_mediaSettingsPanel->isVisible());
        settings->setEnabled(
            loaded && !m_progressMode
            && m_compactMode == CompactMode::Normal);
    }
    if (QAction *onTop =
            m_commandActions.value(PlayerCommand::ToggleAlwaysOnTop)) {
        onTop->setChecked(m_alwaysOnTop);
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

void MainWindow::openUrl()
{
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this, tr("Open URL"), tr("Media URL:"),
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
    requestOpen({url});
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
    showPlaybackView();
    emit openUrlsRequested(urls);
}

void MainWindow::showPlaybackView()
{
    if (!m_contentLayout || !m_playbackPage || m_progressMode) {
        return;
    }
    if (m_compactMode == CompactMode::Music && m_musicModeView) {
        m_musicModeView->refresh();
        m_contentLayout->setCurrentWidget(m_musicModeView);
        return;
    }
    const bool leavingWelcome =
        m_welcomeView
        && m_contentLayout->currentWidget() == m_welcomeView;
    m_contentLayout->setCurrentWidget(m_playbackPage);
    if (leavingWelcome && !isMaximized() && !isFullScreenMode()) {
        resize(1280, 720);
    }
    positionPlayerChrome();
    positionPlaybackFeedback();
}

void MainWindow::showWelcomeView()
{
    if (!m_contentLayout || !m_welcomeView || m_progressMode
        || isFullScreenMode()) {
        return;
    }
    if (m_playlistPanel) {
        m_playlistPanel->hide();
    }
    if (m_mediaSettingsPanel) {
        m_mediaSettingsPanel->hide();
    }
    m_welcomeView->setHistory(m_playerCore->history());
    m_welcomeView->setRecentMedia(m_playerCore->recentMedia());
    m_contentLayout->setCurrentWidget(m_welcomeView);
    if (!isMaximized()) {
        resize(640, 400);
    }
    setWindowTitle(QStringLiteral("Supernova"));
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
    m_mediaSettingsWasVisibleBeforeProgress =
        m_mediaSettingsPanel && m_mediaSettingsPanel->isVisible();
    if (m_playlistPanel) {
        m_playlistPanel->hide();
    }
    if (m_mediaSettingsPanel) {
        m_mediaSettingsPanel->hide();
    }
    updateCommandStates();
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

void MainWindow::enterCompactMode(CompactMode mode)
{
    if (mode == CompactMode::Normal || m_progressMode
        || m_fullScreenState == FullScreenState::Entering
        || m_fullScreenState == FullScreenState::Exiting) {
        return;
    }
    if (m_compactMode == mode) {
        return;
    }
    if (m_compactMode != CompactMode::Normal) {
        exitCompactMode();
        QTimer::singleShot(0, this, [this, mode] {
            enterCompactMode(mode);
        });
        return;
    }

    m_pendingCompactMode = mode;
    m_compactRestoreFullScreen = isFullScreenMode();
    m_compactRestoreMaximized =
        !m_compactRestoreFullScreen && isMaximized();
    if (!m_compactRestoreFullScreen
        && !m_compactRestoreMaximized) {
        m_compactRestoreGeometry = geometry();
    }
    m_playlistWasVisibleBeforeCompact =
        m_playlistPanel && m_playlistPanel->isVisible();
    m_mediaSettingsWasVisibleBeforeCompact =
        m_mediaSettingsPanel && m_mediaSettingsPanel->isVisible();

    if (m_compactRestoreFullScreen) {
        exitFullScreen();
        QTimer::singleShot(
            0, this, &MainWindow::finishEnteringCompactMode);
    } else {
        finishEnteringCompactMode();
    }
}

void MainWindow::finishEnteringCompactMode()
{
    if (m_pendingCompactMode == CompactMode::Normal) {
        return;
    }
    if (m_fullScreenState == FullScreenState::Entering
        || m_fullScreenState == FullScreenState::Exiting) {
        QTimer::singleShot(
            0, this, &MainWindow::finishEnteringCompactMode);
        return;
    }

    const CompactMode mode = m_pendingCompactMode;
    m_pendingCompactMode = CompactMode::Normal;
    m_compactMode = mode;
    m_playerChrome->setPictureInPicture(
        mode == CompactMode::PictureInPicture);
    if (m_playlistPanel) {
        m_playlistPanel->hide();
    }
    if (m_mediaSettingsPanel) {
        m_mediaSettingsPanel->hide();
    }
    if (mode == CompactMode::Music && m_musicModeView) {
        m_musicModeView->attachPlaylistPanel(m_playlistPanel);
        m_musicModeView->setPlaylistVisible(false);
        m_musicModeView->refresh();
        m_contentLayout->setCurrentWidget(m_musicModeView);
    }

    showNormal();
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(
        mode == CompactMode::Music && m_musicModeView
            ? m_musicModeView->preferredSize()
                                   : QSize(280, 158));
    Qt::WindowFlags flags = m_standardWindowFlags;
    if (mode == CompactMode::PictureInPicture) {
        flags |= Qt::FramelessWindowHint | Qt::Tool;
    } else if (mode == CompactMode::Music) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        flags |= Qt::FramelessWindowHint;
    }
    setWindowFlags(flags);

    const QRect available = screen()
        ? screen()->availableGeometry()
        : QRect(0, 0, 1280, 720);
    const QSize target =
        mode == CompactMode::Music && m_musicModeView
            ? m_musicModeView->preferredSize()
            : QSize(480, 270);
    const QPoint topRight(
        available.right() - target.width() - 24,
        available.bottom() - target.height() - 24);
    setGeometry(QRect(topRight, target));
    if (mode == CompactMode::Music) {
        setMinimumSize(target);
        setMaximumSize(target);
    }
    show();
    raise();
    activateWindow();
    applyAlwaysOnTop(
        m_alwaysOnTop
        || mode == CompactMode::PictureInPicture);
    if (mode == CompactMode::PictureInPicture) {
        positionPlayerChrome();
        positionPlaybackFeedback();
        revealPlayerChrome(false);
    }
    updateCommandStates();
}

void MainWindow::exitCompactMode()
{
    if (m_compactMode == CompactMode::Normal) {
        return;
    }

    const bool restoreFullScreen = m_compactRestoreFullScreen;
    const bool restoreMaximized = m_compactRestoreMaximized;
    const QRect restoreGeometry = m_compactRestoreGeometry;
    const CompactMode previousMode = m_compactMode;
    m_compactMode = CompactMode::Normal;
    m_playerChrome->setPictureInPicture(false);
    m_pendingCompactMode = CompactMode::Normal;
    m_compactRestoreFullScreen = false;
    m_compactRestoreMaximized = false;

    if (previousMode == CompactMode::Music && m_musicModeView) {
        m_musicModeView->detachPlaylistPanel(m_playbackPage);
        m_contentLayout->setCurrentWidget(m_playbackPage);
        setAttribute(Qt::WA_TranslucentBackground, false);
    }

    setWindowFlags(m_standardWindowFlags);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(480, 270);
    showNormal();
    if (restoreGeometry.isValid()) {
        setGeometry(restoreGeometry);
    }
    if (m_playlistWasVisibleBeforeCompact && m_playlistPanel) {
        m_playlistPanel->show();
    }
    if (m_mediaSettingsWasVisibleBeforeCompact
        && m_mediaSettingsPanel) {
        m_mediaSettingsPanel->show();
    }
    m_playlistWasVisibleBeforeCompact = false;
    m_mediaSettingsWasVisibleBeforeCompact = false;
    applyAlwaysOnTop(m_alwaysOnTop);
    show();

    if (restoreFullScreen) {
        m_fullScreenState = FullScreenState::Windowed;
        enterFullScreen();
    } else if (restoreMaximized) {
        showMaximized();
    }
    positionPlayerChrome();
    positionPlaybackFeedback();
    revealPlayerChrome(false);
    updateCommandStates();
}

void MainWindow::resizeMusicModeWindow()
{
    if (m_compactMode != CompactMode::Music || !m_musicModeView) {
        return;
    }
    const QSize target = m_musicModeView->preferredSize();
    const QPoint topLeft = geometry().topLeft();
    setMinimumSize(target);
    setMaximumSize(target);
    setGeometry(QRect(topLeft, target));
}

void MainWindow::applyAlwaysOnTop(bool enabled)
{
    if (m_playerCore && m_playerCore->mpvCore()
        && m_playerCore->info().state != PlayerState::ShutDown) {
        m_playerCore->mpvCore()->setFlag(
            QStringLiteral("ontop"), enabled);
    }
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(winId());
    SetWindowPos(
        handle, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            | SWP_NOOWNERZORDER);
#else
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        show();
    }
#endif
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
    if (m_mediaSettingsWasVisibleBeforeProgress
        && m_mediaSettingsPanel) {
        m_mediaSettingsPanel->show();
    }
    m_playlistWasVisibleBeforeProgress = false;
    m_mediaSettingsWasVisibleBeforeProgress = false;
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
        m_playerCore->info().buffering);
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
    if (!fullScreen && m_compactMode == CompactMode::Music) {
        resizeMusicModeWindow();
    }
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
    if (m_musicModeView) {
        m_musicModeView->setFullScreen(fullScreen);
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

void MainWindow::showPlaybackContextMenu(
    const QPoint &globalPosition)
{
    if (!m_playbackContextMenu || m_progressMode) {
        return;
    }
    updateCommandStates();
    const QPoint popupPosition =
        globalPosition.isNull() ? QCursor::pos() : globalPosition;
    m_playbackContextMenu->popup(popupPosition);
    m_playbackContextMenu->raise();
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
    if (m_mediaSettingsPanel && m_mediaSettingsPanel->isVisible()) {
        m_mediaSettingsPanel->raise();
    }
}

void MainWindow::positionPlaybackFeedback()
{
    if (!m_playbackPage || m_progressMode) {
        return;
    }
    if (m_compactMode == CompactMode::Music) {
        return;
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
    if (m_mediaSettingsPanel) {
        const int width = std::clamp(
            m_playbackPage->width() / 3, 300, 390);
        m_mediaSettingsPanel->setGeometry(
            std::max(0, m_playbackPage->width() - width),
            0, width, m_playbackPage->height());
        if (m_mediaSettingsPanel->isVisible()) {
            m_mediaSettingsPanel->raise();
        }
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
    m_playbackPage = nullptr;
    m_playerChrome = nullptr;
    m_timelinePreview = nullptr;
    m_screenshotPreview = nullptr;
    m_bufferingIndicator = nullptr;
    m_playlistPanel = nullptr;
    m_mediaSettingsPanel = nullptr;
    m_welcomeView = nullptr;
    m_progressBar = nullptr;
    m_musicModeView = nullptr;
    m_contentLayout = nullptr;
    m_playerCore->shutdown();
}

#include "PlayerCore/PlayerCore.h"

#include "App/MediaSourceResolver.h"
#include "Core/Logger.h"
#include "Mpv/MpvCore.h"

#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QFileInfo>
#include <utility>

namespace {
QString stateName(PlayerState state)
{
    switch (state) {
    case PlayerState::Loading:
        return QStringLiteral("Loading");
    case PlayerState::Starting:
        return QStringLiteral("Starting");
    case PlayerState::Loaded:
        return QStringLiteral("Loaded");
    case PlayerState::Playing:
        return QStringLiteral("Playing");
    case PlayerState::Paused:
        return QStringLiteral("Paused");
    case PlayerState::Stopping:
        return QStringLiteral("Stopping");
    case PlayerState::Idle:
        return QStringLiteral("Idle");
    case PlayerState::ShuttingDown:
        return QStringLiteral("ShuttingDown");
    case PlayerState::ShutDown:
        return QStringLiteral("ShutDown");
    }
    return QStringLiteral("Unknown");
}
}

PlayerCore::PlayerCore(QObject *parent)
    : QObject(parent),
      m_mpv(std::make_unique<MpvCore>())
{
    connect(m_mpv.get(), &MpvCore::propertyChanged,
            this, &PlayerCore::onMpvPropertyChanged);
    connect(m_mpv.get(), &MpvCore::fileStarted,
            this, &PlayerCore::onMpvFileStarted);
    connect(m_mpv.get(), &MpvCore::fileLoaded,
            this, &PlayerCore::onMpvFileLoaded);
    connect(m_mpv.get(), &MpvCore::fileEnded,
            this, &PlayerCore::onMpvFileEnded);
    connect(m_mpv.get(), &MpvCore::videoReconfig,
            this, &PlayerCore::onMpvVideoReconfig);
    connect(m_mpv.get(), &MpvCore::audioReconfig,
            this, &PlayerCore::onMpvAudioReconfig);
    connect(m_mpv.get(), &MpvCore::seekStarted,
            this, &PlayerCore::onMpvSeekStarted);
    connect(m_mpv.get(), &MpvCore::playbackRestarted,
            this, &PlayerCore::onMpvPlaybackRestarted);
    connect(m_mpv.get(), &MpvCore::eventQueueOverflow,
            this, &PlayerCore::onMpvEventQueueOverflow);
    connect(m_mpv.get(), &MpvCore::mpvError,
            this, &PlayerCore::onMpvError);
    connect(m_mpv.get(), &MpvCore::mpvShutdown,
            this, &PlayerCore::onMpvShutdown);

    m_mpv->addHook(
        QStringLiteral("on_load_fail"), 0,
        [](const QString &name, MpvCore::HookContinuation continueHook) {
            Logger::warn(
                QStringLiteral("libmpv hook invoked: %1").arg(name));
            continueHook();
        });

    connect(m_mpv.get(), &MpvCore::mpvLogMessage, this,
            [](const QString &prefix, const QString &level,
               const QString &text) {
                const QString message =
                    QStringLiteral("mpv[%1/%2] %3")
                        .arg(prefix, level, text.trimmed());
                if (level == QStringLiteral("fatal")
                    || level == QStringLiteral("error")) {
                    Logger::error(message);
                } else {
                    Logger::warn(message);
                }
            });
}

PlayerCore::~PlayerCore() = default;

void PlayerCore::openUrls(const QList<QUrl> &urls)
{
    QList<QUrl> validUrls;
    validUrls.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (url.isValid() && !url.isEmpty()) {
            validUrls.append(url);
        }
    }
    if (validUrls.isEmpty() || !canAccessMpv()) {
        return;
    }

    if (m_info.state == PlayerState::Stopping) {
        m_pendingUrls = validUrls;
        Logger::info(
            QStringLiteral("Queued %1 media item(s) until stop completes")
                .arg(validUrls.size()));
        return;
    }

    const QUrl openedUrl = validUrls.constFirst();
    QList<QUrl> queue = validUrls;
    int openedPosition = 0;
    if (validUrls.size() == 1 && openedUrl.isLocalFile()
        && !MediaSourceResolver::supportedPlaylistExtensions().contains(
            QFileInfo(openedUrl.toLocalFile()).suffix().toLower())) {
        queue = MediaSourceResolver::siblingPlaylistFor(openedUrl);
        const QString openedPath =
            QFileInfo(openedUrl.toLocalFile()).canonicalFilePath();
        for (int index = 0; index < queue.size(); ++index) {
            const QString candidate =
                QFileInfo(queue[index].toLocalFile()).canonicalFilePath();
            if (!openedPath.isEmpty()
                && candidate.compare(
                       openedPath, Qt::CaseInsensitive) == 0) {
                openedPosition = index;
                break;
            }
        }
    }

    openPrimaryUrl(openedUrl);
    for (const QUrl &url : std::as_const(queue)) {
        if (url == openedUrl
            || (url.isLocalFile() && openedUrl.isLocalFile()
                && QFileInfo(url.toLocalFile()).absoluteFilePath().compare(
                       QFileInfo(openedUrl.toLocalFile()).absoluteFilePath(),
                       Qt::CaseInsensitive) == 0)) {
            continue;
        }
        const QString source =
            url.isLocalFile() ? url.toLocalFile() : url.toString();
        m_mpv->command(
            {QStringLiteral("loadfile"), source,
             QStringLiteral("append")},
            [source](const MpvCommandResult &result) {
                if (!result.succeeded()) {
                    Logger::warn(
                        QStringLiteral(
                            "Could not append media '%1': %2")
                            .arg(source, result.errorMessage));
                }
            });
    }
    // IINA inserts naturally earlier siblings before the opened file while
    // retaining that file as the playing entry.
    if (validUrls.size() == 1 && queue.size() > 1) {
        for (int index = 0; index < openedPosition; ++index) {
            m_mpv->command(
                {QStringLiteral("playlist-move"),
                 QString::number(index + 1),
                 QString::number(index)});
        }
    }
    if (queue.size() > 1) {
        Logger::info(
            QStringLiteral("Added %1 additional media item(s) to the playlist")
                .arg(queue.size() - 1));
    }
}

void PlayerCore::openUrl(const QUrl &url)
{
    openUrls({url});
}

void PlayerCore::openPrimaryUrl(const QUrl &url)
{
    savePlaybackPosition();
    m_pendingResumePosition =
        m_history.entryFor(url).resumePosition();
    m_info.currentUrl = url;
    emit currentUrlChanged(url);
    m_info.isNetworkResource = !url.isLocalFile();
    m_info.videoWidth = 0;
    m_info.videoHeight = 0;
    m_info.videoPositionSec = 0.0;
    m_info.videoDurationSec = 0.0;
    m_info.hasVideo = false;
    m_info.hasAudio = false;
    setEofReached(false);
    if (m_isSeeking) {
        m_isSeeking = false;
        m_info.isSeeking = false;
        emit seekingChanged(false);
    }
    setBufferingInfo({});
    m_pendingPlaybackError.clear();

    // Pause before loadfile so audio cannot start before the first video frame
    // is ready, which is noticeable with expensive software decoding.
    if (!m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }

    // Delay force-window until an actual GUI load. Setting it during mpv setup
    // can race VO creation against the QOpenGLWidget render-context setup.
    if (QCoreApplication::instance()
        && QCoreApplication::instance()->inherits("QApplication")) {
        m_mpv->setString(
            QStringLiteral("force-window"), QStringLiteral("yes"));
    }

    m_info.justOpenedFile = true;
    setState(PlayerState::Loading);
    const QString source =
        url.isLocalFile() ? url.toLocalFile() : url.toString();
    m_mpv->command(
        {QStringLiteral("loadfile"), source, QStringLiteral("replace")},
        [this, source](const MpvCommandResult &result) {
            if (!result.succeeded() && canAccessMpv()) {
                handlePlaybackFailure(
                    tr("Could not open %1: %2")
                        .arg(source, result.errorMessage),
                    result.errorCode);
            }
        });
    Logger::info(QStringLiteral("Loading media: %1").arg(source));
}

void PlayerCore::togglePause()
{
    if (m_info.state == PlayerState::Paused) {
        resume();
    } else if (isLoaded(m_info.state)) {
        pause();
    }
}

void PlayerCore::pause()
{
    // This guard is intentionally stricter than merely checking that the mpv
    // handle exists. Pausing while idle/stopping can destabilize the renderer.
    if (!isActive(m_info.state)) {
        return;
    }
    m_mpv->setFlag(QStringLiteral("pause"), true);
}

void PlayerCore::resume()
{
    if (!isLoaded(m_info.state)) {
        return;
    }

    const bool restartingAtEof =
        m_info.eofReached
        || m_mpv->getFlag(QStringLiteral("eof-reached"));
    if (restartingAtEof) {
        m_mpv->command(
            {QStringLiteral("seek"), QStringLiteral("0"),
             QStringLiteral("absolute+exact")});
    }
    m_mpv->setFlag(QStringLiteral("pause"), false);
    if (restartingAtEof) {
        setEofReached(false);
        setState(PlayerState::Playing);
    }
}

void PlayerCore::stop()
{
    if (!canAccessMpv() || m_info.state == PlayerState::Idle
        || m_info.state == PlayerState::Stopping) {
        return;
    }

    savePlaybackPosition();
    if (isActive(m_info.state)
        && !m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }
    setState(PlayerState::Stopping);
    m_mpv->command({QStringLiteral("stop")});
}

void PlayerCore::navigateInPlaylist(bool nextMedia)
{
    if (!isLoaded(m_info.state)) {
        return;
    }

    const qint64 playlistCount =
        m_mpv->getInt(QStringLiteral("playlist-count"));
    const qint64 playlistPosition =
        m_mpv->getInt(QStringLiteral("playlist-pos"));
    if (playlistCount <= 1) {
        if (!nextMedia) {
            seekAbsolute(0.0);
        }
        return;
    }
    if (!nextMedia && playlistPosition <= 0) {
        seekAbsolute(0.0);
        return;
    }
    if (nextMedia && playlistPosition >= playlistCount - 1
        && m_info.playlist.loopMode
               != PlaylistLoopMode::Playlist) {
        return;
    }
    savePlaybackPosition();
    // Match the manual-open lifecycle: pause before switching so audio cannot
    // run ahead of a slow video decoder, then resume only when the replacement
    // has loaded and its video surface is ready.
    if (!m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }
    m_info.justOpenedFile = true;
    setState(PlayerState::Loading);
    m_mpv->command(
        {nextMedia ? QStringLiteral("playlist-next")
                   : QStringLiteral("playlist-prev")});
}

void PlayerCore::appendToPlaylist(
    const QList<QUrl> &urls, int index)
{
    if (!canAccessMpv() || urls.isEmpty()) {
        return;
    }
    if (m_info.state == PlayerState::Idle
        && m_info.playlist.isEmpty()) {
        openUrls(urls);
        return;
    }
    const int previousCount = m_info.playlist.size();
    int appended = 0;
    for (const QUrl &url : urls) {
        if (!url.isValid() || url.isEmpty()) {
            continue;
        }
        const QString source =
            url.isLocalFile() ? url.toLocalFile()
                              : url.toString(QUrl::FullyEncoded);
        m_mpv->command(
            {QStringLiteral("loadfile"), source,
             QStringLiteral("append")});
        ++appended;
    }
    if (index >= 0 && index <= previousCount) {
        for (int offset = 0; offset < appended; ++offset) {
            m_mpv->command(
                {QStringLiteral("playlist-move"),
                 QString::number(previousCount + offset),
                 QString::number(index + offset)});
        }
    }
}

void PlayerCore::removePlaylistItems(const QList<int> &indexes)
{
    QList<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    int removed = 0;
    for (int index : std::as_const(sorted)) {
        if (index < 0 || index >= m_info.playlist.size()) {
            continue;
        }
        m_mpv->command(
            {QStringLiteral("playlist-remove"),
             QString::number(index - removed)});
        ++removed;
    }
}

void PlayerCore::clearPlaylist()
{
    if (canAccessMpv()) {
        m_mpv->command({QStringLiteral("playlist-clear")});
    }
}

void PlayerCore::movePlaylistItems(
    const QList<int> &indexes, int destination)
{
    const QList<PlaylistItem> original = m_info.playlist.items;
    if (original.size() < 2) {
        return;
    }
    QList<int> selected = indexes;
    std::sort(selected.begin(), selected.end());
    selected.erase(
        std::remove_if(
            selected.begin(), selected.end(),
            [size = original.size()](int value) {
                return value < 0 || value >= size;
            }),
        selected.end());
    selected.erase(
        std::unique(selected.begin(), selected.end()), selected.end());
    if (selected.isEmpty()) {
        return;
    }

    QList<PlaylistItem> target = original;
    QList<PlaylistItem> moving;
    for (int index : std::as_const(selected)) {
        moving.append(original[index]);
    }
    for (auto it = selected.crbegin(); it != selected.crend(); ++it) {
        target.removeAt(*it);
    }
    int adjusted = std::clamp(
        destination, 0, static_cast<int>(original.size()));
    for (int index : std::as_const(selected)) {
        if (index < destination) {
            --adjusted;
        }
    }
    adjusted = std::clamp(
        adjusted, 0, static_cast<int>(target.size()));
    for (int offset = 0; offset < moving.size(); ++offset) {
        target.insert(adjusted + offset, moving[offset]);
    }

    QList<PlaylistItem> simulated = original;
    for (int targetIndex = 0; targetIndex < target.size(); ++targetIndex) {
        int sourceIndex = targetIndex;
        while (sourceIndex < simulated.size()
               && simulated[sourceIndex].id != target[targetIndex].id) {
            ++sourceIndex;
        }
        if (sourceIndex <= targetIndex || sourceIndex >= simulated.size()) {
            continue;
        }
        m_mpv->command(
            {QStringLiteral("playlist-move"),
             QString::number(sourceIndex),
             QString::number(targetIndex)});
        simulated.move(sourceIndex, targetIndex);
    }
}

void PlayerCore::playPlaylistIndex(int index)
{
    if (index < 0 || index >= m_info.playlist.size()) {
        return;
    }
    savePlaybackPosition();
    if (!m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }
    m_info.justOpenedFile = true;
    setState(PlayerState::Loading);
    m_mpv->command(
        {QStringLiteral("playlist-play-index"),
         QString::number(index)});
}

void PlayerCore::playPlaylistItemsNext(const QList<int> &indexes)
{
    if (m_info.playlist.currentIndex < 0) {
        return;
    }
    movePlaylistItems(indexes, m_info.playlist.currentIndex + 1);
}

void PlayerCore::shufflePlaylist()
{
    if (m_info.playlist.size() < 2) {
        return;
    }
    // IINA treats Shuffle as a momentary action: each press reshuffles the
    // current queue rather than toggling back to a hidden original order.
    m_mpv->command({QStringLiteral("playlist-shuffle")});
}

void PlayerCore::sortPlaylist(PlaylistSortOrder order)
{
    QList<PlaylistItem> target = m_info.playlist.items;
    const bool byName =
        order == PlaylistSortOrder::NameAscending
        || order == PlaylistSortOrder::NameDescending;
    const bool ascending =
        order == PlaylistSortOrder::NameAscending
        || order == PlaylistSortOrder::PathAscending;
    std::stable_sort(
        target.begin(), target.end(),
        [byName, ascending](
            const PlaylistItem &left, const PlaylistItem &right) {
            const QString a =
                byName ? left.displayName : left.url.toDisplayString();
            const QString b =
                byName ? right.displayName : right.url.toDisplayString();
            const int result = QString::localeAwareCompare(a, b);
            return ascending ? result < 0 : result > 0;
        });
    QList<PlaylistItem> simulated = m_info.playlist.items;
    for (int targetIndex = 0; targetIndex < target.size(); ++targetIndex) {
        int sourceIndex = targetIndex;
        while (sourceIndex < simulated.size()
               && simulated[sourceIndex].id != target[targetIndex].id) {
            ++sourceIndex;
        }
        if (sourceIndex <= targetIndex || sourceIndex >= simulated.size()) {
            continue;
        }
        m_mpv->command(
            {QStringLiteral("playlist-move"),
             QString::number(sourceIndex),
             QString::number(targetIndex)});
        simulated.move(sourceIndex, targetIndex);
    }
}

void PlayerCore::cyclePlaylistLoopMode()
{
    PlaylistLoopMode next = PlaylistLoopMode::Off;
    if (m_info.playlist.loopMode == PlaylistLoopMode::Off) {
        next = PlaylistLoopMode::File;
    } else if (m_info.playlist.loopMode == PlaylistLoopMode::File) {
        next = PlaylistLoopMode::Playlist;
    }
    m_mpv->setString(
        QStringLiteral("loop-file"),
        next == PlaylistLoopMode::File
            ? QStringLiteral("inf") : QStringLiteral("no"));
    m_mpv->setString(
        QStringLiteral("loop-playlist"),
        next == PlaylistLoopMode::Playlist
            ? QStringLiteral("inf") : QStringLiteral("no"));
}

void PlayerCore::playChapter(int index)
{
    if (!isLoaded(m_info.state)
        || index < 0 || index >= m_info.chapters.size()) {
        return;
    }
    seekAbsolute(m_info.chapters[index].startTimeSec);
    resume();
}

void PlayerCore::navigateChapter(bool nextChapter)
{
    if (!isLoaded(m_info.state) || m_info.chapters.isEmpty()) {
        return;
    }
    int target = m_info.currentChapter;
    if (nextChapter) {
        target = std::min(
            target + 1, static_cast<int>(m_info.chapters.size()) - 1);
    } else if (target >= 0
               && m_info.videoPositionSec
                      - m_info.chapters[target].startTimeSec > 3.0) {
        // Match mpv/IINA previous-chapter behavior: restart the current
        // chapter unless playback is already close to its beginning.
    } else {
        target = std::max(0, target - 1);
    }
    playChapter(target);
}

void PlayerCore::toggleAbLoop()
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    m_mpv->command(
        {QStringLiteral("ab-loop")},
        [this](const MpvCommandResult &) {
            if (canAccessMpv()) {
                synchronizeAbLoop();
            }
        });
}

void PlayerCore::removeHistoryEntries(const QStringList &keys)
{
    m_history.remove(keys);
    emit historyChanged(m_history.entries());
    synchronizePlaylist();
}

void PlayerCore::clearHistory()
{
    m_history.clear();
    emit historyChanged(m_history.entries());
    synchronizePlaylist();
}

void PlayerCore::seekPercent(double percent, bool forceExact)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    if (m_info.videoDurationSec > 0.0) {
        percent = std::clamp(percent, 0.0, 99.999);
    }
    m_mpv->command(
        {QStringLiteral("seek"), QString::number(percent),
         forceExact ? QStringLiteral("absolute-percent+exact")
                    : QStringLiteral("absolute-percent")});
}

void PlayerCore::seekRelative(double seconds, bool exact)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    m_mpv->command(
        {QStringLiteral("seek"), QString::number(seconds),
         exact ? QStringLiteral("relative+exact")
               : QStringLiteral("relative")});
}

void PlayerCore::seekAbsolute(double seconds)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    m_mpv->command(
        {QStringLiteral("seek"), QString::number(seconds),
         QStringLiteral("absolute+exact")});
}

void PlayerCore::stepFrame(bool backward)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    m_mpv->command(
        {backward ? QStringLiteral("frame-back-step")
                  : QStringLiteral("frame-step")});
}

void PlayerCore::takeScreenshot()
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    m_mpv->command(
        {QStringLiteral("screenshot"), QStringLiteral("subtitles")});
}

void PlayerCore::setVolume(double volume)
{
    if (!isActive(m_info.state)) {
        return;
    }
    m_mpv->setDouble(
        QStringLiteral("volume"), std::clamp(volume, 0.0, 200.0));
}

void PlayerCore::toggleMute()
{
    if (!isActive(m_info.state)) {
        return;
    }
    m_mpv->setFlag(
        QStringLiteral("mute"),
        !m_mpv->getFlag(QStringLiteral("mute")));
}

void PlayerCore::setSpeed(double speed)
{
    if (!isActive(m_info.state)) {
        return;
    }
    m_mpv->setDouble(
        QStringLiteral("speed"), std::clamp(speed, 0.01, 100.0));
}

void PlayerCore::shutdown()
{
    if (!canAccessMpv()) {
        return;
    }

    savePlaybackPosition();
    setState(PlayerState::ShuttingDown);
    // MPV_EVENT_SHUTDOWN completes the asynchronous quit handshake.
    m_mpv->shutdown();
}

void PlayerCore::onMpvPropertyChanged(
    const QString &name, const QVariant &value)
{
    if (!canAccessMpv()) {
        return;
    }

    if (name == QStringLiteral("pause")
        && isLoaded(m_info.state)
        && m_info.state != PlayerState::Loaded) {
        const bool paused = value.toBool();
        if ((m_info.state == PlayerState::Paused) != paused
            && (paused || !m_info.eofReached)) {
            setState(
                paused ? PlayerState::Paused : PlayerState::Playing);
        }
    } else if (name == QStringLiteral("idle-active")
               && value.toBool()
               && m_info.state != PlayerState::Idle
               && m_info.state != PlayerState::ShuttingDown
               && m_info.state != PlayerState::ShutDown
               && (m_info.state != PlayerState::Loading
                   || !m_pendingPlaybackError.isEmpty())) {
        resetTransientPlaybackInfo();
        setState(PlayerState::Idle);
        emit playbackStopped();
        if (!m_pendingPlaybackError.isEmpty()) {
            const QString error = std::exchange(
                m_pendingPlaybackError, {});
            emit playbackError(error, true);
        }
        if (!m_pendingUrls.isEmpty()) {
            const QList<QUrl> pending = std::exchange(m_pendingUrls, {});
            openUrls(pending);
        }
    } else if (name == QStringLiteral("eof-reached")) {
        setEofReached(value.toBool());
    } else if (name == QStringLiteral("time-pos")) {
        const double position =
            m_info.eofReached && m_info.videoDurationSec > 0.0
                ? m_info.videoDurationSec
                : value.toDouble();
        m_info.videoPositionSec = std::max(0.0, position);
        if (m_info.videoDurationSec > 0.0) {
            m_info.videoPositionSec = std::min(
                m_info.videoPositionSec,
                m_info.videoDurationSec);
        }
        emit positionChanged(m_info.videoPositionSec);
        m_history.updateProgress(
            m_info.currentUrl, m_info.videoPositionSec,
            m_info.videoDurationSec);
        if (std::abs(
                m_info.videoPositionSec
                - m_lastHistorySavePosition) >= 10.0) {
            m_lastHistorySavePosition = m_info.videoPositionSec;
            m_history.save();
            emit historyChanged(m_history.entries());
            synchronizePlaylist();
        }
    } else if (name == QStringLiteral("duration")) {
        const double duration = value.toDouble();
        m_info.videoDurationSec =
            std::isfinite(duration) ? std::max(0.0, duration) : 0.0;
        emit durationChanged(m_info.videoDurationSec);
        if (m_info.eofReached && m_info.videoDurationSec > 0.0) {
            m_info.videoPositionSec = m_info.videoDurationSec;
            emit positionChanged(m_info.videoPositionSec);
        }
        m_history.updateProgress(
            m_info.currentUrl, m_info.videoPositionSec,
            m_info.videoDurationSec);
    } else if (name == QStringLiteral("volume")) {
        m_info.volume = value.toDouble();
    } else if (name == QStringLiteral("mute")) {
        m_info.isMuted = value.toBool();
    } else if (name == QStringLiteral("speed")) {
        m_info.playSpeed = value.toDouble();
    } else if (name == QStringLiteral("chapter-list")) {
        if (isLoaded(m_info.state)) {
            synchronizeChapters();
        }
    } else if (name == QStringLiteral("chapter")) {
        const int chapter = value.toInt();
        if (m_info.currentChapter != chapter) {
            m_info.currentChapter = chapter;
            emit chapterChanged(chapter);
        }
    } else if (name == QStringLiteral("ab-loop-a")
               || name == QStringLiteral("ab-loop-b")) {
        if (isLoaded(m_info.state)) {
            synchronizeAbLoop();
        }
    } else if (name == QStringLiteral("seeking")) {
        const bool seeking = value.toBool();
        if (isLoaded(m_info.state) && m_isSeeking != seeking) {
            m_isSeeking = seeking;
            m_info.isSeeking = seeking;
            emit seekingChanged(seeking);
        }
    } else if (name == QStringLiteral("demuxer-via-network")) {
        m_info.isNetworkResource = value.toBool();
    } else if (name == QStringLiteral("paused-for-cache")) {
        BufferingInfo buffering = m_info.buffering;
        buffering.active = value.toBool();
        setBufferingInfo(buffering);
    } else if (name == QStringLiteral("cache-buffering-state")) {
        BufferingInfo buffering = m_info.buffering;
        buffering.percent = std::clamp(
            static_cast<int>(value.toLongLong()), 0, 100);
        setBufferingInfo(buffering);
    } else if (name == QStringLiteral("cache-speed")) {
        BufferingInfo buffering = m_info.buffering;
        buffering.cacheSpeedBytesPerSecond =
            std::max<qint64>(0, value.toLongLong());
        setBufferingInfo(buffering);
    } else if (
        name == QStringLiteral("demuxer-cache-duration")) {
        BufferingInfo buffering = m_info.buffering;
        buffering.cacheDurationSec =
            std::max(0.0, value.toDouble());
        setBufferingInfo(buffering);
    } else if (name == QStringLiteral("demuxer-cache-idle")) {
        BufferingInfo buffering = m_info.buffering;
        buffering.cacheIdle = value.toBool();
        setBufferingInfo(buffering);
    } else if (name == QStringLiteral("demuxer-cache-state")) {
        const QVariantMap state = value.toMap();
        BufferingInfo buffering = m_info.buffering;
        buffering.cacheUsedBytes = std::max<qint64>(
            0, state.value(QStringLiteral("fw-bytes")).toLongLong());
        const qint64 rawInputRate =
            state.value(QStringLiteral("raw-input-rate")).toLongLong();
        if (rawInputRate >= 0) {
            buffering.cacheSpeedBytesPerSecond = rawInputRate;
        }
        setBufferingInfo(buffering);
    } else if (name == QStringLiteral("playlist")) {
        synchronizePlaylist(value);
    } else if (name == QStringLiteral("playlist-pos")) {
        const int position = value.toInt();
        bool changed = m_info.playlist.currentIndex != position;
        m_info.playlist.currentIndex = position;
        for (int index = 0; index < m_info.playlist.size(); ++index) {
            const bool current = index == position;
            changed = changed
                || m_info.playlist.items[index].current != current;
            m_info.playlist.items[index].current = current;
        }
        if (changed) {
            emit playlistChanged(m_info.playlist);
        }
    } else if (name == QStringLiteral("loop-file")
               || name == QStringLiteral("loop-playlist")) {
        synchronizePlaylist();
    }
}

void PlayerCore::onMpvFileStarted(const QString &path)
{
    if (!isActive(m_info.state)) {
        return;
    }
    if (!path.isEmpty()) {
        Logger::info(QStringLiteral("mpv started file: %1").arg(path));
        const QUrl startedUrl =
            QFileInfo(path).isAbsolute()
                ? QUrl::fromLocalFile(path)
                : QUrl::fromUserInput(path);
        if (startedUrl.isValid()
            && startedUrl != m_info.currentUrl) {
            m_pendingResumePosition =
                m_history.entryFor(startedUrl).resumePosition();
            m_info.currentUrl = startedUrl;
            m_info.isNetworkResource = !startedUrl.isLocalFile();
            emit currentUrlChanged(startedUrl);
        }
    }
    setEofReached(false);
    setBufferingInfo({});
    if (m_isSeeking) {
        m_isSeeking = false;
        m_info.isSeeking = false;
        emit seekingChanged(false);
    }
    setState(PlayerState::Starting);
}

void PlayerCore::onMpvFileLoaded()
{
    if (!isActive(m_info.state)) {
        return;
    }
    setState(PlayerState::Loaded);
    const QString videoTrack =
        m_mpv->getString(QStringLiteral("vid"));
    const QString audioTrack =
        m_mpv->getString(QStringLiteral("aid"));
    m_info.hasVideo =
        !videoTrack.isEmpty() && videoTrack != QStringLiteral("no");
    m_info.hasAudio =
        !audioTrack.isEmpty() && audioTrack != QStringLiteral("no");
    m_info.videoDurationSec = std::max(
        0.0, m_mpv->getDouble(QStringLiteral("duration")));
    m_info.videoPositionSec = std::max(
        0.0, m_mpv->getDouble(QStringLiteral("time-pos")));
    m_history.recordLoaded(
        m_info.currentUrl, m_info.videoDurationSec,
        m_mpv->getString(QStringLiteral("media-title")));
    synchronizeChapters();
    synchronizeAbLoop();
    if (m_info.videoPositionSec < 1.0
        && m_pendingResumePosition > 0.0
        && m_pendingResumePosition
               < m_info.videoDurationSec - 5.0) {
        seekAbsolute(m_pendingResumePosition);
        m_info.videoPositionSec = m_pendingResumePosition;
    }
    m_pendingResumePosition = 0.0;
    m_lastHistorySavePosition = m_info.videoPositionSec;
    emit durationChanged(m_info.videoDurationSec);
    emit positionChanged(m_info.videoPositionSec);
    if (m_info.hasVideo) {
        updateVideoSize();
    }
    emit mediaLoaded(m_info.currentUrl);
    emit historyChanged(m_history.entries());
    if (!m_info.hasVideo) {
        finishLoadingWhenReady();
    }
}

void PlayerCore::onMpvFileEnded(const MpvEndFileInfo &info)
{
    if (!canAccessMpv()) {
        return;
    }

    if (info.reason == MPV_END_FILE_REASON_ERROR) {
        setBufferingInfo({});
        const QString source =
            m_info.currentUrl.isLocalFile()
                ? m_info.currentUrl.toLocalFile()
                : m_info.currentUrl.toDisplayString();
        m_pendingPlaybackError =
            tr("Playback failed for %1: %2")
                .arg(source, info.errorMessage);
        Logger::warn(m_pendingPlaybackError);
    } else if (info.reason == MPV_END_FILE_REASON_EOF) {
        setBufferingInfo({});
        setEofReached(true);
        savePlaybackPosition(true);
    } else if (info.reason == MPV_END_FILE_REASON_STOP) {
        // loadfile replace ends the previous playlist entry with STOP before
        // starting the replacement. openPrimaryUrl has already moved us to
        // Loading, so treating that expected event as an explicit stop would
        // make the following START_FILE/FILE_LOADED events look stale and
        // leave the new media black.
        // For a real stop, idle-active is the authoritative notification that
        // the media has actually been unloaded.
    } else if (info.reason == MPV_END_FILE_REASON_REDIRECT) {
        Logger::info(
            QStringLiteral(
                "Media redirected to %1 playlist entr%2")
                .arg(info.playlistInsertCount)
                .arg(info.playlistInsertCount == 1
                         ? QStringLiteral("y")
                         : QStringLiteral("ies")));
    }
    emit mediaEnded(info);
}

void PlayerCore::onMpvVideoReconfig()
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    updateVideoSize();
    if (m_info.state == PlayerState::Loaded) {
        finishLoadingWhenReady();
    }
}

void PlayerCore::onMpvAudioReconfig()
{
    if (m_info.state == PlayerState::Loaded
        && !m_info.hasVideo) {
        finishLoadingWhenReady();
    }
}

void PlayerCore::onMpvSeekStarted()
{
    if (!isLoaded(m_info.state) || m_isSeeking) {
        return;
    }
    m_isSeeking = true;
    m_info.isSeeking = true;
    setEofReached(false);
    emit seekingChanged(true);
}

void PlayerCore::onMpvPlaybackRestarted()
{
    if (!canAccessMpv()) {
        return;
    }
    if (m_isSeeking) {
        m_isSeeking = false;
        m_info.isSeeking = false;
        emit seekingChanged(false);
    }
    if (m_info.eofReached
        && !m_mpv->getFlag(QStringLiteral("eof-reached"))) {
        setEofReached(false);
    }
}

void PlayerCore::onMpvEventQueueOverflow()
{
    if (!canAccessMpv()) {
        return;
    }
    Logger::warn(
        QStringLiteral(
            "Recovering authoritative playback state after event loss"));
    resynchronizeFromMpv();
    emit playbackError(
        tr("The playback event queue overflowed. "
           "Player state was resynchronized."),
        true);
}

void PlayerCore::onMpvError(
    const QString &context, int errorCode,
    const QString &message, bool recoverable)
{
    Q_UNUSED(errorCode)
    if (context.startsWith(QStringLiteral("Command 'loadfile'"))) {
        return;
    }
    emit playbackError(
        tr("%1: %2").arg(context, message), recoverable);
}

void PlayerCore::onMpvShutdown()
{
    setState(PlayerState::ShutDown);
}

void PlayerCore::handlePlaybackFailure(
    const QString &message, int errorCode)
{
    m_pendingPlaybackError = message;
    Q_UNUSED(errorCode)
    if (m_info.state == PlayerState::Loading
        || m_info.state == PlayerState::Starting
        || m_info.state == PlayerState::Loaded) {
        resetTransientPlaybackInfo();
        setState(PlayerState::Idle);
        emit playbackStopped();
    }
    Logger::warn(message);
    emit playbackError(message, true);
    m_pendingPlaybackError.clear();
}

bool PlayerCore::canAccessMpv() const noexcept
{
    return m_info.state != PlayerState::ShuttingDown
        && m_info.state != PlayerState::ShutDown;
}

void PlayerCore::finishLoadingWhenReady()
{
    if (m_info.state != PlayerState::Loaded) {
        return;
    }
    if (m_info.hasVideo
        && (m_info.videoWidth <= 0 || m_info.videoHeight <= 0)) {
        return;
    }

    if (m_info.justOpenedFile
        && m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), false);
    }
    const bool shouldPlay = m_info.justOpenedFile
        || !m_mpv->getFlag(QStringLiteral("pause"));
    m_info.justOpenedFile = false;
    setState(
        shouldPlay ? PlayerState::Playing : PlayerState::Paused);
}

void PlayerCore::setBufferingInfo(
    const BufferingInfo &buffering)
{
    if (m_info.buffering == buffering) {
        return;
    }
    m_info.buffering = buffering;
    emit bufferingChanged(m_info.buffering);
}

void PlayerCore::synchronizePlaylist(const QVariant &nativePlaylist)
{
    PlaylistState updated = m_info.playlist;
    if (nativePlaylist.isValid()) {
        updated.items.clear();
        updated.currentIndex = -1;
        const QVariantList entries = nativePlaylist.toList();
        updated.items.reserve(entries.size());
        for (int index = 0; index < entries.size(); ++index) {
            const QVariantMap entry = entries[index].toMap();
            const QString filename =
                entry.value(QStringLiteral("filename")).toString();
            if (filename.isEmpty()) {
                continue;
            }
            PlaylistItem item;
            item.id = entry.value(QStringLiteral("id"), index).toLongLong();
            item.networkResource =
                filename.contains(QStringLiteral("://"));
            item.url = item.networkResource
                ? QUrl::fromEncoded(filename.toUtf8())
                : QUrl::fromLocalFile(filename);
            item.title =
                entry.value(QStringLiteral("title")).toString();
            item.displayName = !item.title.isEmpty()
                ? item.title
                : (item.networkResource
                       ? filename
                       : QFileInfo(filename).fileName());
            item.current =
                entry.value(QStringLiteral("current")).toBool();
            item.playing =
                entry.value(QStringLiteral("playing")).toBool();
            const PlaybackHistoryEntry history =
                m_history.entryFor(item.url);
            item.historyPositionSec = history.positionSec;
            item.historyDurationSec = history.durationSec;
            item.completed = history.completed;
            if (item.current) {
                updated.currentIndex = updated.items.size();
            }
            updated.items.append(item);
        }
    }
    for (PlaylistItem &item : updated.items) {
        const PlaybackHistoryEntry history =
            m_history.entryFor(item.url);
        item.historyPositionSec = history.positionSec;
        item.historyDurationSec = history.durationSec;
        item.completed = history.completed;
    }

    const QString loopFile =
        m_mpv->getString(QStringLiteral("loop-file"));
    const QString loopPlaylist =
        m_mpv->getString(QStringLiteral("loop-playlist"));
    if (loopFile != QStringLiteral("no")
        && loopFile != QStringLiteral("0")
        && !loopFile.isEmpty()) {
        updated.loopMode = PlaylistLoopMode::File;
    } else if (loopPlaylist != QStringLiteral("no")
               && loopPlaylist != QStringLiteral("1")
               && !loopPlaylist.isEmpty()) {
        updated.loopMode = PlaylistLoopMode::Playlist;
    } else {
        updated.loopMode = PlaylistLoopMode::Off;
    }

    if (updated == m_info.playlist) {
        return;
    }
    m_info.playlist = std::move(updated);
    emit playlistChanged(m_info.playlist);
}

void PlayerCore::synchronizeChapters()
{
    QList<PlaybackChapter> chapters;
    const int count = std::max(
        0, static_cast<int>(
               m_mpv->getInt(QStringLiteral("chapter-list/count"))));
    chapters.reserve(count);
    for (int index = 0; index < count; ++index) {
        const QString prefix =
            QStringLiteral("chapter-list/%1/").arg(index);
        PlaybackChapter chapter;
        chapter.index = index;
        chapter.title =
            m_mpv->getString(prefix + QStringLiteral("title"));
        if (chapter.title.isEmpty()) {
            chapter.title = tr("Chapter %1").arg(index + 1);
        }
        chapter.startTimeSec =
            std::max(0.0, m_mpv->getDouble(
                              prefix + QStringLiteral("time")));
        chapters.append(chapter);
    }
    if (m_info.chapters != chapters) {
        m_info.chapters = std::move(chapters);
        emit chaptersChanged(m_info.chapters);
    }
    const int current =
        static_cast<int>(m_mpv->getInt(QStringLiteral("chapter")));
    if (m_info.currentChapter != current) {
        m_info.currentChapter = current;
        emit chapterChanged(current);
    }
}

void PlayerCore::synchronizeAbLoop()
{
    AbLoopState state;
    const QString rawA =
        m_mpv->getString(QStringLiteral("ab-loop-a"));
    const QString rawB =
        m_mpv->getString(QStringLiteral("ab-loop-b"));
    const bool hasA = !rawA.isEmpty()
        && rawA != QStringLiteral("no");
    const bool hasB = !rawB.isEmpty()
        && rawB != QStringLiteral("no");
    state.pointA = hasA ? std::max(0.0, rawA.toDouble()) : 0.0;
    state.pointB = hasB ? std::max(0.0, rawB.toDouble()) : 0.0;
    state.status = !hasA ? AbLoopStatus::Cleared
        : !hasB ? AbLoopStatus::ASet : AbLoopStatus::BSet;
    if (m_info.abLoop != state) {
        m_info.abLoop = state;
        emit abLoopChanged(state);
    }
}

void PlayerCore::savePlaybackPosition(bool reachedEnd)
{
    if (!isLoaded(m_info.state)
        || !m_info.currentUrl.isValid()
        || m_info.currentUrl.isEmpty()) {
        return;
    }
    m_history.updateProgress(
        m_info.currentUrl, m_info.videoPositionSec,
        m_info.videoDurationSec, reachedEnd);
    m_history.save();
    emit historyChanged(m_history.entries());
    synchronizePlaylist();
    if (!reachedEnd) {
        // IINA explicitly writes mpv's watch-later configuration before
        // stopping. This preserves mpv-managed per-file state as well as the
        // durable history metadata above.
        m_mpv->command(
            {QStringLiteral("write-watch-later-config")});
    }
}

void PlayerCore::setEofReached(bool reached)
{
    if (m_info.eofReached == reached) {
        return;
    }
    m_info.eofReached = reached;
    if (reached) {
        BufferingInfo buffering = m_info.buffering;
        buffering.active = false;
        setBufferingInfo(buffering);
        if (m_info.videoDurationSec > 0.0) {
            m_info.videoPositionSec = m_info.videoDurationSec;
            emit positionChanged(m_info.videoPositionSec);
        }
        if (isLoaded(m_info.state)) {
            setState(PlayerState::Paused);
        }
    }
    emit eofChanged(reached);
}

void PlayerCore::resetTransientPlaybackInfo()
{
    if (m_isSeeking) {
        m_isSeeking = false;
        emit seekingChanged(false);
    }
    m_info.isSeeking = false;
    setBufferingInfo({});
    setEofReached(false);
    m_info.hasVideo = false;
    m_info.hasAudio = false;
    m_info.justOpenedFile = false;
    m_info.videoWidth = 0;
    m_info.videoHeight = 0;
    m_info.videoPositionSec = 0.0;
    m_info.videoDurationSec = 0.0;
    if (!m_info.chapters.isEmpty()) {
        m_info.chapters.clear();
        emit chaptersChanged(m_info.chapters);
    }
    if (m_info.currentChapter != -1) {
        m_info.currentChapter = -1;
        emit chapterChanged(-1);
    }
    if (m_info.abLoop.status != AbLoopStatus::Cleared) {
        m_info.abLoop = {};
        emit abLoopChanged(m_info.abLoop);
    }
    if (!m_info.playlist.isEmpty()) {
        m_info.playlist = {};
        emit playlistChanged(m_info.playlist);
    }
    emit videoSizeChanged(0, 0);
    emit positionChanged(0.0);
    emit durationChanged(0.0);
}

void PlayerCore::resynchronizeFromMpv()
{
    if (!canAccessMpv()) {
        return;
    }
    if (m_mpv->getFlag(QStringLiteral("idle-active"))) {
        resetTransientPlaybackInfo();
        setState(PlayerState::Idle);
        return;
    }

    m_info.videoPositionSec =
        m_mpv->getDouble(QStringLiteral("time-pos"));
    m_info.videoDurationSec =
        m_mpv->getDouble(QStringLiteral("duration"));
    m_info.volume =
        m_mpv->getDouble(QStringLiteral("volume"));
    m_info.isMuted =
        m_mpv->getFlag(QStringLiteral("mute"));
    m_info.playSpeed =
        m_mpv->getDouble(QStringLiteral("speed"));
    const QString videoTrack =
        m_mpv->getString(QStringLiteral("vid"));
    const QString audioTrack =
        m_mpv->getString(QStringLiteral("aid"));
    m_info.hasVideo =
        !videoTrack.isEmpty() && videoTrack != QStringLiteral("no");
    m_info.hasAudio =
        !audioTrack.isEmpty() && audioTrack != QStringLiteral("no");
    m_info.isNetworkResource =
        m_mpv->getFlag(QStringLiteral("demuxer-via-network"));
    m_info.isSeeking =
        m_mpv->getFlag(QStringLiteral("seeking"));
    m_isSeeking = m_info.isSeeking;
    setEofReached(
        m_mpv->getFlag(QStringLiteral("eof-reached")));

    BufferingInfo buffering = m_info.buffering;
    buffering.active =
        m_mpv->getFlag(QStringLiteral("paused-for-cache"));
    if (m_info.isNetworkResource) {
        buffering.percent = std::clamp(
            static_cast<int>(m_mpv->getInt(
                QStringLiteral("cache-buffering-state"))),
            0, 100);
        buffering.cacheSpeedBytesPerSecond =
            std::max<qint64>(
                0, m_mpv->getInt(QStringLiteral("cache-speed")));
        buffering.cacheDurationSec = std::max(
            0.0, m_mpv->getDouble(
                     QStringLiteral("demuxer-cache-duration")));
        buffering.cacheIdle =
            m_mpv->getFlag(QStringLiteral("demuxer-cache-idle"));
    }
    setBufferingInfo(buffering);
    QVariantList nativePlaylist;
    const int playlistCount = std::max(
        0, static_cast<int>(
               m_mpv->getInt(QStringLiteral("playlist-count"))));
    nativePlaylist.reserve(playlistCount);
    for (int index = 0; index < playlistCount; ++index) {
        const QString prefix =
            QStringLiteral("playlist/%1/").arg(index);
        nativePlaylist.append(QVariantMap{
            {QStringLiteral("id"),
             m_mpv->getInt(prefix + QStringLiteral("id"))},
            {QStringLiteral("filename"),
             m_mpv->getString(prefix + QStringLiteral("filename"))},
            {QStringLiteral("title"),
             m_mpv->getString(prefix + QStringLiteral("title"))},
            {QStringLiteral("current"),
             m_mpv->getFlag(prefix + QStringLiteral("current"))},
            {QStringLiteral("playing"),
             m_mpv->getFlag(prefix + QStringLiteral("playing"))}});
    }
    synchronizePlaylist(nativePlaylist);
    emit positionChanged(m_info.videoPositionSec);
    emit durationChanged(m_info.videoDurationSec);
    emit seekingChanged(m_info.isSeeking);
    if (m_info.hasVideo) {
        updateVideoSize();
    }

    if (isLoaded(m_info.state)) {
        setState(
            m_mpv->getFlag(QStringLiteral("pause"))
                ? PlayerState::Paused
                : PlayerState::Playing);
    }
}

void PlayerCore::updateVideoSize()
{
    if (!isActive(m_info.state)) {
        return;
    }
    const int width =
        static_cast<int>(m_mpv->getInt(QStringLiteral("width")));
    const int height =
        static_cast<int>(m_mpv->getInt(QStringLiteral("height")));
    if (width == m_info.videoWidth && height == m_info.videoHeight) {
        return;
    }
    m_info.videoWidth = width;
    m_info.videoHeight = height;
    emit videoSizeChanged(width, height);
}

void PlayerCore::setState(PlayerState newState)
{
    if (m_info.state == newState) {
        return;
    }
    const PlayerState oldState = m_info.state;
    m_info.state = newState;
    Logger::info(
        QStringLiteral("Player state: %1 -> %2")
            .arg(stateName(oldState), stateName(newState)));
    emit stateChanged(newState);
}

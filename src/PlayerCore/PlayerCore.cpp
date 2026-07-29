#include "PlayerCore/PlayerCore.h"

#include "App/MediaSourceResolver.h"
#include "Core/Logger.h"
#include "Mpv/MpvCore.h"
#include "Preferences/PlayerConfiguration.h"

#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QSettings>
#include <QRegularExpression>
#include <QProcess>
#include <QPointer>
#include <QRunnable>
#include <QStandardPaths>
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
    const QSettings settings;
    m_history.setRecordingEnabled(settings.value(
        QStringLiteral("history/recordPlaybackHistory"), true).toBool());
    m_recentMedia.setRecordingEnabled(settings.value(
        QStringLiteral("history/recordRecentMedia"), true).toBool());
    m_history.refreshWatchLaterPositions(
        PlaybackHistoryStore::defaultWatchLaterDirectory(),
        m_mpv->getFlag(
            QStringLiteral("ignore-path-in-watch-later-config")));
    m_matchingPool.setMaxThreadCount(1);
    m_matchingPool.setExpiryTimeout(-1);
    connect(m_mpv.get(), &MpvCore::propertyChanged,
            this, &PlayerCore::onMpvPropertyChanged);
    connect(&m_thumbnails, &ThumbnailProvider::thumbnailsChanged,
            this, &PlayerCore::thumbnailsChanged);
    connect(&m_thumbnails, &ThumbnailProvider::thumbnailsReady,
            this, &PlayerCore::thumbnailsReady);
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

PlayerCore::~PlayerCore()
{
    m_matchingGeneration.fetch_add(1, std::memory_order_relaxed);
    m_matchingPool.waitForDone();
}

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
    m_shouldAutoMatchCurrentOpen = validUrls.size() == 1;
    QList<QUrl> queue = validUrls;
    int openedPosition = 0;
    const bool autoAddSiblings = QSettings().value(
        QStringLiteral("matching/playlistAutoAdd"), true).toBool();
    if (autoAddSiblings
        && validUrls.size() == 1 && openedUrl.isLocalFile()
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

    for (const QUrl &url : std::as_const(validUrls)) {
        m_recentMedia.note(url);
    }
    emit recentMediaChanged(m_recentMedia.entries());
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
    m_lastWatchLaterSavePosition = 0.0;
    m_loadedAutomaticSubtitles.clear();
    m_thumbnails.clear();

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

    // Embedded cover art is exposed by mpv as a video stream. Broken artwork
    // must not keep an otherwise valid audio file waiting forever for a video
    // reconfiguration event, so audio containers use the audio-only track
    // selection and the window provides their compact presentation.
    m_mpv->setString(
        QStringLiteral("vid"),
        MediaSourceResolver::isAudioFile(url)
            ? QStringLiteral("no") : QStringLiteral("auto"));

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

void PlayerCore::clearRecentMedia()
{
    m_recentMedia.clear();
    emit recentMediaChanged(m_recentMedia.entries());
}

void PlayerCore::setHistoryRecordingEnabled(bool enabled)
{
    QSettings().setValue(
        QStringLiteral("history/recordPlaybackHistory"), enabled);
    m_history.setRecordingEnabled(enabled);
}

void PlayerCore::setResumePlaybackEnabled(bool enabled)
{
    QSettings().setValue(
        QStringLiteral("history/resumePlayback"), enabled);
    if (canAccessMpv()) {
        m_mpv->setFlag(QStringLiteral("save-position-on-quit"), enabled);
        m_mpv->setFlag(QStringLiteral("resume-playback"), enabled);
    }
}

void PlayerCore::setRecentMediaRecordingEnabled(bool enabled)
{
    QSettings().setValue(
        QStringLiteral("history/recordRecentMedia"), enabled);
    m_recentMedia.setRecordingEnabled(enabled);
}

void PlayerCore::setTrackPlaylistFilesAsRecent(bool enabled)
{
    QSettings().setValue(
        QStringLiteral("history/trackPlaylistFilesAsRecent"), enabled);
}

void PlayerCore::executeMpvCommand(const QString &command)
{
    if (!canAccessMpv()) {
        return;
    }
    const QStringList commands =
        command.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    static const QStringList inputPrefixes{
        QStringLiteral("no-osd"),
        QStringLiteral("osd-auto"),
        QStringLiteral("osd-bar"),
        QStringLiteral("osd-msg"),
        QStringLiteral("raw"),
        QStringLiteral("expand-properties"),
        QStringLiteral("repeatable"),
        QStringLiteral("async")};
    for (const QString &commandText : commands) {
        QStringList arguments =
            QProcess::splitCommand(commandText.trimmed());
        while (!arguments.isEmpty()
               && inputPrefixes.contains(arguments.constFirst())) {
            arguments.removeFirst();
        }
        if (arguments.isEmpty()) {
            continue;
        }
        QVariantList nativeArguments;
        nativeArguments.reserve(arguments.size());
        for (const QString &argument : std::as_const(arguments)) {
            nativeArguments.append(argument);
        }
        m_mpv->command(nativeArguments);
    }
}

void PlayerCore::reloadMpvConfiguration()
{
    if (!canAccessMpv()) {
        return;
    }
    m_mpv->command({
        QStringLiteral("load-config-file"),
        PlayerConfiguration::mpvConfigFilePath()});
}

void PlayerCore::applyMpvProfile(const QString &profile)
{
    if (!canAccessMpv() || profile.trimmed().isEmpty()) {
        return;
    }
    reloadMpvConfiguration();
    m_mpv->command({
        QStringLiteral("apply-profile"), profile.trimmed()});
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
    if (!isLoaded(m_info.state) || !m_info.hasVideo) {
        return;
    }
    const QSettings settings;
    const bool saveToFile = settings.value(
        QStringLiteral("screenshots/saveToFile"), true).toBool();
    const bool copyToClipboard = settings.value(
        QStringLiteral("screenshots/copyToClipboard"), false).toBool();
    if (!saveToFile && !copyToClipboard) {
        return;
    }
    QString format = settings.value(
        QStringLiteral("screenshots/format"),
        QStringLiteral("png")).toString().toLower();
    if (!QStringList{
            QStringLiteral("png"), QStringLiteral("jpg"),
            QStringLiteral("webp")}.contains(format)) {
        format = QStringLiteral("png");
    }
    QString directory;
    if (saveToFile) {
        directory = settings.value(
            QStringLiteral("screenshots/folder"),
            QDir(QStandardPaths::writableLocation(
                     QStandardPaths::PicturesLocation))
                .filePath(QStringLiteral("Screenshots")))
                        .toString();
    } else {
        directory = QDir(QStandardPaths::writableLocation(
                             QStandardPaths::TempLocation))
                        .filePath(
                            QStringLiteral("Supernova/screenshot_cache"));
    }
    directory = QDir::cleanPath(
        QDir::fromNativeSeparators(directory));
    if (!QDir().mkpath(directory)) {
        return;
    }
    QString base = m_info.currentUrl.isLocalFile()
        ? QFileInfo(m_info.currentUrl.toLocalFile()).completeBaseName()
        : QStringLiteral("screenshot");
    base.replace(
        QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")),
        QStringLiteral("_"));
    QString path;
    for (int sequence = 1; sequence < 100000; ++sequence) {
        path = QDir(directory).filePath(
            QStringLiteral("%1-%2.%3")
                .arg(base)
                .arg(sequence, 4, 10, QLatin1Char('0'))
                .arg(format));
        if (!QFileInfo::exists(path)) {
            break;
        }
    }
    const bool includeSubtitles = settings.value(
        QStringLiteral("screenshots/includeSubtitles"), true).toBool();
    m_mpv->command(
        {QStringLiteral("screenshot-to-file"),
         QDir::toNativeSeparators(path),
         includeSubtitles ? QStringLiteral("subtitles")
                          : QStringLiteral("video")},
        [this, path, saveToFile](const MpvCommandResult &result) {
            if (!result.succeeded()) {
                return;
            }
            QImage image(path);
            if (image.isNull()) {
                return;
            }
            emit screenshotCaptured(
                image, QUrl::fromLocalFile(path), saveToFile);
            if (!saveToFile) {
                QFile::remove(path);
            }
        });
}

QVariant PlayerCore::mpvProperty(const QString &name) const
{
    return m_mpv ? m_mpv->getNode(name) : QVariant();
}

QString PlayerCore::mpvPropertyString(const QString &name) const
{
    return m_mpv ? m_mpv->getString(name) : QString();
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

void PlayerCore::setTrack(MediaTrackType type, int id)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    const QString property =
        type == MediaTrackType::Video
            ? QStringLiteral("vid")
            : type == MediaTrackType::Audio
                ? QStringLiteral("aid")
                : QStringLiteral("sid");
    m_mpv->setInt(property, std::max(0, id));
}

void PlayerCore::setSubtitleTrack(bool primary, int id)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    if (primary) {
        m_mpv->setInt(
            QStringLiteral("sid"), std::max(0, id));
    } else {
        m_mpv->setString(
            QStringLiteral("secondary-sid"),
            id > 0 ? QString::number(id) : QStringLiteral("no"));
    }
}

void PlayerCore::runExternalTrackCommand(
    const QString &command, const QUrl &url,
    const QString &failureDescription)
{
    if (!isLoaded(m_info.state) || !url.isLocalFile()) {
        return;
    }
    const QString path = QDir::toNativeSeparators(url.toLocalFile());
    m_mpv->command(
        {command, path},
        [this, failureDescription, path](const MpvCommandResult &result) {
            if (result.succeeded()) {
                return;
            }
            const QString message =
                tr("%1\n%2\n%3")
                    .arg(failureDescription, path, result.errorMessage);
            Logger::warn(message);
            emit playbackError(message, true);
        });
}

void PlayerCore::loadExternalVideo(const QUrl &url)
{
    runExternalTrackCommand(
        QStringLiteral("video-add"), url,
        tr("This external video file is unsupported."));
}

void PlayerCore::loadExternalAudio(const QUrl &url)
{
    runExternalTrackCommand(
        QStringLiteral("audio-add"), url,
        tr("This external audio file is unsupported."));
}

void PlayerCore::loadExternalSubtitle(const QUrl &url)
{
    if (!isLoaded(m_info.state) || !url.isLocalFile()) {
        return;
    }
    const QString requested =
        QFileInfo(url.toLocalFile()).canonicalFilePath();
    const auto existing = std::find_if(
        m_info.tracks.subtitleTracks.cbegin(),
        m_info.tracks.subtitleTracks.cend(),
        [&requested](const MediaTrack &track) {
            const QString existingPath =
                QFileInfo(track.externalFilename).canonicalFilePath();
            return track.isExternal && !requested.isEmpty()
                && existingPath == requested;
        });
    if (existing != m_info.tracks.subtitleTracks.cend()) {
        m_mpv->command(
            {QStringLiteral("sub-reload"),
             QString::number(existing->id)});
        setSubtitleTrack(true, existing->id);
        return;
    }
    runExternalTrackCommand(
        QStringLiteral("sub-add"), url,
        tr("This external subtitle file is unsupported."));
}

void PlayerCore::removeExternalTrack(MediaTrackType type, int id)
{
    if (!isLoaded(m_info.state) || id <= 0) {
        return;
    }
    const QList<MediaTrack> *tracks =
        type == MediaTrackType::Video
            ? &m_info.tracks.videoTracks
            : type == MediaTrackType::Audio
                ? &m_info.tracks.audioTracks
                : &m_info.tracks.subtitleTracks;
    const auto track = std::find_if(
        tracks->cbegin(), tracks->cend(),
        [id](const MediaTrack &candidate) {
            return candidate.id == id;
        });
    if (track == tracks->cend() || !track->isExternal) {
        return;
    }
    const QString command =
        type == MediaTrackType::Video
            ? QStringLiteral("video-remove")
            : type == MediaTrackType::Audio
                ? QStringLiteral("audio-remove")
                : QStringLiteral("sub-remove");
    m_mpv->command({command, QString::number(id)});
}

void PlayerCore::reloadExternalSubtitles()
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    const MediaTrack *primary =
        m_info.tracks.selectedSubtitle(true);
    const MediaTrack *secondary =
        m_info.tracks.selectedSubtitle(false);
    const QString primaryFilename =
        primary ? primary->externalFilename : QString();
    const QString secondaryFilename =
        secondary ? secondary->externalFilename : QString();
    QList<int> externalIds;
    for (const MediaTrack &track : m_info.tracks.subtitleTracks) {
        if (track.isExternal) {
            externalIds.append(track.id);
        }
    }
    if (externalIds.isEmpty()) {
        return;
    }
    auto remaining =
        std::make_shared<int>(externalIds.size());
    for (int id : externalIds) {
        m_mpv->command(
            {QStringLiteral("sub-reload"), QString::number(id)},
            [this, remaining, primaryFilename, secondaryFilename](
                const MpvCommandResult &) {
                --*remaining;
                if (*remaining != 0 || !isLoaded(m_info.state)) {
                    return;
                }
                synchronizeTracks();
                const auto findId =
                    [this](const QString &filename) {
                        if (filename.isEmpty()) {
                            return 0;
                        }
                        const QString canonical =
                            QFileInfo(filename).canonicalFilePath();
                        const auto found = std::find_if(
                            m_info.tracks.subtitleTracks.cbegin(),
                            m_info.tracks.subtitleTracks.cend(),
                            [&canonical](const MediaTrack &track) {
                                return QFileInfo(track.externalFilename)
                                           .canonicalFilePath()
                                    == canonical;
                            });
                        return found
                                == m_info.tracks.subtitleTracks.cend()
                            ? 0 : found->id;
                    };
                const int primaryId = findId(primaryFilename);
                const int secondaryId = findId(secondaryFilename);
                if (primaryId > 0) {
                    setSubtitleTrack(true, primaryId);
                }
                if (secondaryId > 0) {
                    setSubtitleTrack(false, secondaryId);
                }
            });
    }
}

void PlayerCore::setSubtitleVisibility(bool primary, bool visible)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setFlag(
            primary ? QStringLiteral("sub-visibility")
                    : QStringLiteral("secondary-sub-visibility"),
            visible);
    }
}

void PlayerCore::setSubtitleDelay(bool primary, double seconds)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setDouble(
            primary ? QStringLiteral("sub-delay")
                    : QStringLiteral("secondary-sub-delay"),
            std::clamp(seconds, -3600.0, 3600.0));
    }
}

void PlayerCore::setSubtitlePosition(bool primary, int position)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setInt(
            primary ? QStringLiteral("sub-pos")
                    : QStringLiteral("secondary-sub-pos"),
            std::clamp(position, 0, 150));
    }
}

void PlayerCore::setSubtitleScale(double scale)
{
    if (isLoaded(m_info.state)) {
        scale = std::clamp(scale, 0.1, 10.0);
        QSettings().setValue(QStringLiteral("subtitles/scale"), scale);
        m_mpv->setDouble(
            QStringLiteral("sub-scale"), scale);
    }
}

void PlayerCore::setSubtitleFont(const QString &font)
{
    if (isLoaded(m_info.state) && !font.trimmed().isEmpty()) {
        const QString value = font.trimmed();
        QSettings().setValue(QStringLiteral("subtitles/font"), value);
        m_mpv->setString(QStringLiteral("sub-font"), value);
    }
}

void PlayerCore::setSubtitleFontSize(double size)
{
    if (isLoaded(m_info.state)) {
        size = std::clamp(size, 1.0, 200.0);
        QSettings().setValue(QStringLiteral("subtitles/fontSize"), size);
        m_mpv->setDouble(
            QStringLiteral("sub-font-size"), size);
    }
}

void PlayerCore::setSubtitleTextColor(const QString &color)
{
    if (isLoaded(m_info.state)) {
        QSettings().setValue(
            QStringLiteral("subtitles/textColor"), color);
        m_mpv->setString(QStringLiteral("sub-color"), color);
    }
}

void PlayerCore::setSubtitleBackgroundColor(const QString &color)
{
    if (isLoaded(m_info.state)) {
        QSettings().setValue(
            QStringLiteral("subtitles/backgroundColor"), color);
        m_mpv->setString(QStringLiteral("sub-back-color"), color);
    }
}

void PlayerCore::setSubtitleBorderColor(const QString &color)
{
    if (isLoaded(m_info.state)) {
        QSettings().setValue(
            QStringLiteral("subtitles/borderColor"), color);
        m_mpv->setString(QStringLiteral("sub-border-color"), color);
    }
}

void PlayerCore::setSubtitleBorderSize(double size)
{
    if (isLoaded(m_info.state)) {
        size = std::clamp(size, 0.0, 20.0);
        QSettings().setValue(QStringLiteral("subtitles/borderSize"), size);
        m_mpv->setDouble(
            QStringLiteral("sub-border-size"), size);
    }
}

void PlayerCore::setSubtitleAssOverride(const QString &mode)
{
    static const QStringList allowed{
        QStringLiteral("no"), QStringLiteral("yes"),
        QStringLiteral("force"), QStringLiteral("scale"),
        QStringLiteral("strip")};
    if (isLoaded(m_info.state) && allowed.contains(mode)) {
        QSettings().setValue(
            QStringLiteral("subtitles/assOverride"), mode);
        m_mpv->setString(QStringLiteral("sub-ass-override"), mode);
        m_mpv->setString(
            QStringLiteral("secondary-sub-ass-override"), mode);
    }
}

void PlayerCore::setVideoAspect(const QString &aspect)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    const QString value = aspect.trimmed();
    static const QRegularExpression valid(
        QStringLiteral(R"(^\d+(?:\.\d+)?(?::\d+(?:\.\d+)?)?$)"));
    if (value.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0
        || value.isEmpty()) {
        m_mpv->setString(
            QStringLiteral("video-aspect-override"),
            QStringLiteral("-1"));
    } else if (valid.match(value).hasMatch()) {
        m_mpv->setString(
            QStringLiteral("video-aspect-override"), value);
    }
}

void PlayerCore::removeManagedFilter(
    bool video, const QString &label)
{
    if (!isLoaded(m_info.state) || label.isEmpty()) {
        return;
    }
    m_mpv->command(
        {video ? QStringLiteral("vf") : QStringLiteral("af"),
         QStringLiteral("remove"),
         QStringLiteral("@%1").arg(label)});
}

void PlayerCore::addManagedFilter(
    bool video, const QString &label, const QString &filter)
{
    if (!isLoaded(m_info.state)
        || label.isEmpty() || filter.trimmed().isEmpty()) {
        return;
    }
    removeManagedFilter(video, label);
    if (video
        && m_mpv->getString(QStringLiteral("hwdec"))
               == QStringLiteral("auto")) {
        // IINA requires software filters to use copy-back decoding. Use its
        // stable Auto (Copy) path so the filter cannot silently fail.
        m_mpv->setString(
            QStringLiteral("hwdec"), QStringLiteral("auto-copy"));
    }
    m_mpv->command(
        {video ? QStringLiteral("vf") : QStringLiteral("af"),
         QStringLiteral("add"),
         QStringLiteral("@%1:%2").arg(label, filter.trimmed())});
}

void PlayerCore::setVideoCrop(const QString &crop)
{
    if (!isLoaded(m_info.state)) {
        return;
    }
    removeManagedFilter(true, QStringLiteral("supernova_crop"));
    if (crop.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0
        || crop.isEmpty()) {
        return;
    }
    const QStringList parts = crop.split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return;
    }
    bool widthOk = false;
    bool heightOk = false;
    const double ratioWidth = parts[0].toDouble(&widthOk);
    const double ratioHeight = parts[1].toDouble(&heightOk);
    if (!widthOk || !heightOk || ratioWidth <= 0.0
        || ratioHeight <= 0.0 || m_info.videoWidth <= 0
        || m_info.videoHeight <= 0) {
        return;
    }
    const double target = ratioWidth / ratioHeight;
    const double source =
        static_cast<double>(m_info.videoWidth) / m_info.videoHeight;
    int width = m_info.videoWidth;
    int height = m_info.videoHeight;
    if (source > target) {
        width = qRound(height * target);
    } else {
        height = qRound(width / target);
    }
    width = std::max(2, width - width % 2);
    height = std::max(2, height - height % 2);
    addManagedFilter(
        true, QStringLiteral("supernova_crop"),
        QStringLiteral("crop=w=%1:h=%2").arg(width).arg(height));
}

void PlayerCore::setVideoCropGeometry(
    int width, int height, int x, int y)
{
    if (!isLoaded(m_info.state) || width <= 0 || height <= 0) {
        return;
    }
    width = std::max(2, width - width % 2);
    height = std::max(2, height - height % 2);
    QString filter =
        QStringLiteral("crop=w=%1:h=%2").arg(width).arg(height);
    if (x >= 0) {
        filter += QStringLiteral(":x=%1").arg(x);
    }
    if (y >= 0) {
        filter += QStringLiteral(":y=%1").arg(y);
    }
    addManagedFilter(
        true, QStringLiteral("supernova_crop"), filter);
}

void PlayerCore::setVideoRotation(int degrees)
{
    if (isLoaded(m_info.state)
        && (degrees == 0 || degrees == 90
            || degrees == 180 || degrees == 270)) {
        m_mpv->setInt(QStringLiteral("video-rotate"), degrees);
    }
}

void PlayerCore::setHardwareDecoding(bool enabled)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setString(
            QStringLiteral("hwdec"),
            enabled ? QStringLiteral("auto")
                    : QStringLiteral("no"));
    }
}

void PlayerCore::setDeinterlace(bool enabled)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setFlag(QStringLiteral("deinterlace"), enabled);
    }
}

void PlayerCore::setVideoFlip(bool enabled)
{
    if (enabled) {
        addManagedFilter(
            true, QStringLiteral("supernova_flip"),
            QStringLiteral("vflip"));
    } else {
        removeManagedFilter(true, QStringLiteral("supernova_flip"));
    }
}

void PlayerCore::setVideoMirror(bool enabled)
{
    if (enabled) {
        addManagedFilter(
            true, QStringLiteral("supernova_mirror"),
            QStringLiteral("hflip"));
    } else {
        removeManagedFilter(true, QStringLiteral("supernova_mirror"));
    }
}

void PlayerCore::setVideoColor(
    const QString &property, int value)
{
    static const QStringList allowed{
        QStringLiteral("brightness"), QStringLiteral("contrast"),
        QStringLiteral("saturation"), QStringLiteral("gamma"),
        QStringLiteral("hue")};
    if (isLoaded(m_info.state) && allowed.contains(property)) {
        m_mpv->setInt(property, std::clamp(value, -100, 100));
    }
}

void PlayerCore::addVideoFilter(const QString &filter)
{
    addManagedFilter(
        true,
        QStringLiteral("supernova_user_vf_%1")
            .arg(m_nextUserFilterId++),
        filter);
}

void PlayerCore::removeVideoFilter(const QString &label)
{
    removeManagedFilter(true, label);
}

void PlayerCore::setAudioDevice(const QString &name)
{
    if (!isLoaded(m_info.state) || name.isEmpty()) {
        return;
    }
    QSettings().setValue(QStringLiteral("audio/device"), name);
    m_mpv->setString(QStringLiteral("audio-device"), name);
}

void PlayerCore::setAudioChannels(const QString &channels)
{
    static const QStringList allowed{
        QStringLiteral("auto-safe"), QStringLiteral("auto"),
        QStringLiteral("mono"), QStringLiteral("stereo"),
        QStringLiteral("2.1"), QStringLiteral("5.1"),
        QStringLiteral("7.1")};
    if (isLoaded(m_info.state) && allowed.contains(channels)) {
        m_mpv->setString(QStringLiteral("audio-channels"), channels);
    }
}

void PlayerCore::setAudioDelay(double seconds)
{
    if (isLoaded(m_info.state)) {
        m_mpv->setDouble(
            QStringLiteral("audio-delay"),
            std::clamp(seconds, -3600.0, 3600.0));
    }
}

void PlayerCore::setAudioEqualizer(
    const std::array<double, 10> &gains)
{
    static constexpr std::array<int, 10> frequencies{
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    QStringList filters;
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        const int frequency = frequencies[index];
        filters.append(
            QStringLiteral("equalizer=f=%1:t=h:width=%2:g=%3")
                .arg(frequency)
                .arg(
                    static_cast<double>(frequency) / 1.224744871,
                    0, 'f', 6)
                .arg(std::clamp(gains[index], -12.0, 12.0),
                     0, 'f', 2));
    }
    m_info.audioSettings.equalizer = gains;
    addManagedFilter(
        false, QStringLiteral("supernova_audio_eq"),
        QStringLiteral("lavfi=[%1]").arg(
            filters.join(QLatin1Char(','))));
    emit audioQuickSettingsChanged(m_info.audioSettings);
}

void PlayerCore::addAudioFilter(const QString &filter)
{
    addManagedFilter(
        false,
        QStringLiteral("supernova_user_af_%1")
            .arg(m_nextUserFilterId++),
        filter);
}

void PlayerCore::removeAudioFilter(const QString &label)
{
    removeManagedFilter(false, label);
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
            m_history.saveAsync();
            emit historyChanged(m_history.entries());
            synchronizePlaylist();
        }
        if (std::abs(
                m_info.videoPositionSec
                - m_lastWatchLaterSavePosition) >= 30.0) {
            cacheWatchLaterPosition();
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
    } else if (
        name == QStringLiteral("video-aspect-override")
        || name == QStringLiteral("video-rotate")
        || name == QStringLiteral("deinterlace")
        || name == QStringLiteral("hwdec")
        || name == QStringLiteral("brightness")
        || name == QStringLiteral("contrast")
        || name == QStringLiteral("saturation")
        || name == QStringLiteral("gamma")
        || name == QStringLiteral("hue")) {
        if (isLoaded(m_info.state)) {
            synchronizeVideoQuickSettings();
        }
    } else if (name == QStringLiteral("vf")) {
        if (isLoaded(m_info.state)) {
            synchronizeVideoQuickSettings(value);
        }
    } else if (
        name == QStringLiteral("audio-device-list")
        || name == QStringLiteral("audio-device")
        || name == QStringLiteral("audio-channels")
        || name == QStringLiteral("audio-delay")) {
        if (isLoaded(m_info.state)) {
            synchronizeAudioQuickSettings();
        }
    } else if (name == QStringLiteral("af")) {
        if (isLoaded(m_info.state)) {
            synchronizeAudioQuickSettings(value);
        }
    } else if (name == QStringLiteral("track-list")) {
        if (isLoaded(m_info.state)) {
            synchronizeTracks(value);
        }
    } else if (name == QStringLiteral("vid")
               || name == QStringLiteral("aid")
               || name == QStringLiteral("sid")
               || name == QStringLiteral("secondary-sid")) {
        if (isLoaded(m_info.state)) {
            synchronizeTracks();
        }
    } else if (
        name == QStringLiteral("sub-visibility")
        || name == QStringLiteral("secondary-sub-visibility")
        || name == QStringLiteral("sub-delay")
        || name == QStringLiteral("secondary-sub-delay")
        || name == QStringLiteral("sub-pos")
        || name == QStringLiteral("secondary-sub-pos")
        || name == QStringLiteral("sub-scale")
        || name == QStringLiteral("sub-font")
        || name == QStringLiteral("sub-font-size")
        || name == QStringLiteral("sub-color")
        || name == QStringLiteral("sub-back-color")
        || name == QStringLiteral("sub-border-color")
        || name == QStringLiteral("sub-border-size")
        || name == QStringLiteral("sub-ass-override")) {
        if (isLoaded(m_info.state)) {
            synchronizeSubtitleSettings();
        }
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
    QUrl startedUrl;
    if (!path.isEmpty()) {
        Logger::info(QStringLiteral("mpv started file: %1").arg(path));
        startedUrl =
            QFileInfo(path).isAbsolute()
                ? QUrl::fromLocalFile(path)
                : QUrl::fromUserInput(path);
        if (startedUrl.isValid()
            && startedUrl != m_info.currentUrl) {
            m_info.currentUrl = startedUrl;
            m_info.isNetworkResource = !startedUrl.isLocalFile();
            emit currentUrlChanged(startedUrl);
        }
    }
    m_loadedAutomaticSubtitles.clear();
    m_lastWatchLaterSavePosition = 0.0;
    if (startedUrl.isValid() && m_shouldAutoMatchCurrentOpen) {
        startAutomaticMatching(startedUrl);
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
    synchronizeTracks();
    synchronizeSubtitleSettings();
    synchronizeVideoQuickSettings();
    synchronizeAudioQuickSettings();
    if (QSettings().value(
            QStringLiteral("history/trackPlaylistFilesAsRecent"), true)
            .toBool()) {
        m_recentMedia.note(m_info.currentUrl);
        emit recentMediaChanged(m_recentMedia.entries());
    }
    m_lastHistorySavePosition = m_info.videoPositionSec;
    m_lastWatchLaterSavePosition = m_info.videoPositionSec;
    loadMatchedSubtitlesForCurrentFile();
    m_thumbnails.request(
        m_info.currentUrl, m_info.videoDurationSec,
        QSettings().value(
            QStringLiteral("thumbnails/width"), 120).toInt());
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

void PlayerCore::synchronizeTracks(const QVariant &trackList)
{
    MediaTrackState updated;
    const QVariant node =
        trackList.isValid()
            ? trackList
            : m_mpv->getNode(QStringLiteral("track-list"));
    for (MediaTrack track : MediaTrack::fromMpvNode(node)) {
        switch (track.type) {
        case MediaTrackType::Video:
            updated.videoTracks.append(std::move(track));
            break;
        case MediaTrackType::Audio:
            updated.audioTracks.append(std::move(track));
            break;
        case MediaTrackType::Subtitle:
            updated.subtitleTracks.append(std::move(track));
            break;
        }
    }
    updated.selectedVideoId =
        static_cast<int>(m_mpv->getInt(QStringLiteral("vid")));
    updated.selectedAudioId =
        static_cast<int>(m_mpv->getInt(QStringLiteral("aid")));
    updated.selectedSubtitleId =
        static_cast<int>(m_mpv->getInt(QStringLiteral("sid")));
    const QString secondarySid =
        m_mpv->getString(QStringLiteral("secondary-sid"));
    bool secondarySidIsNumber = false;
    const int secondarySidValue =
        secondarySid.toInt(&secondarySidIsNumber);
    updated.selectedSecondarySubtitleId =
        secondarySidIsNumber ? secondarySidValue : 0;
    if (updated == m_info.tracks) {
        return;
    }
    m_info.tracks = std::move(updated);
    m_info.hasVideo = m_info.tracks.selectedVideoId > 0;
    m_info.hasAudio = m_info.tracks.selectedAudioId > 0;
    emit tracksChanged(m_info.tracks);
}

void PlayerCore::synchronizeSubtitleSettings()
{
    SubtitleSettings updated;
    updated.primaryVisible =
        m_mpv->getFlag(QStringLiteral("sub-visibility"));
    updated.secondaryVisible =
        m_mpv->getFlag(
            QStringLiteral("secondary-sub-visibility"));
    updated.primaryDelay =
        m_mpv->getDouble(QStringLiteral("sub-delay"));
    updated.secondaryDelay =
        m_mpv->getDouble(
            QStringLiteral("secondary-sub-delay"));
    updated.primaryPosition =
        static_cast<int>(m_mpv->getInt(QStringLiteral("sub-pos")));
    updated.secondaryPosition =
        static_cast<int>(
            m_mpv->getInt(QStringLiteral("secondary-sub-pos")));
    updated.scale = std::clamp(
        m_mpv->getDouble(QStringLiteral("sub-scale")), 0.1, 10.0);
    updated.font = m_mpv->getString(QStringLiteral("sub-font"));
    updated.fontSize =
        m_mpv->getDouble(QStringLiteral("sub-font-size"));
    updated.textColor =
        m_mpv->getString(QStringLiteral("sub-color"));
    updated.backgroundColor =
        m_mpv->getString(QStringLiteral("sub-back-color"));
    updated.borderColor =
        m_mpv->getString(QStringLiteral("sub-border-color"));
    updated.borderSize =
        m_mpv->getDouble(QStringLiteral("sub-border-size"));
    updated.assOverride =
        m_mpv->getString(QStringLiteral("sub-ass-override"));
    if (updated == m_info.subtitles) {
        return;
    }
    m_info.subtitles = std::move(updated);
    emit subtitleSettingsChanged(m_info.subtitles);
}

void PlayerCore::synchronizeVideoQuickSettings(
    const QVariant &filters)
{
    VideoQuickSettings updated;
    const QString aspect =
        m_mpv->getString(QStringLiteral("video-aspect-override"));
    updated.aspectRatio =
        aspect.isEmpty() || aspect == QStringLiteral("-1")
        || aspect == QStringLiteral("no")
            ? QStringLiteral("Default") : aspect;
    updated.rotation = static_cast<int>(
        m_mpv->getInt(QStringLiteral("video-rotate")));
    const QString hwdec =
        m_mpv->getString(QStringLiteral("hwdec"));
    updated.hardwareDecoding =
        !hwdec.isEmpty() && hwdec != QStringLiteral("no");
    updated.deinterlace =
        m_mpv->getFlag(QStringLiteral("deinterlace"));
    updated.brightness = static_cast<int>(
        m_mpv->getInt(QStringLiteral("brightness")));
    updated.contrast = static_cast<int>(
        m_mpv->getInt(QStringLiteral("contrast")));
    updated.saturation = static_cast<int>(
        m_mpv->getInt(QStringLiteral("saturation")));
    updated.gamma = static_cast<int>(
        m_mpv->getInt(QStringLiteral("gamma")));
    updated.hue = static_cast<int>(
        m_mpv->getInt(QStringLiteral("hue")));

    const QVariant filterNode =
        filters.isValid()
            ? filters : m_mpv->getNode(QStringLiteral("vf"));
    updated.filters = mediaFiltersFromMpvNode(filterNode);
    updated.flipped = std::any_of(
        updated.filters.cbegin(), updated.filters.cend(),
        [](const MediaFilterInfo &filter) {
            return filter.label == QStringLiteral("supernova_flip");
        });
    updated.mirrored = std::any_of(
        updated.filters.cbegin(), updated.filters.cend(),
        [](const MediaFilterInfo &filter) {
            return filter.label == QStringLiteral("supernova_mirror");
        });
    updated.crop = QStringLiteral("None");
    for (const QVariant &entry : filterNode.toList()) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("label")).toString()
            != QStringLiteral("supernova_crop")) {
            continue;
        }
        const QVariantMap params =
            map.value(QStringLiteral("params")).toMap();
        const int width =
            params.value(QStringLiteral("w")).toInt();
        const int height =
            params.value(QStringLiteral("h")).toInt();
        if (width > 0 && height > 0) {
            const double ratio =
                static_cast<double>(width) / height;
            const QList<QPair<QString, double>> presets{
                {QStringLiteral("4:3"), 4.0 / 3.0},
                {QStringLiteral("16:9"), 16.0 / 9.0},
                {QStringLiteral("16:10"), 16.0 / 10.0},
                {QStringLiteral("21:9"), 21.0 / 9.0},
                {QStringLiteral("5:4"), 5.0 / 4.0}};
            updated.crop = QStringLiteral("Custom");
            for (const auto &[name, preset] : presets) {
                if (std::abs(ratio - preset) < 0.02) {
                    updated.crop = name;
                    break;
                }
            }
        }
        break;
    }
    if (updated == m_info.videoSettings) {
        return;
    }
    m_info.videoSettings = std::move(updated);
    emit videoQuickSettingsChanged(m_info.videoSettings);
}

void PlayerCore::synchronizeAudioQuickSettings(
    const QVariant &filters)
{
    AudioQuickSettings updated;
    updated.equalizer = m_info.audioSettings.equalizer;
    updated.devices = AudioOutputDevice::fromMpvNode(
        m_mpv->getNode(QStringLiteral("audio-device-list")));
    updated.selectedDevice =
        m_mpv->getString(QStringLiteral("audio-device"));
    if (updated.selectedDevice.isEmpty()) {
        updated.selectedDevice = QStringLiteral("auto");
    }
    updated.channels =
        m_mpv->getString(QStringLiteral("audio-channels"));
    if (updated.channels.isEmpty()) {
        updated.channels = QStringLiteral("auto-safe");
    }
    updated.delay =
        m_mpv->getDouble(QStringLiteral("audio-delay"));
    const QVariant filterNode =
        filters.isValid()
            ? filters : m_mpv->getNode(QStringLiteral("af"));
    updated.filters = mediaFiltersFromMpvNode(filterNode);
    if (updated == m_info.audioSettings) {
        return;
    }
    m_info.audioSettings = std::move(updated);
    emit audioQuickSettingsChanged(m_info.audioSettings);
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
    if (reachedEnd) {
        if (QSettings().value(
                QStringLiteral("history/resumePlayback"), true).toBool()) {
            m_mpv->command(
                {QStringLiteral("delete-watch-later-config")});
        }
    } else {
        cacheWatchLaterPosition();
    }
}

void PlayerCore::cacheWatchLaterPosition()
{
    if (!isLoaded(m_info.state)
        || !QSettings().value(
                QStringLiteral("history/resumePlayback"), true).toBool()
        || m_info.videoPositionSec < 5.0
        || (m_info.videoDurationSec > 0.0
            && m_info.videoPositionSec
                   >= m_info.videoDurationSec - 10.0)) {
        return;
    }
    m_lastWatchLaterSavePosition = m_info.videoPositionSec;
    m_mpv->command(
        {QStringLiteral("write-watch-later-config")});
}

AutomaticMatchOptions PlayerCore::automaticMatchOptions() const
{
    const QSettings settings;
    AutomaticMatchOptions options;
    options.addSiblingsToPlaylist = settings.value(
        QStringLiteral("matching/playlistAutoAdd"), true).toBool();
    options.subtitleMode = static_cast<SubtitleAutoLoadMode>(
        std::clamp(
            settings.value(
                QStringLiteral("matching/subtitleMode"),
                static_cast<int>(SubtitleAutoLoadMode::Smart))
                .toInt(),
            static_cast<int>(SubtitleAutoLoadMode::Disabled),
            static_cast<int>(SubtitleAutoLoadMode::Smart)));
    options.subtitleSearchPaths = settings.value(
        QStringLiteral("matching/subtitleSearchPaths"),
        QStringLiteral("./*")).toString();
    options.subtitlePriorityStrings = settings.value(
        QStringLiteral("matching/subtitlePriorityStrings")).toString();
    return options;
}

void PlayerCore::startAutomaticMatching(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return;
    }
    const QFileInfo file(url.toLocalFile());
    if (!MediaSourceResolver::supportedMediaExtensions().contains(
            file.suffix().toLower())) {
        return;
    }
    QString folder = file.absoluteDir().canonicalPath();
    if (folder.isEmpty()) {
        folder = file.absolutePath();
    }
    folder = QDir::cleanPath(folder);
    if (folder.compare(m_matchingFolder, Qt::CaseInsensitive) == 0) {
        if (m_matchingReady) {
            loadMatchedSubtitlesForCurrentFile();
        }
        return;
    }

    const quint64 generation =
        m_matchingGeneration.fetch_add(
            1, std::memory_order_relaxed) + 1;
    m_matchingFolder = folder;
    m_matchingInProgress = true;
    m_matchingReady = false;
    m_matchedSubtitles.clear();
    const AutomaticMatchOptions options = automaticMatchOptions();
    QPointer<PlayerCore> guarded(this);
    m_matchingPool.start(QRunnable::create(
        [this, guarded, generation, folder, url, options] {
            const AutomaticMatchResult result =
                AutomaticFileMatcher::match(
                    url, options, [this, generation] {
                        return m_matchingGeneration.load(
                                   std::memory_order_relaxed)
                               != generation;
                    });
            if (result.cancelled || !guarded) {
                return;
            }
            QMetaObject::invokeMethod(
                guarded,
                [guarded, generation, folder,
                 matches = result.subtitlesByMedia] {
                    if (guarded) {
                        guarded->applyAutomaticMatches(
                            generation, folder, matches);
                    }
                },
                Qt::QueuedConnection);
        }));
}

void PlayerCore::applyAutomaticMatches(
    quint64 generation, const QString &folder,
    const QHash<QString, QList<QUrl>> &matches)
{
    if (generation
            != m_matchingGeneration.load(std::memory_order_relaxed)
        || folder.compare(m_matchingFolder, Qt::CaseInsensitive) != 0) {
        return;
    }
    m_matchingInProgress = false;
    m_matchingReady = true;
    m_matchedSubtitles = matches;
    loadMatchedSubtitlesForCurrentFile();
}

void PlayerCore::loadMatchedSubtitlesForCurrentFile()
{
    if (!isLoaded(m_info.state) || !m_info.currentUrl.isLocalFile()) {
        return;
    }
    const QList<QUrl> matches = m_matchedSubtitles.value(
        AutomaticFileMatcher::mediaKey(m_info.currentUrl));
    bool selectFirst = m_loadedAutomaticSubtitles.isEmpty();
    for (const QUrl &subtitle : matches) {
        const QString key = AutomaticFileMatcher::mediaKey(subtitle);
        if (m_loadedAutomaticSubtitles.contains(key)) {
            continue;
        }
        const bool alreadyLoaded = std::any_of(
            m_info.tracks.subtitleTracks.cbegin(),
            m_info.tracks.subtitleTracks.cend(),
            [&key](const MediaTrack &track) {
                return track.isExternal
                    && AutomaticFileMatcher::mediaKey(
                           QUrl::fromLocalFile(track.externalFilename))
                           == key;
            });
        m_loadedAutomaticSubtitles.insert(key);
        if (alreadyLoaded) {
            continue;
        }
        const QString path =
            QDir::toNativeSeparators(subtitle.toLocalFile());
        m_mpv->command(
            {QStringLiteral("sub-add"), path,
             selectFirst ? QStringLiteral("select")
                         : QStringLiteral("auto")},
            [path](const MpvCommandResult &result) {
                if (!result.succeeded()) {
                    Logger::warn(
                        QStringLiteral(
                            "Could not auto-load subtitle '%1': %2")
                            .arg(path, result.errorMessage));
                }
            });
        selectFirst = false;
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
    if (!(m_info.tracks == MediaTrackState{})) {
        m_info.tracks = {};
        emit tracksChanged(m_info.tracks);
    }
    if (!(m_info.videoSettings == VideoQuickSettings{})) {
        m_info.videoSettings = {};
        emit videoQuickSettingsChanged(m_info.videoSettings);
    }
    const QList<AudioOutputDevice> devices =
        m_info.audioSettings.devices;
    if (!(m_info.audioSettings == AudioQuickSettings{})) {
        m_info.audioSettings = {};
        m_info.audioSettings.devices = devices;
        emit audioQuickSettingsChanged(m_info.audioSettings);
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
    synchronizeTracks();
    synchronizeSubtitleSettings();
    synchronizeVideoQuickSettings();
    synchronizeAudioQuickSettings();

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

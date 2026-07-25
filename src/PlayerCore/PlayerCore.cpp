#include "PlayerCore/PlayerCore.h"

#include "Core/Logger.h"
#include "Mpv/MpvCore.h"

#include <algorithm>
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

    openPrimaryUrl(validUrls.constFirst());
    for (qsizetype index = 1; index < validUrls.size(); ++index) {
        const QUrl &url = validUrls.at(index);
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
    if (validUrls.size() > 1) {
        Logger::info(
            QStringLiteral("Added %1 additional media item(s) to the playlist")
                .arg(validUrls.size() - 1));
    }
}

void PlayerCore::openUrl(const QUrl &url)
{
    openUrls({url});
}

void PlayerCore::openPrimaryUrl(const QUrl &url)
{
    m_info.currentUrl = url;
    emit currentUrlChanged(url);
    m_info.isNetworkResource = !url.isLocalFile();
    m_info.videoWidth = 0;
    m_info.videoHeight = 0;
    m_info.videoPositionSec = 0.0;
    m_info.videoDurationSec = 0.0;
    m_pendingPlaybackError.clear();

    // Pause before loadfile so audio cannot start before the first video frame
    // is ready, which is noticeable with expensive software decoding.
    if (!m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }

    // Delay force-window until an actual load. Setting it during mpv setup can
    // race VO creation against the QOpenGLWidget render-context setup.
    m_mpv->setString(QStringLiteral("force-window"), QStringLiteral("yes"));

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
    if (m_info.state == PlayerState::Paused
        || (m_info.state == PlayerState::Idle
            && !m_info.currentUrl.isEmpty())) {
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
    const bool restartingAtEof =
        m_info.state == PlayerState::Idle
        && !m_info.currentUrl.isEmpty()
        && canAccessMpv()
        && m_mpv->getFlag(QStringLiteral("eof-reached"));
    if (!isLoaded(m_info.state) && !restartingAtEof) {
        return;
    }

    if (m_mpv->getFlag(QStringLiteral("eof-reached"))) {
        m_mpv->command(
            {QStringLiteral("seek"), QStringLiteral("0"),
             QStringLiteral("absolute+exact")});
    }
    m_mpv->setFlag(QStringLiteral("pause"), false);
    if (restartingAtEof) {
        setState(PlayerState::Playing);
    }
}

void PlayerCore::stop()
{
    if (!canAccessMpv() || m_info.state == PlayerState::Idle
        || m_info.state == PlayerState::Stopping) {
        return;
    }

    if (isActive(m_info.state)
        && !m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), true);
    }
    setState(PlayerState::Stopping);
    m_mpv->command({QStringLiteral("stop")});
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
        if ((m_info.state == PlayerState::Paused) != paused) {
            setState(
                paused ? PlayerState::Paused : PlayerState::Playing);
        }
    } else if (name == QStringLiteral("idle-active")
               && value.toBool()
               && m_info.state != PlayerState::ShuttingDown
               && m_info.state != PlayerState::ShutDown
               && (m_info.state != PlayerState::Loading
                   || !m_pendingPlaybackError.isEmpty())
               && (m_info.state != PlayerState::Starting
                   || !m_pendingPlaybackError.isEmpty())
               && (m_info.state != PlayerState::Loaded
                   || !m_pendingPlaybackError.isEmpty())) {
        setState(PlayerState::Idle);
        if (!m_pendingPlaybackError.isEmpty()) {
            const QString error = std::exchange(
                m_pendingPlaybackError, {});
            emit playbackError(error, true);
        }
        if (!m_pendingUrls.isEmpty()) {
            const QList<QUrl> pending = std::exchange(m_pendingUrls, {});
            openUrls(pending);
        }
    } else if (name == QStringLiteral("eof-reached")
               && value.toBool()
               && isLoaded(m_info.state)) {
        setState(PlayerState::Idle);
    } else if (name == QStringLiteral("time-pos")) {
        m_info.videoPositionSec = value.toDouble();
        emit positionChanged(m_info.videoPositionSec);
    } else if (name == QStringLiteral("duration")) {
        m_info.videoDurationSec = value.toDouble();
        emit durationChanged(m_info.videoDurationSec);
    } else if (name == QStringLiteral("volume")) {
        m_info.volume = value.toDouble();
    } else if (name == QStringLiteral("mute")) {
        m_info.isMuted = value.toBool();
    } else if (name == QStringLiteral("speed")) {
        m_info.playSpeed = value.toDouble();
    }
}

void PlayerCore::onMpvFileStarted(const QString &path)
{
    if (!isActive(m_info.state)) {
        return;
    }
    if (!path.isEmpty()) {
        Logger::info(QStringLiteral("mpv started file: %1").arg(path));
    }
    setState(PlayerState::Starting);
}

void PlayerCore::onMpvFileLoaded()
{
    if (!isActive(m_info.state)) {
        return;
    }
    setState(PlayerState::Loaded);
    m_info.justOpenedFile = false;
    updateVideoSize();
}

void PlayerCore::onMpvFileEnded(const MpvEndFileInfo &info)
{
    if (!canAccessMpv()) {
        return;
    }

    if (info.reason == MPV_END_FILE_REASON_ERROR) {
        const QString source =
            m_info.currentUrl.isLocalFile()
                ? m_info.currentUrl.toLocalFile()
                : m_info.currentUrl.toDisplayString();
        m_pendingPlaybackError =
            tr("Playback failed for %1: %2")
                .arg(source, info.errorMessage);
        Logger::warn(m_pendingPlaybackError);
    } else if (info.reason == MPV_END_FILE_REASON_STOP
               && m_info.state != PlayerState::Stopping) {
        setState(PlayerState::Idle);
    }
}

void PlayerCore::onMpvVideoReconfig()
{
    if (m_info.state != PlayerState::Loaded) {
        return;
    }
    updateVideoSize();
    if (m_mpv->getFlag(QStringLiteral("pause"))) {
        m_mpv->setFlag(QStringLiteral("pause"), false);
    }
    setState(PlayerState::Playing);
}

void PlayerCore::onMpvSeekStarted()
{
    if (!isLoaded(m_info.state) || m_isSeeking) {
        return;
    }
    m_isSeeking = true;
    emit seekingChanged(true);
}

void PlayerCore::onMpvPlaybackRestarted()
{
    if (!canAccessMpv()) {
        return;
    }
    if (m_isSeeking) {
        m_isSeeking = false;
        emit seekingChanged(false);
    }
    if (m_info.state == PlayerState::Loaded
        || m_info.state == PlayerState::Starting) {
        setState(
            m_mpv->getFlag(QStringLiteral("pause"))
                ? PlayerState::Paused
                : PlayerState::Playing);
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
        setState(PlayerState::Idle);
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

void PlayerCore::resynchronizeFromMpv()
{
    if (!canAccessMpv()) {
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
    emit positionChanged(m_info.videoPositionSec);
    emit durationChanged(m_info.videoDurationSec);
    updateVideoSize();

    if (m_mpv->getFlag(QStringLiteral("idle-active"))) {
        setState(PlayerState::Idle);
    } else if (isLoaded(m_info.state)) {
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

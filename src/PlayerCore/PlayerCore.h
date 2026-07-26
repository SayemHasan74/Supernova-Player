#pragma once

#include "Mpv/MpvEvent.h"
#include "PlayerCore/PlaybackInfo.h"

#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariant>

#include <memory>
#include <optional>

class MpvCore;

class PlayerCore final : public QObject {
    Q_OBJECT

public:
    explicit PlayerCore(QObject *parent = nullptr);
    ~PlayerCore() override;

    PlayerCore(const PlayerCore &) = delete;
    PlayerCore &operator=(const PlayerCore &) = delete;

    [[nodiscard]] MpvCore *mpvCore() const noexcept { return m_mpv.get(); }
    [[nodiscard]] const PlaybackInfo &info() const noexcept { return m_info; }

    void openUrls(const QList<QUrl> &urls);
    void openUrl(const QUrl &url);

    void togglePause();
    void pause();
    void resume();
    void stop();
    void navigateInPlaylist(bool nextMedia);

    void seekPercent(double percent, bool forceExact = false);
    void seekRelative(double seconds, bool exact = false);
    void seekAbsolute(double seconds);
    void stepFrame(bool backward);
    void takeScreenshot();

    void setVolume(double volume);
    void toggleMute();
    void setSpeed(double speed);

    void shutdown();

signals:
    void stateChanged(PlayerState newState);
    void currentUrlChanged(const QUrl &url);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void videoSizeChanged(int width, int height);
    void seekingChanged(bool seeking);
    void bufferingChanged(const BufferingInfo &buffering);
    void eofChanged(bool reached);
    void mediaLoaded(const QUrl &url);
    void mediaEnded(const MpvEndFileInfo &info);
    void playbackStopped();
    void playbackError(const QString &message, bool recoverable);

private slots:
    void onMpvPropertyChanged(const QString &name, const QVariant &value);
    void onMpvFileStarted(const QString &path);
    void onMpvFileLoaded();
    void onMpvFileEnded(const MpvEndFileInfo &info);
    void onMpvVideoReconfig();
    void onMpvAudioReconfig();
    void onMpvSeekStarted();
    void onMpvPlaybackRestarted();
    void onMpvEventQueueOverflow();
    void onMpvError(const QString &context, int errorCode,
                    const QString &message, bool recoverable);
    void onMpvShutdown();

private:
    [[nodiscard]] bool canAccessMpv() const noexcept;
    void handlePlaybackFailure(
        const QString &message, int errorCode);
    void openPrimaryUrl(const QUrl &url);
    void finishLoadingWhenReady();
    void setBufferingInfo(const BufferingInfo &buffering);
    void setEofReached(bool reached);
    void resetTransientPlaybackInfo();
    void resynchronizeFromMpv();
    void updateVideoSize();
    void setState(PlayerState newState);

    std::unique_ptr<MpvCore> m_mpv;
    PlaybackInfo m_info;
    QList<QUrl> m_pendingUrls;
    QString m_pendingPlaybackError;
    bool m_isSeeking = false;

    friend class PlayerCoreLifecycleTests;
};

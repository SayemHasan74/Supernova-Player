#pragma once

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

    void seekPercent(double percent, bool forceExact = false);
    void seekRelative(double seconds, bool exact = false);
    void seekAbsolute(double seconds);

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

private slots:
    void onMpvPropertyChanged(const QString &name, const QVariant &value);
    void onMpvFileStarted(const QString &path);
    void onMpvFileLoaded();
    void onMpvVideoReconfig();
    void onMpvShutdown();

private:
    [[nodiscard]] bool canAccessMpv() const noexcept;
    void openPrimaryUrl(const QUrl &url);
    void updateVideoSize();
    void setState(PlayerState newState);

    std::unique_ptr<MpvCore> m_mpv;
    PlaybackInfo m_info;
    QList<QUrl> m_pendingUrls;
};

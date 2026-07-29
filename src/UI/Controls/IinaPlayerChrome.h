#pragma once

#include "PlayerCore/BufferingInfo.h"
#include "PlayerCore/NavigationState.h"

#include <QAbstractButton>
#include <QPoint>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QSlider;
class PlayerCore;

enum class IinaIcon {
    Play,
    Pause,
    Previous,
    Next,
    VolumeOff,
    VolumeLow,
    VolumeMedium,
    VolumeHigh,
    Muted,
    Folder,
    Playlist,
    Settings,
    PictureInPicture,
    FullScreen,
    ExitFullScreen,
    Close,
    Back,
    AlbumArt,
    Shuffle,
    Repeat,
};

class IinaIconButton final : public QAbstractButton {
    Q_OBJECT

public:
    explicit IinaIconButton(
        IinaIcon icon, QWidget *parent = nullptr);

    void setIconType(IinaIcon icon);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    IinaIcon m_icon;
};

class IinaTimeline final : public QWidget {
    Q_OBJECT

public:
    explicit IinaTimeline(QWidget *parent = nullptr);

    void setPlayback(double position, double duration);
    void setBuffering(const BufferingInfo &buffering, bool networkResource);
    void setSeeking(bool seeking);
    void setChapters(const QList<PlaybackChapter> &chapters);
    void setAbLoop(const AbLoopState &state);

signals:
    void seekRequested(double percent);
    void seekStarted();
    void seekFinished(double percent);
    void previewRequested(double seconds, const QPoint &globalAnchor);
    void previewDismissed();
    void interaction();

protected:
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    [[nodiscard]] double ratioAt(double x) const noexcept;
    void previewAt(double x);
    double seekAt(double x);

    double m_position = 0.0;
    double m_duration = 0.0;
    double m_cacheDuration = 0.0;
    double m_previewRatio = 0.0;
    bool m_dragging = false;
    bool m_hovering = false;
    bool m_networkResource = false;
    bool m_seeking = false;
    QList<PlaybackChapter> m_chapters;
    AbLoopState m_abLoop;
};

class IinaPlayerChrome final : public QWidget {
    Q_OBJECT

public:
    explicit IinaPlayerChrome(
        PlayerCore *playerCore, QWidget *parent = nullptr);

    void setFullScreen(bool fullScreen);
    void setPictureInPicture(bool pictureInPicture);
    void reveal(bool animated = true);
    void conceal(bool animated = true);
    [[nodiscard]] bool isConcealed() const noexcept;

signals:
    void activity();
    void fullScreenRequested();
    void pictureInPictureRequested();
    void openFileRequested();
    void playlistRequested();
    void mediaSettingsRequested();
    void progressModeRequested();
    void previewRequested(double seconds, const QPoint &globalAnchor);
    void previewDismissed();

protected:
    void enterEvent(QEnterEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updatePlaybackState();
    void updateTimeLabels();
    void updateVolumeControls(double volume, bool muted);
    static QString formatTime(double seconds);

    PlayerCore *m_playerCore = nullptr;
    IinaIconButton *m_playButton = nullptr;
    IinaIconButton *m_muteButton = nullptr;
    IinaIconButton *m_previousButton = nullptr;
    IinaIconButton *m_nextButton = nullptr;
    IinaIconButton *m_openFileButton = nullptr;
    IinaIconButton *m_playlistButton = nullptr;
    IinaIconButton *m_settingsButton = nullptr;
    IinaIconButton *m_pictureInPictureButton = nullptr;
    IinaIconButton *m_fullScreenButton = nullptr;
    QSlider *m_volumeSlider = nullptr;
    IinaTimeline *m_timeline = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QPropertyAnimation *m_opacityAnimation = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_volume = 100.0;
    bool m_muted = false;
    bool m_concealed = false;
    bool m_wasPausedBeforeTimelineDrag = false;
};

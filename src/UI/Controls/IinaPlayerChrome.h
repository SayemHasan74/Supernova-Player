#pragma once

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QString>
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
    FullScreen,
    ExitFullScreen,
};

class IinaIconButton final : public QAbstractButton {
    Q_OBJECT

public:
    explicit IinaIconButton(
        IinaIcon icon, QWidget *parent = nullptr);

    void setIconType(IinaIcon icon);
    void setBadgeText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    IinaIcon m_icon;
    QString m_badgeText;
};

class IinaTimeline final : public QWidget {
    Q_OBJECT

public:
    explicit IinaTimeline(QWidget *parent = nullptr);

    void setPlayback(double position, double duration);

signals:
    void seekRequested(double percent);
    void interaction();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void seekAt(double x);

    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_dragging = false;
};

class IinaPlayerChrome final : public QWidget {
    Q_OBJECT

public:
    explicit IinaPlayerChrome(
        PlayerCore *playerCore, QWidget *parent = nullptr);

    void setFullScreen(bool fullScreen);
    void reveal(bool animated = true);
    void conceal(bool animated = true);
    [[nodiscard]] bool isConcealed() const noexcept;

signals:
    void activity();
    void fullScreenRequested();
    void progressModeRequested();

protected:
    void enterEvent(QEnterEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updatePlaybackState();
    void updateSpeedControls(double speed);
    void updateTimeLabels();
    void updateVolumeControls(double volume, bool muted);
    void activateSpeedStep(int direction);
    void finishSpeedStep(int direction);
    static QString formatTime(double seconds);

    PlayerCore *m_playerCore = nullptr;
    IinaIconButton *m_playButton = nullptr;
    IinaIconButton *m_muteButton = nullptr;
    IinaIconButton *m_previousButton = nullptr;
    IinaIconButton *m_nextButton = nullptr;
    IinaIconButton *m_fullScreenButton = nullptr;
    QSlider *m_volumeSlider = nullptr;
    IinaTimeline *m_timeline = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QPropertyAnimation *m_opacityAnimation = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_speed = 1.0;
    double m_volume = 100.0;
    bool m_muted = false;
    bool m_speedStepping = false;
    QElapsedTimer m_speedPressTimer;
    bool m_concealed = false;
};

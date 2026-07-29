#pragma once

#include <QImage>
#include <QWidget>

class IinaIconButton;
class IinaTimeline;
class QLabel;
class PlaylistPanel;
class PlayerCore;
class QFrame;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QSlider;
class QTimer;

class MusicModeView final : public QWidget {
    Q_OBJECT

public:
    explicit MusicModeView(
        PlayerCore *playerCore, QWidget *parent = nullptr);

    void attachPlaylistPanel(PlaylistPanel *panel);
    void detachPlaylistPanel(QWidget *newParent);
    void setPlaylistVisible(bool visible);
    [[nodiscard]] bool isPlaylistVisible() const noexcept;
    [[nodiscard]] bool isArtworkVisible() const noexcept;
    [[nodiscard]] QSize preferredSize() const;
    [[nodiscard]] bool isInteractiveAt(
        const QPoint &globalPosition) const;
    void nativePointerMoved();
    void refresh();

signals:
    void backRequested();
    void closeRequested();
    void playlistRequested();
    void preferredSizeChanged();
    void previewRequested(double seconds, const QPoint &globalAnchor);
    void previewDismissed();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setControlsVisible(bool visible, bool animated = true);
    void setArtworkVisible(bool visible);
    void updateMetadata();
    void updatePlaybackState();
    void updateTimeLabels();
    void updateVolume(double volume, bool muted);
    static QString formatTime(double seconds);

    PlayerCore *m_playerCore = nullptr;
    PlaylistPanel *m_playlistPanel = nullptr;
    QWidget *m_infoView = nullptr;
    QWidget *m_controlsView = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_artistAlbumLabel = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    IinaTimeline *m_timeline = nullptr;
    IinaIconButton *m_closeButton = nullptr;
    IinaIconButton *m_backButton = nullptr;
    IinaIconButton *m_volumeButton = nullptr;
    IinaIconButton *m_previousButton = nullptr;
    IinaIconButton *m_playButton = nullptr;
    IinaIconButton *m_nextButton = nullptr;
    IinaIconButton *m_playlistButton = nullptr;
    IinaIconButton *m_artworkButton = nullptr;
    IinaIconButton *m_shuffleButton = nullptr;
    IinaIconButton *m_repeatButton = nullptr;
    QLabel *m_genreLabel = nullptr;
    QFrame *m_volumePopover = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QGraphicsOpacityEffect *m_infoOpacity = nullptr;
    QGraphicsOpacityEffect *m_controlsOpacity = nullptr;
    QPropertyAnimation *m_infoAnimation = nullptr;
    QPropertyAnimation *m_controlsAnimation = nullptr;
    QTimer *m_nativeHoverTimer = nullptr;
    QImage m_artwork;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_volume = 100.0;
    bool m_muted = false;
    bool m_showArtwork = true;
    bool m_showPlaylist = false;
    bool m_controlsVisible = false;
    bool m_wasPausedBeforeTimelineDrag = false;
};

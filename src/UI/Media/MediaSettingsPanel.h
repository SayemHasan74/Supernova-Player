#pragma once

#include "PlayerCore/MediaTrack.h"

#include <QWidget>

class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTabWidget;
class PlayerCore;

class MediaSettingsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit MediaSettingsPanel(
        PlayerCore *playerCore, QWidget *parent = nullptr);

signals:
    void closeRequested();

private:
    QWidget *createTrackPage(MediaTrackType type);
    QWidget *createSubtitlePage();
    QListWidget *createTrackList(const QString &objectName);
    void refreshTracks(const MediaTrackState &tracks);
    void refreshSubtitleSettings(const SubtitleSettings &settings);
    void populateTrackList(
        QListWidget *list, const QList<MediaTrack> &tracks,
        int selectedId);
    void chooseExternalFile(MediaTrackType type);
    void chooseExternalSubtitle();
    void showTrackContextMenu(
        QListWidget *list, MediaTrackType type, const QPoint &position);
    void chooseColor(
        QPushButton *button,
        void (PlayerCore::*setter)(const QString &));
    static QString colorToMpv(const QColor &color);
    static QColor colorFromMpv(const QString &value);
    static double realScaleFromSlider(int value);
    static int sliderFromRealScale(double value);

    PlayerCore *m_playerCore = nullptr;
    QTabWidget *m_tabs = nullptr;
    QListWidget *m_videoTracks = nullptr;
    QListWidget *m_audioTracks = nullptr;
    QListWidget *m_primarySubtitles = nullptr;
    QListWidget *m_secondarySubtitles = nullptr;
    QCheckBox *m_primaryVisible = nullptr;
    QCheckBox *m_secondaryVisible = nullptr;
    QComboBox *m_targetSubtitle = nullptr;
    QDoubleSpinBox *m_delay = nullptr;
    QSlider *m_delaySlider = nullptr;
    QSlider *m_position = nullptr;
    QSlider *m_scale = nullptr;
    QFontComboBox *m_font = nullptr;
    QComboBox *m_fontSize = nullptr;
    QComboBox *m_borderSize = nullptr;
    QComboBox *m_assOverride = nullptr;
    QPushButton *m_textColor = nullptr;
    QPushButton *m_backgroundColor = nullptr;
    QPushButton *m_borderColor = nullptr;
    QLabel *m_styleWarning = nullptr;
    bool m_refreshing = false;
};

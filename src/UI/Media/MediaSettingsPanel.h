#pragma once

#include "PlayerCore/MediaTrack.h"
#include "PlayerCore/QuickSettings.h"

#include <QWidget>
#include <QHash>

#include <array>

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
    QWidget *createVideoControls(QWidget *parent);
    QWidget *createAudioControls(QWidget *parent);
    QWidget *createSubtitlePage();
    QListWidget *createTrackList(const QString &objectName);
    void refreshTracks(const MediaTrackState &tracks);
    void refreshSubtitleSettings(const SubtitleSettings &settings);
    void refreshVideoSettings(const VideoQuickSettings &settings);
    void refreshAudioSettings(const AudioQuickSettings &settings);
    void populateFilterList(
        QListWidget *list, const QList<MediaFilterInfo> &filters);
    void promptForFilter(bool video);
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
    QComboBox *m_aspect = nullptr;
    QComboBox *m_crop = nullptr;
    QComboBox *m_rotation = nullptr;
    QCheckBox *m_hardwareDecoding = nullptr;
    QCheckBox *m_deinterlace = nullptr;
    QCheckBox *m_flip = nullptr;
    QCheckBox *m_mirror = nullptr;
    QHash<QString, QSlider *> m_videoColorSliders;
    QListWidget *m_videoFilters = nullptr;
    QComboBox *m_audioDevice = nullptr;
    QComboBox *m_audioChannels = nullptr;
    QDoubleSpinBox *m_audioDelay = nullptr;
    QSlider *m_audioDelaySlider = nullptr;
    QComboBox *m_equalizerPreset = nullptr;
    std::array<QSlider *, 10> m_equalizerSliders{};
    QListWidget *m_audioFilters = nullptr;
    bool m_refreshing = false;
};

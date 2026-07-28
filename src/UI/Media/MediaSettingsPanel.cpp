#include "UI/Media/MediaSettingsPanel.h"

#include "App/MediaSourceResolver.h"
#include "PlayerCore/PlayerCore.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int trackIdRole = Qt::UserRole;
constexpr int externalRole = Qt::UserRole + 1;

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QPushButton *flatButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(28);
    return button;
}

QString externalFilter(MediaTrackType type)
{
    if (type == MediaTrackType::Audio) {
        return QObject::tr(
            "Audio Files (*.aac *.ac3 *.aiff *.alac *.dts *.eac3 *.flac "
            "*.m4a *.mp3 *.ogg *.opus *.wav *.wma);;All Files (*)");
    }
    return MediaSourceResolver::mediaDialogFilter();
}
}

MediaSettingsPanel::MediaSettingsPanel(
    PlayerCore *playerCore, QWidget *parent)
    : QWidget(parent), m_playerCore(playerCore)
{
    setObjectName(QStringLiteral("mediaSettingsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#mediaSettingsPanel { background: rgba(27, 27, 31, 247);"
        " border-left: 1px solid rgba(255,255,255,28); }"
        "#mediaSettingsPanel QLabel { color: rgb(235,235,240); }"
        "#mediaSettingsPanel QTabWidget::pane { border: 0; }"
        "#mediaSettingsPanel QTabBar::tab { color: rgba(235,235,240,170);"
        " padding: 9px 13px; border: 0; }"
        "#mediaSettingsPanel QTabBar::tab:selected {"
        " color: white; border-bottom: 2px solid rgb(64,145,255); }"
        "#mediaSettingsPanel QListWidget { background: rgba(255,255,255,8);"
        " border: 1px solid rgba(255,255,255,20); border-radius: 7px;"
        " color: rgb(235,235,240); outline: 0; }"
        "#mediaSettingsPanel QListWidget::item { height: 26px;"
        " border-bottom: 1px solid rgba(255,255,255,14); padding-left: 9px; }"
        "#mediaSettingsPanel QListWidget::item:selected {"
        " background: rgba(64,145,255,85);"
        " border-left: 4px solid rgb(64,145,255); }"
        "#mediaSettingsPanel QPushButton, #mediaSettingsPanel QComboBox,"
        "#mediaSettingsPanel QDoubleSpinBox, #mediaSettingsPanel QFontComboBox {"
        " color: rgb(235,235,240); background: rgba(255,255,255,18);"
        " border: 1px solid rgba(255,255,255,28); border-radius: 6px;"
        " padding: 4px 8px; }"
        "#mediaSettingsPanel QCheckBox { color: rgb(235,235,240); }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(7);
    auto *header = new QHBoxLayout;
    auto *title = sectionLabel(tr("Quick Settings"), this);
    header->addWidget(title);
    header->addStretch();
    auto *close = flatButton(QStringLiteral("×"), this);
    close->setObjectName(QStringLiteral("closeMediaSettingsButton"));
    close->setFixedSize(28, 28);
    header->addWidget(close);
    root->addLayout(header);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(
        createTrackPage(MediaTrackType::Video), tr("Video"));
    m_tabs->addTab(
        createTrackPage(MediaTrackType::Audio), tr("Audio"));
    m_tabs->addTab(createSubtitlePage(), tr("Subtitles"));
    root->addWidget(m_tabs, 1);

    connect(close, &QPushButton::clicked,
            this, &MediaSettingsPanel::closeRequested);
    connect(
        m_playerCore, &PlayerCore::tracksChanged,
        this, &MediaSettingsPanel::refreshTracks);
    connect(
        m_playerCore, &PlayerCore::subtitleSettingsChanged,
        this, &MediaSettingsPanel::refreshSubtitleSettings);
    refreshTracks(m_playerCore->info().tracks);
    refreshSubtitleSettings(m_playerCore->info().subtitles);
    hide();
}

QListWidget *MediaSettingsPanel::createTrackList(
    const QString &objectName)
{
    auto *list = new QListWidget(this);
    list->setObjectName(objectName);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setMinimumHeight(98);
    return list;
}

QWidget *MediaSettingsPanel::createTrackPage(MediaTrackType type)
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);
    auto *label = sectionLabel(
        type == MediaTrackType::Video
            ? tr("Video Track") : tr("Audio Track"),
        page);
    layout->addWidget(label);
    QListWidget *list = createTrackList(
        type == MediaTrackType::Video
            ? QStringLiteral("videoTrackList")
            : QStringLiteral("audioTrackList"));
    if (type == MediaTrackType::Video) {
        m_videoTracks = list;
    } else {
        m_audioTracks = list;
    }
    layout->addWidget(list);
    auto *load = flatButton(
        type == MediaTrackType::Video
            ? tr("Load External Video…")
            : tr("Load External Audio…"),
        page);
    layout->addWidget(load);
    layout->addStretch();

    connect(list, &QListWidget::itemClicked, this,
            [this, type](QListWidgetItem *item) {
                if (!m_refreshing && item) {
                    m_playerCore->setTrack(
                        type, item->data(trackIdRole).toInt());
                }
            });
    connect(list, &QListWidget::customContextMenuRequested,
            this, [this, list, type](const QPoint &position) {
                showTrackContextMenu(list, type, position);
            });
    connect(load, &QPushButton::clicked, this,
            [this, type] { chooseExternalFile(type); });
    return page;
}

QWidget *MediaSettingsPanel::createSubtitlePage()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *page = new QWidget(scroll);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 4, 10);
    layout->setSpacing(8);

    auto addTrackSection = [this, layout, page](
                               bool primary) {
        auto *header = new QHBoxLayout;
        header->addWidget(sectionLabel(
            primary ? tr("Primary") : tr("Secondary"), page));
        header->addStretch();
        auto *visible = new QCheckBox(tr("Visible"), page);
        visible->setChecked(true);
        header->addWidget(visible);
        layout->addLayout(header);
        auto *list = createTrackList(
            primary ? QStringLiteral("primarySubtitleList")
                    : QStringLiteral("secondarySubtitleList"));
        if (primary) {
            m_primaryVisible = visible;
            m_primarySubtitles = list;
        } else {
            m_secondaryVisible = visible;
            m_secondarySubtitles = list;
        }
        layout->addWidget(list);
        connect(visible, &QCheckBox::toggled, this,
                [this, primary](bool checked) {
                    if (!m_refreshing) {
                        m_playerCore->setSubtitleVisibility(
                            primary, checked);
                    }
                });
        connect(list, &QListWidget::itemClicked, this,
                [this, primary](QListWidgetItem *item) {
                    if (!m_refreshing && item) {
                        m_playerCore->setSubtitleTrack(
                            primary,
                            item->data(trackIdRole).toInt());
                    }
                });
        connect(list, &QListWidget::customContextMenuRequested,
                this, [this, list](const QPoint &position) {
                    showTrackContextMenu(
                        list, MediaTrackType::Subtitle, position);
                });
    };
    addTrackSection(true);
    addTrackSection(false);

    auto *fileButtons = new QHBoxLayout;
    auto *load = flatButton(tr("Load Subtitle…"), page);
    auto *reload = flatButton(tr("Reload External"), page);
    fileButtons->addWidget(load, 1);
    fileButtons->addWidget(reload);
    layout->addLayout(fileButtons);
    connect(load, &QPushButton::clicked,
            this, &MediaSettingsPanel::chooseExternalSubtitle);
    connect(reload, &QPushButton::clicked,
            m_playerCore, &PlayerCore::reloadExternalSubtitles);

    m_targetSubtitle = new QComboBox(page);
    m_targetSubtitle->addItems({tr("Primary"), tr("Secondary")});
    layout->addWidget(m_targetSubtitle);

    auto *delayRow = new QHBoxLayout;
    delayRow->addWidget(sectionLabel(tr("Delay"), page));
    m_delay = new QDoubleSpinBox(page);
    m_delay->setDecimals(2);
    m_delay->setRange(-3600.0, 3600.0);
    m_delay->setSingleStep(0.05);
    m_delay->setSuffix(tr(" s"));
    m_delaySlider = new QSlider(Qt::Horizontal, page);
    m_delaySlider->setRange(-100, 100);
    m_delaySlider->setTickInterval(10);
    delayRow->addWidget(m_delaySlider, 1);
    delayRow->addWidget(m_delay);
    layout->addLayout(delayRow);

    auto *positionRow = new QHBoxLayout;
    positionRow->addWidget(sectionLabel(tr("Position"), page));
    m_position = new QSlider(Qt::Horizontal, page);
    m_position->setRange(0, 100);
    positionRow->addWidget(m_position, 1);
    layout->addLayout(positionRow);

    layout->addWidget(sectionLabel(tr("Text Style"), page));
    auto *scaleRow = new QHBoxLayout;
    scaleRow->addWidget(new QLabel(tr("Scale"), page));
    m_scale = new QSlider(Qt::Horizontal, page);
    m_scale->setRange(-100, 100);
    m_scale->setValue(0);
    scaleRow->addWidget(m_scale, 1);
    auto *resetScale = flatButton(tr("Reset"), page);
    scaleRow->addWidget(resetScale);
    layout->addLayout(scaleRow);

    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(new QLabel(tr("Font"), page));
    m_font = new QFontComboBox(page);
    fontRow->addWidget(m_font, 1);
    m_fontSize = new QComboBox(page);
    for (int size = 25; size <= 75; size += 5) {
        m_fontSize->addItem(QString::number(size), size);
    }
    fontRow->addWidget(m_fontSize);
    layout->addLayout(fontRow);

    auto colorRow = [page, layout](
                        const QString &label, QPushButton *&button) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, page));
        row->addStretch();
        button = flatButton(QString(), page);
        button->setFixedSize(38, 24);
        row->addWidget(button);
        layout->addLayout(row);
    };
    colorRow(tr("Text Color"), m_textColor);
    colorRow(tr("Background"), m_backgroundColor);

    auto *borderRow = new QHBoxLayout;
    borderRow->addWidget(new QLabel(tr("Border"), page));
    borderRow->addStretch();
    m_borderSize = new QComboBox(page);
    for (double size : {0.0, 0.25, 0.5, 1.0, 1.5,
                        2.0, 2.5, 3.0, 4.0, 5.0}) {
        m_borderSize->addItem(QString::number(size), size);
    }
    borderRow->addWidget(m_borderSize);
    m_borderColor = flatButton(QString(), page);
    m_borderColor->setFixedSize(38, 24);
    borderRow->addWidget(m_borderColor);
    layout->addLayout(borderRow);

    auto *assRow = new QHBoxLayout;
    assRow->addWidget(new QLabel(tr("ASS Style Override"), page));
    m_assOverride = new QComboBox(page);
    m_assOverride->addItem(tr("Use Embedded Styles"), QStringLiteral("no"));
    m_assOverride->addItem(tr("Override Selectively"), QStringLiteral("yes"));
    m_assOverride->addItem(tr("Scale Embedded Styles"), QStringLiteral("scale"));
    m_assOverride->addItem(tr("Force Player Style"), QStringLiteral("force"));
    m_assOverride->addItem(tr("Strip Embedded Styles"), QStringLiteral("strip"));
    assRow->addWidget(m_assOverride, 1);
    layout->addLayout(assRow);

    m_styleWarning = new QLabel(page);
    m_styleWarning->setWordWrap(true);
    m_styleWarning->setStyleSheet(
        QStringLiteral("color: rgba(235,235,240,150); font-size: 11px;"));
    layout->addWidget(m_styleWarning);
    layout->addStretch();
    scroll->setWidget(page);

    connect(m_targetSubtitle, &QComboBox::currentIndexChanged,
            this, [this] {
                refreshSubtitleSettings(
                    m_playerCore->info().subtitles);
            });
    connect(m_delay,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (!m_refreshing) {
                    m_playerCore->setSubtitleDelay(
                        m_targetSubtitle->currentIndex() == 0, value);
                }
            });
    connect(m_delaySlider, &QSlider::valueChanged,
            this, [this](int value) {
                if (m_refreshing) {
                    return;
                }
                const double seconds =
                    static_cast<double>(value) / 20.0;
                {
                    const QSignalBlocker blocker(m_delay);
                    m_delay->setValue(seconds);
                }
                m_playerCore->setSubtitleDelay(
                    m_targetSubtitle->currentIndex() == 0, seconds);
            });
    connect(m_position, &QSlider::valueChanged,
            this, [this](int value) {
                if (!m_refreshing) {
                    m_playerCore->setSubtitlePosition(
                        m_targetSubtitle->currentIndex() == 0, value);
                }
            });
    connect(m_scale, &QSlider::valueChanged,
            this, [this](int value) {
                if (!m_refreshing) {
                    m_playerCore->setSubtitleScale(
                        realScaleFromSlider(value));
                }
            });
    connect(resetScale, &QPushButton::clicked,
            this, [this] {
                m_scale->setValue(0);
                m_playerCore->setSubtitleScale(1.0);
            });
    connect(m_font, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &font) {
                if (!m_refreshing) {
                    m_playerCore->setSubtitleFont(font.family());
                }
            });
    connect(m_fontSize, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (!m_refreshing && index >= 0) {
                    m_playerCore->setSubtitleFontSize(
                        m_fontSize->itemData(index).toDouble());
                }
            });
    connect(m_borderSize, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (!m_refreshing && index >= 0) {
                    m_playerCore->setSubtitleBorderSize(
                        m_borderSize->itemData(index).toDouble());
                }
            });
    connect(m_assOverride, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (!m_refreshing && index >= 0) {
                    m_playerCore->setSubtitleAssOverride(
                        m_assOverride->itemData(index).toString());
                }
            });
    connect(m_textColor, &QPushButton::clicked, this,
            [this] {
                chooseColor(
                    m_textColor, &PlayerCore::setSubtitleTextColor);
            });
    connect(m_backgroundColor, &QPushButton::clicked, this,
            [this] {
                chooseColor(
                    m_backgroundColor,
                    &PlayerCore::setSubtitleBackgroundColor);
            });
    connect(m_borderColor, &QPushButton::clicked, this,
            [this] {
                chooseColor(
                    m_borderColor,
                    &PlayerCore::setSubtitleBorderColor);
            });
    return scroll;
}

void MediaSettingsPanel::populateTrackList(
    QListWidget *list, const QList<MediaTrack> &tracks,
    int selectedId)
{
    if (!list) {
        return;
    }
    const QSignalBlocker blocker(list);
    list->clear();
    auto *none = new QListWidgetItem(tr("<None>"), list);
    none->setData(trackIdRole, 0);
    none->setForeground(QColor(170, 170, 176));
    for (const MediaTrack &track : tracks) {
        auto *item = new QListWidgetItem(
            track.readableTitle(false), list);
        item->setData(trackIdRole, track.id);
        item->setData(externalRole, track.isExternal);
        item->setToolTip(track.readableTitle());
        if (!track.readableLanguage().isEmpty()) {
            item->setText(QStringLiteral("%1    %2")
                              .arg(track.readableTitle(false),
                                   track.readableLanguage()));
        }
    }
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem *item = list->item(index);
        if (item->data(trackIdRole).toInt() == selectedId) {
            list->setCurrentItem(item);
            break;
        }
    }
}

void MediaSettingsPanel::refreshTracks(
    const MediaTrackState &tracks)
{
    m_refreshing = true;
    populateTrackList(
        m_videoTracks, tracks.videoTracks, tracks.selectedVideoId);
    populateTrackList(
        m_audioTracks, tracks.audioTracks, tracks.selectedAudioId);
    populateTrackList(
        m_primarySubtitles, tracks.subtitleTracks,
        tracks.selectedSubtitleId);
    populateTrackList(
        m_secondarySubtitles, tracks.subtitleTracks,
        tracks.selectedSecondarySubtitleId);
    const MediaTrack *primary = tracks.selectedSubtitle(true);
    const MediaTrack *secondary = tracks.selectedSubtitle(false);
    const bool hasImage =
        (primary && primary->isImageSubtitle())
        || (secondary && secondary->isImageSubtitle());
    const bool hasAss =
        (primary && primary->isAssSubtitle())
        || (secondary && secondary->isAssSubtitle());
    const bool hasText =
        (!primary && !secondary)
        || (primary && !primary->isImageSubtitle())
        || (secondary && !secondary->isImageSubtitle());
    for (QWidget *control :
         {static_cast<QWidget *>(m_scale),
          static_cast<QWidget *>(m_font),
          static_cast<QWidget *>(m_fontSize),
          static_cast<QWidget *>(m_borderSize),
          static_cast<QWidget *>(m_assOverride),
          static_cast<QWidget *>(m_textColor),
          static_cast<QWidget *>(m_backgroundColor),
          static_cast<QWidget *>(m_borderColor)}) {
        control->setEnabled(hasText);
    }
    if (!hasText && hasImage) {
        m_styleWarning->setText(
            tr("Text style settings are unavailable for image subtitles."));
    } else if (hasAss) {
        m_styleWarning->setText(
            tr("ASS subtitles may use embedded styles. Choose an override "
               "mode above to apply the player style."));
    } else {
        m_styleWarning->clear();
    }
    m_refreshing = false;
}

void MediaSettingsPanel::refreshSubtitleSettings(
    const SubtitleSettings &settings)
{
    m_refreshing = true;
    m_primaryVisible->setChecked(settings.primaryVisible);
    m_secondaryVisible->setChecked(settings.secondaryVisible);
    const bool primary = m_targetSubtitle->currentIndex() == 0;
    m_delay->setValue(
        primary ? settings.primaryDelay : settings.secondaryDelay);
    m_delaySlider->setValue(qRound(std::clamp(
        primary ? settings.primaryDelay : settings.secondaryDelay,
        -5.0, 5.0) * 20.0));
    m_position->setValue(
        primary ? settings.primaryPosition : settings.secondaryPosition);
    m_scale->setValue(sliderFromRealScale(settings.scale));
    if (!settings.font.isEmpty()) {
        m_font->setCurrentFont(QFont(settings.font));
    }
    const int fontIndex =
        m_fontSize->findData(qRound(settings.fontSize));
    if (fontIndex >= 0) {
        m_fontSize->setCurrentIndex(fontIndex);
    }
    const int borderIndex =
        m_borderSize->findData(settings.borderSize);
    if (borderIndex >= 0) {
        m_borderSize->setCurrentIndex(borderIndex);
    }
    const int overrideIndex =
        m_assOverride->findData(settings.assOverride);
    if (overrideIndex >= 0) {
        m_assOverride->setCurrentIndex(overrideIndex);
    }
    auto setSwatch = [](QPushButton *button, const QColor &color) {
        button->setProperty("subtitleColor", color);
        button->setStyleSheet(QStringLiteral(
            "background:%1; border:1px solid rgba(255,255,255,80);"
            "border-radius:5px;").arg(color.name(QColor::HexArgb)));
    };
    setSwatch(m_textColor, colorFromMpv(settings.textColor));
    setSwatch(
        m_backgroundColor,
        colorFromMpv(settings.backgroundColor));
    setSwatch(m_borderColor, colorFromMpv(settings.borderColor));
    m_refreshing = false;
}

void MediaSettingsPanel::chooseExternalFile(MediaTrackType type)
{
    const QUrl current = m_playerCore->info().currentUrl;
    const QString initial =
        current.isLocalFile()
            ? QFileInfo(current.toLocalFile()).absolutePath()
            : QString();
    const QString path = QFileDialog::getOpenFileName(
        this,
        type == MediaTrackType::Video
            ? tr("Load External Video")
            : tr("Load External Audio"),
        initial, externalFilter(type));
    if (path.isEmpty()) {
        return;
    }
    if (type == MediaTrackType::Video) {
        m_playerCore->loadExternalVideo(QUrl::fromLocalFile(path));
    } else {
        m_playerCore->loadExternalAudio(QUrl::fromLocalFile(path));
    }
}

void MediaSettingsPanel::chooseExternalSubtitle()
{
    const QUrl current = m_playerCore->info().currentUrl;
    const QString initial =
        current.isLocalFile()
            ? QFileInfo(current.toLocalFile()).absolutePath()
            : QString();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load External Subtitle"), initial,
        tr("Subtitle Files (*.ass *.idx *.lrc *.mks *.pgs *.smi *.srt "
           "*.ssa *.sub *.sup *.ttml *.vtt);;All Files (*)"));
    if (!path.isEmpty()) {
        m_playerCore->loadExternalSubtitle(QUrl::fromLocalFile(path));
    }
}

void MediaSettingsPanel::showTrackContextMenu(
    QListWidget *list, MediaTrackType type, const QPoint &position)
{
    QListWidgetItem *item = list->itemAt(position);
    if (!item || !item->data(externalRole).toBool()) {
        return;
    }
    QMenu menu(this);
    QAction *remove = menu.addAction(tr("Remove External Track"));
    if (menu.exec(list->viewport()->mapToGlobal(position)) == remove) {
        m_playerCore->removeExternalTrack(
            type, item->data(trackIdRole).toInt());
    }
}

void MediaSettingsPanel::chooseColor(
    QPushButton *button,
    void (PlayerCore::*setter)(const QString &))
{
    const QColor initial = button->palette().button().color();
    const QColor stored =
        button->property("subtitleColor").value<QColor>();
    const QColor selected = QColorDialog::getColor(
        stored.isValid() ? stored : initial,
        this, tr("Choose Subtitle Color"),
        QColorDialog::ShowAlphaChannel);
    if (selected.isValid()) {
        (m_playerCore->*setter)(colorToMpv(selected));
    }
}

QString MediaSettingsPanel::colorToMpv(const QColor &color)
{
    return QStringLiteral("#%1%2%3%4")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.alpha(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QColor MediaSettingsPanel::colorFromMpv(const QString &value)
{
    QString hex = value.trimmed();
    if (hex.startsWith(QLatin1Char('#'))) {
        hex.remove(0, 1);
    }
    bool ok = false;
    if (hex.size() == 8) {
        const int red = hex.mid(0, 2).toInt(&ok, 16);
        if (!ok) {
            return QColor(Qt::transparent);
        }
        const int green = hex.mid(2, 2).toInt(&ok, 16);
        const int blue = hex.mid(4, 2).toInt(&ok, 16);
        const int alpha = hex.mid(6, 2).toInt(&ok, 16);
        return ok ? QColor(red, green, blue, alpha)
                  : QColor(Qt::transparent);
    }
    const QColor color(QStringLiteral("#") + hex);
    return color.isValid() ? color : QColor(Qt::transparent);
}

double MediaSettingsPanel::realScaleFromSlider(int value)
{
    if (value == 0) {
        return 1.0;
    }
    const double display = static_cast<double>(value) / 20.0;
    const double mapped = display > 0.0
        ? std::round((display + 1.0) * 20.0) / 20.0
        : std::round((display - 1.0) * 20.0) / 20.0;
    return display > 0.0 ? mapped : 1.0 / -mapped;
}

int MediaSettingsPanel::sliderFromRealScale(double value)
{
    value = std::clamp(value, 0.1, 10.0);
    if (std::abs(value - 1.0) < 0.05) {
        return 0;
    }
    return value > 1.0
        ? std::clamp(qRound((value - 1.0) * 20.0), 1, 100)
        : std::clamp(
              qRound((1.0 - 1.0 / value) * 20.0), -100, -1);
}

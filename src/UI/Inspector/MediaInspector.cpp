#include "UI/Inspector/MediaInspector.h"

#include "PlayerCore/PlayerCore.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QSettings>
#include <QShowEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QTableWidget *propertyTable(QWidget *parent)
{
    auto *table = new QTableWidget(0, 2, parent);
    table->setHorizontalHeaderLabels(
        {QObject::tr("Property"), QObject::tr("Value")});
    table->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    return table;
}

QString yesNo(bool value)
{
    return value ? QObject::tr("Yes") : QObject::tr("No");
}

QString readableBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = std::max<qint64>(0, bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', unit == 0 ? 0 : (value < 10 ? 2 : 1))
        .arg(QString::fromLatin1(units[unit]));
}

QString trackTypeText(MediaTrackType type)
{
    switch (type) {
    case MediaTrackType::Video: return QObject::tr("Video");
    case MediaTrackType::Audio: return QObject::tr("Audio");
    case MediaTrackType::Subtitle: return QObject::tr("Subtitle");
    }
    return {};
}
}

MediaInspector::MediaInspector(
    PlayerCore *playerCore, QWidget *parent)
    : QDialog(parent),
      m_playerCore(playerCore)
{
    setObjectName(QStringLiteral("mediaInspector"));
    setWindowTitle(tr("Media Inspector"));
    setModal(false);
    resize(700, 560);
    setMinimumSize(560, 420);
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_general = propertyTable(m_tabs);
    m_media = propertyTable(m_tabs);
    m_statistics = propertyTable(m_tabs);

    auto *trackPage = new QWidget(m_tabs);
    auto *trackLayout = new QVBoxLayout(trackPage);
    m_track = new QComboBox(trackPage);
    m_trackDetails = propertyTable(trackPage);
    trackLayout->addWidget(m_track);
    trackLayout->addWidget(m_trackDetails, 1);

    auto *watchPage = new QWidget(m_tabs);
    auto *watchLayout = new QVBoxLayout(watchPage);
    m_watch = propertyTable(watchPage);
    m_watch->setSelectionMode(QAbstractItemView::ExtendedSelection);
    watchLayout->addWidget(m_watch, 1);
    auto *watchButtons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add Property…"), watchPage);
    auto *remove = new QPushButton(tr("Remove"), watchPage);
    watchButtons->addWidget(add);
    watchButtons->addWidget(remove);
    watchButtons->addStretch();
    watchLayout->addLayout(watchButtons);

    m_tabs->addTab(m_general, tr("General"));
    m_tabs->addTab(m_media, tr("Video & Audio"));
    m_tabs->addTab(trackPage, tr("Tracks"));
    m_tabs->addTab(m_statistics, tr("Statistics"));
    m_tabs->addTab(watchPage, tr("Watch"));
    layout->addWidget(m_tabs);

    setStyleSheet(QStringLiteral(
        "#mediaInspector { background: rgb(29,30,34); color: rgb(239,239,244); }"
        "QTabWidget::pane { border: 1px solid rgba(255,255,255,28); }"
        "QTabBar::tab { color: rgb(225,225,232); padding: 7px 12px;"
        " background: rgb(38,39,44); }"
        "QTabBar::tab:selected { background: rgb(55,57,64); }"
        "QTableWidget, QComboBox { background: rgb(22,23,27);"
        " color: rgb(239,239,244); alternate-background-color: rgb(29,30,35);"
        " border: 1px solid rgba(255,255,255,28); }"
        "QHeaderView::section { background: rgb(43,44,50);"
        " color: rgb(225,225,232); border: 0; padding: 5px; }"
        "QPushButton { color: rgb(239,239,244); padding: 5px 11px;"
        " border: 1px solid rgba(255,255,255,34); border-radius: 5px;"
        " background: rgb(48,49,55); }"));

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout,
            this, &MediaInspector::refreshStatistics);
    connect(m_timer, &QTimer::timeout,
            this, &MediaInspector::refreshWatch);
    connect(m_track, &QComboBox::currentIndexChanged,
            this, &MediaInspector::refreshSelectedTrack);
    connect(add, &QPushButton::clicked,
            this, &MediaInspector::addWatchProperty);
    connect(remove, &QPushButton::clicked,
            this, &MediaInspector::removeWatchProperties);
    connect(m_playerCore, &PlayerCore::currentUrlChanged,
            this, [this] { refreshAll(); });
    connect(m_playerCore, &PlayerCore::tracksChanged,
            this, [this] { refreshTracks(); });
}

void MediaInspector::showInspector(int tab)
{
    m_tabs->setCurrentIndex(
        std::clamp(tab, 0, m_tabs->count() - 1));
    refreshAll();
    show();
    raise();
    activateWindow();
}

void MediaInspector::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshAll();
    m_timer->start();
}

void MediaInspector::hideEvent(QHideEvent *event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void MediaInspector::refreshAll()
{
    refreshGeneral();
    refreshMedia();
    refreshTracks();
    refreshStatistics();
    refreshWatch();
}

QString MediaInspector::propertyText(const QString &name) const
{
    const QVariant value = m_playerCore->mpvProperty(name);
    if (!value.isValid() || value.isNull()) {
        return tr("N/A");
    }
    if (value.metaType().id() == QMetaType::QVariantList
        || value.metaType().id() == QMetaType::QVariantMap) {
        return QString::fromUtf8(
            QJsonDocument::fromVariant(value)
                .toJson(QJsonDocument::Compact));
    }
    return value.toString().isEmpty() ? tr("N/A") : value.toString();
}

void MediaInspector::refreshGeneral()
{
    const QUrl url = m_playerCore->info().currentUrl;
    const QFileInfo file(url.toLocalFile());
    setRows(m_general, {
        {tr("Path"), url.isLocalFile()
             ? QDir::toNativeSeparators(file.absoluteFilePath())
             : url.toDisplayString()},
        {tr("File size"), url.isLocalFile()
             ? readableBytes(file.size())
             : propertyText(QStringLiteral("file-size"))},
        {tr("Format"), propertyText(QStringLiteral("file-format"))},
        {tr("Title"), propertyText(QStringLiteral("media-title"))},
        {tr("Duration"), propertyText(QStringLiteral("duration/full"))},
        {tr("Chapters"), propertyText(QStringLiteral("chapters"))},
        {tr("Editions"), propertyText(QStringLiteral("editions"))},
        {tr("Comment"),
         propertyText(QStringLiteral("metadata/by-key/comment"))}});
}

void MediaInspector::refreshMedia()
{
    setRows(m_media, {
        {tr("Video format"),
         propertyText(QStringLiteral("current-tracks/video/codec"))},
        {tr("Video codec"),
         propertyText(QStringLiteral("current-tracks/video/codec-desc"))},
        {tr("Video decoder"),
         propertyText(QStringLiteral("current-tracks/video/decoder-desc"))},
        {tr("Resolution"),
         QStringLiteral("%1×%2")
             .arg(m_playerCore->info().videoWidth)
             .arg(m_playerCore->info().videoHeight)},
        {tr("Pixel format"),
         propertyText(QStringLiteral("video-params/pixelformat"))},
        {tr("Color matrix"),
         propertyText(QStringLiteral("video-params/colormatrix"))},
        {tr("Primaries"),
         propertyText(QStringLiteral("video-params/primaries"))},
        {tr("Transfer"),
         propertyText(QStringLiteral("video-params/gamma"))},
        {tr("Video output"),
         propertyText(QStringLiteral("current-vo"))},
        {tr("Audio format"),
         propertyText(QStringLiteral("audio-params/format"))},
        {tr("Audio codec"),
         propertyText(QStringLiteral("current-tracks/audio/codec-desc"))},
        {tr("Channels"),
         propertyText(QStringLiteral("audio-params/channels"))},
        {tr("Sample rate"),
         propertyText(QStringLiteral("audio-params/samplerate"))},
        {tr("Audio output"),
         propertyText(QStringLiteral("current-ao"))}});
}

void MediaInspector::refreshTracks()
{
    const int previous = m_track->currentIndex();
    m_track->clear();
    const MediaTrackState &tracks = m_playerCore->info().tracks;
    const auto append = [this](const QList<MediaTrack> &items,
                               const QString &kind) {
        for (int index = 0; index < items.size(); ++index) {
            const MediaTrack &track = items[index];
            m_track->addItem(
                QStringLiteral("%1 %2 — %3")
                    .arg(kind)
                    .arg(track.id)
                    .arg(track.readableTitle()),
                QVariantMap{
                    {QStringLiteral("type"),
                     static_cast<int>(track.type)},
                    {QStringLiteral("index"), index}});
        }
    };
    append(tracks.videoTracks, tr("Video"));
    append(tracks.audioTracks, tr("Audio"));
    append(tracks.subtitleTracks, tr("Subtitle"));
    if (m_track->count() > 0) {
        m_track->setCurrentIndex(
            std::clamp(previous, 0, m_track->count() - 1));
    }
    refreshSelectedTrack();
}

void MediaInspector::refreshSelectedTrack()
{
    const QVariantMap trackData = m_track->currentData().toMap();
    if (trackData.isEmpty()) {
        setRows(m_trackDetails, {});
        return;
    }
    const MediaTrackType type = static_cast<MediaTrackType>(
        trackData.value(QStringLiteral("type")).toInt());
    const int index =
        trackData.value(QStringLiteral("index")).toInt();
    const MediaTrackState &tracks = m_playerCore->info().tracks;
    const QList<MediaTrack> *list = type == MediaTrackType::Video
        ? &tracks.videoTracks
        : type == MediaTrackType::Audio
            ? &tracks.audioTracks : &tracks.subtitleTracks;
    if (index < 0 || index >= list->size()) {
        setRows(m_trackDetails, {});
        return;
    }
    const MediaTrack &track = list->at(index);
    setRows(m_trackDetails, {
        {tr("ID"), QString::number(track.id)},
        {tr("Type"), trackTypeText(track.type)},
        {tr("Title"), track.title.isEmpty() ? tr("N/A") : track.title},
        {tr("Language"),
         track.language.isEmpty() ? tr("N/A") : track.language},
        {tr("Codec"), track.codec.isEmpty() ? tr("N/A") : track.codec},
        {tr("Decoder"),
         track.decoderDescription.isEmpty()
             ? tr("N/A") : track.decoderDescription},
        {tr("Default"), yesNo(track.isDefault)},
        {tr("Forced"), yesNo(track.isForced)},
        {tr("Selected"), yesNo(track.isSelected)},
        {tr("External"), yesNo(track.isExternal)},
        {tr("External file"),
         track.externalFilename.isEmpty()
             ? tr("N/A")
             : QDir::toNativeSeparators(track.externalFilename)},
        {tr("Details"), track.readableTitle()}});
}

void MediaInspector::refreshStatistics()
{
    setRows(m_statistics, {
        {tr("Video bitrate"),
         propertyText(QStringLiteral("video-bitrate"))},
        {tr("Audio bitrate"),
         propertyText(QStringLiteral("audio-bitrate"))},
        {tr("Container FPS"),
         propertyText(QStringLiteral("container-fps"))},
        {tr("Video output FPS"),
         propertyText(QStringLiteral("estimated-vf-fps"))},
        {tr("Display FPS"),
         propertyText(QStringLiteral("display-fps"))},
        {tr("Estimated display FPS"),
         propertyText(QStringLiteral("estimated-display-fps"))},
        {tr("A/V sync"),
         propertyText(QStringLiteral("avsync"))},
        {tr("Total A/V correction"),
         propertyText(QStringLiteral("total-avsync-change"))},
        {tr("Decoder dropped frames"),
         propertyText(QStringLiteral("decoder-frame-drop-count"))},
        {tr("Output dropped frames"),
         propertyText(QStringLiteral("frame-drop-count"))},
        {tr("Mistimed frames"),
         propertyText(QStringLiteral("mistimed-frame-count"))},
        {tr("Delayed frames"),
         propertyText(QStringLiteral("vo-delayed-frame-count"))},
        {tr("Hardware decoder"),
         propertyText(QStringLiteral("hwdec-current"))},
        {tr("Cache used"),
         propertyText(QStringLiteral("demuxer-cache-state"))}});
}

void MediaInspector::refreshWatch()
{
    const QStringList properties = QSettings().value(
        QStringLiteral("inspector/watchProperties")).toStringList();
    QList<QPair<QString, QString>> rows;
    rows.reserve(properties.size());
    for (const QString &property : properties) {
        rows.append({property, propertyText(property)});
    }
    setRows(m_watch, rows);
}

void MediaInspector::addWatchProperty()
{
    bool accepted = false;
    const QString property = QInputDialog::getText(
        this, tr("Add Watched Property"),
        tr("mpv property name:"), QLineEdit::Normal,
        {}, &accepted).trimmed();
    if (!accepted || property.isEmpty()) {
        return;
    }
    QSettings settings;
    QStringList properties = settings.value(
        QStringLiteral("inspector/watchProperties")).toStringList();
    if (!properties.contains(property)) {
        properties.append(property);
        settings.setValue(
            QStringLiteral("inspector/watchProperties"), properties);
    }
    refreshWatch();
}

void MediaInspector::removeWatchProperties()
{
    QSettings settings;
    QStringList properties = settings.value(
        QStringLiteral("inspector/watchProperties")).toStringList();
    QList<int> rows;
    for (const QModelIndex &index :
         m_watch->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for (int row : std::as_const(rows)) {
        if (row >= 0 && row < properties.size()) {
            properties.removeAt(row);
        }
    }
    settings.setValue(
        QStringLiteral("inspector/watchProperties"), properties);
    refreshWatch();
}

void MediaInspector::setRows(
    QTableWidget *table,
    const QList<QPair<QString, QString>> &rows)
{
    table->setUpdatesEnabled(false);
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(rows[row].first));
        table->setItem(row, 1, new QTableWidgetItem(rows[row].second));
    }
    table->setUpdatesEnabled(true);
}

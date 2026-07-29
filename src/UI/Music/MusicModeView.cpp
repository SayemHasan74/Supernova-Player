#include "UI/Music/MusicModeView.h"

#include "Mpv/MpvCore.h"
#include "PlayerCore/PlayerCore.h"
#include "UI/Controls/IinaPlayerChrome.h"
#include "UI/Design/DesignTokens.h"
#include "UI/Playlist/PlaylistPanel.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int baseMusicWidth = 688;
constexpr int baseMusicHeight = 520;
constexpr int compactMusicHeight = 230;
constexpr int playlistWidth = 360;
constexpr int hoverFadeMs = 200;

quint32 synchsafe(const uchar *value)
{
    return (quint32(value[0] & 0x7f) << 21)
        | (quint32(value[1] & 0x7f) << 14)
        | (quint32(value[2] & 0x7f) << 7)
        | quint32(value[3] & 0x7f);
}

quint32 bigEndian32(const uchar *value)
{
    return (quint32(value[0]) << 24)
        | (quint32(value[1]) << 16)
        | (quint32(value[2]) << 8)
        | quint32(value[3]);
}

QImage embeddedMp3Artwork(const QUrl &url)
{
    if (!url.isLocalFile()
        || QFileInfo(url.toLocalFile()).suffix().compare(
            QStringLiteral("mp3"), Qt::CaseInsensitive) != 0) {
        return {};
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = file.read(10);
    if (header.size() != 10 || !header.startsWith("ID3")) {
        return {};
    }
    const int version = uchar(header[3]);
    if (version < 3 || version > 4) {
        return {};
    }
    const quint32 tagSize = synchsafe(
        reinterpret_cast<const uchar *>(header.constData() + 6));
    if (tagSize == 0 || tagSize > 32U * 1024U * 1024U) {
        return {};
    }
    QByteArray tag = file.read(tagSize);
    qsizetype offset = (uchar(header[5]) & 0x40) != 0
        ? (version == 4 && tag.size() >= 4
               ? qsizetype(synchsafe(
                     reinterpret_cast<const uchar *>(tag.constData())))
               : tag.size() >= 4
                   ? qsizetype(bigEndian32(
                         reinterpret_cast<const uchar *>(tag.constData())))
                   : 0)
        : 0;
    while (offset + 10 <= tag.size()) {
        const QByteArray id = tag.mid(offset, 4);
        if (id == QByteArray(4, '\0')) {
            break;
        }
        const auto *sizeBytes =
            reinterpret_cast<const uchar *>(tag.constData() + offset + 4);
        const quint32 frameSize =
            version == 4 ? synchsafe(sizeBytes) : bigEndian32(sizeBytes);
        const qsizetype payloadStart = offset + 10;
        if (frameSize == 0
            || payloadStart + qsizetype(frameSize) > tag.size()) {
            break;
        }
        if (id == "APIC") {
            const QByteArray payload =
                tag.mid(payloadStart, qsizetype(frameSize));
            if (payload.size() < 5) {
                return {};
            }
            qsizetype cursor = 1;
            cursor = payload.indexOf('\0', cursor);
            if (cursor < 0 || ++cursor >= payload.size()) {
                return {};
            }
            ++cursor; // Picture type.
            if (cursor >= payload.size()) {
                return {};
            }
            const int encoding = uchar(payload[0]);
            if (encoding == 1 || encoding == 2) {
                while (cursor + 1 < payload.size()
                       && (payload[cursor] != '\0'
                           || payload[cursor + 1] != '\0')) {
                    cursor += 2;
                }
                cursor += 2;
            } else {
                const qsizetype end = payload.indexOf('\0', cursor);
                cursor = end < 0 ? payload.size() : end + 1;
            }
            if (cursor < payload.size()) {
                QImage image;
                image.loadFromData(payload.mid(cursor));
                return image;
            }
        }
        offset = payloadStart + qsizetype(frameSize);
    }
    return {};
}

void styleMetadataLabel(QLabel *label, int pixelSize, const QColor &color)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(pixelSize);
    label->setFont(font);
    label->setStyleSheet(
        QStringLiteral("background: transparent; color: rgba(%1,%2,%3,%4);")
            .arg(color.red()).arg(color.green()).arg(color.blue())
            .arg(color.alpha()));
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    label->setTextFormat(Qt::PlainText);
}

void styleTimeLabel(QLabel *label)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(9);
    label->setFont(font);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        QStringLiteral("background: transparent; color: rgba(235,235,245,175);"));
}
}

MusicModeView::MusicModeView(PlayerCore *playerCore, QWidget *parent)
    : QWidget(parent), m_playerCore(playerCore)
{
    setObjectName(QStringLiteral("musicModeView"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(baseMusicWidth);

    m_infoView = new QWidget(this);
    m_infoView->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_titleLabel = new QLabel(m_infoView);
    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    styleMetadataLabel(
        m_titleLabel, 13, Supernova::Ui::primaryText);
    m_artistAlbumLabel = new QLabel(m_infoView);
    m_artistAlbumLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    styleMetadataLabel(
        m_artistAlbumLabel, 11, Supernova::Ui::secondaryText);
    m_genreLabel = new QLabel(m_infoView);
    m_genreLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    styleMetadataLabel(
        m_genreLabel, 11, QColor(235, 235, 245, 140));

    m_controlsView = new QWidget(this);
    m_controlsView->setMouseTracking(true);
    m_closeButton = new IinaIconButton(IinaIcon::Close, m_controlsView);
    m_backButton = new IinaIconButton(IinaIcon::Back, m_controlsView);
    m_volumeButton =
        new IinaIconButton(IinaIcon::VolumeHigh, m_controlsView);
    m_previousButton =
        new IinaIconButton(IinaIcon::Previous, m_controlsView);
    m_playButton = new IinaIconButton(IinaIcon::Play, m_controlsView);
    m_nextButton = new IinaIconButton(IinaIcon::Next, m_controlsView);
    m_playlistButton =
        new IinaIconButton(IinaIcon::Playlist, m_controlsView);
    m_artworkButton =
        new IinaIconButton(IinaIcon::AlbumArt, m_controlsView);
    m_shuffleButton =
        new IinaIconButton(IinaIcon::Shuffle, m_controlsView);
    m_repeatButton =
        new IinaIconButton(IinaIcon::Repeat, m_controlsView);

    m_closeButton->setToolTip(tr("Close Player"));
    m_backButton->setToolTip(tr("Return to Video Window"));
    m_volumeButton->setToolTip(tr("Volume"));
    m_previousButton->setToolTip(tr("Previous Media"));
    m_playButton->setToolTip(tr("Play"));
    m_nextButton->setToolTip(tr("Next Media"));
    m_playlistButton->setToolTip(tr("Show Playlist"));
    m_artworkButton->setToolTip(tr("Hide Album Art"));
    m_shuffleButton->setToolTip(tr("Shuffle Playlist"));
    m_repeatButton->setToolTip(tr("Change Repeat Mode"));

    m_elapsedLabel = new QLabel(QStringLiteral("0:00"), this);
    m_durationLabel = new QLabel(QStringLiteral("0:00"), this);
    styleTimeLabel(m_elapsedLabel);
    styleTimeLabel(m_durationLabel);
    m_timeline = new IinaTimeline(this);

    m_volumePopover = new QFrame(this);
    m_volumePopover->setObjectName(QStringLiteral("musicVolumePopover"));
    m_volumePopover->setStyleSheet(QStringLiteral(
        "#musicVolumePopover { background: rgba(31,31,34,245);"
        " border: 1px solid rgba(255,255,255,36); border-radius: 6px; }"
        " QSlider::groove:horizontal { height: 3px;"
        " background: rgba(235,235,245,62); border-radius: 1px; }"
        " QSlider::sub-page:horizontal { background: rgba(245,245,247,220); }"
        " QSlider::handle:horizontal { width: 10px; margin: -4px 0;"
        " border-radius: 5px; background: rgb(250,250,252); }"));
    m_volumeSlider = new QSlider(Qt::Horizontal, m_volumePopover);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setGeometry(14, 8, 132, 14);
    m_volumePopover->setFixedSize(160, 30);
    m_volumePopover->hide();
    m_volumeSlider->setParent(this);
    m_volumeSlider->show();

    m_infoOpacity = new QGraphicsOpacityEffect(m_infoView);
    m_controlsOpacity = new QGraphicsOpacityEffect(m_controlsView);
    m_infoView->setGraphicsEffect(m_infoOpacity);
    m_controlsView->setGraphicsEffect(m_controlsOpacity);
    m_infoOpacity->setOpacity(1.0);
    m_controlsOpacity->setOpacity(1.0);
    m_controlsView->setEnabled(true);
    m_infoAnimation =
        new QPropertyAnimation(m_infoOpacity, "opacity", this);
    m_controlsAnimation =
        new QPropertyAnimation(m_controlsOpacity, "opacity", this);
    for (QPropertyAnimation *animation :
         {m_infoAnimation, m_controlsAnimation}) {
        animation->setDuration(hoverFadeMs);
        animation->setEasingCurve(QEasingCurve::InOutQuad);
    }
    m_nativeHoverTimer = new QTimer(this);
    m_nativeHoverTimer->setSingleShot(true);
    m_nativeHoverTimer->setInterval(120);
    connect(m_nativeHoverTimer, &QTimer::timeout, this, [this] {
        if (rect().contains(mapFromGlobal(QCursor::pos()))) {
            m_nativeHoverTimer->start();
        } else {
            setControlsVisible(false);
        }
    });

    connect(m_closeButton, &QAbstractButton::clicked,
            this, &MusicModeView::closeRequested);
    connect(m_backButton, &QAbstractButton::clicked,
            this, &MusicModeView::backRequested);
    connect(m_playlistButton, &QAbstractButton::clicked,
            this, &MusicModeView::playlistRequested);
    connect(m_artworkButton, &QAbstractButton::clicked,
            this, [this] { setArtworkVisible(!m_showArtwork); });
    connect(m_shuffleButton, &QAbstractButton::clicked,
            m_playerCore, &PlayerCore::shufflePlaylist);
    connect(m_repeatButton, &QAbstractButton::clicked,
            m_playerCore, &PlayerCore::cyclePlaylistLoopMode);
    connect(m_volumeButton, &QAbstractButton::clicked,
            this, [this] {
                m_playerCore->toggleMute();
            });
    connect(m_playButton, &QAbstractButton::clicked,
            m_playerCore, &PlayerCore::togglePause);
    connect(m_previousButton, &QAbstractButton::clicked,
            m_playerCore, [this] {
                m_playerCore->navigateInPlaylist(false);
            });
    connect(m_nextButton, &QAbstractButton::clicked,
            m_playerCore, [this] {
                m_playerCore->navigateInPlaylist(true);
            });
    connect(m_volumeSlider, &QSlider::valueChanged,
            m_playerCore, [this](int value) {
                m_playerCore->setVolume(value);
            });
    connect(m_timeline, &IinaTimeline::seekRequested,
            m_playerCore, [this](double percent) {
                m_playerCore->seekPercent(percent, true);
            });
    connect(m_timeline, &IinaTimeline::seekStarted,
            this, [this] {
                m_wasPausedBeforeTimelineDrag =
                    m_playerCore->info().state == PlayerState::Paused;
                m_playerCore->pause();
            });
    connect(m_timeline, &IinaTimeline::seekFinished,
            this, [this](double) {
                if (!m_wasPausedBeforeTimelineDrag) {
                    m_playerCore->resume();
                }
            });
    connect(m_timeline, &IinaTimeline::previewRequested,
            this, &MusicModeView::previewRequested);
    connect(m_timeline, &IinaTimeline::previewDismissed,
            this, &MusicModeView::previewDismissed);

    connect(m_playerCore, &PlayerCore::stateChanged,
            this, [this](PlayerState) { updatePlaybackState(); });
    connect(m_playerCore, &PlayerCore::mediaLoaded,
            this, [this] { refresh(); });
    connect(m_playerCore, &PlayerCore::currentUrlChanged,
            this, [this] { updateMetadata(); });
    connect(m_playerCore, &PlayerCore::chapterChanged,
            this, [this] { updateMetadata(); });
    connect(m_playerCore, &PlayerCore::positionChanged,
            this, [this](double position) {
                m_position = position;
                m_timeline->setPlayback(m_position, m_duration);
                updateTimeLabels();
            });
    connect(m_playerCore, &PlayerCore::durationChanged,
            this, [this](double duration) {
                m_duration = duration;
                m_timeline->setPlayback(m_position, m_duration);
                updateTimeLabels();
            });
    connect(m_playerCore, &PlayerCore::bufferingChanged,
            this, [this](const BufferingInfo &buffering) {
                m_timeline->setBuffering(
                    buffering, m_playerCore->info().isNetworkResource);
            });
    connect(m_playerCore, &PlayerCore::seekingChanged,
            m_timeline, &IinaTimeline::setSeeking);
    connect(m_playerCore, &PlayerCore::chaptersChanged,
            m_timeline, &IinaTimeline::setChapters);
    connect(m_playerCore, &PlayerCore::abLoopChanged,
            m_timeline, &IinaTimeline::setAbLoop);
    connect(m_playerCore, &PlayerCore::thumbnailsChanged,
            this, [this] {
                const QImage candidate = m_playerCore->thumbnailAt(0.0);
                if (!candidate.isNull()) {
                    m_artwork = candidate;
                    update();
                }
            });
    connect(m_playerCore->mpvCore(), &MpvCore::propertyChanged,
            this, [this](const QString &name, const QVariant &value) {
                if (name == QStringLiteral("volume")) {
                    updateVolume(value.toDouble(), m_muted);
                } else if (name == QStringLiteral("mute")) {
                    updateVolume(m_volume, value.toBool());
                } else if (name.startsWith(QStringLiteral("metadata"))) {
                    updateMetadata();
                }
            });

    m_showArtwork = QSettings().value(
        QStringLiteral("window/musicShowArtwork"), true).toBool();
    refresh();
}

void MusicModeView::attachPlaylistPanel(PlaylistPanel *panel)
{
    if (m_playlistPanel == panel) {
        return;
    }
    m_playlistPanel = panel;
    if (m_playlistPanel) {
        m_playlistPanel->setParent(this);
        m_playlistPanel->hide();
        m_playlistPanel->setGeometry(
            baseMusicWidth, 0, playlistWidth, height());
    }
}

void MusicModeView::detachPlaylistPanel(QWidget *newParent)
{
    if (!m_playlistPanel) {
        return;
    }
    m_playlistPanel->hide();
    m_playlistPanel->setParent(newParent);
    m_playlistPanel = nullptr;
    m_showPlaylist = false;
}

void MusicModeView::setPlaylistVisible(bool visible)
{
    if (!m_playlistPanel || m_showPlaylist == visible) {
        return;
    }
    m_showPlaylist = visible;
    m_playlistPanel->setVisible(visible);
    m_playlistButton->setToolTip(
        visible ? tr("Hide Playlist") : tr("Show Playlist"));
    emit preferredSizeChanged();
}

bool MusicModeView::isPlaylistVisible() const noexcept
{
    return m_showPlaylist;
}

bool MusicModeView::isArtworkVisible() const noexcept
{
    return m_showArtwork;
}

QSize MusicModeView::preferredSize() const
{
    return QSize(
        baseMusicWidth + (m_showPlaylist ? playlistWidth : 0),
        m_showArtwork ? baseMusicHeight : compactMusicHeight);
}

bool MusicModeView::isInteractiveAt(const QPoint &globalPosition) const
{
    QWidget *child = childAt(mapFromGlobal(globalPosition));
    while (child && child != this) {
        if (qobject_cast<QAbstractButton *>(child)
            || qobject_cast<QSlider *>(child)
            || (m_playlistPanel
                && (child == m_playlistPanel
                    || m_playlistPanel->isAncestorOf(child)))) {
            return true;
        }
        child = child->parentWidget();
    }
    return false;
}

void MusicModeView::nativePointerMoved()
{
    setControlsVisible(true);
    m_nativeHoverTimer->start();
}

void MusicModeView::refresh()
{
    m_position = m_playerCore->info().videoPositionSec;
    m_duration = m_playerCore->info().videoDurationSec;
    m_timeline->setPlayback(m_position, m_duration);
    m_timeline->setBuffering(
        m_playerCore->info().buffering,
        m_playerCore->info().isNetworkResource);
    m_timeline->setSeeking(m_playerCore->info().isSeeking);
    m_timeline->setChapters(m_playerCore->info().chapters);
    m_timeline->setAbLoop(m_playerCore->info().abLoop);
    updateVolume(
        m_playerCore->info().volume, m_playerCore->info().isMuted);
    updateMetadata();
    updatePlaybackState();
    updateTimeLabels();
}

void MusicModeView::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    setControlsVisible(true);
}

void MusicModeView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!m_volumePopover->isVisible()) {
        setControlsVisible(false);
    }
}

void MusicModeView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath windowPath;
    windowPath.addRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    painter.setClipPath(windowPath);
    painter.fillRect(rect(), QColor(18, 18, 20, 246));

    const QRect baseRect(0, 0, baseMusicWidth, height());
    if (!m_artwork.isNull()) {
        painter.setOpacity(0.22);
        painter.drawImage(
            baseRect,
            m_artwork.scaled(
                baseRect.size(), Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation));
        painter.setOpacity(1.0);
        painter.fillRect(baseRect, QColor(15, 13, 19, 142));
    }
    if (m_showArtwork) {
        const QRect artRect(
            (baseMusicWidth - 220) / 2, 62, 220, 220);
        if (!m_artwork.isNull()) {
            painter.drawImage(
                artRect,
                m_artwork.scaled(
                    artRect.size(), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation));
        } else {
            QLinearGradient gradient(0, 0, artRect.width(), artRect.height());
            gradient.setColorAt(0.0, QColor(13, 20, 38));
            gradient.setColorAt(0.48, QColor(31, 27, 57));
            gradient.setColorAt(1.0, QColor(13, 14, 23));
            painter.fillRect(artRect, gradient);
            const QPointF center = artRect.center();
            const qreal radius =
                std::min(artRect.width(), artRect.height()) * 0.22;
            painter.setPen(QPen(QColor(245, 245, 247, 230), 3.0));
            painter.setBrush(QColor(8, 9, 13, 180));
            painter.drawEllipse(center, radius, radius);
            QPainterPath mark;
            mark.moveTo(center.x() - radius * 0.18,
                        center.y() - radius * 0.42);
            mark.lineTo(center.x() + radius * 0.48, center.y());
            mark.lineTo(center.x() - radius * 0.18,
                        center.y() + radius * 0.42);
            mark.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(34, 202, 255));
            painter.drawPath(mark);
            painter.setBrush(QColor(66, 67, 255));
            painter.drawRoundedRect(
                QRectF(center.x() - radius * 0.55,
                       center.y() - radius * 0.42,
                       radius * 0.12, radius * 0.84),
                2, 2);
        }
    }

    const QRect controlsRect(
        0, height() - 58, baseMusicWidth, 58);
    painter.fillRect(controlsRect, QColor(18, 18, 21, 115));
    painter.setPen(QColor(255, 255, 255, 32));
    painter.drawLine(
        controlsRect.topLeft(), controlsRect.topRight());
    painter.setClipping(false);
    painter.setPen(QPen(QColor(255, 255, 255, 45), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
}

void MusicModeView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int metadataY = m_showArtwork ? 295 : 42;
    m_infoView->setGeometry(80, metadataY, baseMusicWidth - 160, 104);
    m_titleLabel->setGeometry(0, 0, m_infoView->width(), 30);
    m_artistAlbumLabel->setGeometry(0, 31, m_infoView->width(), 24);
    m_genreLabel->setGeometry(0, 56, m_infoView->width(), 22);
    m_controlsView->setGeometry(0, 0, baseMusicWidth, height());

    m_closeButton->setGeometry(12, 10, 24, 24);
    m_backButton->setGeometry(40, 10, 24, 24);
    m_volumeButton->setGeometry(12, height() - 50, 26, 26);
    m_volumeSlider->setGeometry(42, height() - 42, 82, 16);
    m_shuffleButton->setGeometry(258, height() - 48, 28, 28);
    m_previousButton->setGeometry(292, height() - 50, 30, 30);
    m_playButton->setGeometry(330, height() - 56, 40, 40);
    m_nextButton->setGeometry(378, height() - 50, 30, 30);
    m_repeatButton->setGeometry(414, height() - 48, 28, 28);
    m_artworkButton->setGeometry(614, height() - 48, 28, 28);
    m_playlistButton->setGeometry(648, height() - 48, 28, 28);

    const int timelineY = height() - 92;
    m_elapsedLabel->setGeometry(8, timelineY, 38, 16);
    m_timeline->setGeometry(48, timelineY, baseMusicWidth - 96, 16);
    m_durationLabel->setGeometry(
        baseMusicWidth - 46, timelineY, 38, 16);
    if (m_playlistPanel) {
        m_playlistPanel->setGeometry(
            baseMusicWidth, 0, playlistWidth, height());
        if (m_showPlaylist) {
            m_playlistPanel->raise();
        }
    }
}

void MusicModeView::wheelEvent(QWheelEvent *event)
{
    if (!isLoaded(m_playerCore->info().state)) {
        QWidget::wheelEvent(event);
        return;
    }
    double delta = event->pixelDelta().isNull()
        ? event->angleDelta().y() / 120.0
        : event->pixelDelta().y() / 40.0;
    if (event->inverted()) {
        delta = -delta;
    }
    if (std::abs(delta) > 0.001) {
        m_playerCore->setVolume(m_playerCore->info().volume + delta * 3.0);
    }
    event->accept();
}

void MusicModeView::setControlsVisible(bool visible, bool animated)
{
    Q_UNUSED(visible)
    Q_UNUSED(animated)
    m_infoOpacity->setOpacity(1.0);
    m_controlsOpacity->setOpacity(1.0);
    m_controlsView->setEnabled(true);
}

void MusicModeView::setArtworkVisible(bool visible)
{
    if (m_showArtwork == visible) {
        return;
    }
    m_showArtwork = visible;
    QSettings().setValue(
        QStringLiteral("window/musicShowArtwork"), visible);
    m_artworkButton->setToolTip(
        visible ? tr("Hide Album Art") : tr("Show Album Art"));
    emit preferredSizeChanged();
}

void MusicModeView::updateMetadata()
{
    m_artwork = embeddedMp3Artwork(m_playerCore->info().currentUrl);
    update();
    QString title =
        m_playerCore->mpvPropertyString(QStringLiteral("media-title"));
    if (title.trimmed().isEmpty()) {
        title = QFileInfo(
            m_playerCore->info().currentUrl.toLocalFile()).completeBaseName();
    }
    QString artist = m_playerCore->mpvPropertyString(
        QStringLiteral("metadata/by-key/artist"));
    if (artist.isEmpty()) {
        artist = m_playerCore->mpvPropertyString(
            QStringLiteral("metadata/by-key/album_artist"));
    }
    const QString album = m_playerCore->mpvPropertyString(
        QStringLiteral("metadata/by-key/album"));
    const QString genre = m_playerCore->mpvPropertyString(
        QStringLiteral("metadata/by-key/genre"));
    QString detail;
    if (!artist.isEmpty() && !album.isEmpty()) {
        detail = tr("%1 — %2").arg(artist, album);
    } else {
        detail = !artist.isEmpty() ? artist : album;
    }
    m_titleLabel->setText(title);
    m_titleLabel->setToolTip(title);
    m_artistAlbumLabel->setText(detail);
    m_artistAlbumLabel->setToolTip(detail);
    m_genreLabel->setText(genre);
}

void MusicModeView::updatePlaybackState()
{
    const bool playing =
        m_playerCore->info().state == PlayerState::Playing;
    m_playButton->setIconType(
        playing ? IinaIcon::Pause : IinaIcon::Play);
    m_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
}

void MusicModeView::updateTimeLabels()
{
    m_elapsedLabel->setText(formatTime(m_position));
    m_durationLabel->setText(formatTime(m_duration));
}

void MusicModeView::updateVolume(double volume, bool muted)
{
    m_volume = volume;
    m_muted = muted;
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setValue(qRound(std::clamp(volume, 0.0, 100.0)));
    const IinaIcon icon =
        muted ? IinaIcon::Muted
        : volume < 0.5 ? IinaIcon::VolumeOff
        : volume < 34.0 ? IinaIcon::VolumeLow
        : volume < 67.0 ? IinaIcon::VolumeMedium
                        : IinaIcon::VolumeHigh;
    m_volumeButton->setIconType(icon);
}

QString MusicModeView::formatTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const qint64 total = qRound64(seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 remainder = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainder, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(remainder, 2, 10, QLatin1Char('0'));
}

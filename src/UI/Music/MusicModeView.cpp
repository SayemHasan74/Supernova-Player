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
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int baseMusicWidth = 688;
constexpr int baseMusicHeight = 520;
constexpr int compactMusicWidth = 260;
constexpr int compactMusicHeight = 330;
constexpr int playlistWidth = 300;
constexpr int hoverFadeMs = 200;

QColor mixColor(const QColor &first, const QColor &second, double amount)
{
    const double t = std::clamp(amount, 0.0, 1.0);
    return QColor(
        qRound(first.red() * (1.0 - t) + second.red() * t),
        qRound(first.green() * (1.0 - t) + second.green() * t),
        qRound(first.blue() * (1.0 - t) + second.blue() * t));
}

QPair<QColor, QColor> coverPalette(const QImage &artwork)
{
    if (artwork.isNull()) {
        return {QColor(72, 46, 92), QColor(20, 92, 104)};
    }
    const QImage sample = artwork.scaled(
        24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);
    auto colorForRows = [&sample](int firstRow, int lastRow) {
        qint64 red = 0;
        qint64 green = 0;
        qint64 blue = 0;
        int count = 0;
        QColor accent;
        double accentScore = -1.0;
        for (int y = firstRow; y < lastRow; ++y) {
            for (int x = 0; x < sample.width(); ++x) {
                const QColor color = sample.pixelColor(x, y);
                red += color.red();
                green += color.green();
                blue += color.blue();
                ++count;
                const double score =
                    color.hslSaturationF()
                    * (0.35 + color.lightnessF());
                if (score > accentScore) {
                    accentScore = score;
                    accent = color;
                }
            }
        }
        const QColor average(
            int(red / std::max(1, count)),
            int(green / std::max(1, count)),
            int(blue / std::max(1, count)));
        return accentScore > 0.12
            ? mixColor(average, accent, 0.58)
            : average;
    };
    QColor top = colorForRows(0, sample.height() / 2);
    QColor bottom =
        colorForRows(sample.height() / 2, sample.height());
    top = mixColor(top, QColor(255, 255, 255), 0.12);
    bottom = mixColor(bottom, QColor(8, 10, 16), 0.42);
    return {top, bottom};
}

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
    setMinimumSize(0, 0);

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
    m_minimizeButton =
        new IinaIconButton(IinaIcon::Minimize, m_controlsView);
    m_compactButton =
        new IinaIconButton(IinaIcon::PictureInPicture, m_controlsView);
    m_backButton = new IinaIconButton(IinaIcon::Back, m_controlsView);
    m_volumeButton =
        new IinaIconButton(IinaIcon::VolumeHigh, m_controlsView);
    m_previousButton =
        new IinaIconButton(IinaIcon::Previous, m_controlsView);
    m_playButton = new IinaIconButton(IinaIcon::Play, m_controlsView);
    m_nextButton = new IinaIconButton(IinaIcon::Next, m_controlsView);
    m_playlistButton =
        new IinaIconButton(IinaIcon::Playlist, m_controlsView);
    m_openFileButton =
        new IinaIconButton(IinaIcon::Folder, m_controlsView);
    m_artworkButton =
        new IinaIconButton(IinaIcon::FullScreen, m_controlsView);
    m_shuffleButton =
        new IinaIconButton(IinaIcon::Shuffle, m_controlsView);
    m_repeatButton =
        new IinaIconButton(IinaIcon::Repeat, m_controlsView);

    m_closeButton->setToolTip(tr("Close Player"));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_compactButton->setToolTip(tr("Enter Compact Music Mode"));
    m_backButton->hide();
    m_volumeButton->setToolTip(tr("Volume"));
    m_previousButton->setToolTip(tr("Previous Media"));
    m_playButton->setToolTip(tr("Play"));
    m_nextButton->setToolTip(tr("Next Media"));
    m_playlistButton->setToolTip(tr("Show Playlist"));
    m_openFileButton->setToolTip(tr("Open File"));
    m_artworkButton->setToolTip(tr("Enter Full Screen"));
    m_shuffleButton->setToolTip(tr("Shuffle Playlist"));
    m_repeatButton->setToolTip(tr("Change Repeat Mode"));

    m_upNextLabel = new QLabel(this);
    styleMetadataLabel(
        m_upNextLabel, 10, QColor(245, 245, 247, 185));
    m_upNextLabel->setTextFormat(Qt::PlainText);

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
    connect(m_minimizeButton, &QAbstractButton::clicked,
            this, &MusicModeView::minimizeRequested);
    connect(m_compactButton, &QAbstractButton::clicked,
            this, &MusicModeView::toggleCompactPresentation);
    connect(m_playlistButton, &QAbstractButton::clicked,
            this, &MusicModeView::playlistRequested);
    connect(m_openFileButton, &QAbstractButton::clicked,
            this, &MusicModeView::openFileRequested);
    connect(m_artworkButton, &QAbstractButton::clicked,
            this, &MusicModeView::fullScreenRequested);
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
    connect(m_playerCore, &PlayerCore::playlistChanged,
            this, [this] { updateUpNext(); });
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

    m_showArtwork = true;
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
            std::max(0, width() - playlistWidth),
            0, playlistWidth, height());
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
    resizeEvent(nullptr);
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
    return m_compactPresentation
        ? QSize(compactMusicWidth, compactMusicHeight)
        : QSize(baseMusicWidth, baseMusicHeight);
}

void MusicModeView::setFullScreen(bool fullScreen)
{
    m_fullScreen = fullScreen;
    m_artworkButton->setIconType(
        fullScreen ? IinaIcon::ExitFullScreen
                   : IinaIcon::FullScreen);
    m_artworkButton->setToolTip(
        fullScreen ? tr("Exit Full Screen")
                   : tr("Enter Full Screen"));
    resizeEvent(nullptr);
}

bool MusicModeView::isInteractiveAt(const QPoint &globalPosition) const
{
    QWidget *child = childAt(mapFromGlobal(globalPosition));
    while (child && child != this) {
        if (qobject_cast<QAbstractButton *>(child)
            || qobject_cast<QSlider *>(child)
            || child == m_timeline
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
    updateUpNext();
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
    const auto [coverTop, coverBottom] = coverPalette(m_artwork);
    QLinearGradient coverGradient(0, 0, 0, height());
    coverGradient.setColorAt(
        0.0, mixColor(coverTop, QColor(18, 18, 22), 0.30));
    coverGradient.setColorAt(
        1.0, mixColor(coverBottom, QColor(9, 11, 16), 0.24));
    painter.fillRect(rect(), coverGradient);

    const QRect baseRect = rect();
    if (!m_artwork.isNull()) {
        painter.setOpacity(m_compactPresentation && !m_fullScreen
                               ? 0.10 : 0.18);
        painter.drawImage(
            baseRect,
            m_artwork.scaled(
                baseRect.size(), Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation));
        painter.setOpacity(1.0);
        painter.fillRect(baseRect, QColor(10, 11, 16, 92));
    }
    if (m_showArtwork) {
        const int contentWidth = width();
        const bool compact =
            m_compactPresentation && !m_fullScreen;
        const int artSize = compact
            ? 112
            : width() <= baseMusicWidth && height() <= baseMusicHeight
            ? 220
            : std::clamp(
                  std::min(contentWidth * 36 / 100,
                           height() * 46 / 100),
                  220, 420);
        const int artTop = compact
            ? 42
            : std::max(42, (height() - artSize - 205) / 2);
        const QRect artRect(
            (contentWidth - artSize) / 2,
            artTop, artSize, artSize);
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

    const bool compactControls =
        m_compactPresentation && !m_fullScreen;
    const QRect controlsRect(
        0, compactControls ? 240 : height() - 58,
        width(), compactControls ? height() - 240 : 58);
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
    if (event) {
        QWidget::resizeEvent(event);
    }
    const bool compact = m_compactPresentation && !m_fullScreen;
    const int contentWidth = width();
    const int artSize = compact
        ? 112
        : width() <= baseMusicWidth && height() <= baseMusicHeight
        ? 220
        : std::clamp(
              std::min(contentWidth * 36 / 100,
                       height() * 46 / 100),
              220, 420);
    const int artTop = compact
        ? 42
        : std::max(42, (height() - artSize - 205) / 2);
    const int metadataY = artTop + artSize + 13;
    const int infoWidth = compact
        ? width() - 24 : std::max(260, contentWidth - 160);
    m_infoView->setGeometry(
        std::max(12, (contentWidth - infoWidth) / 2),
        metadataY, infoWidth, 104);
    m_titleLabel->setGeometry(
        0, 0, m_infoView->width(), compact ? 24 : 30);
    m_artistAlbumLabel->setGeometry(
        0, compact ? 23 : 31,
        m_infoView->width(), compact ? 20 : 24);
    m_genreLabel->setGeometry(
        0, compact ? 43 : 56,
        m_infoView->width(), compact ? 18 : 22);
    m_controlsView->setGeometry(0, 0, width(), height());

    m_closeButton->setGeometry(12, 10, 24, 24);
    m_minimizeButton->setGeometry(40, 10, 24, 24);
    m_compactButton->setGeometry(
        width() - 36, 10, 24, 24);
    m_backButton->hide();
    const int center = contentWidth / 2;
    if (compact) {
        m_volumeButton->setGeometry(10, 256, 24, 24);
        m_volumeSlider->hide();
        m_shuffleButton->setGeometry(48, 254, 24, 24);
        m_previousButton->setGeometry(78, 252, 28, 28);
        m_playButton->setGeometry(110, 246, 38, 38);
        m_nextButton->setGeometry(152, 252, 28, 28);
        m_repeatButton->setGeometry(182, 254, 24, 24);
        m_artworkButton->setGeometry(218, 254, 24, 24);
        m_openFileButton->setGeometry(188, 286, 24, 24);
        m_playlistButton->setGeometry(218, 286, 24, 24);
        m_upNextLabel->setGeometry(20, 294, 190, 22);
        m_upNextLabel->show();
    } else {
        m_volumeButton->setGeometry(12, height() - 50, 26, 26);
        m_volumeSlider->setGeometry(42, height() - 42, 82, 16);
        m_volumeSlider->show();
        m_shuffleButton->setGeometry(
            center - 86, height() - 48, 28, 28);
        m_previousButton->setGeometry(
            center - 52, height() - 50, 30, 30);
        m_playButton->setGeometry(
            center - 14, height() - 56, 40, 40);
        m_nextButton->setGeometry(
            center + 34, height() - 50, 30, 30);
        m_repeatButton->setGeometry(
            center + 70, height() - 48, 28, 28);
        m_artworkButton->setGeometry(
            contentWidth - 108, height() - 48, 28, 28);
        m_openFileButton->setGeometry(
            contentWidth - 74, height() - 48, 28, 28);
        m_playlistButton->setGeometry(
            contentWidth - 40, height() - 48, 28, 28);
        m_upNextLabel->hide();
    }

    const int timelineY = compact ? 222 : height() - 92;
    m_elapsedLabel->setGeometry(8, timelineY, 38, 16);
    m_timeline->setGeometry(
        48, timelineY, std::max(100, contentWidth - 96), 16);
    m_durationLabel->setGeometry(
        contentWidth - 46, timelineY, 38, 16);
    if (m_playlistPanel) {
        m_playlistPanel->setGeometry(
            std::max(0, width() - playlistWidth),
            0, playlistWidth, height());
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

void MusicModeView::toggleCompactPresentation()
{
    if (m_fullScreen) {
        return;
    }
    m_compactPresentation = !m_compactPresentation;
    m_compactButton->setToolTip(
        m_compactPresentation
            ? tr("Exit Compact Music Mode")
            : tr("Enter Compact Music Mode"));
    if (m_showPlaylist) {
        setPlaylistVisible(false);
    }
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

void MusicModeView::updateUpNext()
{
    const PlaylistState &playlist = m_playerCore->info().playlist;
    const int next = playlist.currentIndex + 1;
    if (next >= 0 && next < playlist.items.size()) {
        const PlaylistItem &item = playlist.items[next];
        const QString name = !item.displayName.isEmpty()
            ? item.displayName
            : !item.title.isEmpty() ? item.title : item.url.fileName();
        m_upNextLabel->setText(tr("Up Next  ·  %1").arg(name));
        m_upNextLabel->setToolTip(name);
    } else {
        m_upNextLabel->setText(tr("Up Next"));
        m_upNextLabel->setToolTip({});
    }
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

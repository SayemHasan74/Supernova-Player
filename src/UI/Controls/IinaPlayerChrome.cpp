#include "UI/Controls/IinaPlayerChrome.h"

#include "Mpv/MpvCore.h"
#include "PlayerCore/PlayerCore.h"
#include "UI/Design/DesignTokens.h"

#include <QEnterEvent>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace {
using namespace Supernova::Ui;

QPainterPath iconPath(IinaIcon icon, const QRectF &bounds)
{
    QPainterPath path;
    const QPointF center = bounds.center();
    const qreal unit = std::min(bounds.width(), bounds.height()) / 16.0;

    switch (icon) {
    case IinaIcon::Play:
        path.moveTo(center.x() - 3.8 * unit, center.y() - 5.8 * unit);
        path.lineTo(center.x() + 5.7 * unit, center.y());
        path.lineTo(center.x() - 3.8 * unit, center.y() + 5.8 * unit);
        path.closeSubpath();
        break;
    case IinaIcon::Pause:
        path.addRoundedRect(
            QRectF(center.x() - 5.0 * unit, center.y() - 5.7 * unit,
                   3.2 * unit, 11.4 * unit),
            unit, unit);
        path.addRoundedRect(
            QRectF(center.x() + 1.8 * unit, center.y() - 5.7 * unit,
                   3.2 * unit, 11.4 * unit),
            unit, unit);
        break;
    case IinaIcon::Previous:
        path.moveTo(center.x() + 3.3 * unit, center.y() - 5.2 * unit);
        path.lineTo(center.x() - 3.5 * unit, center.y());
        path.lineTo(center.x() + 3.3 * unit, center.y() + 5.2 * unit);
        path.closeSubpath();
        path.addRect(QRectF(
            center.x() - 5.4 * unit, center.y() - 5.2 * unit,
            1.5 * unit, 10.4 * unit));
        break;
    case IinaIcon::Next:
        path.moveTo(center.x() - 3.3 * unit, center.y() - 5.2 * unit);
        path.lineTo(center.x() + 3.5 * unit, center.y());
        path.lineTo(center.x() - 3.3 * unit, center.y() + 5.2 * unit);
        path.closeSubpath();
        path.addRect(QRectF(
            center.x() + 3.9 * unit, center.y() - 5.2 * unit,
            1.5 * unit, 10.4 * unit));
        break;
    case IinaIcon::VolumeOff:
    case IinaIcon::VolumeLow:
    case IinaIcon::VolumeMedium:
    case IinaIcon::VolumeHigh:
    case IinaIcon::Muted:
        path.moveTo(center.x() - 6.0 * unit, center.y() - 2.3 * unit);
        path.lineTo(center.x() - 3.0 * unit, center.y() - 2.3 * unit);
        path.lineTo(center.x() + 1.2 * unit, center.y() - 5.8 * unit);
        path.lineTo(center.x() + 1.2 * unit, center.y() + 5.8 * unit);
        path.lineTo(center.x() - 3.0 * unit, center.y() + 2.3 * unit);
        path.lineTo(center.x() - 6.0 * unit, center.y() + 2.3 * unit);
        path.closeSubpath();
        if (icon == IinaIcon::Muted) {
            path.moveTo(center.x() + 3.2 * unit, center.y() - 3.2 * unit);
            path.lineTo(center.x() + 7.0 * unit, center.y() + 3.2 * unit);
            path.moveTo(center.x() + 7.0 * unit, center.y() - 3.2 * unit);
            path.lineTo(center.x() + 3.2 * unit, center.y() + 3.2 * unit);
        } else {
            const int waveCount =
                icon == IinaIcon::VolumeHigh ? 3
                : icon == IinaIcon::VolumeMedium ? 2
                : icon == IinaIcon::VolumeLow ? 1
                                             : 0;
            for (int wave = 0; wave < waveCount; ++wave) {
                const qreal radius = (2.4 + wave * 1.9) * unit;
                path.moveTo(
                    center.x() + 2.4 * unit,
                    center.y() - radius * 0.62);
                path.cubicTo(
                    center.x() + radius, center.y() - radius * 0.46,
                    center.x() + radius, center.y() + radius * 0.46,
                    center.x() + 2.4 * unit,
                    center.y() + radius * 0.62);
            }
        }
        break;
    case IinaIcon::Folder:
        path.moveTo(
            center.x() - 6.2 * unit,
            center.y() - 4.5 * unit);
        path.lineTo(
            center.x() - 1.9 * unit,
            center.y() - 4.5 * unit);
        path.lineTo(
            center.x() - 0.1 * unit,
            center.y() - 2.6 * unit);
        path.lineTo(
            center.x() + 6.2 * unit,
            center.y() - 2.6 * unit);
        path.lineTo(
            center.x() + 6.2 * unit,
            center.y() + 4.8 * unit);
        path.lineTo(
            center.x() - 6.2 * unit,
            center.y() + 4.8 * unit);
        path.closeSubpath();
        break;
    case IinaIcon::Playlist:
        for (int row = -1; row <= 1; ++row) {
            const qreal y = center.y() + row * 4.0 * unit;
            path.addEllipse(
                QPointF(center.x() - 5.6 * unit, y),
                0.9 * unit, 0.9 * unit);
            path.moveTo(center.x() - 2.8 * unit, y);
            path.lineTo(center.x() + 6.0 * unit, y);
        }
        break;
    case IinaIcon::Settings: {
        path.addEllipse(
            QRectF(center.x() - 5.0, center.y() - 5.0, 10.0, 10.0));
        QPainterPath hole;
        hole.addEllipse(
            QRectF(center.x() - 1.8, center.y() - 1.8, 3.6, 3.6));
        path = path.subtracted(hole);
        for (int index = 0; index < 8; ++index) {
            const double angle =
                static_cast<double>(index) * std::numbers::pi / 4.0;
            const QPointF offset(
                std::cos(angle) * 6.2, std::sin(angle) * 6.2);
            path.addRoundedRect(
                QRectF(center.x() + offset.x() - 1.2,
                       center.y() + offset.y() - 1.2, 2.4, 2.4),
                0.6, 0.6);
        }
        break;
    }
    case IinaIcon::FullScreen:
    case IinaIcon::ExitFullScreen: {
        const bool inward = icon == IinaIcon::ExitFullScreen;
        const qreal outer = 5.7 * unit;
        const qreal inner = 1.7 * unit;
        for (int xSign : {-1, 1}) {
            for (int ySign : {-1, 1}) {
                const qreal x0 = center.x() + xSign * outer;
                const qreal y0 = center.y() + ySign * outer;
                const qreal x1 =
                    center.x() + xSign * (inward ? inner : 2.2 * unit);
                const qreal y1 =
                    center.y() + ySign * (inward ? inner : 2.2 * unit);
                path.moveTo(x0, y1);
                path.lineTo(x0, y0);
                path.lineTo(x1, y0);
            }
        }
        break;
    }
    }
    return path;
}

void configureLabel(QLabel *label, Qt::Alignment alignment)
{
    label->setAlignment(alignment);
    label->setFixedWidth(timeLabelWidth);
    label->setStyleSheet(
        QStringLiteral("color: rgba(245,245,247,190); background: transparent;"));
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(10);
    label->setFont(font);
}
}

IinaIconButton::IinaIconButton(IinaIcon icon, QWidget *parent)
    : QAbstractButton(parent), m_icon(icon)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
}

void IinaIconButton::setIconType(IinaIcon icon)
{
    if (m_icon == icon) {
        return;
    }
    m_icon = icon;
    update();
}

void IinaIconButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) {
        painter.setOpacity(0.36);
    }

    if (isDown()) {
        painter.setBrush(controlPressed);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    } else if (underMouse()) {
        painter.setBrush(controlHover);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }

    QPainterPath path =
        iconPath(m_icon, rect().adjusted(4, 4, -4, -4));
    QPen pen(primaryText, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(primaryText);
    painter.drawPath(path);
}

IinaTimeline::IinaTimeline(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setMinimumHeight(16);
}

void IinaTimeline::setPlayback(double position, double duration)
{
    m_position = std::max(0.0, position);
    m_duration = std::max(0.0, duration);
    update();
}

void IinaTimeline::setBuffering(
    const BufferingInfo &buffering, bool networkResource)
{
    m_cacheDuration = std::max(0.0, buffering.cacheDurationSec);
    m_networkResource = networkResource;
    update();
}

void IinaTimeline::setSeeking(bool seeking)
{
    if (m_seeking == seeking) {
        return;
    }
    m_seeking = seeking;
    update();
}

void IinaTimeline::setChapters(
    const QList<PlaybackChapter> &chapters)
{
    m_chapters = chapters;
    update();
}

void IinaTimeline::setAbLoop(const AbLoopState &state)
{
    m_abLoop = state;
    update();
}

void IinaTimeline::leaveEvent(QEvent *event)
{
    m_hovering = false;
    if (!m_dragging) {
        emit previewDismissed();
    }
    update();
    QWidget::leaveEvent(event);
}

void IinaTimeline::mouseMoveEvent(QMouseEvent *event)
{
    emit interaction();
    m_hovering = true;
    previewAt(event->position().x());
    if (m_dragging) {
        seekAt(event->position().x());
    }
    QWidget::mouseMoveEvent(event);
}

void IinaTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        emit seekStarted();
        previewAt(event->position().x());
        seekAt(event->position().x());
        emit interaction();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void IinaTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        const double percent = seekAt(event->position().x());
        m_dragging = false;
        emit seekFinished(percent);
        if (!rect().contains(event->position().toPoint())) {
            m_hovering = false;
            emit previewDismissed();
        }
        emit interaction();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void IinaTimeline::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF track(1.5, height() / 2.0 - 1.5, width() - 3.0, 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(sliderRemaining);
    painter.drawRoundedRect(track, 1.5, 1.5);

    const double ratio = m_duration > 0.0
        ? std::clamp(m_position / m_duration, 0.0, 1.0)
        : 0.0;
    const double cachedRatio =
        m_networkResource && m_duration > 0.0
        ? std::clamp(
              (m_position + m_cacheDuration) / m_duration,
              ratio, 1.0)
        : ratio;
    if (cachedRatio > ratio) {
        QRectF cached = track;
        cached.setLeft(track.left() + track.width() * ratio);
        cached.setRight(
            track.left() + track.width() * cachedRatio);
        painter.setBrush(QColor(235, 235, 245, 112));
        painter.drawRoundedRect(cached, 1.5, 1.5);
    }
    QRectF played = track;
    played.setWidth(track.width() * ratio);
    painter.setBrush(sliderPlayed);
    painter.drawRoundedRect(played, 1.5, 1.5);

    if (m_duration > 0.0) {
        painter.setPen(QPen(QColor(255, 255, 255, 145), 1.0));
        for (const PlaybackChapter &chapter : std::as_const(m_chapters)) {
            const double chapterRatio = std::clamp(
                chapter.startTimeSec / m_duration, 0.0, 1.0);
            const qreal x =
                track.left() + track.width() * chapterRatio;
            painter.drawLine(
                QPointF(x, track.top() - 2.0),
                QPointF(x, track.bottom() + 2.0));
        }
        auto drawAbMarker = [&painter, &track, this](
                                bool visible, double seconds,
                                const QColor &color) {
            if (!visible) {
                return;
            }
            const qreal x = track.left() + track.width()
                * std::clamp(seconds / m_duration, 0.0, 1.0);
            QPainterPath marker;
            marker.moveTo(x, track.top() - 5.0);
            marker.lineTo(x - 3.5, track.top() - 9.0);
            marker.lineTo(x + 3.5, track.top() - 9.0);
            marker.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPath(marker);
        };
        drawAbMarker(
            m_abLoop.status != AbLoopStatus::Cleared,
            m_abLoop.pointA, QColor(75, 170, 255));
        drawAbMarker(
            m_abLoop.status == AbLoopStatus::BSet,
            m_abLoop.pointB, QColor(255, 152, 71));
    }

    const qreal knobX = track.left() + track.width() * ratio;
    painter.setBrush(sliderKnob);
    painter.drawRoundedRect(
        QRectF(knobX - 1.5, height() / 2.0 - 7.5, 3.0, 15.0),
        1.0, 1.0);

    if (m_hovering && m_duration > 0.0 && !m_dragging) {
        const qreal previewX =
            track.left() + track.width() * m_previewRatio;
        painter.setBrush(QColor(255, 255, 255, 150));
        painter.drawEllipse(QPointF(previewX, track.center().y()), 2.2, 2.2);
    }

    if (m_seeking) {
        painter.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(
            QPointF(knobX, track.center().y()), 5.0, 5.0);
    }
}

double IinaTimeline::ratioAt(double x) const noexcept
{
    if (width() <= 3) {
        return 0.0;
    }
    return std::clamp(
        (x - 1.5) / static_cast<double>(width() - 3),
        0.0, 1.0);
}

void IinaTimeline::previewAt(double x)
{
    if (m_duration <= 0.0) {
        emit previewDismissed();
        return;
    }
    m_previewRatio = ratioAt(x);
    const int anchorX = qRound(
        1.5 + m_previewRatio * static_cast<double>(width() - 3));
    emit previewRequested(
        m_duration * m_previewRatio,
        mapToGlobal(QPoint(anchorX, 0)));
    update();
}

double IinaTimeline::seekAt(double x)
{
    if (m_duration <= 0.0) {
        return 0.0;
    }
    const double ratio = ratioAt(x);
    m_position = m_duration * ratio;
    update();
    emit seekRequested(ratio * 100.0);
    return ratio * 100.0;
}

IinaPlayerChrome::IinaPlayerChrome(
    PlayerCore *playerCore, QWidget *parent)
    : QWidget(parent), m_playerCore(playerCore)
{
    setObjectName(QStringLiteral("iinaPlayerChrome"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFixedHeight(floatingControlHeight);
    setMinimumWidth(floatingControlMinWidth);
    setMaximumWidth(floatingControlWidth);

    auto *opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(1.0);
    setGraphicsEffect(opacity);
    m_opacityAnimation = new QPropertyAnimation(opacity, "opacity", this);
    m_opacityAnimation->setDuration(controlFadeDurationMs);
    m_opacityAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 7, 8, 5);
    outer->setSpacing(4);

    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(3, 0, 3, 0);
    controls->setSpacing(4);

    m_muteButton = new IinaIconButton(IinaIcon::VolumeHigh, this);
    m_muteButton->setObjectName(QStringLiteral("muteButton"));
    m_muteButton->setFixedSize(compactButtonExtent, compactButtonExtent);
    m_muteButton->setToolTip(tr("Mute"));
    controls->addWidget(m_muteButton);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setObjectName(QStringLiteral("volumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(
        qRound(std::clamp(m_playerCore->info().volume, 0.0, 100.0)));
    m_volumeSlider->setFixedWidth(volumeSliderWidth);
    m_volumeSlider->setFixedHeight(12);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 3px; border-radius: 1px;"
        " background: rgba(235,235,245,62); }"
        "QSlider::sub-page:horizontal { background: rgba(245,245,247,220);"
        " border-radius: 1px; }"
        "QSlider::handle:horizontal { width: 9px; margin: -3px 0;"
        " border-radius: 4px; background: rgb(250,250,252); }"));
    controls->addWidget(m_volumeSlider);
    controls->addStretch(1);

    m_previousButton =
        new IinaIconButton(IinaIcon::Previous, this);
    m_previousButton->setObjectName(QStringLiteral("previousButton"));
    m_previousButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_previousButton->setToolTip(tr("Previous Media"));
    controls->addWidget(m_previousButton);

    m_playButton = new IinaIconButton(IinaIcon::Play, this);
    m_playButton->setObjectName(QStringLiteral("playPauseButton"));
    m_playButton->setFixedSize(primaryButtonExtent, primaryButtonExtent);
    m_playButton->setToolTip(tr("Play"));
    controls->addSpacing(18);
    controls->addWidget(m_playButton);
    controls->addSpacing(18);

    m_nextButton = new IinaIconButton(IinaIcon::Next, this);
    m_nextButton->setObjectName(QStringLiteral("nextButton"));
    m_nextButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_nextButton->setToolTip(tr("Next Media"));
    controls->addWidget(m_nextButton);
    controls->addStretch(1);

    m_openFileButton =
        new IinaIconButton(IinaIcon::Folder, this);
    m_openFileButton->setObjectName(
        QStringLiteral("openFileButton"));
    m_openFileButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_openFileButton->setToolTip(tr("Open File"));
    controls->addWidget(m_openFileButton);

    m_playlistButton =
        new IinaIconButton(IinaIcon::Playlist, this);
    m_playlistButton->setObjectName(
        QStringLiteral("playlistButton"));
    m_playlistButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_playlistButton->setToolTip(tr("Show Playlist"));
    controls->addWidget(m_playlistButton);

    m_settingsButton =
        new IinaIconButton(IinaIcon::Settings, this);
    m_settingsButton->setObjectName(
        QStringLiteral("mediaSettingsButton"));
    m_settingsButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_settingsButton->setToolTip(tr("Quick Settings"));
    controls->addWidget(m_settingsButton);

    m_fullScreenButton =
        new IinaIconButton(IinaIcon::FullScreen, this);
    m_fullScreenButton->setObjectName(QStringLiteral("fullScreenButton"));
    m_fullScreenButton->setFixedSize(
        compactButtonExtent, compactButtonExtent);
    m_fullScreenButton->setToolTip(tr("Enter Full Screen"));
    controls->addWidget(m_fullScreenButton);
    outer->addLayout(controls);

    auto *timelineRow = new QHBoxLayout;
    timelineRow->setContentsMargins(0, 0, 0, 0);
    timelineRow->setSpacing(3);
    m_elapsedLabel = new QLabel(QStringLiteral("0:00"), this);
    m_elapsedLabel->setObjectName(QStringLiteral("elapsedTimeLabel"));
    configureLabel(m_elapsedLabel, Qt::AlignCenter);
    timelineRow->addWidget(m_elapsedLabel);
    m_timeline = new IinaTimeline(this);
    m_timeline->setObjectName(QStringLiteral("playbackTimeline"));
    timelineRow->addWidget(m_timeline, 1);
    m_durationLabel = new QLabel(QStringLiteral("0:00"), this);
    m_durationLabel->setObjectName(QStringLiteral("durationTimeLabel"));
    configureLabel(m_durationLabel, Qt::AlignCenter);
    timelineRow->addWidget(m_durationLabel);
    outer->addLayout(timelineRow);

    connect(m_playButton, &QAbstractButton::clicked,
            this, [this] {
                if (m_playerCore->info().state == PlayerState::Paused) {
                    m_playerCore->resume();
                    emit osdRequested(
                        tr("Play"), formatTime(m_position),
                        m_duration > 0.0 ? m_position / m_duration : -1.0);
                } else if (isLoaded(m_playerCore->info().state)) {
                    m_playerCore->pause();
                    emit osdRequested(
                        tr("Pause"), formatTime(m_position),
                        m_duration > 0.0 ? m_position / m_duration : -1.0);
                }
            });
    connect(m_muteButton, &QAbstractButton::clicked,
            this, [this] {
                m_playerCore->toggleMute();
                emit osdRequested(
                    m_muted ? tr("Sound On") : tr("Muted"),
                    m_muted ? tr("Volume %1%").arg(qRound(m_volume))
                            : QString(),
                    m_muted ? m_volume / 100.0 : 0.0);
            });
    connect(m_volumeSlider, &QSlider::valueChanged,
            m_playerCore, [this](int value) {
                m_playerCore->setVolume(value);
                emit osdRequested(
                    tr("Volume %1%").arg(value), QString(),
                    static_cast<double>(value) / 100.0);
                emit activity();
            });
    connect(m_fullScreenButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::fullScreenRequested);
    connect(m_openFileButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::openFileRequested);
    connect(m_playlistButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::playlistRequested);
    connect(m_settingsButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::mediaSettingsRequested);
    connect(m_previousButton, &QAbstractButton::clicked,
            m_playerCore, [this] {
                m_playerCore->navigateInPlaylist(false);
                emit activity();
            });
    connect(m_nextButton, &QAbstractButton::clicked,
            m_playerCore, [this] {
                m_playerCore->navigateInPlaylist(true);
                emit activity();
            });
    connect(m_playButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_muteButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_fullScreenButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_openFileButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_playlistButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
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
            this, [this](double percent) {
                Q_UNUSED(percent)
                if (!m_wasPausedBeforeTimelineDrag) {
                    m_playerCore->resume();
                }
            });
    connect(m_timeline, &IinaTimeline::previewRequested,
            this, &IinaPlayerChrome::previewRequested);
    connect(m_timeline, &IinaTimeline::previewDismissed,
            this, &IinaPlayerChrome::previewDismissed);
    connect(m_timeline, &IinaTimeline::interaction,
            this, &IinaPlayerChrome::activity);
    connect(m_playerCore, &PlayerCore::stateChanged,
            this, [this](PlayerState) {
                updatePlaybackState();
            });
    connect(m_playerCore, &PlayerCore::mediaLoaded,
            this, [this] { updatePlaybackState(); });
    connect(m_playerCore, &PlayerCore::playbackStopped,
            this, [this] { updatePlaybackState(); });
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
    connect(m_playerCore->mpvCore(), &MpvCore::propertyChanged,
            this, [this](const QString &name, const QVariant &value) {
                if (name == QStringLiteral("mute")) {
                    m_muted = value.toBool();
                    updateVolumeControls(m_volume, m_muted);
                } else if (name == QStringLiteral("volume")
                           && !m_volumeSlider->isSliderDown()) {
                    m_volume = value.toDouble();
                    const QSignalBlocker blocker(m_volumeSlider);
                    m_volumeSlider->setValue(qRound(m_volume));
                    updateVolumeControls(m_volume, m_muted);
                }
            });

    m_position = m_playerCore->info().videoPositionSec;
    m_duration = m_playerCore->info().videoDurationSec;
    m_volume = m_playerCore->info().volume;
    m_muted = m_playerCore->info().isMuted;
    m_timeline->setPlayback(m_position, m_duration);
    m_timeline->setBuffering(
        m_playerCore->info().buffering,
        m_playerCore->info().isNetworkResource);
    m_timeline->setSeeking(m_playerCore->info().isSeeking);
    m_timeline->setChapters(m_playerCore->info().chapters);
    m_timeline->setAbLoop(m_playerCore->info().abLoop);
    updateTimeLabels();
    updateVolumeControls(m_volume, m_muted);
    updatePlaybackState();

    const auto interactiveChildren = findChildren<QWidget *>();
    for (QWidget *child : interactiveChildren) {
        child->installEventFilter(this);
        child->setMouseTracking(true);
    }
}

void IinaPlayerChrome::setFullScreen(bool fullScreen)
{
    m_fullScreenButton->setIconType(
        fullScreen ? IinaIcon::ExitFullScreen
                   : IinaIcon::FullScreen);
    m_fullScreenButton->setToolTip(
        fullScreen ? tr("Exit Full Screen")
                   : tr("Enter Full Screen"));
}

void IinaPlayerChrome::reveal(bool animated)
{
    m_concealed = false;
    show();
    raise();
    m_opacityAnimation->stop();
    auto *effect =
        qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
    if (!animated) {
        effect->setOpacity(1.0);
        return;
    }
    m_opacityAnimation->setStartValue(effect->opacity());
    m_opacityAnimation->setEndValue(1.0);
    m_opacityAnimation->start();
}

void IinaPlayerChrome::conceal(bool animated)
{
    if (underMouse()) {
        emit activity();
        return;
    }
    m_concealed = true;
    m_opacityAnimation->stop();
    auto *effect =
        qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
    if (!animated) {
        effect->setOpacity(0.0);
        hide();
        return;
    }
    m_opacityAnimation->setStartValue(effect->opacity());
    m_opacityAnimation->setEndValue(0.0);
    connect(
        m_opacityAnimation, &QPropertyAnimation::finished,
        this, [this] {
            if (m_concealed) {
                hide();
            }
        },
        Qt::SingleShotConnection);
    m_opacityAnimation->start();
}

bool IinaPlayerChrome::isConcealed() const noexcept
{
    return m_concealed;
}

void IinaPlayerChrome::enterEvent(QEnterEvent *event)
{
    emit activity();
    QWidget::enterEvent(event);
}

bool IinaPlayerChrome::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            emit progressModeRequested();
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove
        || event->type() == QEvent::Enter
        || event->type() == QEvent::Wheel) {
        emit activity();
    }
    return QWidget::eventFilter(watched, event);
}

void IinaPlayerChrome::mouseMoveEvent(QMouseEvent *event)
{
    emit activity();
    QWidget::mouseMoveEvent(event);
}

void IinaPlayerChrome::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        emit progressModeRequested();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void IinaPlayerChrome::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient fill(panel.topLeft(), panel.bottomLeft());
    fill.setColorAt(0.0, QColor(38, 38, 42, 224));
    fill.setColorAt(0.24, panelFill);
    fill.setColorAt(1.0, QColor(15, 15, 17, 224));
    painter.setBrush(fill);
    painter.setPen(QPen(panelBorder, 1.0));
    painter.drawRoundedRect(
        panel, floatingControlRadius, floatingControlRadius);
    painter.setPen(QPen(panelHighlight, 1.0));
    painter.drawLine(
        QPointF(floatingControlRadius, 1.5),
        QPointF(width() - floatingControlRadius, 1.5));
}

void IinaPlayerChrome::updatePlaybackState()
{
    const bool loaded = isLoaded(m_playerCore->info().state);
    const bool playing =
        m_playerCore->info().state == PlayerState::Playing;
    m_playButton->setIconType(
        playing ? IinaIcon::Pause : IinaIcon::Play);
    m_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
    m_playButton->setEnabled(loaded);
    m_previousButton->setEnabled(loaded);
    m_nextButton->setEnabled(loaded);
    m_openFileButton->setEnabled(true);
    m_playlistButton->setEnabled(true);
    m_timeline->setEnabled(loaded);
    m_muteButton->setVisible(true);
    m_volumeSlider->setVisible(true);
    m_muteButton->setEnabled(loaded);
    m_volumeSlider->setEnabled(loaded);
}

void IinaPlayerChrome::updateVolumeControls(
    double volume, bool muted)
{
    IinaIcon icon = IinaIcon::VolumeHigh;
    if (muted) {
        icon = IinaIcon::Muted;
    } else if (volume <= 0.0) {
        icon = IinaIcon::VolumeOff;
    } else if (volume <= 33.0) {
        icon = IinaIcon::VolumeLow;
    } else if (volume <= 66.0) {
        icon = IinaIcon::VolumeMedium;
    }
    m_muteButton->setIconType(icon);
    m_muteButton->setToolTip(muted ? tr("Unmute") : tr("Mute"));
}

void IinaPlayerChrome::updateTimeLabels()
{
    m_elapsedLabel->setText(formatTime(m_position));
    m_durationLabel->setText(formatTime(m_duration));
}

QString IinaPlayerChrome::formatTime(double seconds)
{
    const qint64 total = qRound64(std::max(0.0, seconds));
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 remainingSeconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

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
#include <QSlider>
#include <QStyleOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    case IinaIcon::Volume:
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
        }
        break;
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

    if (isDown()) {
        painter.setBrush(controlPressed);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    } else if (underMouse()) {
        painter.setBrush(controlHover);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }

    QPainterPath path = iconPath(m_icon, rect().adjusted(4, 4, -4, -4));
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

void IinaTimeline::mouseMoveEvent(QMouseEvent *event)
{
    emit interaction();
    if (m_dragging) {
        seekAt(event->position().x());
    }
    QWidget::mouseMoveEvent(event);
}

void IinaTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
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
        m_dragging = false;
        seekAt(event->position().x());
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
    QRectF played = track;
    played.setWidth(track.width() * ratio);
    painter.setBrush(sliderPlayed);
    painter.drawRoundedRect(played, 1.5, 1.5);

    const qreal knobX = track.left() + track.width() * ratio;
    painter.setBrush(sliderKnob);
    painter.drawRoundedRect(
        QRectF(knobX - 1.5, height() / 2.0 - 7.5, 3.0, 15.0),
        1.0, 1.0);
}

void IinaTimeline::seekAt(double x)
{
    if (width() <= 1 || m_duration <= 0.0) {
        return;
    }
    const double ratio =
        std::clamp(x / static_cast<double>(width()), 0.0, 1.0);
    m_position = m_duration * ratio;
    update();
    emit seekRequested(ratio * 100.0);
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

    m_muteButton = new IinaIconButton(IinaIcon::Volume, this);
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

    auto *previous = new IinaIconButton(IinaIcon::Previous, this);
    previous->setObjectName(QStringLiteral("previousButton"));
    previous->setFixedSize(compactButtonExtent, compactButtonExtent);
    previous->setToolTip(tr("Previous"));
    controls->addWidget(previous);

    m_playButton = new IinaIconButton(IinaIcon::Play, this);
    m_playButton->setObjectName(QStringLiteral("playPauseButton"));
    m_playButton->setFixedSize(primaryButtonExtent, primaryButtonExtent);
    m_playButton->setToolTip(tr("Play"));
    controls->addSpacing(18);
    controls->addWidget(m_playButton);
    controls->addSpacing(18);

    auto *next = new IinaIconButton(IinaIcon::Next, this);
    next->setObjectName(QStringLiteral("nextButton"));
    next->setFixedSize(compactButtonExtent, compactButtonExtent);
    next->setToolTip(tr("Next"));
    controls->addWidget(next);
    controls->addStretch(1);

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
            m_playerCore, &PlayerCore::togglePause);
    connect(m_muteButton, &QAbstractButton::clicked,
            m_playerCore, &PlayerCore::toggleMute);
    connect(m_volumeSlider, &QSlider::valueChanged,
            m_playerCore, [this](int value) {
                m_playerCore->setVolume(value);
                emit activity();
            });
    connect(m_fullScreenButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::fullScreenRequested);
    connect(previous, &QAbstractButton::clicked,
            this, [this] {
                m_playerCore->mpvCore()->command(
                    {QStringLiteral("playlist-prev"),
                     QStringLiteral("force")});
                emit activity();
            });
    connect(next, &QAbstractButton::clicked,
            this, [this] {
                m_playerCore->mpvCore()->command(
                    {QStringLiteral("playlist-next"),
                     QStringLiteral("force")});
                emit activity();
            });
    connect(m_playButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_muteButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_fullScreenButton, &QAbstractButton::clicked,
            this, &IinaPlayerChrome::activity);
    connect(m_timeline, &IinaTimeline::seekRequested,
            m_playerCore, [this](double percent) {
                m_playerCore->seekPercent(percent, true);
            });
    connect(m_timeline, &IinaTimeline::interaction,
            this, &IinaPlayerChrome::activity);
    connect(m_playerCore, &PlayerCore::stateChanged,
            this, [this](PlayerState) { updatePlaybackState(); });
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
    connect(m_playerCore->mpvCore(), &MpvCore::propertyChanged,
            this, [this](const QString &name, const QVariant &value) {
                if (name == QStringLiteral("mute")) {
                    m_muteButton->setIconType(
                        value.toBool() ? IinaIcon::Muted
                                       : IinaIcon::Volume);
                    m_muteButton->setToolTip(
                        value.toBool() ? tr("Unmute") : tr("Mute"));
                } else if (name == QStringLiteral("volume")
                           && !m_volumeSlider->isSliderDown()) {
                    const QSignalBlocker blocker(m_volumeSlider);
                    m_volumeSlider->setValue(qRound(value.toDouble()));
                }
            });

    m_position = m_playerCore->info().videoPositionSec;
    m_duration = m_playerCore->info().videoDurationSec;
    m_timeline->setPlayback(m_position, m_duration);
    updateTimeLabels();
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
        || event->type() == QEvent::Enter) {
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
    const bool playing =
        m_playerCore->info().state == PlayerState::Playing;
    m_playButton->setIconType(
        playing ? IinaIcon::Pause : IinaIcon::Play);
    m_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
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

#include "UI/Controls/PlaybackFeedback.h"

#include "UI/Design/DesignTokens.h"

#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int osdWidth = 270;
constexpr int osdHeight = 70;
constexpr int previewPaddingX = 10;
constexpr int previewHeight = 28;
}

PlaybackOsd::PlaybackOsd(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playbackOsd"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(osdWidth, osdHeight);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);
    m_animation =
        new QPropertyAnimation(m_opacity, "opacity", this);
    m_animation->setDuration(200);
    m_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_animation, &QPropertyAnimation::finished,
            this, [this] {
                if (m_opacity->opacity() <= 0.001) {
                    hide();
                }
            });

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(1400);
    connect(m_hideTimer, &QTimer::timeout,
            this, &PlaybackOsd::beginFadeOut);
    hide();
}

void PlaybackOsd::showMessage(
    const QString &title, const QString &detail, double progress)
{
    m_title = title;
    m_detail = detail;
    m_progress = progress < 0.0
        ? -1.0
        : std::clamp(progress, 0.0, 1.0);
    update();
    show();
    raise();
    m_animation->stop();
    m_opacity->setOpacity(1.0);
    m_hideTimer->start();
}

void PlaybackOsd::hideNow()
{
    m_hideTimer->stop();
    m_animation->stop();
    m_opacity->setOpacity(0.0);
    hide();
}

void PlaybackOsd::beginFadeOut()
{
    m_animation->stop();
    m_animation->setStartValue(m_opacity->opacity());
    m_animation->setEndValue(0.0);
    m_animation->start();
}

void PlaybackOsd::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    using namespace Supernova::Ui;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(panelBorder, 1.0));
    painter.setBrush(panelFill);
    painter.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    QFont titleFont = font();
    titleFont.setPixelSize(14);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(primaryText);
    painter.drawText(
        QRect(14, 10, width() - 28, 20),
        Qt::AlignLeft | Qt::AlignVCenter, m_title);

    QFont detailFont = font();
    detailFont.setPixelSize(11);
    painter.setFont(detailFont);
    painter.setPen(secondaryText);
    if (!m_detail.isEmpty()) {
        painter.drawText(
            QRect(14, 31, width() - 28, 16),
            Qt::AlignLeft | Qt::AlignVCenter, m_detail);
    }

    if (m_progress >= 0.0) {
        const QRectF track(14, height() - 13, width() - 28, 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(sliderRemaining);
        painter.drawRoundedRect(track, 1.5, 1.5);
        QRectF filled = track;
        filled.setWidth(track.width() * m_progress);
        painter.setBrush(sliderPlayed);
        painter.drawRoundedRect(filled, 1.5, 1.5);
    }
}

TimelinePreview::TimelinePreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("timelinePreview"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void TimelinePreview::showTime(
    double seconds, const QPoint &anchorInParent, int chromeTop)
{
    m_text = formatTime(seconds);
    QFont previewFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    previewFont.setPixelSize(11);
    const QFontMetrics metrics(previewFont);
    const int previewWidth =
        std::max(48, metrics.horizontalAdvance(m_text)
                         + previewPaddingX * 2);
    resize(previewWidth, previewHeight);
    const int parentWidth = parentWidget()
        ? parentWidget()->width() : previewWidth;
    const int x = std::clamp(
        anchorInParent.x() - previewWidth / 2,
        4, std::max(4, parentWidth - previewWidth - 4));
    const int y = std::max(4, chromeTop - previewHeight - 5);
    move(x, y);
    show();
    raise();
    update();
}

void TimelinePreview::dismiss()
{
    hide();
}

void TimelinePreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    using namespace Supernova::Ui;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(panelBorder, 1.0));
    painter.setBrush(panelFill);
    painter.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    QFont previewFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    previewFont.setPixelSize(11);
    painter.setFont(previewFont);
    painter.setPen(primaryText);
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

QString TimelinePreview::formatTime(double seconds)
{
    const qint64 total = qRound64(std::max(0.0, seconds));
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 remaining = total % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3")
              .arg(hours)
              .arg(minutes, 2, 10, QLatin1Char('0'))
              .arg(remaining, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2")
              .arg(minutes)
              .arg(remaining, 2, 10, QLatin1Char('0'));
}

BufferingIndicator::BufferingIndicator(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bufferingIndicator"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(190, 88);
    m_spinnerTimer = new QTimer(this);
    m_spinnerTimer->setInterval(33);
    connect(m_spinnerTimer, &QTimer::timeout, this, [this] {
        m_spinnerAngle = (m_spinnerAngle + 12) % 360;
        update();
    });
    hide();
}

void BufferingIndicator::updateStatus(
    const BufferingInfo &buffering, bool seeking)
{
    m_buffering = buffering;
    m_seeking = seeking;
    const bool visible = buffering.active || seeking;
    if (visible) {
        if (!m_spinnerTimer->isActive()) {
            m_spinnerTimer->start();
        }
        show();
        raise();
        update();
    } else {
        m_spinnerTimer->stop();
        hide();
    }
}

void BufferingIndicator::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    using namespace Supernova::Ui;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(panelBorder, 1.0));
    painter.setBrush(panelFill);
    painter.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QRectF spinnerRect(18, 18, 22, 22);
    QPen spinnerPen(primaryText, 2.2, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(spinnerPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(
        spinnerRect, (90 - m_spinnerAngle) * 16, -260 * 16);

    QFont titleFont = font();
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(primaryText);
    const QString title = m_buffering.active
        ? tr("Buffering… %1%").arg(m_buffering.percent)
        : tr("Seeking…");
    painter.drawText(
        QRect(51, 14, width() - 63, 28),
        Qt::AlignLeft | Qt::AlignVCenter, title);

    QString detail;
    if (m_buffering.active) {
        detail = tr("%1 · %2/s")
            .arg(formatBytes(m_buffering.cacheUsedBytes),
                 formatBytes(m_buffering.cacheSpeedBytesPerSecond));
    }
    QFont detailFont = font();
    detailFont.setPixelSize(10);
    painter.setFont(detailFont);
    painter.setPen(secondaryText);
    painter.drawText(
        QRect(14, 52, width() - 28, 20),
        Qt::AlignCenter, detail);
}

QString BufferingIndicator::formatBytes(qint64 bytes)
{
    static constexpr const char *units[] = {
        "B", "KiB", "MiB", "GiB"};
    double value = std::max<qint64>(0, bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0
        ? QStringLiteral("%1 %2").arg(qRound64(value)).arg(units[unit])
        : QStringLiteral("%1 %2")
              .arg(value, 0, 'f', value < 10.0 ? 1 : 0)
              .arg(units[unit]);
}

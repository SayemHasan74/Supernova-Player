#include "UI/Controls/PlaybackFeedback.h"

#include "UI/Design/DesignTokens.h"

#include <QFontDatabase>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int previewPaddingX = 10;
constexpr int previewHeight = 28;
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
    showPreview(seconds, anchorInParent, chromeTop, {});
}

void TimelinePreview::showPreview(
    double seconds, const QPoint &anchorInParent, int chromeTop,
    const QImage &image)
{
    m_text = formatTime(seconds);
    m_image = image;
    QFont previewFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    previewFont.setPixelSize(11);
    const QFontMetrics metrics(previewFont);
    const int imageWidth = m_image.isNull()
        ? 0 : std::clamp(m_image.width() / 2, 96, 180);
    const int imageHeight = m_image.isNull()
        ? 0
        : qRound(
              static_cast<double>(imageWidth) * m_image.height()
              / std::max(1, m_image.width()));
    const int previewWidth = std::max(
        std::max(48, metrics.horizontalAdvance(m_text)
                         + previewPaddingX * 2),
        imageWidth + (m_image.isNull() ? 0 : 8));
    const int totalHeight = previewHeight
        + (m_image.isNull() ? 0 : imageHeight + 5);
    resize(previewWidth, totalHeight);
    const int parentWidth = parentWidget()
        ? parentWidget()->width() : previewWidth;
    const int x = std::clamp(
        anchorInParent.x() - previewWidth / 2,
        4, std::max(4, parentWidth - previewWidth - 4));
    const int y = std::max(4, chromeTop - totalHeight - 5);
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
    if (!m_image.isNull()) {
        const QRect imageRect =
            rect().adjusted(4, 4, -4, -previewHeight);
        painter.drawImage(imageRect, m_image);
    }
    painter.drawText(
        QRect(0, height() - previewHeight, width(), previewHeight),
        Qt::AlignCenter, m_text);
}

ScreenshotPreview::ScreenshotPreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("screenshotPreview"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(5000);
    connect(m_hideTimer, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void ScreenshotPreview::showScreenshot(
    const QImage &image, const QUrl &fileUrl)
{
    if (image.isNull()) {
        return;
    }
    m_image = image;
    m_filename = fileUrl.fileName();
    const QSize scaled = image.size().scaled(
        QSize(300, 200), Qt::KeepAspectRatio);
    resize(scaled.width() + 16, scaled.height() + 42);
    if (parentWidget()) {
        move(
            std::max(8, parentWidget()->width() - width() - 22),
            22);
    }
    show();
    raise();
    update();
    m_hideTimer->start();
}

void ScreenshotPreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    using namespace Supernova::Ui;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(panelBorder, 1.0));
    painter.setBrush(panelFill);
    painter.drawRoundedRect(
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
    painter.drawImage(
        rect().adjusted(8, 8, -8, -34), m_image);
    painter.setPen(primaryText);
    painter.drawText(
        QRect(10, height() - 28, width() - 20, 20),
        Qt::AlignLeft | Qt::AlignVCenter,
        m_filename.isEmpty() ? tr("Screenshot captured") : m_filename);
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

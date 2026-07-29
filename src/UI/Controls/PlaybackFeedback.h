#pragma once

#include "PlayerCore/BufferingInfo.h"

#include <QPoint>
#include <QImage>
#include <QUrl>
#include <QWidget>

class QTimer;

class TimelinePreview final : public QWidget {
    Q_OBJECT

public:
    explicit TimelinePreview(QWidget *parent = nullptr);

    void showTime(double seconds, const QPoint &anchorInParent, int chromeTop);
    void showPreview(
        double seconds, const QPoint &anchorInParent, int chromeTop,
        const QImage &image);
    void dismiss();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString formatTime(double seconds);

    QString m_text;
    QImage m_image;
};

class ScreenshotPreview final : public QWidget {
    Q_OBJECT

public:
    explicit ScreenshotPreview(QWidget *parent = nullptr);
    void showScreenshot(const QImage &image, const QUrl &fileUrl);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_filename;
    QTimer *m_hideTimer = nullptr;
};

class BufferingIndicator final : public QWidget {
    Q_OBJECT

public:
    explicit BufferingIndicator(QWidget *parent = nullptr);

    void updateStatus(const BufferingInfo &buffering);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString formatBytes(qint64 bytes);

    BufferingInfo m_buffering;
    int m_spinnerAngle = 0;
    QTimer *m_spinnerTimer = nullptr;
};

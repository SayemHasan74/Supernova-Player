#pragma once

#include "PlayerCore/BufferingInfo.h"

#include <QPoint>
#include <QWidget>

class QTimer;

class TimelinePreview final : public QWidget {
    Q_OBJECT

public:
    explicit TimelinePreview(QWidget *parent = nullptr);

    void showTime(double seconds, const QPoint &anchorInParent, int chromeTop);
    void dismiss();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString formatTime(double seconds);

    QString m_text;
};

class BufferingIndicator final : public QWidget {
    Q_OBJECT

public:
    explicit BufferingIndicator(QWidget *parent = nullptr);

    void updateStatus(
        const BufferingInfo &buffering, bool seeking);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString formatBytes(qint64 bytes);

    BufferingInfo m_buffering;
    bool m_seeking = false;
    int m_spinnerAngle = 0;
    QTimer *m_spinnerTimer = nullptr;
};

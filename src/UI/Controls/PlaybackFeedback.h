#pragma once

#include "PlayerCore/BufferingInfo.h"

#include <QPoint>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

class PlaybackOsd final : public QWidget {
    Q_OBJECT

public:
    explicit PlaybackOsd(QWidget *parent = nullptr);

    void showMessage(
        const QString &title, const QString &detail,
        double progress = -1.0);
    void hideNow();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void beginFadeOut();

    QString m_title;
    QString m_detail;
    double m_progress = -1.0;
    QGraphicsOpacityEffect *m_opacity = nullptr;
    QPropertyAnimation *m_animation = nullptr;
    QTimer *m_hideTimer = nullptr;
};

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

#pragma once

#include "PlayerCore/PlaybackHistory.h"
#include "PlayerCore/RecentMedia.h"

#include <QUrl>
#include <QWidget>

class QListWidget;
class QPushButton;

class WelcomeView final : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeView(QWidget *parent = nullptr);

    void setHistory(const QList<PlaybackHistoryEntry> &history);
    void setRecentMedia(const QList<RecentMediaEntry> &recent);

signals:
    void openFileRequested();
    void openUrlRequested();
    void historyRequested(const QUrl &url);
    void showHistoryRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void activateCurrentItem();
    void rebuildMedia();

    QPushButton *m_resumeButton = nullptr;
    QPushButton *m_historyButton = nullptr;
    QListWidget *m_recentList = nullptr;
    QList<PlaybackHistoryEntry> m_history;
    QList<RecentMediaEntry> m_recentMedia;
    QList<PlaybackHistoryEntry> m_visibleHistory;
    bool m_hasRecentMedia = false;
};

#pragma once

#include "PlayerCore/PlaylistState.h"
#include "PlayerCore/PlaybackHistory.h"
#include "PlayerCore/NavigationState.h"

#include <QList>
#include <QHash>
#include <QPair>
#include <QUrl>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class PlaylistPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistPanel(QWidget *parent = nullptr);

    void setPlaylist(const PlaylistState &playlist);
    void setPlaybackPosition(double seconds);
    void setPlaybackDuration(double seconds);
    void setHistory(const QList<PlaybackHistoryEntry> &history);
    void setChapters(
        const QList<PlaybackChapter> &chapters, int currentChapter);
    void setCurrentChapter(int index);

signals:
    void closeRequested();
    void addRequested();
    void addUrlRequested();
    void importRequested();
    void exportRequested();
    void removeRequested(const QList<int> &indexes);
    void clearRequested();
    void playRequested(int index);
    void playNextRequested(const QList<int> &indexes);
    void moveRequested(const QList<int> &indexes, int destination);
    void urlsDropped(const QList<QUrl> &urls, int destination);
    void loopRequested();
    void shuffleRequested();
    void sortRequested(PlaylistSortOrder order);
    void chapterRequested(int index);
    void historyRequested(const QUrl &url);
    void removeHistoryRequested(const QStringList &keys);
    void clearHistoryRequested();

private:
    [[nodiscard]] QList<int> selectedIndexes() const;
    void showContextMenu(const QPoint &position);
    void showSortMenu();

    QListWidget *m_list = nullptr;
    QListWidget *m_chapterList = nullptr;
    QListWidget *m_historyList = nullptr;
    QWidget *m_playlistFooter = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_loopButton = nullptr;
    QPushButton *m_shuffleButton = nullptr;
    PlaylistState m_playlist;
    QHash<qint64, QPair<double, double>> m_progressById;
    double m_currentPosition = 0.0;
    double m_currentDuration = 0.0;
    QList<PlaybackHistoryEntry> m_history;
    QList<PlaybackChapter> m_chapters;
    int m_currentChapter = -1;
};

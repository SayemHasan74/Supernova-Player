#pragma once

#include "PlayerCore/PlaylistState.h"

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

private:
    [[nodiscard]] QList<int> selectedIndexes() const;
    void showContextMenu(const QPoint &position);
    void showSortMenu();

    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_loopButton = nullptr;
    QPushButton *m_shuffleButton = nullptr;
    PlaylistState m_playlist;
    QHash<qint64, QPair<double, double>> m_progressById;
    double m_currentPosition = 0.0;
    double m_currentDuration = 0.0;
};

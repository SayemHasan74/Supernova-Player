#pragma once

#include "PlayerCore/PlaybackHistory.h"

#include <QDialog>
#include <QList>
#include <QUrl>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class HistoryWindow final : public QDialog {
    Q_OBJECT

public:
    explicit HistoryWindow(QWidget *parent = nullptr);

    void setHistory(const QList<PlaybackHistoryEntry> &history);

signals:
    void playRequested(const QList<QUrl> &urls);
    void removeRequested(const QStringList &keys);
    void clearRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum class GroupMode {
        LastPlayed,
        Location,
    };

    void rebuild();
    void playSelection();
    void revealSelection();
    void moveSelectionToTrash();
    void removeSelection();
    void clearHistory();
    void showContextMenu(const QPoint &position);
    [[nodiscard]] QList<QTreeWidgetItem *> selectedEntryItems() const;
    [[nodiscard]] const PlaybackHistoryEntry *entryForItem(
        const QTreeWidgetItem *item) const;

    QComboBox *m_group = nullptr;
    QComboBox *m_searchScope = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_clear = nullptr;
    QList<PlaybackHistoryEntry> m_history;
};

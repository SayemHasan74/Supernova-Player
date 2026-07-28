#include "UI/Playlist/PlaylistPanel.h"

#include "App/MediaSourceResolver.h"
#include "UI/Design/DesignTokens.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {
QString formatTime(double seconds)
{
    const int value = std::max(0, static_cast<int>(seconds));
    const int hours = value / 3600;
    const int minutes = (value % 3600) / 60;
    const int secs = value % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3")
              .arg(hours)
              .arg(minutes, 2, 10, QLatin1Char('0'))
              .arg(secs, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2")
              .arg(minutes)
              .arg(secs, 2, 10, QLatin1Char('0'));
}

class PlaylistRow final : public QWidget {
public:
    explicit PlaylistRow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(3, 3, 5, 2);
        layout->setSpacing(1);
        auto *line = new QHBoxLayout;
        line->setSpacing(5);
        m_pointer = new QLabel(this);
        m_pointer->setFixedWidth(12);
        m_title = new QLabel(this);
        m_title->setTextInteractionFlags(Qt::NoTextInteraction);
        m_title->setSizePolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_duration = new QLabel(this);
        m_duration->setStyleSheet(
            QStringLiteral("color: rgba(235,235,245,155);"));
        QFont durationFont = m_duration->font();
        durationFont.setStyleHint(QFont::Monospace);
        durationFont.setFixedPitch(true);
        durationFont.setPixelSize(11);
        m_duration->setFont(durationFont);
        line->addWidget(m_pointer);
        line->addWidget(m_title, 1);
        line->addWidget(m_duration);
        layout->addLayout(line);
        m_progress = new QProgressBar(this);
        m_progress->setTextVisible(false);
        m_progress->setRange(0, 1000);
        m_progress->setFixedHeight(3);
        m_progress->setStyleSheet(QStringLiteral(
            "QProgressBar { border: 0; background: rgba(255,255,255,22);"
            " border-radius: 1px; }"
            "QProgressBar::chunk { background: rgb(67,137,225);"
            " border-radius: 1px; }"));
        layout->addWidget(m_progress);
    }

    void setEntry(
        const PlaylistItem &entry, double position, double duration)
    {
        m_pointer->setText(
            entry.playing || entry.current
                ? QStringLiteral("▶") : QString());
        m_title->setText(entry.displayName);
        m_title->setToolTip(entry.url.toDisplayString());
        QFont font = m_title->font();
        font.setWeight(
            entry.playing || entry.current
                ? QFont::DemiBold : QFont::Normal);
        m_title->setFont(font);
        setProgress(position, duration);
    }

    void setProgress(double position, double duration)
    {
        if (duration > 0.0) {
            m_duration->setText(formatTime(duration));
            m_progress->setValue(static_cast<int>(
                std::clamp(position / duration, 0.0, 1.0) * 1000.0));
            m_progress->show();
        } else {
            m_duration->clear();
            m_progress->setValue(0);
            m_progress->hide();
        }
    }

private:
    QLabel *m_pointer = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_duration = nullptr;
    QProgressBar *m_progress = nullptr;
};

class PlaylistList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(const QList<int> &, int)> moveHandler;
    std::function<void(const QList<QUrl> &, int)> urlHandler;
    std::function<void()> deleteHandler;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Delete
            || event->key() == Qt::Key_Backspace) {
            if (deleteHandler) {
                deleteHandler();
            }
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        int destination = indexAt(event->position().toPoint()).row();
        if (destination < 0) {
            destination = count();
        }
        if (event->source() == this) {
            QList<int> rows;
            for (const QModelIndex &index :
                 selectionModel()->selectedRows()) {
                rows.append(index.row());
            }
            std::sort(rows.begin(), rows.end());
            if (moveHandler) {
                moveHandler(rows, destination);
            }
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        const QList<QUrl> urls =
            MediaSourceResolver::fromMimeData(event->mimeData());
        if (!urls.isEmpty() && urlHandler) {
            urlHandler(urls, destination);
            event->acceptProposedAction();
            return;
        }
        event->ignore();
    }
};

QPushButton *toolButton(
    const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFixedSize(27, 25);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}
}

PlaylistPanel::PlaylistPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playlistPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(305);
    setMaximumWidth(420);
    setStyleSheet(QStringLiteral(
        "#playlistPanel { background: rgba(25,25,28,246);"
        " border-left: 1px solid rgba(255,255,255,35); }"
        "QLabel { color: rgb(240,240,244); }"
        "QListWidget { background: transparent; color: rgb(238,238,242);"
        " border: 0; outline: 0; padding: 3px; }"
        "QListWidget::item { height: 39px; padding: 1px 4px;"
        " border-bottom: 1px solid rgba(255,255,255,14); }"
        "QListWidget::item:selected { background: rgba(73,112,169,155);"
        " border-radius: 5px; }"
        "QPushButton { color: rgb(240,240,244); background: transparent;"
        " border: 0; border-radius: 5px; }"
        "QPushButton:hover { background: rgba(255,255,255,28); }"
        "QPushButton:pressed { background: rgba(255,255,255,42); }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 9, 10, 9);
    layout->setSpacing(7);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Playlist"), this);
    QFont titleFont = title->font();
    titleFont.setPixelSize(14);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    header->addWidget(title);
    auto *playlistTab = toolButton(
        QStringLiteral("☷"), tr("Playlist"), this);
    auto *chaptersTab = toolButton(
        QStringLiteral("≡"), tr("Chapters"), this);
    auto *historyTab = toolButton(
        QStringLiteral("◷"), tr("History"), this);
    header->addWidget(playlistTab);
    header->addWidget(chaptersTab);
    header->addWidget(historyTab);
    header->addStretch();
    auto *closeButton = toolButton(
        QStringLiteral("×"), tr("Close Playlist"), this);
    header->addWidget(closeButton);
    layout->addLayout(header);

    auto *list = new PlaylistList(this);
    m_list = list;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->moveHandler = [this](const QList<int> &rows, int destination) {
        emit moveRequested(rows, destination);
    };
    list->urlHandler = [this](const QList<QUrl> &urls, int destination) {
        emit urlsDropped(urls, destination);
    };
    list->deleteHandler = [this] {
        const QList<int> selected = selectedIndexes();
        if (!selected.isEmpty()) {
            emit removeRequested(selected);
        }
    };
    layout->addWidget(m_list, 1);

    m_chapterList = new QListWidget(this);
    m_chapterList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_chapterList->hide();
    layout->addWidget(m_chapterList, 1);

    m_historyList = new QListWidget(this);
    m_historyList->setSelectionMode(
        QAbstractItemView::ExtendedSelection);
    m_historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyList->hide();
    layout->addWidget(m_historyList, 1);

    m_playlistFooter = new QWidget(this);
    auto *footer = new QHBoxLayout(m_playlistFooter);
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(3);
    m_loopButton = toolButton(
        QStringLiteral("↻"), tr("Cycle Loop Mode"), this);
    m_shuffleButton = toolButton(
        QStringLiteral("⇄"), tr("Shuffle Playlist"), this);
    auto *sortButton = toolButton(
        QStringLiteral("⇅"), tr("Sort Playlist"), this);
    m_summary = new QLabel(tr("0 items"), this);
    m_summary->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,150);"));
    auto *addButton = toolButton(
        QStringLiteral("+"), tr("Add Media"), this);
    auto *removeButton = toolButton(
        QStringLiteral("−"), tr("Remove Selected"), this);
    auto *moreButton = toolButton(
        QStringLiteral("•••"), tr("Playlist Actions"), this);
    footer->addWidget(m_loopButton);
    footer->addWidget(m_shuffleButton);
    footer->addWidget(sortButton);
    footer->addStretch();
    footer->addWidget(m_summary);
    footer->addStretch();
    footer->addWidget(addButton);
    footer->addWidget(removeButton);
    footer->addWidget(moreButton);
    layout->addWidget(m_playlistFooter);

    const auto showPage = [this, title](
                              int page, const QString &pageTitle) {
        title->setText(pageTitle);
        m_list->setVisible(page == 0);
        m_playlistFooter->setVisible(page == 0);
        m_chapterList->setVisible(page == 1);
        m_historyList->setVisible(page == 2);
    };
    connect(playlistTab, &QPushButton::clicked, this,
            [showPage] { showPage(0, QObject::tr("Playlist")); });
    connect(chaptersTab, &QPushButton::clicked, this,
            [showPage] { showPage(1, QObject::tr("Chapters")); });
    connect(historyTab, &QPushButton::clicked, this,
            [showPage] { showPage(2, QObject::tr("History")); });

    connect(closeButton, &QPushButton::clicked,
            this, &PlaylistPanel::closeRequested);
    connect(addButton, &QPushButton::clicked, this, [this, addButton] {
        QMenu menu(this);
        QAction *fileAction = menu.addAction(tr("Add File…"));
        QAction *urlAction = menu.addAction(tr("Add URL…"));
        QAction *chosen = menu.exec(
            addButton->mapToGlobal(QPoint(0, addButton->height())));
        if (chosen == fileAction) {
            emit addRequested();
        } else if (chosen == urlAction) {
            emit addUrlRequested();
        }
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        emit removeRequested(selectedIndexes());
    });
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton] {
        QMenu menu(this);
        QAction *importAction = menu.addAction(tr("Import Playlist…"));
        QAction *exportAction = menu.addAction(tr("Save Playlist…"));
        exportAction->setEnabled(!m_playlist.isEmpty());
        menu.addSeparator();
        QAction *clearAction = menu.addAction(tr("Clear Playlist"));
        clearAction->setEnabled(!m_playlist.isEmpty());
        QAction *chosen = menu.exec(
            moreButton->mapToGlobal(
                QPoint(0, moreButton->height())));
        if (chosen == importAction) {
            emit importRequested();
        } else if (chosen == exportAction) {
            emit exportRequested();
        } else if (chosen == clearAction) {
            emit clearRequested();
        }
    });
    connect(m_loopButton, &QPushButton::clicked,
            this, &PlaylistPanel::loopRequested);
    connect(m_shuffleButton, &QPushButton::clicked,
            this, &PlaylistPanel::shuffleRequested);
    connect(sortButton, &QPushButton::clicked,
            this, &PlaylistPanel::showSortMenu);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
                emit playRequested(m_list->row(item));
            });
    connect(m_list, &QWidget::customContextMenuRequested,
            this, &PlaylistPanel::showContextMenu);
    connect(m_chapterList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
                emit chapterRequested(
                    item->data(Qt::UserRole).toInt());
            });
    connect(m_historyList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
                emit historyRequested(
                    item->data(Qt::UserRole + 1).toUrl());
            });
    connect(m_historyList, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &position) {
                if (QListWidgetItem *clicked =
                        m_historyList->itemAt(position);
                    clicked && !clicked->isSelected()) {
                    m_historyList->clearSelection();
                    clicked->setSelected(true);
                }
                QMenu menu(this);
                QAction *play = menu.addAction(tr("Play"));
                play->setEnabled(m_historyList->currentItem());
                QAction *remove =
                    menu.addAction(tr("Remove from History"));
                remove->setEnabled(
                    !m_historyList->selectedItems().isEmpty());
                menu.addSeparator();
                QAction *clear = menu.addAction(tr("Clear History"));
                clear->setEnabled(!m_history.isEmpty());
                QAction *chosen = menu.exec(
                    m_historyList->viewport()->mapToGlobal(position));
                if (chosen == play && m_historyList->currentItem()) {
                    emit historyRequested(
                        m_historyList->currentItem()
                            ->data(Qt::UserRole + 1).toUrl());
                } else if (chosen == remove) {
                    QStringList keys;
                    for (QListWidgetItem *item :
                         m_historyList->selectedItems()) {
                        keys.append(
                            item->data(Qt::UserRole).toString());
                    }
                    emit removeHistoryRequested(keys);
                } else if (chosen == clear) {
                    emit clearHistoryRequested();
                }
            });
    hide();
}

void PlaylistPanel::setPlaylist(const PlaylistState &playlist)
{
    QList<qint64> selectedIds;
    for (QListWidgetItem *item : m_list->selectedItems()) {
        selectedIds.append(item->data(Qt::UserRole).toLongLong());
    }
    m_playlist = playlist;
    m_list->clear();
    for (const PlaylistItem &entry : playlist.items) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.url.toDisplayString());
        auto *row = new PlaylistRow(m_list);
        const double position = entry.current
            ? m_currentPosition : entry.historyPositionSec;
        const double duration = entry.current
            ? std::max(m_currentDuration, entry.historyDurationSec)
            : entry.historyDurationSec;
        row->setEntry(entry, position, duration);
        m_list->setItemWidget(item, row);
        item->setSelected(selectedIds.contains(entry.id));
    }
    m_summary->setText(
        playlist.currentIndex >= 0
            ? tr("%1 of %2")
                  .arg(playlist.currentIndex + 1)
                  .arg(playlist.size())
            : tr("%n item(s)", nullptr, playlist.size()));
    m_loopButton->setText(
        playlist.loopMode == PlaylistLoopMode::File
            ? QStringLiteral("↻1")
            : playlist.loopMode == PlaylistLoopMode::Playlist
                ? QStringLiteral("↻●") : QStringLiteral("↻"));
    if (playlist.currentIndex >= 0
        && playlist.currentIndex < m_list->count()) {
        m_list->scrollToItem(m_list->item(playlist.currentIndex));
        setPlaybackDuration(m_currentDuration);
        setPlaybackPosition(m_currentPosition);
    }
}

void PlaylistPanel::setHistory(
    const QList<PlaybackHistoryEntry> &history)
{
    m_history = history;
    m_historyList->clear();
    for (const PlaybackHistoryEntry &entry : history) {
        auto *item = new QListWidgetItem(m_historyList);
        item->setData(Qt::UserRole, entry.key);
        item->setData(Qt::UserRole + 1, entry.url);
        const QString name = !entry.title.isEmpty()
            ? entry.title : entry.displayName;
        const QString progress = entry.durationSec > 0.0
            ? tr("%1 of %2")
                  .arg(formatTime(entry.positionSec),
                       formatTime(entry.durationSec))
            : QString();
        item->setText(progress.isEmpty()
            ? name : QStringLiteral("%1\n%2").arg(name, progress));
        item->setToolTip(entry.url.toDisplayString());
        item->setSizeHint(QSize(item->sizeHint().width(), 45));
    }
}

void PlaylistPanel::setChapters(
    const QList<PlaybackChapter> &chapters, int currentChapter)
{
    m_chapters = chapters;
    m_currentChapter = currentChapter;
    m_chapterList->clear();
    for (const PlaybackChapter &chapter : chapters) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2")
                .arg(formatTime(chapter.startTimeSec), chapter.title),
            m_chapterList);
        item->setData(Qt::UserRole, chapter.index);
        if (chapter.index == currentChapter) {
            item->setText(QStringLiteral("▶  %1").arg(item->text()));
            QFont font = item->font();
            font.setWeight(QFont::DemiBold);
            item->setFont(font);
            m_chapterList->setCurrentItem(item);
        }
    }
}

void PlaylistPanel::setCurrentChapter(int index)
{
    setChapters(m_chapters, index);
}

void PlaylistPanel::setPlaybackPosition(double seconds)
{
    m_currentPosition = std::max(0.0, seconds);
    const int index = m_playlist.currentIndex;
    if (index < 0 || index >= m_playlist.size()) {
        return;
    }
    const qint64 id = m_playlist.items[index].id;
    m_progressById[id] = {m_currentPosition, m_currentDuration};
    if (auto *row = dynamic_cast<PlaylistRow *>(
            m_list->itemWidget(m_list->item(index)))) {
        row->setProgress(m_currentPosition, m_currentDuration);
    }
}

void PlaylistPanel::setPlaybackDuration(double seconds)
{
    m_currentDuration = std::max(0.0, seconds);
    setPlaybackPosition(m_currentPosition);
}

QList<int> PlaylistPanel::selectedIndexes() const
{
    QList<int> indexes;
    for (const QModelIndex &index :
         m_list->selectionModel()->selectedRows()) {
        indexes.append(index.row());
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

void PlaylistPanel::showContextMenu(const QPoint &position)
{
    if (QListWidgetItem *clicked = m_list->itemAt(position);
        clicked && !clicked->isSelected()) {
        m_list->clearSelection();
        clicked->setSelected(true);
        m_list->setCurrentItem(clicked);
    }
    const QList<int> selected = selectedIndexes();
    QMenu menu(this);
    if (!selected.isEmpty()) {
        const PlaylistItem &first = m_playlist.items[selected.constFirst()];
        QAction *heading = menu.addAction(
            selected.size() == 1
                ? first.displayName
                : tr("%n Items", nullptr, selected.size()));
        heading->setEnabled(false);
        menu.addSeparator();
        QAction *play = menu.addAction(tr("Play"));
        QAction *playNext = menu.addAction(tr("Play Next"));
        QAction *remove = menu.addAction(
            selected.size() == 1
                ? tr("Remove from Playlist")
                : tr("Remove %n Items", nullptr, selected.size()));
        menu.addSeparator();
        QAction *openLocation = nullptr;
        QAction *copyLocation = nullptr;
        if (first.networkResource) {
            openLocation = menu.addAction(tr("Open in Browser"));
            copyLocation = menu.addAction(tr("Copy URL"));
        } else {
            openLocation = menu.addAction(tr("Show in File Explorer"));
            copyLocation = menu.addAction(tr("Copy Path"));
        }
        menu.addSeparator();
        QAction *addFile = menu.addAction(tr("Add File…"));
        QAction *addUrl = menu.addAction(tr("Add URL…"));
        QAction *clear = menu.addAction(tr("Clear Playlist"));
        QAction *chosen = menu.exec(
            m_list->viewport()->mapToGlobal(position));
        if (chosen == play) {
            emit playRequested(selected.constFirst());
        } else if (chosen == playNext) {
            emit playNextRequested(selected);
        } else if (chosen == remove) {
            emit removeRequested(selected);
        } else if (chosen == openLocation) {
            if (first.networkResource) {
                QDesktopServices::openUrl(first.url);
            } else {
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(first.url.toLocalFile()).absolutePath()));
            }
        } else if (chosen == copyLocation) {
            QStringList locations;
            for (int index : selected) {
                const QUrl &url = m_playlist.items[index].url;
                locations.append(
                    url.isLocalFile()
                        ? QDir::toNativeSeparators(url.toLocalFile())
                        : url.toString(QUrl::FullyEncoded));
            }
            QApplication::clipboard()->setText(
                locations.join(QLatin1Char('\n')));
        } else if (chosen == addFile) {
            emit addRequested();
        } else if (chosen == addUrl) {
            emit addUrlRequested();
        } else if (chosen == clear) {
            emit clearRequested();
        }
        return;
    }

    QAction *add = menu.addAction(tr("Add File…"));
    QAction *urlAction = menu.addAction(tr("Add URL…"));
    QAction *importAction = menu.addAction(tr("Import Playlist…"));
    QAction *clear = menu.addAction(tr("Clear Playlist"));
    clear->setEnabled(!m_playlist.isEmpty());
    QAction *chosen = menu.exec(
        m_list->viewport()->mapToGlobal(position));
    if (chosen == add) {
        emit addRequested();
    } else if (chosen == urlAction) {
        emit addUrlRequested();
    } else if (chosen == importAction) {
        emit importRequested();
    } else if (chosen == clear) {
        emit clearRequested();
    }
}

void PlaylistPanel::showSortMenu()
{
    QMenu menu(this);
    QAction *nameAsc = menu.addAction(tr("Name Ascending"));
    QAction *nameDesc = menu.addAction(tr("Name Descending"));
    menu.addSeparator();
    QAction *pathAsc = menu.addAction(tr("Path Ascending"));
    QAction *pathDesc = menu.addAction(tr("Path Descending"));
    QAction *chosen = menu.exec(QCursor::pos());
    if (chosen == nameAsc) {
        emit sortRequested(PlaylistSortOrder::NameAscending);
    } else if (chosen == nameDesc) {
        emit sortRequested(PlaylistSortOrder::NameDescending);
    } else if (chosen == pathAsc) {
        emit sortRequested(PlaylistSortOrder::PathAscending);
    } else if (chosen == pathDesc) {
        emit sortRequested(PlaylistSortOrder::PathDescending);
    }
}

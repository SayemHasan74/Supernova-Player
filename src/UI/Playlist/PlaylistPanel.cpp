#include "UI/Playlist/PlaylistPanel.h"

#include "App/MediaSourceResolver.h"
#include "UI/Design/DesignTokens.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QCursor>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {
class PlaylistList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(const QList<int> &, int)> moveHandler;
    std::function<void(const QList<QUrl> &, int)> urlHandler;

protected:
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
            event->acceptProposedAction();
            return;
        }
        QList<QUrl> urls =
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
    setMinimumWidth(285);
    setMaximumWidth(370);
    setStyleSheet(QStringLiteral(
        "#playlistPanel { background: rgba(25,25,28,246);"
        " border-left: 1px solid rgba(255,255,255,35); }"
        "QLabel { color: rgb(240,240,244); }"
        "QListWidget { background: transparent; color: rgb(238,238,242);"
        " border: 0; outline: 0; padding: 4px; }"
        "QListWidget::item { height: 32px; padding: 2px 7px;"
        " border-bottom: 1px solid rgba(255,255,255,16); }"
        "QListWidget::item:selected { background: rgba(75,115,175,150);"
        " border-radius: 5px; }"
        "QPushButton { color: rgb(240,240,244); background: transparent;"
        " border: 0; border-radius: 5px; }"
        "QPushButton:hover { background: rgba(255,255,255,28); }"
        "QPushButton:checked { background: rgba(95,135,195,150); }"));

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
    layout->addWidget(m_list, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(3);
    m_loopButton = toolButton(
        QStringLiteral("↻"), tr("Cycle Loop Mode"), this);
    m_shuffleButton = toolButton(
        QStringLiteral("⇄"), tr("Shuffle / Unshuffle"), this);
    auto *sortButton = toolButton(
        QStringLiteral("⇅"), tr("Sort Playlist"), this);
    m_summary = new QLabel(tr("0 items"), this);
    m_summary->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,150);"));
    auto *addButton = toolButton(
        QStringLiteral("+"), tr("Add Media"), this);
    auto *removeButton = toolButton(
        QStringLiteral("−"), tr("Remove Selected"), this);
    auto *clearButton = toolButton(
        QStringLiteral("⌫"), tr("Clear Queue"), this);
    footer->addWidget(m_loopButton);
    footer->addWidget(m_shuffleButton);
    footer->addWidget(sortButton);
    footer->addStretch();
    footer->addWidget(m_summary);
    footer->addStretch();
    footer->addWidget(addButton);
    footer->addWidget(removeButton);
    footer->addWidget(clearButton);
    layout->addLayout(footer);

    connect(closeButton, &QPushButton::clicked,
            this, &PlaylistPanel::closeRequested);
    connect(addButton, &QPushButton::clicked,
            this, &PlaylistPanel::addRequested);
    connect(removeButton, &QPushButton::clicked,
            this, [this] { emit removeRequested(selectedIndexes()); });
    connect(clearButton, &QPushButton::clicked,
            this, &PlaylistPanel::clearRequested);
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
        QString text = entry.displayName;
        if (entry.current || entry.playing) {
            text.prepend(QStringLiteral("▶  "));
        }
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.url.toDisplayString());
        if (entry.current || entry.playing) {
            QFont font = item->font();
            font.setWeight(QFont::DemiBold);
            item->setFont(font);
            item->setForeground(Supernova::Ui::primaryText);
        }
        item->setSelected(selectedIds.contains(entry.id));
    }
    m_summary->setText(
        tr("%n item(s)", nullptr, playlist.size()));
    m_loopButton->setChecked(
        playlist.loopMode != PlaylistLoopMode::Off);
    m_loopButton->setText(
        playlist.loopMode == PlaylistLoopMode::File
            ? QStringLiteral("↻1") : QStringLiteral("↻"));
    m_shuffleButton->setChecked(playlist.shuffled);
    if (playlist.currentIndex >= 0) {
        m_list->scrollToItem(m_list->item(playlist.currentIndex));
    }
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
    const QList<int> selected = selectedIndexes();
    if (selected.isEmpty()) {
        return;
    }
    QMenu menu(this);
    QAction *play = menu.addAction(tr("Play"));
    QAction *playNext = menu.addAction(tr("Play Next"));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove from Playlist"));
    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
    if (chosen == play) {
        emit playRequested(selected.constFirst());
    } else if (chosen == playNext) {
        emit playNextRequested(selected);
    } else if (chosen == remove) {
        emit removeRequested(selected);
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

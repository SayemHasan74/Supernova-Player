#include "UI/History/HistoryWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMap>
#include <QPalette>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>

namespace {
constexpr int historyKeyRole = Qt::UserRole;
constexpr int historyUrlRole = Qt::UserRole + 1;

QString formatTime(double seconds)
{
    const int value = std::max(0, qRound(seconds));
    const int hours = value / 3600;
    const int minutes = (value % 3600) / 60;
    const int remaining = value % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3")
              .arg(hours)
              .arg(minutes, 2, 10, QLatin1Char('0'))
              .arg(remaining, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2")
              .arg(minutes)
              .arg(remaining, 2, 10, QLatin1Char('0'));
}

QString entryName(const PlaybackHistoryEntry &entry)
{
    return !entry.title.isEmpty() ? entry.title : entry.displayName;
}
}

HistoryWindow::HistoryWindow(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("historyWindow"));
    setWindowTitle(tr("Playback History"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setModal(false);
    resize(820, 560);
    setMinimumSize(520, 300);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(tr("Group by"), this));
    m_group = new QComboBox(this);
    m_group->addItem(tr("Last Played"), static_cast<int>(GroupMode::LastPlayed));
    m_group->addItem(tr("Location"), static_cast<int>(GroupMode::Location));
    toolbar->addWidget(m_group);
    toolbar->addSpacing(8);
    m_searchScope = new QComboBox(this);
    m_searchScope->addItem(tr("Filename"));
    m_searchScope->addItem(tr("Full Path"));
    m_searchScope->setCurrentIndex(1);
    toolbar->addWidget(m_searchScope);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("historySearch"));
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Search playback history"));
    toolbar->addWidget(m_search, 1);
    auto *expand = new QPushButton(tr("Expand All"), this);
    auto *collapse = new QPushButton(tr("Collapse All"), this);
    toolbar->addWidget(expand);
    toolbar->addWidget(collapse);
    m_clear = new QPushButton(tr("Clear…"), this);
    toolbar->addWidget(m_clear);
    root->addLayout(toolbar);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("historyTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(
        {tr("Media"), tr("Progress"), tr("Played At")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 170);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setAlternatingRowColors(false);
    root->addWidget(m_tree, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "#historyWindow { background: rgb(28,29,33); color: rgb(240,240,245); }"
        "QTreeWidget { background: rgb(22,23,27); border: 1px solid rgba(255,255,255,28);"
        " border-radius: 6px; color: rgb(238,238,243); outline: 0; }"
        "QTreeWidget::item { min-height: 28px; }"
        "QTreeWidget::item:selected { background: rgba(75,125,235,105); }"
        "QHeaderView::section { background: rgb(38,39,44); color: rgb(225,225,232);"
        " border: 0; border-right: 1px solid rgba(255,255,255,22); padding: 6px; }"
        "QLineEdit, QComboBox { background: rgb(43,44,50); color: rgb(240,240,245);"
        " border: 1px solid rgba(255,255,255,35); border-radius: 5px; padding: 5px; }"
        "QPushButton { color: rgb(240,240,245); padding: 5px 12px;"
        " border: 1px solid rgba(255,255,255,35); border-radius: 5px;"
        " background: rgb(48,49,55); }"
        "QPushButton:hover { background: rgb(61,62,69); }"));

    connect(m_group, &QComboBox::currentIndexChanged,
            this, &HistoryWindow::rebuild);
    connect(m_searchScope, &QComboBox::currentIndexChanged,
            this, &HistoryWindow::rebuild);
    connect(m_search, &QLineEdit::textChanged,
            this, &HistoryWindow::rebuild);
    connect(m_clear, &QPushButton::clicked,
            this, &HistoryWindow::clearHistory);
    connect(expand, &QPushButton::clicked,
            m_tree, &QTreeWidget::expandAll);
    connect(collapse, &QPushButton::clicked,
            m_tree, &QTreeWidget::collapseAll);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item) {
                if (entryForItem(item)) {
                    playSelection();
                }
            });
    connect(m_tree, &QWidget::customContextMenuRequested,
            this, &HistoryWindow::showContextMenu);
}

void HistoryWindow::setHistory(
    const QList<PlaybackHistoryEntry> &history)
{
    m_history = history;
    m_clear->setEnabled(!m_history.isEmpty());
    rebuild();
}

void HistoryWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Find)) {
        m_search->setFocus();
        m_search->selectAll();
        return;
    }
    if (event->key() == Qt::Key_Delete
        || event->key() == Qt::Key_Backspace) {
        removeSelection();
        return;
    }
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter) {
        playSelection();
        return;
    }
    QDialog::keyPressEvent(event);
}

void HistoryWindow::rebuild()
{
    m_tree->clear();
    const QString search = m_search->text().trimmed();
    const GroupMode mode = static_cast<GroupMode>(
        m_group->currentData().toInt());
    QMap<QString, QList<const PlaybackHistoryEntry *>> groups;
    for (const PlaybackHistoryEntry &entry : std::as_const(m_history)) {
        const QString searchable = m_searchScope->currentIndex() == 0
            ? QStringLiteral("%1 %2")
                  .arg(entry.displayName, entry.title)
            : QStringLiteral("%1 %2 %3")
                  .arg(entry.url.toDisplayString(),
                       entry.displayName, entry.title);
        if (!search.isEmpty()
            && !searchable.contains(search, Qt::CaseInsensitive)) {
            continue;
        }
        QString group;
        if (mode == GroupMode::LastPlayed) {
            const QDate date = entry.lastPlayed.toLocalTime().date();
            group = date == QDate::currentDate()
                ? tr("Today")
                : date == QDate::currentDate().addDays(-1)
                    ? tr("Yesterday")
                    : QLocale().toString(date, QLocale::LongFormat);
        } else {
            group = entry.location.isEmpty() ? tr("Unknown") : entry.location;
        }
        groups[group].append(&entry);
    }

    QStringList groupOrder = groups.keys();
    if (mode == GroupMode::LastPlayed) {
        std::stable_sort(
            groupOrder.begin(), groupOrder.end(),
            [&groups](const QString &left, const QString &right) {
                return groups[left].constFirst()->lastPlayed
                    > groups[right].constFirst()->lastPlayed;
            });
    }

    QFileIconProvider icons;
    for (const QString &groupName : std::as_const(groupOrder)) {
        auto *groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, groupName);
        groupItem->setFirstColumnSpanned(true);
        QFont font = groupItem->font(0);
        font.setBold(true);
        groupItem->setFont(0, font);
        groupItem->setFlags(
            groupItem->flags() & ~Qt::ItemIsSelectable);
        for (const PlaybackHistoryEntry *entry : groups[groupName]) {
            auto *item = new QTreeWidgetItem(groupItem);
            item->setText(0, entryName(*entry));
            item->setText(
                2, QLocale().toString(
                       entry->lastPlayed.toLocalTime(),
                       QLocale::ShortFormat));
            item->setData(0, historyKeyRole, entry->key);
            item->setData(0, historyUrlRole, entry->url);
            item->setToolTip(
                0, QStringLiteral("%1\n%2")
                       .arg(entry->url.toDisplayString(),
                            entry->location));
            item->setIcon(
                0, entry->url.isLocalFile()
                       ? icons.icon(QFileInfo(entry->url.toLocalFile()))
                       : style()->standardIcon(QStyle::SP_DriveNetIcon));
            if (!entry->isAvailable()) {
                for (int column = 0; column < 3; ++column) {
                    item->setForeground(
                        column, palette().color(
                                    QPalette::Disabled,
                                    QPalette::Text));
                }
            }

            auto *progress = new QProgressBar(m_tree);
            progress->setRange(0, 1000);
            progress->setValue(qRound(entry->progressRatio() * 1000.0));
            progress->setTextVisible(true);
            progress->setFormat(
                entry->completed
                    ? tr("Watched")
                    : entry->durationSec > 0.0
                        ? QStringLiteral("%1 / %2")
                              .arg(formatTime(entry->positionSec),
                                   formatTime(entry->durationSec))
                        : QString());
            progress->setFixedHeight(18);
            m_tree->setItemWidget(item, 1, progress);
        }
        groupItem->setExpanded(true);
    }
}

void HistoryWindow::playSelection()
{
    QList<QUrl> urls;
    for (QTreeWidgetItem *item : selectedEntryItems()) {
        if (const PlaybackHistoryEntry *entry = entryForItem(item);
            entry && entry->isAvailable()) {
            urls.append(entry->url);
        }
    }
    if (!urls.isEmpty()) {
        emit playRequested(urls);
    }
}

void HistoryWindow::revealSelection()
{
    const QList<QTreeWidgetItem *> items = selectedEntryItems();
    if (items.isEmpty()) {
        return;
    }
    const PlaybackHistoryEntry *entry = entryForItem(items.constFirst());
    if (!entry || !entry->url.isLocalFile() || !entry->isAvailable()) {
        return;
    }
#ifdef Q_OS_WIN
    QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,"),
         QDir::toNativeSeparators(entry->url.toLocalFile())});
#else
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(
            QFileInfo(entry->url.toLocalFile()).absolutePath()));
#endif
}

void HistoryWindow::removeSelection()
{
    QStringList keys;
    for (QTreeWidgetItem *item : selectedEntryItems()) {
        const QString key = item->data(0, historyKeyRole).toString();
        if (!key.isEmpty()) {
            keys.append(key);
        }
    }
    keys.removeDuplicates();
    if (keys.isEmpty()) {
        return;
    }
    if (QMessageBox::question(
            this, tr("Remove History"),
            tr("Remove the selected %n history item(s)?", nullptr, keys.size()))
        == QMessageBox::Yes) {
        emit removeRequested(keys);
    }
}

void HistoryWindow::moveSelectionToTrash()
{
    QList<const PlaybackHistoryEntry *> entries;
    for (QTreeWidgetItem *item : selectedEntryItems()) {
        if (const PlaybackHistoryEntry *entry = entryForItem(item);
            entry && entry->url.isLocalFile() && entry->isAvailable()) {
            entries.append(entry);
        }
    }
    if (entries.isEmpty()
        || QMessageBox::question(
               this, tr("Move Files to Recycle Bin"),
               tr("Move the selected %n local file(s) to the Recycle Bin?",
                  nullptr, entries.size()))
            != QMessageBox::Yes) {
        return;
    }
    for (const PlaybackHistoryEntry *entry : std::as_const(entries)) {
        QFile file(entry->url.toLocalFile());
        if (!file.moveToTrash()) {
            QMessageBox::warning(
                this, tr("Move to Recycle Bin"),
                tr("Could not move “%1” to the Recycle Bin.")
                    .arg(entry->displayName));
        }
    }
    rebuild();
}

void HistoryWindow::clearHistory()
{
    if (m_history.isEmpty()) {
        return;
    }
    if (QMessageBox::question(
            this, tr("Clear Playback History"),
            tr("Remove every item from playback history? This cannot be undone."))
        == QMessageBox::Yes) {
        emit clearRequested();
    }
}

void HistoryWindow::showContextMenu(const QPoint &position)
{
    if (QTreeWidgetItem *clicked = m_tree->itemAt(position);
        clicked && entryForItem(clicked) && !clicked->isSelected()) {
        m_tree->clearSelection();
        clicked->setSelected(true);
    }
    const QList<QTreeWidgetItem *> selected = selectedEntryItems();
    QMenu menu(this);
    QAction *play = menu.addAction(tr("Play"));
    QAction *reveal = menu.addAction(tr("Show in File Explorer"));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove from History…"));
    QAction *trash = menu.addAction(tr("Move File to Recycle Bin…"));
    menu.addSeparator();
    QAction *clear = menu.addAction(tr("Clear History…"));
    play->setEnabled(!selected.isEmpty());
    remove->setEnabled(!selected.isEmpty());
    clear->setEnabled(!m_history.isEmpty());
    bool canReveal = false;
    if (!selected.isEmpty()) {
        if (const PlaybackHistoryEntry *entry =
                entryForItem(selected.constFirst())) {
            canReveal = entry->url.isLocalFile() && entry->isAvailable();
        }
    }
    reveal->setEnabled(canReveal);
    trash->setEnabled(canReveal);
    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (chosen == play) {
        playSelection();
    } else if (chosen == reveal) {
        revealSelection();
    } else if (chosen == remove) {
        removeSelection();
    } else if (chosen == trash) {
        moveSelectionToTrash();
    } else if (chosen == clear) {
        clearHistory();
    }
}

QList<QTreeWidgetItem *> HistoryWindow::selectedEntryItems() const
{
    QList<QTreeWidgetItem *> entries;
    for (QTreeWidgetItem *item : m_tree->selectedItems()) {
        if (entryForItem(item)) {
            entries.append(item);
        }
    }
    return entries;
}

const PlaybackHistoryEntry *HistoryWindow::entryForItem(
    const QTreeWidgetItem *item) const
{
    if (!item) {
        return nullptr;
    }
    const QString key = item->data(0, historyKeyRole).toString();
    if (key.isEmpty()) {
        return nullptr;
    }
    const auto found = std::find_if(
        m_history.cbegin(), m_history.cend(),
        [&key](const PlaybackHistoryEntry &entry) {
            return entry.key == key;
        });
    return found == m_history.cend() ? nullptr : &*found;
}

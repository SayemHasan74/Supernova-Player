#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>

enum class PlaylistLoopMode {
    Off,
    File,
    Playlist,
};

enum class PlaylistSortOrder {
    NameAscending,
    NameDescending,
    PathAscending,
    PathDescending,
};

struct PlaylistItem {
    qint64 id = 0;
    QUrl url;
    QString title;
    QString displayName;
    bool current = false;
    bool playing = false;
    bool networkResource = false;

    friend bool operator==(
        const PlaylistItem &, const PlaylistItem &) = default;
};

struct PlaylistState {
    QList<PlaylistItem> items;
    int currentIndex = -1;
    PlaylistLoopMode loopMode = PlaylistLoopMode::Off;
    bool shuffled = false;

    [[nodiscard]] bool isEmpty() const noexcept { return items.isEmpty(); }
    [[nodiscard]] int size() const noexcept { return items.size(); }

    friend bool operator==(
        const PlaylistState &, const PlaylistState &) = default;
};

Q_DECLARE_METATYPE(PlaylistItem)
Q_DECLARE_METATYPE(PlaylistState)
Q_DECLARE_METATYPE(PlaylistLoopMode)

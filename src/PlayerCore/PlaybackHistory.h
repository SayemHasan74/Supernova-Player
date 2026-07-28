#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

struct PlaybackHistoryEntry {
    QString key;
    QUrl url;
    QString displayName;
    QString title;
    double positionSec = 0.0;
    double durationSec = 0.0;
    QDateTime lastPlayed;
    bool completed = false;

    [[nodiscard]] double resumePosition() const noexcept;

    friend bool operator==(
        const PlaybackHistoryEntry &,
        const PlaybackHistoryEntry &) = default;
};

class PlaybackHistoryStore final {
public:
    explicit PlaybackHistoryStore(
        const QString &historyFilePath = {});
    ~PlaybackHistoryStore();

    PlaybackHistoryStore(const PlaybackHistoryStore &) = delete;
    PlaybackHistoryStore &operator=(
        const PlaybackHistoryStore &) = delete;

    [[nodiscard]] static QString keyForUrl(const QUrl &url);
    [[nodiscard]] static QString defaultHistoryFilePath();
    [[nodiscard]] static QString defaultWatchLaterDirectory();

    [[nodiscard]] const QList<PlaybackHistoryEntry> &entries() const noexcept
    {
        return m_entries;
    }
    [[nodiscard]] PlaybackHistoryEntry entryFor(
        const QUrl &url) const;
    [[nodiscard]] QString filePath() const { return m_filePath; }

    void recordLoaded(
        const QUrl &url, double durationSec,
        const QString &title = {});
    void updateProgress(
        const QUrl &url, double positionSec, double durationSec,
        bool reachedEnd = false);
    void remove(const QStringList &keys);
    void clear();
    bool save();

private:
    void load();
    PlaybackHistoryEntry &ensureEntry(const QUrl &url);
    void sortNewestFirst();

    QString m_filePath;
    QList<PlaybackHistoryEntry> m_entries;
    bool m_dirty = false;
};

Q_DECLARE_METATYPE(PlaybackHistoryEntry)
Q_DECLARE_METATYPE(QList<PlaybackHistoryEntry>)

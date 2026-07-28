#include "PlayerCore/PlaybackHistory.h"

#include "Core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double minimumResumePositionSec = 5.0;
constexpr double completionMarginSec = 10.0;
}

double PlaybackHistoryEntry::resumePosition() const noexcept
{
    if (completed || positionSec < minimumResumePositionSec
        || durationSec <= 0.0
        || positionSec >= durationSec - completionMarginSec) {
        return 0.0;
    }
    return positionSec;
}

PlaybackHistoryStore::PlaybackHistoryStore(
    const QString &historyFilePath)
    : m_filePath(historyFilePath.isEmpty()
                     ? defaultHistoryFilePath()
                     : historyFilePath)
{
    load();
}

PlaybackHistoryStore::~PlaybackHistoryStore()
{
    save();
}

QString PlaybackHistoryStore::defaultHistoryFilePath()
{
    const QString directory =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
    return QDir(directory).filePath(QStringLiteral("history.json"));
}

QString PlaybackHistoryStore::defaultWatchLaterDirectory()
{
    const QString directory =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
    return QDir(directory).filePath(QStringLiteral("watch_later"));
}

QString PlaybackHistoryStore::keyForUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        QFileInfo info(url.toLocalFile());
        QString path = info.canonicalFilePath();
        if (path.isEmpty()) {
            path = info.absoluteFilePath();
        }
        path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
        path = path.toLower();
#endif
        return QUrl::fromLocalFile(path)
            .toString(QUrl::FullyEncoded);
    }
    QUrl normalized = url.adjusted(
        QUrl::NormalizePathSegments | QUrl::RemoveFragment);
    return normalized.toString(QUrl::FullyEncoded);
}

PlaybackHistoryEntry PlaybackHistoryStore::entryFor(
    const QUrl &url) const
{
    const QString key = keyForUrl(url);
    const auto found = std::find_if(
        m_entries.cbegin(), m_entries.cend(),
        [&key](const PlaybackHistoryEntry &entry) {
            return entry.key == key;
        });
    return found == m_entries.cend()
        ? PlaybackHistoryEntry{} : *found;
}

PlaybackHistoryEntry &PlaybackHistoryStore::ensureEntry(
    const QUrl &url)
{
    const QString key = keyForUrl(url);
    auto found = std::find_if(
        m_entries.begin(), m_entries.end(),
        [&key](const PlaybackHistoryEntry &entry) {
            return entry.key == key;
        });
    if (found != m_entries.end()) {
        return *found;
    }
    PlaybackHistoryEntry entry;
    entry.key = key;
    entry.url = url;
    entry.displayName = url.isLocalFile()
        ? QFileInfo(url.toLocalFile()).fileName()
        : url.fileName();
    if (entry.displayName.isEmpty()) {
        entry.displayName = url.host();
    }
    m_entries.append(entry);
    return m_entries.last();
}

void PlaybackHistoryStore::recordLoaded(
    const QUrl &url, double durationSec, const QString &title)
{
    if (!url.isValid() || url.isEmpty()) {
        return;
    }
    PlaybackHistoryEntry &entry = ensureEntry(url);
    entry.url = url;
    entry.durationSec = std::max(entry.durationSec, durationSec);
    entry.lastPlayed = QDateTime::currentDateTimeUtc();
    if (!title.isEmpty()) {
        entry.title = title;
    }
    m_dirty = true;
    sortNewestFirst();
    save();
}

void PlaybackHistoryStore::updateProgress(
    const QUrl &url, double positionSec, double durationSec,
    bool reachedEnd)
{
    if (!url.isValid() || url.isEmpty()) {
        return;
    }
    PlaybackHistoryEntry &entry = ensureEntry(url);
    entry.positionSec = std::max(0.0, positionSec);
    entry.durationSec = std::max(entry.durationSec, durationSec);
    entry.lastPlayed = QDateTime::currentDateTimeUtc();
    entry.completed = reachedEnd
        || (entry.durationSec > 0.0
            && entry.positionSec
                   >= entry.durationSec - completionMarginSec);
    m_dirty = true;
}

void PlaybackHistoryStore::remove(const QStringList &keys)
{
    m_entries.removeIf(
        [&keys](const PlaybackHistoryEntry &entry) {
            return keys.contains(entry.key);
        });
    m_dirty = true;
    save();
}

void PlaybackHistoryStore::clear()
{
    m_entries.clear();
    m_dirty = true;
    save();
}

bool PlaybackHistoryStore::save()
{
    if (!m_dirty) {
        return true;
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QJsonArray values;
    for (const PlaybackHistoryEntry &entry : std::as_const(m_entries)) {
        values.append(QJsonObject{
            {QStringLiteral("key"), entry.key},
            {QStringLiteral("url"),
             entry.url.toString(QUrl::FullyEncoded)},
            {QStringLiteral("name"), entry.displayName},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("position"), entry.positionSec},
            {QStringLiteral("duration"), entry.durationSec},
            {QStringLiteral("lastPlayed"),
             entry.lastPlayed.toString(Qt::ISODateWithMs)},
            {QStringLiteral("completed"), entry.completed}});
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Logger::warn(
            QStringLiteral("Could not write playback history: %1")
                .arg(m_filePath));
        return false;
    }
    file.write(QJsonDocument(
        QJsonObject{{QStringLiteral("version"), 1},
                    {QStringLiteral("entries"), values}})
                   .toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }
    m_dirty = false;
    return true;
}

void PlaybackHistoryStore::load()
{
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll());
    const QJsonArray values =
        document.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : values) {
        const QJsonObject object = value.toObject();
        PlaybackHistoryEntry entry;
        entry.url = QUrl::fromEncoded(
            object.value(QStringLiteral("url"))
                .toString().toUtf8());
        entry.key = object.value(QStringLiteral("key")).toString();
        if (entry.key.isEmpty()) {
            entry.key = keyForUrl(entry.url);
        }
        entry.displayName =
            object.value(QStringLiteral("name")).toString();
        entry.title =
            object.value(QStringLiteral("title")).toString();
        entry.positionSec =
            object.value(QStringLiteral("position")).toDouble();
        entry.durationSec =
            object.value(QStringLiteral("duration")).toDouble();
        entry.lastPlayed = QDateTime::fromString(
            object.value(QStringLiteral("lastPlayed")).toString(),
            Qt::ISODateWithMs);
        entry.completed =
            object.value(QStringLiteral("completed")).toBool();
        if (entry.url.isValid() && !entry.key.isEmpty()) {
            m_entries.append(entry);
        }
    }
    sortNewestFirst();
    m_dirty = false;
}

void PlaybackHistoryStore::sortNewestFirst()
{
    std::stable_sort(
        m_entries.begin(), m_entries.end(),
        [](const PlaybackHistoryEntry &left,
           const PlaybackHistoryEntry &right) {
            return left.lastPlayed > right.lastPlayed;
        });
}

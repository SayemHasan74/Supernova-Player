#include "PlayerCore/PlaybackHistory.h"

#include "Core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
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

double PlaybackHistoryEntry::progressRatio() const noexcept
{
    if (durationSec <= 0.0 || !std::isfinite(positionSec)
        || !std::isfinite(durationSec)) {
        return 0.0;
    }
    return std::clamp(positionSec / durationSec, 0.0, 1.0);
}

bool PlaybackHistoryEntry::isAvailable() const
{
    return !url.isLocalFile() || QFileInfo::exists(url.toLocalFile());
}

PlaybackHistoryStore::PlaybackHistoryStore(
    const QString &historyFilePath)
    : m_filePath(historyFilePath.isEmpty()
                     ? defaultHistoryFilePath()
                     : historyFilePath)
{
    m_savePool.setMaxThreadCount(1);
    m_savePool.setExpiryTimeout(-1);
    load();
}

PlaybackHistoryStore::~PlaybackHistoryStore()
{
    m_savePool.waitForDone();
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
    entry.location = url.isLocalFile()
        ? QFileInfo(url.toLocalFile()).absolutePath()
        : url.host();
    m_entries.append(entry);
    return m_entries.last();
}

void PlaybackHistoryStore::recordLoaded(
    const QUrl &url, double durationSec, const QString &title)
{
    if (!m_recordingEnabled || !url.isValid() || url.isEmpty()) {
        return;
    }
    PlaybackHistoryEntry &entry = ensureEntry(url);
    if (entry.completed) {
        entry.positionSec = 0.0;
    }
    entry.url = url;
    if (std::isfinite(durationSec) && durationSec > 0.0) {
        entry.durationSec = durationSec;
    }
    entry.lastPlayed = QDateTime::currentDateTimeUtc();
    entry.completed = false;
    if (!title.isEmpty()) {
        entry.title = title;
    }
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        entry.displayName = info.fileName();
        entry.location = info.absolutePath();
        entry.fileSize = info.exists() ? info.size() : -1;
        entry.fileModified =
            info.exists() ? info.lastModified().toUTC() : QDateTime();
    } else {
        entry.location = url.host();
        if (entry.displayName.isEmpty()) {
            entry.displayName = url.toDisplayString();
        }
    }
    m_dirty = true;
    sortNewestFirst();
    saveAsync();
}

void PlaybackHistoryStore::updateProgress(
    const QUrl &url, double positionSec, double durationSec,
    bool reachedEnd)
{
    if (!m_recordingEnabled || !url.isValid() || url.isEmpty()) {
        return;
    }
    PlaybackHistoryEntry &entry = ensureEntry(url);
    entry.positionSec = std::max(
        0.0, std::isfinite(positionSec) ? positionSec : 0.0);
    entry.durationSec = std::max(
        entry.durationSec,
        std::isfinite(durationSec) ? durationSec : 0.0);
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
    m_savePool.waitForDone();
    if (!m_dirty) {
        return true;
    }
    const QByteArray contents = serializedHistory();
    const bool written = writeHistory(m_filePath, contents);
    if (written) {
        m_dirty = false;
    }
    return written;
}

void PlaybackHistoryStore::saveAsync()
{
    if (!m_dirty) {
        return;
    }
    const QString path = m_filePath;
    const QByteArray contents = serializedHistory();
    m_dirty = false;
    m_savePool.start([path, contents] {
        if (!PlaybackHistoryStore::writeHistory(path, contents)) {
            Logger::warn(
                QStringLiteral("Could not asynchronously write playback history: %1")
                    .arg(path));
        }
    });
}

QByteArray PlaybackHistoryStore::serializedHistory() const
{
    QJsonArray values;
    for (const PlaybackHistoryEntry &entry : std::as_const(m_entries)) {
        values.append(QJsonObject{
            {QStringLiteral("key"), entry.key},
            {QStringLiteral("url"),
             entry.url.toString(QUrl::FullyEncoded)},
            {QStringLiteral("name"), entry.displayName},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("location"), entry.location},
            {QStringLiteral("fileSize"), entry.fileSize},
            {QStringLiteral("fileModified"),
             entry.fileModified.toString(Qt::ISODateWithMs)},
            {QStringLiteral("position"), entry.positionSec},
            {QStringLiteral("duration"), entry.durationSec},
            {QStringLiteral("lastPlayed"),
             entry.lastPlayed.toString(Qt::ISODateWithMs)},
            {QStringLiteral("completed"), entry.completed}});
    }
    return QJsonDocument(
        QJsonObject{{QStringLiteral("version"), 2},
                    {QStringLiteral("entries"), values}})
        .toJson(QJsonDocument::Indented);
}

bool PlaybackHistoryStore::writeHistory(
    const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        Logger::warn(
            QStringLiteral("Could not write playback history: %1")
                .arg(path));
        return false;
    }
    if (file.write(contents) != contents.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        return false;
    }
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
    QHash<QString, qsizetype> indexByKey;
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
        entry.location =
            object.value(QStringLiteral("location")).toString();
        entry.fileSize =
            object.value(QStringLiteral("fileSize")).toInteger(-1);
        entry.fileModified = QDateTime::fromString(
            object.value(QStringLiteral("fileModified")).toString(),
            Qt::ISODateWithMs);
        entry.positionSec = std::max(
            0.0, object.value(QStringLiteral("position")).toDouble());
        entry.durationSec = std::max(
            0.0, object.value(QStringLiteral("duration")).toDouble());
        entry.lastPlayed = QDateTime::fromString(
            object.value(QStringLiteral("lastPlayed")).toString(),
            Qt::ISODateWithMs);
        entry.completed =
            object.value(QStringLiteral("completed")).toBool();
        if (entry.location.isEmpty()) {
            entry.location = entry.url.isLocalFile()
                ? QFileInfo(entry.url.toLocalFile()).absolutePath()
                : entry.url.host();
        }
        if (entry.displayName.isEmpty()) {
            entry.displayName = entry.url.isLocalFile()
                ? QFileInfo(entry.url.toLocalFile()).fileName()
                : entry.url.fileName();
        }
        if (entry.url.isValid() && !entry.url.isEmpty()
            && !entry.key.isEmpty()) {
            const auto duplicate = indexByKey.constFind(entry.key);
            if (duplicate == indexByKey.cend()) {
                indexByKey.insert(entry.key, m_entries.size());
                m_entries.append(entry);
            } else if (entry.lastPlayed
                       > m_entries[*duplicate].lastPlayed) {
                m_entries[*duplicate] = entry;
            }
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

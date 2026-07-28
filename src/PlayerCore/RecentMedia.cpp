#include "PlayerCore/RecentMedia.h"

#include "PlayerCore/PlaybackHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int maximumRecentItems = 20;
}

RecentMediaStore::RecentMediaStore(const QString &filePath)
    : m_filePath(filePath.isEmpty() ? defaultFilePath() : filePath)
{
    load();
}

QString RecentMediaStore::defaultFilePath()
{
    return QDir(QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("recent-media.json"));
}

void RecentMediaStore::note(const QUrl &url)
{
    if (!m_recordingEnabled || !url.isValid() || url.isEmpty()) {
        return;
    }
    const QString key = PlaybackHistoryStore::keyForUrl(url);
    m_entries.erase(
        std::remove_if(
            m_entries.begin(), m_entries.end(),
            [&key](const RecentMediaEntry &entry) {
                return PlaybackHistoryStore::keyForUrl(entry.url) == key;
            }),
        m_entries.end());
    m_entries.prepend(
        RecentMediaEntry{url, QDateTime::currentDateTimeUtc()});
    while (m_entries.size() > maximumRecentItems) {
        m_entries.removeLast();
    }
    save();
}

void RecentMediaStore::clear()
{
    m_entries.clear();
    save();
}

bool RecentMediaStore::save() const
{
    QJsonArray entries;
    for (const RecentMediaEntry &entry : m_entries) {
        entries.append(QJsonObject{
            {QStringLiteral("url"),
             entry.url.toString(QUrl::FullyEncoded)},
            {QStringLiteral("opened"),
             entry.openedAt.toString(Qt::ISODateWithMs)}});
    }
    const QFileInfo target(m_filePath);
    if (!QDir().mkpath(target.absolutePath())) {
        return false;
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("entries"), entries}});
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void RecentMediaStore::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()) {
        return;
    }
    for (const QJsonValue &value :
         document.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        const QUrl url(
            object.value(QStringLiteral("url")).toString(),
            QUrl::StrictMode);
        if (!url.isValid() || url.isEmpty()) {
            continue;
        }
        m_entries.append(RecentMediaEntry{
            url,
            QDateTime::fromString(
                object.value(QStringLiteral("opened")).toString(),
                Qt::ISODateWithMs)});
        if (m_entries.size() == maximumRecentItems) {
            break;
        }
    }
}

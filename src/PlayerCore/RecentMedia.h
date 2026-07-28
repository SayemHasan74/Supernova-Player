#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

struct RecentMediaEntry {
    QUrl url;
    QDateTime openedAt;

    friend bool operator==(
        const RecentMediaEntry &, const RecentMediaEntry &) = default;
};

class RecentMediaStore final {
public:
    explicit RecentMediaStore(const QString &filePath = {});

    [[nodiscard]] static QString defaultFilePath();
    [[nodiscard]] const QList<RecentMediaEntry> &entries() const noexcept
    {
        return m_entries;
    }
    [[nodiscard]] bool recordingEnabled() const noexcept
    {
        return m_recordingEnabled;
    }
    void setRecordingEnabled(bool enabled) noexcept
    {
        m_recordingEnabled = enabled;
    }

    void note(const QUrl &url);
    void clear();
    bool save() const;

private:
    void load();

    QString m_filePath;
    QList<RecentMediaEntry> m_entries;
    bool m_recordingEnabled = true;
};

Q_DECLARE_METATYPE(RecentMediaEntry)
Q_DECLARE_METATYPE(QList<RecentMediaEntry>)

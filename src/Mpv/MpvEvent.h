#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVariantMap>

struct MpvEvent {
    int id = 0;
    QString name;
    int errorCode = 0;
    QString errorMessage;
    quint64 replyUserdata = 0;
    QVariantMap data;
};

struct MpvCommandResult {
    quint64 requestId = 0;
    QString command;
    QVariant value;
    int errorCode = 0;
    QString errorMessage;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return errorCode >= 0;
    }
};

struct MpvEndFileInfo {
    int reason = 0;
    int errorCode = 0;
    QString errorMessage;
    qint64 playlistEntryId = 0;
    qint64 playlistInsertId = 0;
    int playlistInsertCount = 0;

    [[nodiscard]] bool failed() const noexcept
    {
        return errorCode < 0;
    }
};

Q_DECLARE_METATYPE(MpvEvent)
Q_DECLARE_METATYPE(MpvCommandResult)
Q_DECLARE_METATYPE(MpvEndFileInfo)

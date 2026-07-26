#pragma once

#include "PlayerCore/PlaylistState.h"

#include <QList>
#include <QString>
#include <QUrl>

class PlaylistIO final {
public:
    struct ImportResult {
        QList<QUrl> urls;
        QString error;

        [[nodiscard]] bool succeeded() const noexcept
        {
            return error.isEmpty();
        }
    };

    [[nodiscard]] static ImportResult importFile(const QString &path);
    [[nodiscard]] static QString exportM3u8(
        const QString &path, const PlaylistState &playlist);
};

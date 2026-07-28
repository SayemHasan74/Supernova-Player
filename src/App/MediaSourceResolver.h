#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QMimeData;

class MediaSourceResolver final {
public:
    [[nodiscard]] static QList<QUrl> fromUserInputs(
        const QStringList &inputs,
        const QString &workingDirectory = {});
    [[nodiscard]] static QList<QUrl> fromMimeData(
        const QMimeData *mimeData,
        const QString &workingDirectory = {});
    [[nodiscard]] static QList<QUrl> resolve(const QList<QUrl> &urls);
    [[nodiscard]] static QList<QUrl> siblingPlaylistFor(
        const QUrl &openedUrl);
    [[nodiscard]] static bool canResolve(const QMimeData *mimeData);

    [[nodiscard]] static QString mediaDialogFilter();
    [[nodiscard]] static QStringList supportedMediaExtensions();
    [[nodiscard]] static QStringList supportedSubtitleExtensions();
    [[nodiscard]] static QStringList supportedPlaylistExtensions();

private:
    [[nodiscard]] static bool isBluRayFolder(const QString &path);
};

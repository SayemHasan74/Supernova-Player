#include "PlayerCore/PlaylistIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

namespace {
QUrl resolveEntry(const QString &entry, const QDir &baseDirectory)
{
    const QString trimmed = entry.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QUrl parsed(trimmed);
    if (!parsed.scheme().isEmpty()
        && parsed.scheme().compare(
               QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
        return parsed;
    }
    const QString path = parsed.isLocalFile()
        ? parsed.toLocalFile()
        : QDir::fromNativeSeparators(trimmed);
    return QUrl::fromLocalFile(
        QFileInfo(path).isAbsolute()
            ? QDir::cleanPath(path)
            : QDir::cleanPath(baseDirectory.absoluteFilePath(path)));
}

QString urlKey(const QUrl &url)
{
    return url.isLocalFile()
        ? QDir::cleanPath(url.toLocalFile()).toCaseFolded()
        : url.toString(QUrl::FullyEncoded);
}
}

PlaylistIO::ImportResult PlaylistIO::importFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, file.errorString()};
    }

    const QDir baseDirectory = QFileInfo(path).absoluteDir();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool isPls = suffix == QStringLiteral("pls");
    QList<QUrl> urls;
    QSet<QString> seen;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.startsWith(QChar::ByteOrderMark)) {
            line.remove(0, 1);
        }
        const bool plsMetadata =
            isPls
            && (line.compare(
                    QStringLiteral("[playlist]"),
                    Qt::CaseInsensitive) == 0
                || line.startsWith(
                    QStringLiteral("NumberOfEntries="),
                    Qt::CaseInsensitive)
                || line.startsWith(
                    QStringLiteral("Version="),
                    Qt::CaseInsensitive)
                || line.startsWith(
                    QStringLiteral("Title"),
                    Qt::CaseInsensitive)
                || line.startsWith(
                    QStringLiteral("Length"),
                    Qt::CaseInsensitive));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || plsMetadata) {
            continue;
        }
        if (isPls) {
            if (!line.startsWith(
                    QStringLiteral("File"),
                    Qt::CaseInsensitive)) {
                continue;
            }
            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals < 0) {
                continue;
            }
            line = line.sliced(equals + 1).trimmed();
        }
        const QUrl url = resolveEntry(line, baseDirectory);
        if (!url.isValid() || url.isEmpty()) {
            continue;
        }
        const QString key = urlKey(url);
        if (!seen.contains(key)) {
            seen.insert(key);
            urls.append(url);
        }
    }
    if (urls.isEmpty()) {
        return {{}, QStringLiteral("The playlist does not contain any media entries.")};
    }
    return {urls, {}};
}

QString PlaylistIO::exportM3u8(
    const QString &path, const PlaylistState &playlist)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return file.errorString();
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const PlaylistItem &item : playlist.items) {
        stream << (item.url.isLocalFile()
                       ? QDir::toNativeSeparators(item.url.toLocalFile())
                       : item.url.toString(QUrl::FullyEncoded))
               << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        return file.errorString().isEmpty()
            ? QStringLiteral("Could not write the playlist.")
            : file.errorString();
    }
    return {};
}

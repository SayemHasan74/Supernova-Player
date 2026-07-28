#include "App/AutomaticFileMatcher.h"

#include "App/MediaSourceResolver.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
struct FileInfo {
    QUrl url;
    QString path;
    QString filename;
    QString prefix;
    QString suffix;
    QString nameInSeries;
    bool matched = false;
};

using Groups = QHash<QString, QList<int>>;

bool wasCancelled(
    const AutomaticFileMatcher::CancellationCheck &cancelled)
{
    return cancelled && cancelled();
}

bool naturalLess(const FileInfo &left, const FileInfo &right)
{
    static const QCollator collator = [] {
        QCollator value;
        value.setCaseSensitivity(Qt::CaseInsensitive);
        value.setNumericMode(true);
        return value;
    }();
    return collator.compare(left.filename, right.filename) < 0;
}

FileInfo makeFileInfo(const QFileInfo &file)
{
    FileInfo result;
    result.path = QDir::cleanPath(file.absoluteFilePath());
    result.url = QUrl::fromLocalFile(result.path);
    result.filename = file.completeBaseName();
    result.suffix = result.filename;
    return result;
}

void setPrefix(FileInfo &file, const QString &prefix)
{
    if (prefix.size() >= file.filename.size()) {
        file.prefix.clear();
        file.suffix = file.filename;
        file.nameInSeries.clear();
        return;
    }
    file.prefix = prefix;
    file.suffix = file.filename.mid(prefix.size());
    bool foundDigit = false;
    qsizetype length = 0;
    for (const QChar character : std::as_const(file.suffix)) {
        if (character.isDigit()) {
            foundDigit = true;
        } else if (foundDigit) {
            break;
        }
        ++length;
    }
    file.nameInSeries = file.suffix.left(length);
}

bool shouldStopGrouping(const QList<QChar> &characters)
{
    static const QString chineseNumbers =
        QStringLiteral("零一二三四五六七八九十");
    int chineseCount = 0;
    for (const QChar character : characters) {
        if (character.isDigit()) {
            return true;
        }
        if (chineseNumbers.contains(character) && ++chineseCount >= 3) {
            return true;
        }
    }
    return false;
}

void groupRecursive(
    QVector<FileInfo> &files, const QList<int> &indexes,
    QString prefix, Groups &output)
{
    if (indexes.size() < 3) {
        for (int index : indexes) {
            setPrefix(files[index], prefix);
        }
        output[prefix].append(indexes);
        return;
    }

    QHash<QString, QList<int>> temporary;
    QList<QChar> currentCharacters;
    qsizetype characterIndex = prefix.size();
    while (temporary.size() < 2) {
        QString lastPrefix = prefix;
        bool processedAny = false;
        for (int index : indexes) {
            const QString &name = files[index].filename;
            if (characterIndex >= name.size()) {
                temporary[prefix].append(index);
                currentCharacters.append(QLatin1Char('/'));
                continue;
            }
            const QChar character = name[characterIndex];
            QString candidate = prefix;
            candidate.append(character);
            lastPrefix = candidate;
            if (!temporary.contains(candidate)) {
                currentCharacters.append(character);
            }
            temporary[candidate].append(index);
            processedAny = true;
        }
        if (temporary.size() == 1) {
            prefix = lastPrefix;
            temporary.clear();
            currentCharacters.clear();
        }
        ++characterIndex;
        if (!processedAny) {
            break;
        }
    }

    qsizetype largestSubgroup = 0;
    for (const QList<int> &subgroup : std::as_const(temporary)) {
        largestSubgroup = std::max(largestSubgroup, subgroup.size());
    }
    if (temporary.isEmpty()
        || shouldStopGrouping(currentCharacters)
        || largestSubgroup < 3) {
        for (int index : indexes) {
            setPrefix(files[index], prefix);
        }
        output[prefix].append(indexes);
        return;
    }
    for (auto it = temporary.cbegin(); it != temporary.cend(); ++it) {
        groupRecursive(files, it.value(), it.key(), output);
    }
}

Groups groupFiles(QVector<FileInfo> &files)
{
    QList<int> indexes;
    indexes.reserve(files.size());
    for (int index = 0; index < files.size(); ++index) {
        indexes.append(index);
    }
    Groups groups;
    groupRecursive(files, indexes, QString(), groups);
    return groups;
}

quint32 editDistance(const QString &left, const QString &right)
{
    const QString a = left.normalized(QString::NormalizationForm_C);
    const QString b = right.normalized(QString::NormalizationForm_C);
    QVector<quint32> previous(b.size() + 1);
    QVector<quint32> current(b.size() + 1);
    for (int column = 0; column <= b.size(); ++column) {
        previous[column] = static_cast<quint32>(column);
    }
    for (int row = 1; row <= a.size(); ++row) {
        current[0] = static_cast<quint32>(row);
        for (int column = 1; column <= b.size(); ++column) {
            const quint32 substitution =
                previous[column - 1]
                + (a[row - 1] == b[column - 1] ? 0U : 1U);
            current[column] = std::min(
                {previous[column] + 1U, current[column - 1] + 1U,
                 substitution});
        }
        previous.swap(current);
    }
    return previous.constLast();
}

QStringList splitSearchPaths(const QString &value)
{
    QStringList paths = value.split(
        QRegularExpression(QStringLiteral("[;\\r\\n]+")),
        Qt::SkipEmptyParts);
#ifndef Q_OS_WIN
    if (paths.size() <= 1) {
        paths = value.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    }
#endif
    for (QString &path : paths) {
        path = path.trimmed();
    }
    paths.removeAll(QString());
    return paths;
}

QList<QDir> subtitleDirectories(
    const QDir &current, const QString &configured)
{
    QList<QDir> result;
    QSet<QString> seen;
    for (QString path : splitSearchPaths(configured)) {
        path = QDir::fromNativeSeparators(path);
        const bool wildcard =
            path.endsWith(QStringLiteral("/*"));
        if (wildcard) {
            path.chop(2);
        }
        QString absolute;
        if (QDir::isAbsolutePath(path)) {
            absolute = path;
        } else {
            absolute = current.absoluteFilePath(path);
        }
        QDir directory(QDir::cleanPath(absolute));
        if (wildcard) {
            const QFileInfoList children = directory.entryInfoList(
                QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot
                    | QDir::Hidden,
                QDir::NoSort);
            for (const QFileInfo &child : children) {
                if (child.isHidden()) {
                    continue;
                }
                const QString key =
                    QDir::cleanPath(child.absoluteFilePath()).toCaseFolded();
                if (!seen.contains(key)) {
                    seen.insert(key);
                    result.append(QDir(child.absoluteFilePath()));
                }
            }
        } else if (directory.exists()) {
            const QString key =
                QDir::cleanPath(directory.absolutePath()).toCaseFolded();
            if (!seen.contains(key)) {
                seen.insert(key);
                result.append(directory);
            }
        }
    }
    return result;
}

void appendUnique(
    QHash<QString, QList<QUrl>> &matches,
    const FileInfo &media, const FileInfo &subtitle)
{
    QList<QUrl> &items =
        matches[AutomaticFileMatcher::mediaKey(media.url)];
    const QString key = AutomaticFileMatcher::mediaKey(subtitle.url);
    const bool exists = std::any_of(
        items.cbegin(), items.cend(), [&key](const QUrl &url) {
            return AutomaticFileMatcher::mediaKey(url) == key;
        });
    if (!exists) {
        items.append(subtitle.url);
    }
}

int occurrenceCount(const QString &name, const QStringList &priorities)
{
    int total = 0;
    for (const QString &priority : priorities) {
        qsizetype position = 0;
        while ((position = name.indexOf(priority, position)) >= 0) {
            ++total;
            position += std::max<qsizetype>(1, priority.size());
        }
    }
    return total;
}
}

QString AutomaticFileMatcher::mediaKey(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return url.adjusted(
                      QUrl::NormalizePathSegments | QUrl::RemoveFragment)
            .toString(QUrl::FullyEncoded);
    }
    const QFileInfo file(url.toLocalFile());
    QString path = file.canonicalFilePath();
    if (path.isEmpty()) {
        path = file.absoluteFilePath();
    }
    path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    path = path.toLower();
#endif
    return path;
}

AutomaticMatchResult AutomaticFileMatcher::match(
    const QUrl &mediaUrl, const AutomaticMatchOptions &options,
    const CancellationCheck &cancelled)
{
    AutomaticMatchResult result;
    if (!mediaUrl.isLocalFile() || wasCancelled(cancelled)) {
        result.cancelled = wasCancelled(cancelled);
        return result;
    }
    const QFileInfo opened(mediaUrl.toLocalFile());
    if (!opened.exists() || !opened.isFile()) {
        return result;
    }
    const QDir current = opened.absoluteDir();
    const QStringList supportedMedia =
        MediaSourceResolver::supportedMediaExtensions();
    const QStringList supportedSubtitles =
        MediaSourceResolver::supportedSubtitleExtensions();
    const QSet<QString> mediaExtensions(
        supportedMedia.cbegin(), supportedMedia.cend());
    const QSet<QString> subtitleExtensions(
        supportedSubtitles.cbegin(), supportedSubtitles.cend());

    QVector<FileInfo> videos;
    QVector<FileInfo> audios;
    QVector<FileInfo> subtitles;
    const auto collectDirectory =
        [&](const QDir &directory, bool collectMedia) {
            const QFileInfoList entries = directory.entryInfoList(
                QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                QDir::NoSort);
            for (const QFileInfo &entry : entries) {
                if (wasCancelled(cancelled)) {
                    return false;
                }
                const QString extension = entry.suffix().toLower();
                if (subtitleExtensions.contains(extension)) {
                    subtitles.append(makeFileInfo(entry));
                } else if (collectMedia
                           && mediaExtensions.contains(extension)) {
                    const QStringList knownAudio{
                        QStringLiteral("mp3"), QStringLiteral("aac"),
                        QStringLiteral("mka"), QStringLiteral("dts"),
                        QStringLiteral("flac"), QStringLiteral("ogg"),
                        QStringLiteral("oga"), QStringLiteral("mogg"),
                        QStringLiteral("m4a"), QStringLiteral("ac3"),
                        QStringLiteral("opus"), QStringLiteral("wav"),
                        QStringLiteral("wv"), QStringLiteral("aiff"),
                        QStringLiteral("aif"), QStringLiteral("ape"),
                        QStringLiteral("tta"), QStringLiteral("tak")};
                    (knownAudio.contains(extension) ? audios : videos)
                        .append(makeFileInfo(entry));
                }
            }
            return true;
        };
    if (!collectDirectory(current, true)) {
        result.cancelled = true;
        return result;
    }
    for (const QDir &directory :
         subtitleDirectories(current, options.subtitleSearchPaths)) {
        if (QDir::cleanPath(directory.absolutePath()).compare(
                QDir::cleanPath(current.absolutePath()),
                Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (!collectDirectory(directory, false)) {
            result.cancelled = true;
            return result;
        }
    }
    std::sort(videos.begin(), videos.end(), naturalLess);
    std::sort(audios.begin(), audios.end(), naturalLess);
    std::sort(subtitles.begin(), subtitles.end(), naturalLess);
    for (const FileInfo &file : std::as_const(videos)) {
        result.playlist.append(file.url);
    }
    for (const FileInfo &file : std::as_const(audios)) {
        result.playlist.append(file.url);
    }
    if (options.subtitleMode == SubtitleAutoLoadMode::Disabled
        || wasCancelled(cancelled)) {
        result.cancelled = wasCancelled(cancelled);
        return result;
    }

    QVector<FileInfo> media = videos;
    media += audios;
    const Groups mediaGroups = groupFiles(media);
    const Groups subtitleGroups = groupFiles(subtitles);
    QHash<QString, QString> matchedPrefixes;
    if (options.subtitleMode == SubtitleAutoLoadMode::Smart) {
        QHash<QString, QString> closestMediaForSubtitle;
        for (auto subGroup = subtitleGroups.cbegin();
             subGroup != subtitleGroups.cend(); ++subGroup) {
            quint32 best = std::numeric_limits<quint32>::max();
            QString closest;
            for (auto mediaGroup = mediaGroups.cbegin();
                 mediaGroup != mediaGroups.cend(); ++mediaGroup) {
                if (mediaGroup.value().size() <= 2) {
                    continue;
                }
                const quint32 distance =
                    editDistance(mediaGroup.key(), subGroup.key());
                if (distance < best) {
                    best = distance;
                    closest = mediaGroup.key();
                }
            }
            closestMediaForSubtitle.insert(subGroup.key(), closest);
        }
        for (auto mediaGroup = mediaGroups.cbegin();
             mediaGroup != mediaGroups.cend(); ++mediaGroup) {
            if (mediaGroup.value().size() <= 2) {
                continue;
            }
            quint32 best = std::numeric_limits<quint32>::max();
            QString closestSubtitle;
            for (auto subGroup = subtitleGroups.cbegin();
                 subGroup != subtitleGroups.cend(); ++subGroup) {
                const quint32 distance =
                    editDistance(mediaGroup.key(), subGroup.key());
                if (distance < best) {
                    best = distance;
                    closestSubtitle = subGroup.key();
                }
            }
            const quint32 threshold = static_cast<quint32>(
                (mediaGroup.key().size() + closestSubtitle.size()) * 0.6);
            if (!closestSubtitle.isEmpty()
                && closestMediaForSubtitle.value(closestSubtitle)
                       == mediaGroup.key()
                && best < threshold) {
                matchedPrefixes.insert(
                    mediaGroup.key(), closestSubtitle);
            }
        }
    }

    QList<int> unmatchedMedia;
    for (int mediaIndex = 0; mediaIndex < media.size(); ++mediaIndex) {
        if (wasCancelled(cancelled)) {
            result.cancelled = true;
            return result;
        }
        FileInfo &mediaFile = media[mediaIndex];
        QSet<int> matchedIndexes;
        if (options.subtitleMode == SubtitleAutoLoadMode::Smart
            && !mediaFile.prefix.isEmpty()
            && matchedPrefixes.contains(mediaFile.prefix)) {
            const QString subtitlePrefix =
                matchedPrefixes.value(mediaFile.prefix);
            for (int subIndex = 0; subIndex < subtitles.size(); ++subIndex) {
                FileInfo &subtitle = subtitles[subIndex];
                bool episodeMatches = false;
                bool mediaNumber = false;
                bool subtitleNumber = false;
                const int mediaEpisode =
                    mediaFile.nameInSeries.toInt(&mediaNumber);
                const int subtitleEpisode =
                    subtitle.nameInSeries.toInt(&subtitleNumber);
                episodeMatches =
                    mediaNumber && subtitleNumber
                    ? mediaEpisode == subtitleEpisode
                    : mediaFile.nameInSeries == subtitle.nameInSeries;
                if (episodeMatches && subtitle.prefix == subtitlePrefix) {
                    appendUnique(
                        result.subtitlesByMedia, mediaFile, subtitle);
                    subtitle.matched = true;
                    matchedIndexes.insert(subIndex);
                }
            }
        }
        for (int subIndex = 0; subIndex < subtitles.size(); ++subIndex) {
            FileInfo &subtitle = subtitles[subIndex];
            if (!subtitle.matched
                && subtitle.filename.contains(mediaFile.filename)) {
                appendUnique(result.subtitlesByMedia, mediaFile, subtitle);
                subtitle.matched = true;
                matchedIndexes.insert(subIndex);
            }
        }
        if (matchedIndexes.isEmpty()) {
            unmatchedMedia.append(mediaIndex);
        }
    }

    if (options.subtitleMode == SubtitleAutoLoadMode::Smart
        && options.addSiblingsToPlaylist) {
        QList<int> unmatchedSubtitles;
        for (int index = 0; index < subtitles.size(); ++index) {
            if (!subtitles[index].matched) {
                unmatchedSubtitles.append(index);
            }
        }
        if (unmatchedMedia.size() * unmatchedSubtitles.size()
            < options.maximumFuzzyComparisons) {
            QHash<int, QHash<int, quint32>> distances;
            QHash<int, QList<int>> closestMedia;
            for (int subIndex : unmatchedSubtitles) {
                quint32 best = std::numeric_limits<quint32>::max();
                for (int mediaIndex : unmatchedMedia) {
                    const FileInfo &mediaFile = media[mediaIndex];
                    const FileInfo &subtitle = subtitles[subIndex];
                    const quint32 threshold = static_cast<quint32>(
                        (mediaFile.filename.size()
                         + subtitle.filename.size())
                        * 0.6);
                    const quint32 raw =
                        editDistance(mediaFile.prefix, subtitle.prefix)
                        + editDistance(mediaFile.suffix, subtitle.suffix);
                    const quint32 distance =
                        raw < threshold
                            ? raw : std::numeric_limits<quint32>::max();
                    distances[mediaIndex][subIndex] = distance;
                    best = std::min(best, distance);
                }
                if (best != std::numeric_limits<quint32>::max()) {
                    for (int mediaIndex : unmatchedMedia) {
                        if (distances[mediaIndex][subIndex] == best) {
                            closestMedia[subIndex].append(mediaIndex);
                        }
                    }
                }
            }
            for (int mediaIndex : unmatchedMedia) {
                quint32 best = std::numeric_limits<quint32>::max();
                for (int subIndex : unmatchedSubtitles) {
                    best = std::min(best, distances[mediaIndex][subIndex]);
                }
                if (best == std::numeric_limits<quint32>::max()) {
                    continue;
                }
                for (int subIndex : unmatchedSubtitles) {
                    if (distances[mediaIndex][subIndex] == best
                        && closestMedia[subIndex].contains(mediaIndex)) {
                        appendUnique(
                            result.subtitlesByMedia,
                            media[mediaIndex], subtitles[subIndex]);
                    }
                }
            }
        }
    }

    QStringList priorities =
        options.subtitlePriorityStrings.split(
            QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &priority : priorities) {
        priority = priority.trimmed();
    }
    priorities.removeAll(QString());
    if (!priorities.isEmpty()) {
        for (auto it = result.subtitlesByMedia.begin();
             it != result.subtitlesByMedia.end(); ++it) {
            std::stable_sort(
                it.value().begin(), it.value().end(),
                [&priorities](const QUrl &left, const QUrl &right) {
                    return occurrenceCount(
                               QFileInfo(left.toLocalFile())
                                   .completeBaseName(),
                               priorities)
                           > occurrenceCount(
                               QFileInfo(right.toLocalFile())
                                   .completeBaseName(),
                               priorities);
                });
        }
    }
    return result;
}

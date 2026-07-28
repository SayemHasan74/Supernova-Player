#include "App/MediaSourceResolver.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
const QStringList kVideoExtensions{
    QStringLiteral("mkv"),  QStringLiteral("mp4"),  QStringLiteral("avi"),
    QStringLiteral("m4v"),  QStringLiteral("mov"),  QStringLiteral("3gp"),
    QStringLiteral("ts"),   QStringLiteral("mts"),  QStringLiteral("m2ts"),
    QStringLiteral("wmv"),  QStringLiteral("flv"),  QStringLiteral("f4v"),
    QStringLiteral("asf"),  QStringLiteral("webm"), QStringLiteral("rm"),
    QStringLiteral("rmvb"), QStringLiteral("qt"),   QStringLiteral("dv"),
    QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("mxf"),
    QStringLiteral("vob"),  QStringLiteral("gif"),  QStringLiteral("ogv"),
    QStringLiteral("ogm")};

const QStringList kAudioExtensions{
    QStringLiteral("mp3"),  QStringLiteral("aac"),  QStringLiteral("mka"),
    QStringLiteral("dts"),  QStringLiteral("flac"), QStringLiteral("ogg"),
    QStringLiteral("oga"),  QStringLiteral("mogg"), QStringLiteral("m4a"),
    QStringLiteral("ac3"),  QStringLiteral("opus"), QStringLiteral("wav"),
    QStringLiteral("wv"),   QStringLiteral("aiff"), QStringLiteral("aif"),
    QStringLiteral("ape"),  QStringLiteral("tta"),  QStringLiteral("tak")};

const QStringList kSubtitleExtensions{
    QStringLiteral("utf"),  QStringLiteral("utf8"), QStringLiteral("utf-8"),
    QStringLiteral("idx"),  QStringLiteral("sub"),  QStringLiteral("srt"),
    QStringLiteral("smi"),  QStringLiteral("rt"),   QStringLiteral("ssa"),
    QStringLiteral("aqt"),  QStringLiteral("jss"),  QStringLiteral("js"),
    QStringLiteral("ass"),  QStringLiteral("mks"),  QStringLiteral("vtt"),
    QStringLiteral("sup"),  QStringLiteral("scc"),  QStringLiteral("lrc")};

const QStringList kPlaylistExtensions{
    QStringLiteral("cue"), QStringLiteral("m3u"),
    QStringLiteral("m3u8"), QStringLiteral("pls")};

QString normalizedLocalPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(
        canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool isSupportedMediaFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return kVideoExtensions.contains(suffix)
           || kAudioExtensions.contains(suffix);
}

QUrl normalizedUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return url.adjusted(QUrl::NormalizePathSegments);
    }
    return QUrl::fromLocalFile(normalizedLocalPath(url.toLocalFile()));
}

QString deduplicationKey(const QUrl &url)
{
    if (url.isLocalFile()) {
        return normalizedLocalPath(url.toLocalFile()).toCaseFolded();
    }
    return url.adjusted(QUrl::NormalizePathSegments).toString(
        QUrl::FullyEncoded);
}

bool naturalUrlLessThan(const QUrl &left, const QUrl &right)
{
    if (!left.isLocalFile() || !right.isLocalFile()) {
        return left.toDisplayString().localeAwareCompare(
                   right.toDisplayString())
               < 0;
    }

    const QFileInfo leftInfo(left.toLocalFile());
    const QFileInfo rightInfo(right.toLocalFile());
    const int folderOrder =
        leftInfo.absolutePath().compare(
            rightInfo.absolutePath(), Qt::CaseInsensitive);
    if (folderOrder != 0) {
        return folderOrder < 0;
    }
    static const QCollator collator = [] {
        QCollator value;
        value.setCaseSensitivity(Qt::CaseInsensitive);
        value.setNumericMode(true);
        return value;
    }();
    return collator.compare(leftInfo.fileName(), rightInfo.fileName()) < 0;
}
}

QList<QUrl> MediaSourceResolver::fromUserInputs(
    const QStringList &inputs, const QString &workingDirectory)
{
    const QString baseDirectory =
        workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory;
    QList<QUrl> urls;
    urls.reserve(inputs.size());
    for (const QString &input : inputs) {
        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        urls.append(
            QUrl::fromUserInput(
                trimmed, baseDirectory, QUrl::AssumeLocalFile));
    }
    return resolve(urls);
}

QList<QUrl> MediaSourceResolver::fromMimeData(
    const QMimeData *mimeData, const QString &workingDirectory)
{
    if (!mimeData) {
        return {};
    }
    if (mimeData->hasUrls()) {
        return resolve(mimeData->urls());
    }
    if (mimeData->hasText()) {
        return fromUserInputs(
            QStringList{mimeData->text()}, workingDirectory);
    }
    return {};
}

QList<QUrl> MediaSourceResolver::resolve(const QList<QUrl> &urls)
{
    QList<QUrl> resolved;
    const bool isSingleInput = urls.size() == 1;

    for (const QUrl &rawUrl : urls) {
        if (!rawUrl.isValid() || rawUrl.isEmpty()) {
            continue;
        }
        if (!rawUrl.isLocalFile()) {
            if (!rawUrl.scheme().isEmpty()) {
                resolved.append(normalizedUrl(rawUrl));
            }
            continue;
        }

        const QString path = normalizedLocalPath(rawUrl.toLocalFile());
        const QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }

        if (info.isDir()) {
            if (isSingleInput && isBluRayFolder(path)) {
                resolved.append(QUrl::fromLocalFile(path));
                continue;
            }

            QDirIterator iterator(
                path,
                QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QFileInfo child(iterator.next());
                if (isSupportedMediaFile(child)) {
                    resolved.append(
                        QUrl::fromLocalFile(
                            normalizedLocalPath(child.absoluteFilePath())));
                }
            }
            continue;
        }

        const QString suffix = info.suffix().toLower();
        if (kSubtitleExtensions.contains(suffix)) {
            continue;
        }
        if (isSingleInput && kPlaylistExtensions.contains(suffix)) {
            resolved.append(QUrl::fromLocalFile(path));
            continue;
        }
        if (!kPlaylistExtensions.contains(suffix)) {
            // IINA deliberately allows unknown non-subtitle file types so
            // mpv, rather than the UI's extension list, remains authoritative.
            resolved.append(QUrl::fromLocalFile(path));
        }
    }

    std::sort(resolved.begin(), resolved.end(), naturalUrlLessThan);
    QSet<QString> seen;
    QList<QUrl> unique;
    unique.reserve(resolved.size());
    for (const QUrl &url : std::as_const(resolved)) {
        const QString key = deduplicationKey(url);
        if (!seen.contains(key)) {
            seen.insert(key);
            unique.append(url);
        }
    }
    return unique;
}

QList<QUrl> MediaSourceResolver::siblingPlaylistFor(
    const QUrl &openedUrl)
{
    if (!openedUrl.isLocalFile()) {
        return {openedUrl};
    }
    const QFileInfo opened(normalizedLocalPath(openedUrl.toLocalFile()));
    if (!opened.exists() || !opened.isFile()
        || !isSupportedMediaFile(opened)) {
        return {openedUrl};
    }

    QList<QUrl> videos;
    QList<QUrl> audios;
    const QDir directory = opened.absoluteDir();
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        const QString suffix = entry.suffix().toLower();
        QList<QUrl> *target = nullptr;
        if (kVideoExtensions.contains(suffix)) {
            target = &videos;
        } else if (kAudioExtensions.contains(suffix)) {
            target = &audios;
        }
        if (target) {
            target->append(QUrl::fromLocalFile(
                normalizedLocalPath(entry.absoluteFilePath())));
        }
    }
    std::sort(videos.begin(), videos.end(), naturalUrlLessThan);
    std::sort(audios.begin(), audios.end(), naturalUrlLessThan);
    QList<QUrl> result = videos + audios;
    const QString openedKey =
        deduplicationKey(QUrl::fromLocalFile(opened.absoluteFilePath()));
    if (std::none_of(
            result.cbegin(), result.cend(),
            [&openedKey](const QUrl &candidate) {
                return deduplicationKey(candidate) == openedKey;
            })) {
        return {QUrl::fromLocalFile(opened.absoluteFilePath())};
    }
    return result;
}

bool MediaSourceResolver::canResolve(const QMimeData *mimeData)
{
    if (!mimeData) {
        return false;
    }
    if (mimeData->hasUrls()) {
        return std::any_of(
            mimeData->urls().cbegin(), mimeData->urls().cend(),
            [](const QUrl &url) {
                return url.isValid()
                       && ((!url.isLocalFile() && !url.scheme().isEmpty())
                           || QFileInfo::exists(url.toLocalFile()));
            });
    }
    if (!mimeData->hasText()) {
        return false;
    }

    const QString text = mimeData->text().trimmed();
    if (text.isEmpty()) {
        return false;
    }
    const QUrl url =
        QUrl::fromUserInput(
            text, QDir::currentPath(), QUrl::AssumeLocalFile);
    return url.isValid()
           && ((!url.isLocalFile() && !url.scheme().isEmpty())
               || QFileInfo::exists(url.toLocalFile()));
}

QString MediaSourceResolver::mediaDialogFilter()
{
    QStringList patterns;
    for (const QString &extension : supportedMediaExtensions()
                                      + supportedPlaylistExtensions()) {
        patterns.append(QStringLiteral("*.%1").arg(extension));
    }
    return QStringLiteral("Media files (%1);;All files (*.*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

QStringList MediaSourceResolver::supportedMediaExtensions()
{
    return kVideoExtensions + kAudioExtensions;
}

QStringList MediaSourceResolver::supportedSubtitleExtensions()
{
    return kSubtitleExtensions;
}

QStringList MediaSourceResolver::supportedPlaylistExtensions()
{
    return kPlaylistExtensions;
}

bool MediaSourceResolver::isBluRayFolder(const QString &path)
{
    const auto isBdmvDirectory = [](const QString &candidate) {
        const QDir directory(candidate);
        return directory.exists(QStringLiteral("MovieObject.bdmv"))
               && directory.exists(QStringLiteral("index.bdmv"));
    };

    return isBdmvDirectory(path)
           || isBdmvDirectory(
               QDir(path).filePath(QStringLiteral("BDMV")));
}

#include "App/MediaSourceResolver.h"
#include "App/AutomaticFileMatcher.h"
#include "PlayerCore/PlaylistIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool createFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("test") == 4;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
           && file.write(contents) == contents.size();
}

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!expect(temporaryDirectory.isValid(),
                "temporary directory must be available")) {
        return 1;
    }

    QDir root(temporaryDirectory.path());
    root.mkpath(QStringLiteral("Disc"));
    const QString video2 = root.filePath(QStringLiteral("video2.mp4"));
    const QString video10 = root.filePath(QStringLiteral("video10.mkv"));
    const QString audio =
        root.filePath(QStringLiteral("Disc/audio.mp3"));
    const QString subtitle = root.filePath(QStringLiteral("notes.srt"));
    const QString playlist = root.filePath(QStringLiteral("queue.m3u8"));
    if (!expect(
            createFile(video2) && createFile(video10) && createFile(audio)
                && createFile(subtitle) && createFile(playlist),
            "test media files must be created")) {
        return 1;
    }

    bool passed = true;
    std::cerr << "Checking folder expansion\n";
    const QList<QUrl> folder =
        MediaSourceResolver::resolve(
            {QUrl::fromLocalFile(root.path())});
    passed &= expect(folder.size() == 3,
                     "folders must include media and exclude subtitles/playlists");
    passed &= expect(folder.value(0).toLocalFile() == video2,
                     "same-folder files must use natural numeric order");
    passed &= expect(folder.value(1).toLocalFile() == video10,
                     "video10 must sort after video2");
    passed &= expect(folder.value(2).toLocalFile() == audio,
                     "nested media must be included recursively");
    passed &= expect(
        MediaSourceResolver::isAudioFile(
            QUrl::fromLocalFile(audio)),
        "audio containers must be classified before playback");
    passed &= expect(
        !MediaSourceResolver::isAudioFile(
            QUrl::fromLocalFile(video2)),
        "video containers must not use the audio-only fallback");

    std::cerr << "Checking IINA-style sibling autoload\n";
    const QList<QUrl> siblings =
        MediaSourceResolver::siblingPlaylistFor(
            QUrl::fromLocalFile(video10));
    passed &= expect(siblings.size() == 2,
                     "autoload must stay in the opened file's folder");
    passed &= expect(siblings.value(0).toLocalFile() == video2,
                     "autoload must use natural numeric order");
    passed &= expect(siblings.value(1).toLocalFile() == video10,
                     "opened file must retain its natural queue position");

    std::cerr << "Checking playlist handling\n";
    const QList<QUrl> singlePlaylist =
        MediaSourceResolver::resolve(
            {QUrl::fromLocalFile(playlist)});
    passed &= expect(singlePlaylist.size() == 1,
                     "a single playlist file must be accepted");

    std::cerr << "Checking filtering and deduplication\n";
    const QList<QUrl> mixed =
        MediaSourceResolver::resolve(
            {QUrl::fromLocalFile(playlist),
             QUrl::fromLocalFile(video2),
             QUrl::fromLocalFile(video2),
             QUrl::fromLocalFile(subtitle)});
    passed &= expect(mixed.size() == 1,
                     "mixed input must exclude playlists/subtitles and deduplicate");
    passed &= expect(mixed.constFirst().toLocalFile() == video2,
                     "the playable mixed input must be preserved");

    std::cerr << "Checking network input\n";
    const QList<QUrl> remote =
        MediaSourceResolver::fromUserInputs(
            {QStringLiteral("https://example.com/video")});
    passed &= expect(
        remote.size() == 1
            && remote.constFirst().scheme() == QStringLiteral("https"),
        "network URLs must be accepted");

    std::cerr << "Checking automatic series and subtitle matching\n";
    root.mkpath(QStringLiteral("Subs"));
    const QString episode1 =
        root.filePath(QStringLiteral("Series.S01E01.mkv"));
    const QString episode2 =
        root.filePath(QStringLiteral("Series.S01E02.mkv"));
    const QString episode3 =
        root.filePath(QStringLiteral("Series.S01E03.mkv"));
    const QString subtitle1 = root.filePath(
        QStringLiteral("Subs/Series.S01E01.en.srt"));
    const QString subtitle2 = root.filePath(
        QStringLiteral("Subs/Series.S01E02.en.srt"));
    const QString subtitle3 = root.filePath(
        QStringLiteral("Subs/Series.S01E03.en.srt"));
    passed &= expect(
        createFile(episode1) && createFile(episode2)
            && createFile(episode3) && createFile(subtitle1)
            && createFile(subtitle2) && createFile(subtitle3),
        "series matching fixtures must be created");
    AutomaticMatchOptions matchOptions;
    matchOptions.subtitleSearchPaths = QStringLiteral("./*");
    const AutomaticMatchResult matched =
        AutomaticFileMatcher::match(
            QUrl::fromLocalFile(episode1), matchOptions);
    passed &= expect(
        matched.subtitlesByMedia.value(
            AutomaticFileMatcher::mediaKey(
                QUrl::fromLocalFile(episode2))).contains(
                    QUrl::fromLocalFile(subtitle2)),
        "smart matching must associate each episode with its subtitle");
    const AutomaticMatchResult cancelled =
        AutomaticFileMatcher::match(
            QUrl::fromLocalFile(episode1), matchOptions,
            [] { return true; });
    passed &= expect(
        cancelled.cancelled,
        "background matching must honor cancellation");

    std::cerr << "Checking playlist import and export\n";
    const QString importedPath =
        root.filePath(QStringLiteral("import.m3u8"));
    passed &= expect(
        writeFile(
            importedPath,
            QByteArray("#EXTM3U\nvideo2.mp4\n"
                       "https://example.com/live\n")),
        "playlist fixture must be written");
    const PlaylistIO::ImportResult imported =
        PlaylistIO::importFile(importedPath);
    passed &= expect(
        imported.succeeded() && imported.urls.size() == 2,
        "M3U8 import must accept relative local and network entries");
    passed &= expect(
        imported.urls.value(0).toLocalFile() == video2,
        "relative playlist entries must resolve beside the playlist");

    PlaylistState state;
    state.items.append(
        PlaylistItem{.id = 1,
                     .url = QUrl::fromLocalFile(video2),
                     .displayName = QStringLiteral("video2.mp4")});
    state.items.append(
        PlaylistItem{.id = 2,
                     .url = QUrl(QStringLiteral("https://example.com/live")),
                     .displayName = QStringLiteral("live"),
                     .networkResource = true});
    const QString exportedPath =
        root.filePath(QStringLiteral("export.m3u8"));
    passed &= expect(
        PlaylistIO::exportM3u8(exportedPath, state).isEmpty(),
        "playlist export must succeed");
    const PlaylistIO::ImportResult roundTrip =
        PlaylistIO::importFile(exportedPath);
    passed &= expect(
        roundTrip.succeeded() && roundTrip.urls.size() == 2,
        "exported playlists must import without losing entries");

    std::cerr << "MediaSourceResolver tests complete\n";
    return passed ? 0 : 1;
}

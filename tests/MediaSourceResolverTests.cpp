#include "App/MediaSourceResolver.h"

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

    std::cerr << "MediaSourceResolver tests complete\n";
    return passed ? 0 : 1;
}

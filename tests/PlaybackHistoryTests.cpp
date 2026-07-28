#include "PlayerCore/PlaybackHistory.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class PlaybackHistoryTests final : public QObject {
    Q_OBJECT

private slots:
    void persistsResumePositionByStableFileIdentity();
    void completedMediaDoesNotResume();
    void removesAndClearsEntries();
    void storesLocalFileMetadataAndStablePlayTime();
    void respectsHistoryRecordingPreference();
};

void PlaybackHistoryTests::persistsResumePositionByStableFileIdentity()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path =
        temporary.filePath(QStringLiteral("history.json"));
    const QUrl media = QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("episode.mkv")));

    {
        PlaybackHistoryStore history(path);
        history.recordLoaded(media, 1200.0, QStringLiteral("Episode"));
        history.updateProgress(media, 430.5, 1200.0);
        QVERIFY(history.save());
    }

    PlaybackHistoryStore restored(path);
    QCOMPARE(restored.entries().size(), 1);
    const PlaybackHistoryEntry entry = restored.entryFor(media);
    QCOMPARE(entry.title, QStringLiteral("Episode"));
    QCOMPARE(entry.positionSec, 430.5);
    QCOMPARE(entry.durationSec, 1200.0);
    QCOMPARE(entry.resumePosition(), 430.5);
}

void PlaybackHistoryTests::completedMediaDoesNotResume()
{
    QTemporaryDir temporary;
    const QString path =
        temporary.filePath(QStringLiteral("history.json"));
    const QUrl media = QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("movie.mp4")));

    PlaybackHistoryStore history(path);
    history.recordLoaded(media, 100.0);
    history.updateProgress(media, 96.0, 100.0, true);
    const PlaybackHistoryEntry entry = history.entryFor(media);
    QVERIFY(entry.completed);
    QCOMPARE(entry.resumePosition(), 0.0);
}

void PlaybackHistoryTests::removesAndClearsEntries()
{
    QTemporaryDir temporary;
    PlaybackHistoryStore history(
        temporary.filePath(QStringLiteral("history.json")));
    const QUrl first = QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("one.mp4")));
    const QUrl second = QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("two.mp4")));
    history.recordLoaded(first, 10.0);
    history.recordLoaded(second, 20.0);
    QCOMPARE(history.entries().size(), 2);

    history.remove(
        {PlaybackHistoryStore::keyForUrl(first)});
    QCOMPARE(history.entries().size(), 1);
    QCOMPARE(history.entries().constFirst().url, second);
    history.clear();
    QVERIFY(history.entries().isEmpty());
    QVERIFY(QFileInfo::exists(history.filePath()));
}

void PlaybackHistoryTests::storesLocalFileMetadataAndStablePlayTime()
{
    QTemporaryDir temporary;
    const QString mediaPath =
        temporary.filePath(QStringLiteral("metadata.mkv"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QCOMPARE(media.write("supernova", 9), 9);
    media.close();

    PlaybackHistoryStore history(
        temporary.filePath(QStringLiteral("history.json")));
    const QUrl url = QUrl::fromLocalFile(mediaPath);
    history.recordLoaded(url, 200.0, QStringLiteral("Metadata"));
    const QDateTime loadedAt = history.entryFor(url).lastPlayed;
    history.updateProgress(url, 75.0, 200.0);
    QVERIFY(history.save());

    PlaybackHistoryStore restored(history.filePath());
    const PlaybackHistoryEntry entry = restored.entryFor(url);
    QCOMPARE(entry.fileSize, 9);
    QCOMPARE(entry.location, QFileInfo(mediaPath).absolutePath());
    QCOMPARE(entry.lastPlayed, loadedAt);
    QVERIFY(entry.isAvailable());
    QCOMPARE(entry.progressRatio(), 0.375);
}

void PlaybackHistoryTests::respectsHistoryRecordingPreference()
{
    QTemporaryDir temporary;
    PlaybackHistoryStore history(
        temporary.filePath(QStringLiteral("history.json")));
    history.setRecordingEnabled(false);
    const QUrl url = QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("private.mp4")));

    history.recordLoaded(url, 100.0);
    history.updateProgress(url, 20.0, 100.0);

    QVERIFY(history.entries().isEmpty());
}

QTEST_APPLESS_MAIN(PlaybackHistoryTests)

#include "PlaybackHistoryTests.moc"

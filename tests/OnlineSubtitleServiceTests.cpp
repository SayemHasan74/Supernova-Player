#include "Network/OnlineSubtitleService.h"
#include "Network/SecureCredentialStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class OnlineSubtitleServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void calculatesProviderHashes();
    void sanitizesDownloadedNames();
    void protectsProviderCredentials();
};

void OnlineSubtitleServiceTests::calculatesProviderHashes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("movie.mkv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray data(200'000, '\0');
    for (qsizetype i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>((i * 31) & 0xff);
    }
    QCOMPARE(file.write(data), data.size());
    file.close();

    QString error;
    const QString openHash =
        OnlineSubtitleService::openSubtitlesHash(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(openHash.size(), 16);
    QCOMPARE(
        OnlineSubtitleService::openSubtitlesHash(path), openHash);

    const QString shooterHash =
        OnlineSubtitleService::shooterHash(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(shooterHash.split(QLatin1Char(';')).size(), 4);
    for (const QString &part :
         shooterHash.split(QLatin1Char(';'))) {
        QCOMPARE(part.size(), 32);
    }
}

void OnlineSubtitleServiceTests::sanitizesDownloadedNames()
{
    QCOMPARE(
        OnlineSubtitleService::safeFileName(
            QStringLiteral("../../bad:name?.srt")),
        QStringLiteral("bad_name_.srt"));
    QCOMPARE(
        OnlineSubtitleService::safeFileName(QString()),
        QStringLiteral("subtitle.srt"));
}

void OnlineSubtitleServiceTests::protectsProviderCredentials()
{
    const QString key = QStringLiteral("test-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString secret = QStringLiteral("not-plain-text");
    QVERIFY(SecureCredentialStore::write(key, secret));
    QCOMPARE(SecureCredentialStore::read(key), secret);
    QVERIFY(SecureCredentialStore::write(key, QString()));
    QVERIFY(SecureCredentialStore::read(key).isEmpty());
}

QTEST_APPLESS_MAIN(OnlineSubtitleServiceTests)

#include "OnlineSubtitleServiceTests.moc"

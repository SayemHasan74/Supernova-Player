#include "PlayerCore/ThumbnailProvider.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

class ThumbnailProviderTests final : public QObject {
    Q_OBJECT

private slots:
    void ignoresUnsupportedRequests();
    void generatesSinglePreviewWhenFixtureIsProvided();
    void generatesFramesWhenFixtureIsProvided();
};

void ThumbnailProviderTests::ignoresUnsupportedRequests()
{
    ThumbnailProvider provider;
    QSignalSpy ready(&provider, &ThumbnailProvider::thumbnailsReady);
    provider.request({}, 0.0);
    QCOMPARE(ready.count(), 0);
    QVERIFY(provider.imageAt(10.0).isNull());
}

void ThumbnailProviderTests::generatesSinglePreviewWhenFixtureIsProvided()
{
    const QString path =
        qEnvironmentVariable("SUPERNOVA_TEST_VIDEO");
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QSKIP("SUPERNOVA_TEST_VIDEO was not provided");
    }
    const QImage preview = ThumbnailProvider::previewFor(
        QUrl::fromLocalFile(path), 160);
    QVERIFY(!preview.isNull());
    QVERIFY(preview.width() >= 160);
}

void ThumbnailProviderTests::generatesFramesWhenFixtureIsProvided()
{
    const QString path =
        qEnvironmentVariable("SUPERNOVA_TEST_VIDEO");
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QSKIP("SUPERNOVA_TEST_VIDEO was not provided");
    }
    ThumbnailProvider provider;
    QSignalSpy ready(&provider, &ThumbnailProvider::thumbnailsReady);
    provider.request(QUrl::fromLocalFile(path), 30.0, 120);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 180000);
    QVERIFY(provider.ready());
    QVERIFY(!provider.imageAt(15.0).isNull());
}

QTEST_GUILESS_MAIN(ThumbnailProviderTests)

#include "ThumbnailProviderTests.moc"

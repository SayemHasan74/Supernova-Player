#include "PlayerCore/PlayerCore.h"
#include "UI/Controls/IinaPlayerChrome.h"
#include "UI/Design/DesignTokens.h"

#include <QSignalSpy>
#include <QSlider>
#include <QTest>

#include <cmath>

class IinaPlayerChromeTests final : public QObject {
    Q_OBJECT

private slots:
    void usesIinaFloatingGeometry();
    void exposesStableControlTopology();
    void timelineMapsPointerToMediaPercent();
    void timelineSeekDoesNotCreateOsdMessage();
    void middleClickPassesThroughChromeControls();
};

void IinaPlayerChromeTests::usesIinaFloatingGeometry()
{
    QCOMPARE(Supernova::Ui::floatingControlWidth, 460);
    QCOMPARE(Supernova::Ui::floatingControlHeight, 67);
    QCOMPARE(Supernova::Ui::controlAutoHideMs, 2500);
    QCOMPARE(Supernova::Ui::controlFadeDurationMs, 250);
}

void IinaPlayerChromeTests::exposesStableControlTopology()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);

    QCOMPARE(chrome.objectName(), QStringLiteral("iinaPlayerChrome"));
    QCOMPARE(chrome.minimumWidth(), 200);
    QCOMPARE(chrome.maximumWidth(), 460);
    QCOMPARE(chrome.height(), 67);
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("playPauseButton")));
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("previousButton")));
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("nextButton")));
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("fullScreenButton")));
    QVERIFY(chrome.findChild<QSlider *>(
        QStringLiteral("volumeSlider")));
    QVERIFY(chrome.findChild<IinaTimeline *>(
        QStringLiteral("playbackTimeline")));
}

void IinaPlayerChromeTests::timelineMapsPointerToMediaPercent()
{
    IinaTimeline timeline;
    timeline.resize(300, 16);
    timeline.setPlayback(25.0, 100.0);
    timeline.show();
    QSignalSpy spy(&timeline, &IinaTimeline::seekRequested);

    QTest::mouseClick(
        &timeline, Qt::LeftButton, Qt::NoModifier,
        QPoint(timeline.width() / 2, timeline.height() / 2));

    QVERIFY(!spy.isEmpty());
    const double percent = spy.constLast().constFirst().toDouble();
    QVERIFY(std::abs(percent - 50.0) < 0.5);
}

void IinaPlayerChromeTests::middleClickPassesThroughChromeControls()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);
    auto *playButton = chrome.findChild<IinaIconButton *>(
        QStringLiteral("playPauseButton"));
    QVERIFY(playButton);
    QSignalSpy spy(
        &chrome, &IinaPlayerChrome::progressModeRequested);

    QTest::mouseClick(playButton, Qt::MiddleButton);

    QCOMPARE(spy.count(), 1);
}

void IinaPlayerChromeTests::timelineSeekDoesNotCreateOsdMessage()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);
    auto *timeline = chrome.findChild<IinaTimeline *>(
        QStringLiteral("playbackTimeline"));
    QVERIFY(timeline);
    QSignalSpy osdSpy(&chrome, &IinaPlayerChrome::osdRequested);

    emit timeline->seekStarted();
    emit timeline->seekFinished(50.0);

    QCOMPARE(osdSpy.count(), 0);
}

QTEST_MAIN(IinaPlayerChromeTests)

#include "IinaPlayerChromeTests.moc"

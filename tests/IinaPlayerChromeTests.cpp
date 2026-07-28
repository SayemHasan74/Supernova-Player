#include "PlayerCore/PlayerCore.h"
#include "UI/Controls/IinaPlayerChrome.h"
#include "UI/Design/DesignTokens.h"

#include <QSignalSpy>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSlider>
#include <QTest>

#include <cmath>

class IinaPlayerChromeTests final : public QObject {
    Q_OBJECT

private slots:
    void usesIinaFloatingGeometry();
    void exposesStableControlTopology();
    void timelineMapsPointerToMediaPercent();
    void repeatedActivityDoesNotRestartRevealAnimation();
    void middleClickPassesThroughChromeControls();
    void openFileButtonRequestsFileDialog();
    void playlistButtonRequestsSidebar();
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
        QStringLiteral("openFileButton")));
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("playlistButton")));
    QVERIFY(chrome.findChild<IinaIconButton *>(
        QStringLiteral("mediaSettingsButton")));
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

void IinaPlayerChromeTests::repeatedActivityDoesNotRestartRevealAnimation()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);
    chrome.conceal(false);
    chrome.reveal();

    auto *animation = chrome.findChild<QPropertyAnimation *>();
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(
        chrome.graphicsEffect());
    QVERIFY(animation);
    QVERIFY(effect);
    QTRY_VERIFY_WITH_TIMEOUT(effect->opacity() > 0.0, 100);

    const int elapsed = animation->currentTime();
    chrome.reveal();

    QVERIFY(animation->currentTime() >= elapsed);
    QCOMPARE(animation->state(), QAbstractAnimation::Running);
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

void IinaPlayerChromeTests::openFileButtonRequestsFileDialog()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);
    auto *openFileButton = chrome.findChild<IinaIconButton *>(
        QStringLiteral("openFileButton"));
    QVERIFY(openFileButton);
    QSignalSpy spy(
        &chrome, &IinaPlayerChrome::openFileRequested);

    QTest::mouseClick(openFileButton, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
}

void IinaPlayerChromeTests::playlistButtonRequestsSidebar()
{
    PlayerCore playerCore;
    IinaPlayerChrome chrome(&playerCore);
    auto *playlistButton = chrome.findChild<IinaIconButton *>(
        QStringLiteral("playlistButton"));
    QVERIFY(playlistButton);
    QSignalSpy spy(
        &chrome, &IinaPlayerChrome::playlistRequested);

    QTest::mouseClick(playlistButton, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(IinaPlayerChromeTests)

#include "IinaPlayerChromeTests.moc"

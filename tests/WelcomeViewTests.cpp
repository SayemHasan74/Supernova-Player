#include "UI/Welcome/WelcomeView.h"

#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class WelcomeViewTests final : public QObject {
    Q_OBJECT

private slots:
    void showsResumeAndNineMoreOfTenRecentVideos();
    void recentMediaRemainsIndependentFromPlaybackHistory();
};

void WelcomeViewTests::showsResumeAndNineMoreOfTenRecentVideos()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QList<PlaybackHistoryEntry> history;
    for (int index = 0; index < 11; ++index) {
        const QString path = temporary.filePath(
            QStringLiteral("video-%1.mp4").arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        PlaybackHistoryEntry entry;
        entry.key = QString::number(index);
        entry.url = QUrl::fromLocalFile(path);
        entry.displayName = QFileInfo(path).fileName();
        entry.positionSec = 65.0 + index;
        entry.durationSec = 600.0;
        history.append(entry);
    }

    WelcomeView view;
    view.resize(640, 400);
    view.setHistory(history);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *resume = view.findChild<QPushButton *>(
        QStringLiteral("welcomeResumeButton"));
    auto *recents = view.findChild<QListWidget *>(
        QStringLiteral("welcomeRecentList"));
    QVERIFY(resume);
    QVERIFY(recents);
    QVERIFY(resume->isVisible());
    QVERIFY(resume->property("leftText").toString().contains(
        QStringLiteral("video-0.mp4")));
    QCOMPARE(
        resume->property("rightText").toString(),
        QStringLiteral("1:05"));
    QCOMPARE(recents->count(), 9);

    QSignalSpy opened(&view, &WelcomeView::historyRequested);
    recents->setFocus();
    QTest::keyClick(recents, Qt::Key_Return);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().constFirst().toUrl(), history[0].url);
    QTest::keyClick(recents, Qt::Key_Down);
    QCOMPARE(recents->currentRow(), 0);
    QTest::keyClick(recents, Qt::Key_Return);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().constFirst().toUrl(), history[1].url);
    QTest::keyClick(recents, Qt::Key_Up);
    QCOMPARE(recents->currentRow(), -1);

    QTest::mouseClick(resume, Qt::LeftButton);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().constFirst().toUrl(), history[0].url);

    QListWidgetItem *firstRecent = recents->item(0);
    QVERIFY(firstRecent);
    QTest::mouseClick(
        recents->viewport(), Qt::LeftButton, Qt::NoModifier,
        recents->visualItemRect(firstRecent).center());
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().constFirst().toUrl(), history[1].url);
}

void WelcomeViewTests::recentMediaRemainsIndependentFromPlaybackHistory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QList<RecentMediaEntry> recent;
    for (int index = 0; index < 3; ++index) {
        const QString path = temporary.filePath(
            QStringLiteral("recent-%1.mp4").arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        recent.append(
            {QUrl::fromLocalFile(path),
             QDateTime::currentDateTime().addSecs(-index)});
    }

    WelcomeView view;
    view.setHistory({});
    view.setRecentMedia(recent);
    auto *resume = view.findChild<QPushButton *>(
        QStringLiteral("welcomeResumeButton"));
    auto *recents = view.findChild<QListWidget *>(
        QStringLiteral("welcomeRecentList"));
    QVERIFY(resume);
    QVERIFY(recents);
    QVERIFY(!resume->isVisible());
    QCOMPARE(recents->count(), 3);
    QCOMPARE(
        recents->item(0)->data(Qt::UserRole).toUrl(),
        recent.constFirst().url);
}

QTEST_MAIN(WelcomeViewTests)

#include "WelcomeViewTests.moc"

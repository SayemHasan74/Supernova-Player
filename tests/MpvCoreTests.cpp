#include "Mpv/MpvCore.h"

#include <QSignalSpy>
#include <QTest>

#include <atomic>

class MpvCoreTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void asynchronousCommandsReturnCorrelatedResults();
    void hooksContinueAndMissingFilesReportEndErrors();
    void shutdownCompletesThroughTheEventPump();
};

void MpvCoreTests::initTestCase()
{
    qRegisterMetaType<MpvEvent>();
    qRegisterMetaType<MpvCommandResult>();
    qRegisterMetaType<MpvEndFileInfo>();
}

void MpvCoreTests::asynchronousCommandsReturnCorrelatedResults()
{
    MpvCore core;
    QSignalSpy commandSpy(&core, &MpvCore::commandFinished);
    QSignalSpy eventSpy(&core, &MpvCore::eventReceived);

    const quint64 versionRequest = core.command(
        {QStringLiteral("expand-text"),
         QStringLiteral("${mpv-version}")});
    QVERIFY(versionRequest != 0);
    QTRY_VERIFY_WITH_TIMEOUT(commandSpy.count() >= 1, 5000);

    bool foundVersionReply = false;
    for (const QList<QVariant> &arguments : commandSpy) {
        const MpvCommandResult result =
            qvariant_cast<MpvCommandResult>(arguments.constFirst());
        if (result.requestId == versionRequest) {
            QVERIFY2(
                result.succeeded(),
                qPrintable(result.errorMessage));
            QVERIFY(!result.value.toString().isEmpty());
            foundVersionReply = true;
        }
    }
    QVERIFY(foundVersionReply);

    const quint64 invalidRequest = core.command(
        {QStringLiteral("supernova-command-that-does-not-exist")});
    QTRY_VERIFY_WITH_TIMEOUT(commandSpy.count() >= 2, 5000);

    bool foundFailure = false;
    for (const QList<QVariant> &arguments : commandSpy) {
        const MpvCommandResult result =
            qvariant_cast<MpvCommandResult>(arguments.constFirst());
        if ((invalidRequest == 0 || result.requestId == invalidRequest)
            && result.command
                == QStringLiteral(
                    "supernova-command-that-does-not-exist")) {
            QVERIFY(!result.succeeded());
            QVERIFY(!result.errorMessage.isEmpty());
            foundFailure = true;
        }
    }
    QVERIFY(foundFailure);

    const quint64 propertyRequest =
        core.setDouble(QStringLiteral("volume"), 37.0);
    QVERIFY(propertyRequest != 0);
    const auto foundPropertyReply = [&eventSpy, propertyRequest] {
        for (const QList<QVariant> &arguments : eventSpy) {
            const MpvEvent event =
                qvariant_cast<MpvEvent>(arguments.constFirst());
            if (event.id == MPV_EVENT_SET_PROPERTY_REPLY
                && event.replyUserdata == propertyRequest) {
                return event.errorCode >= 0;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(foundPropertyReply(), 5000);
}

void MpvCoreTests::hooksContinueAndMissingFilesReportEndErrors()
{
    MpvCore core;
    QSignalSpy endSpy(&core, &MpvCore::fileEnded);
    std::atomic_int hookInvocations = 0;

    const quint64 hookRegistration = core.addHook(
        QStringLiteral("on_before_start_file"), 0,
        [&hookInvocations](
            const QString &name,
            MpvCore::HookContinuation continueHook) {
            QCOMPARE(
                name, QStringLiteral("on_before_start_file"));
            ++hookInvocations;
            continueHook();
            continueHook();
        });
    QVERIFY(hookRegistration != 0);

    const QString missingPath =
        QStringLiteral(
            "Z:/supernova-tests/this-media-file-does-not-exist.mkv");
    const quint64 loadRequest = core.command(
        {QStringLiteral("loadfile"), missingPath,
         QStringLiteral("replace")});
    QVERIFY(loadRequest != 0);

    QTRY_VERIFY_WITH_TIMEOUT(hookInvocations.load() == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(endSpy.count() >= 1, 10000);
    const MpvEndFileInfo info =
        qvariant_cast<MpvEndFileInfo>(
            endSpy.constLast().constFirst());
    QCOMPARE(info.reason, static_cast<int>(MPV_END_FILE_REASON_ERROR));
    QVERIFY(info.failed());
    QVERIFY(!info.errorMessage.isEmpty());
}

void MpvCoreTests::shutdownCompletesThroughTheEventPump()
{
    MpvCore core;
    QSignalSpy shutdownSpy(&core, &MpvCore::mpvShutdown);

    core.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(shutdownSpy.count(), 1, 5000);
    QVERIFY(core.isShuttingDown());
    QCOMPARE(
        core.command({QStringLiteral("stop")}),
        quint64(0));
}

QTEST_GUILESS_MAIN(MpvCoreTests)

#include "MpvCoreTests.moc"

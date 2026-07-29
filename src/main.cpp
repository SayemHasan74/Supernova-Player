#include "App/Application.h"
#include "App/CommandLineArgs.h"
#include "App/MediaSourceResolver.h"
#include "App/PlayerWindowManager.h"
#include "App/SingleInstanceGuard.h"
#include "Core/Logger.h"

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QUrl>

#include <exception>

namespace {
QList<QUrl> mediaUrls(const QStringList &paths)
{
    return MediaSourceResolver::fromUserInputs(paths);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    Application app(argc, argv);
    app.setApplicationName(QStringLiteral("Supernova"));
    app.setApplicationVersion(QStringLiteral(SUPERNOVA_VERSION));
    app.setOrganizationName(QStringLiteral("Supernova Project"));
    app.initialize();

    const QStringList applicationArguments = Application::arguments();
    const QStringList rawArguments = applicationArguments.mid(1);
    const CommandLineArgs parsedArguments =
        CommandLineArgs::parse(applicationArguments);

    SingleInstanceGuard guard;
    if (!guard.tryAcquire(rawArguments)) {
        Logger::info(QStringLiteral(
            "This process did not acquire primary-instance ownership and will exit."));
        return 0;
    }

    Logger::info(QStringLiteral("Supernova %1 starting (channel: %2)")
                     .arg(QStringLiteral(SUPERNOVA_VERSION),
                          QStringLiteral(SUPERNOVA_CHANNEL)));

    try {
        PlayerWindowManager players;

        QObject::connect(
            &guard,
            &SingleInstanceGuard::argumentsReceivedFromNewInstance,
            &players,
            [&players](
                const QStringList &arguments) {
                Logger::info(
                    QStringLiteral("Received forwarded args: %1")
                        .arg(arguments.join(QStringLiteral(", "))));

                QStringList forwardedArguments{
                    QCoreApplication::applicationFilePath()};
                forwardedArguments.append(arguments);
                const CommandLineArgs parsedForwarded =
                    CommandLineArgs::parse(forwardedArguments);
                players.open(
                    mediaUrls(parsedForwarded.mediaPaths),
                    parsedForwarded.forceNewWindow);
            });

        players.createPlayer(
            mediaUrls(parsedArguments.mediaPaths));
        return app.exec();
    } catch (const std::exception &error) {
        Logger::error(
            QStringLiteral("Fatal player initialization error: %1")
                .arg(QString::fromUtf8(error.what())));
        return 1;
    }
}

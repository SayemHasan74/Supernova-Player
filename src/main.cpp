#include "App/Application.h"
#include "App/CommandLineArgs.h"
#include "App/SingleInstanceGuard.h"
#include "Core/Logger.h"
#include "UI/MainWindow/MainWindow.h"

#include <QObject>
#include <QCoreApplication>

#include <exception>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

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
        MainWindow window;
        QObject::connect(
            &guard,
            &SingleInstanceGuard::argumentsReceivedFromNewInstance,
            &window,
            [&window](const QStringList &arguments) {
                window.show();
                window.raise();
                window.activateWindow();
                Logger::info(
                    QStringLiteral("Received forwarded args: %1")
                        .arg(arguments.join(QStringLiteral(", "))));

                QStringList forwardedArguments{
                    QCoreApplication::applicationFilePath()};
                forwardedArguments.append(arguments);
                const CommandLineArgs parsedForwarded =
                    CommandLineArgs::parse(forwardedArguments);
                if (!parsedForwarded.mediaPaths.isEmpty()) {
                    window.openMedia(parsedForwarded.mediaPaths.constFirst());
                }
            });

        if (!parsedArguments.mediaPaths.isEmpty()) {
            window.openMedia(parsedArguments.mediaPaths.constFirst());
        }

        window.resize(1280, 720);
        window.show();
        return app.exec();
    } catch (const std::exception &error) {
        Logger::error(
            QStringLiteral("Fatal player initialization error: %1")
                .arg(QString::fromUtf8(error.what())));
        return 1;
    }
}

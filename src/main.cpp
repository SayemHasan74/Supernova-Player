#include "App/Application.h"
#include "App/CommandLineArgs.h"
#include "App/MediaSourceResolver.h"
#include "App/SingleInstanceGuard.h"
#include "Core/Logger.h"
#include "PlayerCore/PlayerCore.h"
#include "UI/MainWindow/MainWindow.h"

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QSize>
#include <QUrl>

#include <exception>
#include <utility>

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
        PlayerCore playerCore;
        MainWindow window(&playerCore);
        QList<QUrl> pendingUrls;

        const auto openWhenRendererReady =
            [&window, &playerCore, &pendingUrls](const QList<QUrl> &urls) {
                if (urls.isEmpty()) {
                    return;
                }
                if (window.isRenderContextReady()) {
                    playerCore.openUrls(urls);
                } else {
                    pendingUrls = urls;
                }
            };

        QObject::connect(
            &window, &MainWindow::renderContextReady,
            &playerCore, [&playerCore, &pendingUrls] {
                if (pendingUrls.isEmpty()) {
                    return;
                }
                playerCore.openUrls(std::exchange(pendingUrls, {}));
            });

        QObject::connect(
            &window, &MainWindow::openUrlsRequested,
            &playerCore, openWhenRendererReady);

        QObject::connect(
            &guard,
            &SingleInstanceGuard::argumentsReceivedFromNewInstance,
            &window,
            [&window, &openWhenRendererReady](
                const QStringList &arguments) {
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
                openWhenRendererReady(
                    mediaUrls(parsedForwarded.mediaPaths));
            });

        if (!parsedArguments.mediaPaths.isEmpty()) {
            openWhenRendererReady(mediaUrls(parsedArguments.mediaPaths));
        }

        window.resize(
            parsedArguments.mediaPaths.isEmpty() ? QSize(640, 400)
                                                 : QSize(1280, 720));
        window.show();
        return app.exec();
    } catch (const std::exception &error) {
        Logger::error(
            QStringLiteral("Fatal player initialization error: %1")
                .arg(QString::fromUtf8(error.what())));
        return 1;
    }
}

#include "App/Application.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    connect(this, &QCoreApplication::aboutToQuit,
            this, &Application::aboutToQuitCleanly);
}

void Application::initialize()
{
    // Section-specific lifecycle hooks attach here as the application grows.
}

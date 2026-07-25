#include "App/CommandLineArgs.h"

#include <QCommandLineOption>
#include <QCommandLineParser>

CommandLineArgs CommandLineArgs::parse(const QStringList &argv)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Supernova media player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("Media files or URLs to open"),
        QStringLiteral("[files...]"));

    const QCommandLineOption mpvOption(
        QStringList{QStringLiteral("mpv-option")},
        QStringLiteral("Raw mpv option to forward"),
        QStringLiteral("value"));
    const QCommandLineOption newWindowOption(
        QStringList{QStringLiteral("new-window")},
        QStringLiteral("Open media in a new player window"));

    parser.addOption(mpvOption);
    parser.addOption(newWindowOption);
    parser.process(argv);

    CommandLineArgs result;
    result.mediaPaths = parser.positionalArguments();
    result.rawMpvOptions = parser.values(mpvOption);
    result.forceNewWindow = parser.isSet(newWindowOption);
    return result;
}

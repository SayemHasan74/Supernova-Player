#pragma once

#include <QStringList>

struct CommandLineArgs {
    QStringList mediaPaths;
    QStringList rawMpvOptions;
    bool forceNewWindow = false;

    static CommandLineArgs parse(const QStringList &argv);
};

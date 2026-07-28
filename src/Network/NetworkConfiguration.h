#pragma once

#include <QString>

struct NetworkConfiguration {
    bool cacheEnabled = true;
    int cacheSeconds = 60;
    int cacheMemoryMiB = 150;
    bool cacheOnDisk = false;
    int timeoutSeconds = 60;
    QString proxy;
    QString userAgent;
    QString referrer;
    QString cookiesFile;

    bool ytdlEnabled = true;
    QString ytdlPath;
    QString ytdlFormat = QStringLiteral("bestvideo+bestaudio/best");
    QString ytdlRawOptions;
    bool tryYtdlFirst = false;
    bool includeSubtitles = true;
    bool includeAutomaticSubtitles = false;
    QString javascriptRuntime;

    [[nodiscard]] static NetworkConfiguration load();
    [[nodiscard]] QString ytdlScriptOptions() const;
    [[nodiscard]] QString ytdlRawOptionList() const;
};

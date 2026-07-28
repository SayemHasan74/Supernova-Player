#include "Network/NetworkConfiguration.h"

#include <QDir>
#include <QSettings>

namespace {
QString normalizedPath(const QString &path)
{
    const QString value = path.trimmed();
    return value.isEmpty()
        ? QString()
        : QDir::toNativeSeparators(QDir::cleanPath(value));
}

void appendListOption(QStringList &options, const QString &name,
                      const QString &value)
{
    if (value.trimmed().isEmpty()) {
        return;
    }
    // mpv's key/value-list parser accepts values wrapped in square brackets;
    // this preserves spaces in Windows paths and proxy URLs.
    options.append(QStringLiteral("%1=[%2]").arg(name, value.trimmed()));
}
}

NetworkConfiguration NetworkConfiguration::load()
{
    const QSettings settings;
    NetworkConfiguration result;
    result.cacheEnabled =
        settings.value(QStringLiteral("network/cacheEnabled"), true).toBool();
    result.cacheSeconds = settings.value(
        QStringLiteral("network/cacheSeconds"), 60).toInt();
    result.cacheMemoryMiB = settings.value(
        QStringLiteral("network/cacheMemoryMiB"), 150).toInt();
    result.cacheOnDisk =
        settings.value(QStringLiteral("network/cacheOnDisk"), false).toBool();
    result.timeoutSeconds = settings.value(
        QStringLiteral("network/timeoutSeconds"), 60).toInt();
    result.proxy =
        settings.value(QStringLiteral("network/proxy")).toString().trimmed();
    result.userAgent =
        settings.value(QStringLiteral("network/userAgent")).toString().trimmed();
    result.referrer =
        settings.value(QStringLiteral("network/referrer")).toString().trimmed();
    result.cookiesFile = normalizedPath(
        settings.value(QStringLiteral("network/cookiesFile")).toString());

    result.ytdlEnabled =
        settings.value(QStringLiteral("network/ytdlEnabled"), true).toBool();
    result.ytdlPath = normalizedPath(
        settings.value(QStringLiteral("network/ytdlPath")).toString());
    result.ytdlFormat = settings.value(
        QStringLiteral("network/ytdlFormat"),
        QStringLiteral("bestvideo+bestaudio/best")).toString().trimmed();
    result.ytdlRawOptions = settings.value(
        QStringLiteral("network/ytdlRawOptions")).toString().trimmed();
    result.tryYtdlFirst = settings.value(
        QStringLiteral("network/tryYtdlFirst"), false).toBool();
    result.includeSubtitles = settings.value(
        QStringLiteral("network/includeSubtitles"), true).toBool();
    result.includeAutomaticSubtitles = settings.value(
        QStringLiteral("network/includeAutomaticSubtitles"), false).toBool();
    result.javascriptRuntime = normalizedPath(
        settings.value(QStringLiteral("network/javascriptRuntime")).toString());
    return result;
}

QString NetworkConfiguration::ytdlScriptOptions() const
{
    QStringList options;
    if (!ytdlPath.isEmpty()) {
        options.append(
            QStringLiteral("ytdl_hook-ytdl_path=%1").arg(ytdlPath));
    }
    if (tryYtdlFirst) {
        options.append(
            QStringLiteral("ytdl_hook-try_ytdl_first=yes"));
    }
    return options.join(QLatin1Char(','));
}

QString NetworkConfiguration::ytdlRawOptionList() const
{
    QStringList options;
    if (!ytdlRawOptions.isEmpty()) {
        options.append(ytdlRawOptions);
    }
    appendListOption(options, QStringLiteral("proxy"), proxy);
    appendListOption(options, QStringLiteral("cookies"), cookiesFile);
    appendListOption(
        options, QStringLiteral("js-runtimes"), javascriptRuntime);
    options.append(
        includeSubtitles ? QStringLiteral("sub-langs=all")
                         : QStringLiteral("sub-langs=none"));
    if (includeSubtitles && includeAutomaticSubtitles) {
        options.append(QStringLiteral("write-auto-subs="));
    }
    options.append(QStringLiteral("no-playlist="));
    return options.join(QLatin1Char(','));
}

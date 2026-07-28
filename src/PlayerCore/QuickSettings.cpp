#include "PlayerCore/QuickSettings.h"

#include <QVariantMap>

QString AudioOutputDevice::displayName() const
{
    if (description.isEmpty()) {
        return name;
    }
    return QStringLiteral("[%1] %2").arg(description, name);
}

QList<AudioOutputDevice> AudioOutputDevice::fromMpvNode(
    const QVariant &node)
{
    QList<AudioOutputDevice> devices;
    for (const QVariant &entry : node.toList()) {
        const QVariantMap map = entry.toMap();
        AudioOutputDevice device;
        device.name = map.value(QStringLiteral("name")).toString();
        device.description =
            map.value(QStringLiteral("description")).toString();
        if (!device.name.isEmpty()) {
            devices.append(std::move(device));
        }
    }
    return devices;
}

QList<MediaFilterInfo> mediaFiltersFromMpvNode(
    const QVariant &node)
{
    QList<MediaFilterInfo> filters;
    for (const QVariant &entry : node.toList()) {
        const QVariantMap map = entry.toMap();
        MediaFilterInfo filter;
        filter.name = map.value(QStringLiteral("name")).toString();
        filter.label = map.value(QStringLiteral("label")).toString();
        const QVariantMap parameters =
            map.value(QStringLiteral("params")).toMap();
        QStringList renderedParameters;
        for (auto it = parameters.cbegin();
             it != parameters.cend(); ++it) {
            renderedParameters.append(
                QStringLiteral("%1=%2").arg(it.key(), it.value().toString()));
        }
        filter.description = filter.name;
        if (!renderedParameters.isEmpty()) {
            filter.description +=
                QStringLiteral("=%1").arg(
                    renderedParameters.join(QLatin1Char(':')));
        }
        if (!filter.label.isEmpty()) {
            filter.description.prepend(
                QStringLiteral("@%1:").arg(filter.label));
        }
        filter.managed =
            filter.label.startsWith(QStringLiteral("supernova_"));
        if (!filter.name.isEmpty()) {
            filters.append(std::move(filter));
        }
    }
    return filters;
}

#include "Core/Logger.h"

#include <QDebug>

namespace {
QString formatMessage(const QString &message)
{
    return QStringLiteral("[Supernova] %1").arg(message);
}
}
namespace Logger {
void info(const QString &message)
{
    qInfo().noquote() << formatMessage(message);
}

void warn(const QString &message)
{
    qWarning().noquote() << formatMessage(message);
}

void error(const QString &message)
{
    qCritical().noquote() << formatMessage(message);
}
}

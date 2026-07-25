#include "Mpv/MpvCore.h"

#include "Core/Logger.h"

#include <QByteArray>
#include <QMetaObject>
#include <QString>

#include <stdexcept>
#include <vector>

namespace {
void requireInitializationStep(int result, const char *operation)
{
    if (result < 0) {
        throw std::runtime_error(
            QStringLiteral("%1: %2")
                .arg(QString::fromLatin1(operation),
                     QString::fromUtf8(mpv_error_string(result)))
                .toStdString());
    }
}

QVariant decodeProperty(const mpv_event_property &property)
{
    if (!property.data || property.format == MPV_FORMAT_NONE) {
        return {};
    }

    switch (property.format) {
    case MPV_FORMAT_FLAG:
        return QVariant::fromValue(*static_cast<int *>(property.data) != 0);
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(
            static_cast<qint64>(*static_cast<int64_t *>(property.data)));
    case MPV_FORMAT_DOUBLE:
        return QVariant::fromValue(*static_cast<double *>(property.data));
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING: {
        const auto value = *static_cast<char **>(property.data);
        return value ? QVariant(QString::fromUtf8(value)) : QVariant();
    }
    default:
        return {};
    }
}
}

MpvCore::MpvCore(QObject *parent)
    : QObject(parent),
      m_mpv(mpv_create())
{
    if (!m_mpv) {
        throw std::runtime_error("mpv_create failed");
    }

    try {
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "vo", "libmpv"),
            "Setting vo=libmpv failed");
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "hwdec", "auto"),
            "Setting hwdec=auto failed");
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "keep-open", "yes"),
            "Setting keep-open=yes failed");
        requireInitializationStep(
            mpv_request_log_messages(m_mpv, "warn"),
            "Requesting mpv log messages failed");

        observe(QStringLiteral("pause"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("duration"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("volume"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("mute"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("track-list"), MPV_FORMAT_NONE);
        observe(QStringLiteral("chapter"), MPV_FORMAT_INT64);
        observe(QStringLiteral("speed"), MPV_FORMAT_DOUBLE);

        mpv_set_wakeup_callback(m_mpv, &MpvCore::wakeupTrampoline, this);
        requireInitializationStep(mpv_initialize(m_mpv),
                                  "mpv_initialize failed");
    } catch (...) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        throw;
    }

    const unsigned long version = mpv_client_api_version();
    Logger::info(
        QStringLiteral("libmpv client API %1.%2 initialized")
            .arg(version >> 16U)
            .arg(version & 0xFFFFU));
}

MpvCore::~MpvCore()
{
    if (!m_mpv) {
        return;
    }

    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void MpvCore::command(const QVariantList &arguments)
{
    if (!m_mpv || arguments.isEmpty()) {
        return;
    }

    std::vector<QByteArray> encoded;
    encoded.reserve(static_cast<std::size_t>(arguments.size()));
    for (const QVariant &argument : arguments) {
        encoded.emplace_back(argument.toString().toUtf8());
    }

    std::vector<const char *> commandArguments;
    commandArguments.reserve(encoded.size() + 1U);
    for (const QByteArray &argument : encoded) {
        commandArguments.push_back(argument.constData());
    }
    commandArguments.push_back(nullptr);

    const int result = mpv_command(m_mpv, commandArguments.data());
    if (result < 0) {
        logError(QStringLiteral("Command '%1'")
                     .arg(arguments.constFirst().toString()),
                 result);
    }
}

void MpvCore::setFlag(const QString &name, bool value)
{
    int data = value ? 1 : 0;
    const QByteArray encodedName = name.toUtf8();
    const int result =
        mpv_set_property(m_mpv, encodedName.constData(), MPV_FORMAT_FLAG, &data);
    if (result < 0) {
        logError(QStringLiteral("Setting property '%1'").arg(name), result);
    }
}

void MpvCore::setInt(const QString &name, qint64 value)
{
    int64_t data = static_cast<int64_t>(value);
    const QByteArray encodedName = name.toUtf8();
    const int result =
        mpv_set_property(m_mpv, encodedName.constData(), MPV_FORMAT_INT64, &data);
    if (result < 0) {
        logError(QStringLiteral("Setting property '%1'").arg(name), result);
    }
}

void MpvCore::setDouble(const QString &name, double value)
{
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_set_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_DOUBLE, &value);
    if (result < 0) {
        logError(QStringLiteral("Setting property '%1'").arg(name), result);
    }
}

void MpvCore::setString(const QString &name, const QString &value)
{
    const QByteArray encodedName = name.toUtf8();
    const QByteArray encodedValue = value.toUtf8();
    const int result = mpv_set_property_string(
        m_mpv, encodedName.constData(), encodedValue.constData());
    if (result < 0) {
        logError(QStringLiteral("Setting property '%1'").arg(name), result);
    }
}

qint64 MpvCore::getInt(const QString &name) const
{
    int64_t value = 0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_INT64, &value);
    if (result < 0) {
        logError(QStringLiteral("Getting property '%1'").arg(name), result);
        return 0;
    }
    return static_cast<qint64>(value);
}

double MpvCore::getDouble(const QString &name) const
{
    double value = 0.0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_DOUBLE, &value);
    if (result < 0) {
        logError(QStringLiteral("Getting property '%1'").arg(name), result);
        return 0.0;
    }
    return value;
}

bool MpvCore::getFlag(const QString &name) const
{
    int value = 0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        logError(QStringLiteral("Getting property '%1'").arg(name), result);
        return false;
    }
    return value != 0;
}

QString MpvCore::getString(const QString &name) const
{
    char *value = nullptr;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_STRING, &value);
    if (result < 0) {
        logError(QStringLiteral("Getting property '%1'").arg(name), result);
        return {};
    }

    const QString converted = value ? QString::fromUtf8(value) : QString();
    mpv_free(value);
    return converted;
}

void MpvCore::observe(const QString &name, mpv_format format)
{
    if (!m_mpv) {
        return;
    }

    const QByteArray encodedName = name.toUtf8();
    const int result =
        mpv_observe_property(m_mpv, 0, encodedName.constData(), format);
    if (result < 0) {
        logError(QStringLiteral("Observing property '%1'").arg(name), result);
    }
}

void MpvCore::drainEventQueue()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handleEvent(event);
    }
}

void MpvCore::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *property =
            static_cast<mpv_event_property *>(event->data);
        if (property && property->name) {
            emit propertyChanged(QString::fromUtf8(property->name),
                                 decodeProperty(*property));
        }
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        const auto *message =
            static_cast<mpv_event_log_message *>(event->data);
        if (message) {
            emit mpvLogMessage(
                QString::fromUtf8(message->prefix ? message->prefix : ""),
                QString::fromUtf8(message->level ? message->level : ""),
                QString::fromUtf8(message->text ? message->text : ""));
        }
        break;
    }
    case MPV_EVENT_SHUTDOWN:
        emit mpvShutdown();
        break;
    default:
        break;
    }
}

void MpvCore::logError(const QString &operation, int errorCode) const
{
    Logger::warn(
        QStringLiteral("%1 failed: %2")
            .arg(operation, QString::fromUtf8(mpv_error_string(errorCode))));
}

void MpvCore::wakeupTrampoline(void *context)
{
    auto *core = static_cast<MpvCore *>(context);
    QMetaObject::invokeMethod(
        core, &MpvCore::drainEventQueue, Qt::QueuedConnection);
}

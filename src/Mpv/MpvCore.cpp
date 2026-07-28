#include "Mpv/MpvCore.h"

#include "Core/Logger.h"
#include "Preferences/PlayerConfiguration.h"

#include <QByteArray>
#include <QDir>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QStringList>
#include <QStandardPaths>
#include <QSettings>
#include <QThread>

#include <mpv/client.h>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void requireInitializationStep(int result, const char *operation)
{
    if (result >= 0) {
        return;
    }
    throw std::runtime_error(
        QStringLiteral("%1: %2")
            .arg(QString::fromLatin1(operation),
                 QString::fromUtf8(mpv_error_string(result)))
            .toStdString());
}

QVariant decodeNode(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_NONE:
        return {};
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING:
        return node.u.string
            ? QVariant(QString::fromUtf8(node.u.string))
            : QVariant();
    case MPV_FORMAT_FLAG:
        return QVariant(node.u.flag != 0);
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(static_cast<qint64>(node.u.int64));
    case MPV_FORMAT_DOUBLE:
        return QVariant(node.u.double_);
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList result;
        if (!node.u.list) {
            return result;
        }
        result.reserve(node.u.list->num);
        for (int index = 0; index < node.u.list->num; ++index) {
            result.append(decodeNode(node.u.list->values[index]));
        }
        return result;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap result;
        if (!node.u.list) {
            return result;
        }
        for (int index = 0; index < node.u.list->num; ++index) {
            const char *key = node.u.list->keys[index];
            if (key) {
                result.insert(
                    QString::fromUtf8(key),
                    decodeNode(node.u.list->values[index]));
            }
        }
        return result;
    }
    case MPV_FORMAT_BYTE_ARRAY: {
        const mpv_byte_array *bytes = node.u.ba;
        if (!bytes || !bytes->data || bytes->size == 0) {
            return QByteArray();
        }
        return QByteArray(
            static_cast<const char *>(bytes->data),
            static_cast<qsizetype>(bytes->size));
    }
    case MPV_FORMAT_NODE:
        return {};
    }
    return {};
}

QVariant decodeProperty(const mpv_event_property &property)
{
    if (!property.data || property.format == MPV_FORMAT_NONE) {
        return {};
    }

    switch (property.format) {
    case MPV_FORMAT_FLAG:
        return QVariant(
            *static_cast<const int *>(property.data) != 0);
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(
            static_cast<qint64>(
                *static_cast<const int64_t *>(property.data)));
    case MPV_FORMAT_DOUBLE:
        return QVariant(
            *static_cast<const double *>(property.data));
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING: {
        const char *value =
            *static_cast<char *const *>(property.data);
        return value ? QVariant(QString::fromUtf8(value)) : QVariant();
    }
    case MPV_FORMAT_NODE:
        return decodeNode(
            *static_cast<const mpv_node *>(property.data));
    case MPV_FORMAT_BYTE_ARRAY: {
        mpv_node node{};
        node.format = MPV_FORMAT_BYTE_ARRAY;
        node.u.ba =
            static_cast<mpv_byte_array *>(property.data);
        return decodeNode(node);
    }
    case MPV_FORMAT_NODE_ARRAY:
    case MPV_FORMAT_NODE_MAP:
    case MPV_FORMAT_NONE:
        return {};
    }
    return {};
}

QString eventName(mpv_event_id id)
{
    const char *name = mpv_event_name(id);
    return name ? QString::fromUtf8(name)
                : QStringLiteral("unknown");
}

MpvEvent copyEvent(const mpv_event &source)
{
    MpvEvent event;
    event.id = static_cast<int>(source.event_id);
    event.name = eventName(source.event_id);
    event.errorCode = source.error;
    if (source.error < 0) {
        event.errorMessage =
            QString::fromUtf8(mpv_error_string(source.error));
    }
    event.replyUserdata = source.reply_userdata;

    switch (source.event_id) {
    case MPV_EVENT_GET_PROPERTY_REPLY:
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *property =
            static_cast<const mpv_event_property *>(source.data);
        if (property) {
            event.data.insert(
                QStringLiteral("name"),
                property->name
                    ? QString::fromUtf8(property->name)
                    : QString());
            event.data.insert(
                QStringLiteral("format"),
                static_cast<int>(property->format));
            event.data.insert(
                QStringLiteral("available"),
                property->format != MPV_FORMAT_NONE
                    && property->data != nullptr);
            event.data.insert(
                QStringLiteral("value"), decodeProperty(*property));
        }
        break;
    }
    case MPV_EVENT_COMMAND_REPLY: {
        const auto *command =
            static_cast<const mpv_event_command *>(source.data);
        if (command) {
            event.data.insert(
                QStringLiteral("result"),
                decodeNode(command->result));
        }
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        const auto *message =
            static_cast<const mpv_event_log_message *>(source.data);
        if (message) {
            event.data.insert(
                QStringLiteral("prefix"),
                message->prefix
                    ? QString::fromUtf8(message->prefix)
                    : QString());
            event.data.insert(
                QStringLiteral("level"),
                message->level
                    ? QString::fromUtf8(message->level)
                    : QString());
            event.data.insert(
                QStringLiteral("text"),
                message->text
                    ? QString::fromUtf8(message->text).trimmed()
                    : QString());
            event.data.insert(
                QStringLiteral("numericLevel"),
                static_cast<int>(message->log_level));
        }
        break;
    }
    case MPV_EVENT_START_FILE: {
        const auto *start =
            static_cast<const mpv_event_start_file *>(source.data);
        if (start) {
            event.data.insert(
                QStringLiteral("playlistEntryId"),
                static_cast<qint64>(start->playlist_entry_id));
        }
        break;
    }
    case MPV_EVENT_END_FILE: {
        const auto *end =
            static_cast<const mpv_event_end_file *>(source.data);
        if (end) {
            event.data.insert(
                QStringLiteral("reason"),
                static_cast<int>(end->reason));
            event.data.insert(
                QStringLiteral("endError"), end->error);
            event.data.insert(
                QStringLiteral("playlistEntryId"),
                static_cast<qint64>(end->playlist_entry_id));
            event.data.insert(
                QStringLiteral("playlistInsertId"),
                static_cast<qint64>(end->playlist_insert_id));
            event.data.insert(
                QStringLiteral("playlistInsertCount"),
                end->playlist_insert_num_entries);
        }
        break;
    }
    case MPV_EVENT_CLIENT_MESSAGE: {
        const auto *message =
            static_cast<const mpv_event_client_message *>(source.data);
        QStringList arguments;
        if (message) {
            arguments.reserve(message->num_args);
            for (int index = 0; index < message->num_args; ++index) {
                arguments.append(
                    QString::fromUtf8(message->args[index]));
            }
        }
        event.data.insert(QStringLiteral("arguments"), arguments);
        break;
    }
    case MPV_EVENT_HOOK: {
        const auto *hook =
            static_cast<const mpv_event_hook *>(source.data);
        if (hook) {
            event.data.insert(
                QStringLiteral("name"),
                hook->name
                    ? QString::fromUtf8(hook->name)
                    : QString());
            event.data.insert(
                QStringLiteral("hookId"),
                QVariant::fromValue<qulonglong>(hook->id));
        }
        break;
    }
    default:
        break;
    }
    return event;
}
}

class MpvEventThread final : public QThread {
public:
    MpvEventThread(MpvCore *core, mpv_handle *handle)
        : m_core(core),
          m_handle(handle)
    {
    }

    void requestStop()
    {
        m_stopRequested.store(true, std::memory_order_release);
        if (m_handle
            && !m_shutdownReceived.load(std::memory_order_acquire)) {
            mpv_wakeup(m_handle);
        }
    }

protected:
    void run() override
    {
        while (!m_stopRequested.load(std::memory_order_acquire)) {
            mpv_event *rawEvent = mpv_wait_event(m_handle, -1.0);
            if (!rawEvent) {
                continue;
            }
            if (rawEvent->event_id == MPV_EVENT_NONE) {
                continue;
            }

            MpvEvent event = copyEvent(*rawEvent);
            if (rawEvent->event_id == MPV_EVENT_START_FILE) {
                char *path = nullptr;
                if (mpv_get_property(
                        m_handle, "path", MPV_FORMAT_STRING,
                        &path) >= 0
                    && path) {
                    event.data.insert(
                        QStringLiteral("path"),
                        QString::fromUtf8(path));
                    mpv_free(path);
                }
            }
            const bool isShutdown =
                rawEvent->event_id == MPV_EVENT_SHUTDOWN;
            if (isShutdown) {
                m_shutdownReceived.store(
                    true, std::memory_order_release);
                m_core->m_shutdownComplete.store(
                    true, std::memory_order_release);
            }

            QMetaObject::invokeMethod(
                m_core,
                [core = m_core, event = std::move(event)] {
                    core->dispatchEvent(event);
                },
                Qt::QueuedConnection);

            if (isShutdown) {
                break;
            }
        }
    }

private:
    MpvCore *m_core = nullptr;
    mpv_handle *m_handle = nullptr;
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_shutdownReceived = false;
};

MpvCore::MpvCore(QObject *parent)
    : QObject(parent),
      m_mpv(mpv_create())
{
    if (!m_mpv) {
        throw std::runtime_error("mpv_create failed");
    }

    try {
        const QSettings settings;
        const QString configDirectory =
            PlayerConfiguration::mpvConfigDirectory();
        QDir().mkpath(configDirectory);
        const QByteArray encodedConfigDirectory =
            QDir::toNativeSeparators(configDirectory).toUtf8();
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "config", "yes"),
            "Setting config=yes failed");
        requireInitializationStep(
            mpv_set_option_string(
                m_mpv, "config-dir",
                encodedConfigDirectory.constData()),
            "Setting config-dir failed");
        const QByteArray encodedInputConfig =
            QDir::toNativeSeparators(
                PlayerConfiguration::inputConfigPath(
                    PlayerConfiguration::currentInputConfigName()))
                .toUtf8();
        requireInitializationStep(
            mpv_set_option_string(
                m_mpv, "input-conf",
                encodedInputConfig.constData()),
            "Setting input-conf failed");

        const QString watchLaterDirectory =
            QDir(QStandardPaths::writableLocation(
                     QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("watch_later"));
        QDir().mkpath(watchLaterDirectory);
        const QByteArray encodedWatchLater =
            QDir::toNativeSeparators(watchLaterDirectory).toUtf8();
        requireInitializationStep(
            mpv_set_option_string(
                m_mpv, "watch-later-directory",
                encodedWatchLater.constData()),
            "Setting watch-later-directory failed");
        requireInitializationStep(
            mpv_set_option_string(
                m_mpv, "save-position-on-quit",
                settings.value(
                    QStringLiteral("history/resumePlayback"), true)
                        .toBool() ? "yes" : "no"),
            "Setting save-position-on-quit failed");
        requireInitializationStep(
            mpv_set_option_string(
                m_mpv, "resume-playback",
                settings.value(
                    QStringLiteral("history/resumePlayback"), true)
                        .toBool() ? "yes" : "no"),
            "Setting resume-playback failed");
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "vo", "libmpv"),
            "Setting vo=libmpv failed");
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "hwdec", "auto"),
            "Setting hwdec=auto failed");
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "keep-open", "yes"),
            "Setting keep-open=yes failed");
        const auto restoreTextOption =
            [this, &settings](const char *option, const char *key) {
                if (!settings.contains(QString::fromLatin1(key))) {
                    return;
                }
                const QByteArray value =
                    settings.value(QString::fromLatin1(key))
                        .toString()
                        .toUtf8();
                requireInitializationStep(
                    mpv_set_option_string(m_mpv, option, value.constData()),
                    option);
            };
        restoreTextOption("sub-scale", "subtitles/scale");
        restoreTextOption("sub-font", "subtitles/font");
        restoreTextOption("sub-font-size", "subtitles/fontSize");
        restoreTextOption("sub-color", "subtitles/textColor");
        restoreTextOption(
            "sub-back-color", "subtitles/backgroundColor");
        restoreTextOption(
            "sub-border-color", "subtitles/borderColor");
        restoreTextOption(
            "sub-border-size", "subtitles/borderSize");
        restoreTextOption(
            "sub-ass-override", "subtitles/assOverride");
        restoreTextOption(
            "secondary-sub-ass-override",
            "subtitles/assOverride");
        restoreTextOption("audio-device", "audio/device");

        if (PlayerConfiguration::advancedSettingsEnabled()) {
            for (const ConfiguredMpvOption &option :
                 PlayerConfiguration::advancedOptions()) {
                QString name = option.name.trimmed();
                while (name.startsWith(QLatin1Char('-'))) {
                    name.remove(0, 1);
                }
                if (name.isEmpty()) {
                    continue;
                }
                const QByteArray encodedName = name.toUtf8();
                const QByteArray encodedValue =
                    (option.value.isEmpty()
                         ? QStringLiteral("yes") : option.value)
                        .toUtf8();
                const int result = mpv_set_option_string(
                    m_mpv, encodedName.constData(),
                    encodedValue.constData());
                if (result < 0) {
                    Logger::warn(
                        QStringLiteral(
                            "Ignoring invalid advanced mpv option '%1': %2")
                            .arg(name, QString::fromUtf8(
                                           mpv_error_string(result))));
                }
            }
        }
        // The render API requires libmpv output regardless of user config.
        requireInitializationStep(
            mpv_set_option_string(m_mpv, "vo", "libmpv"),
            "Restoring vo=libmpv failed");
        requireInitializationStep(
            mpv_request_log_messages(m_mpv, "warn"),
            "Requesting mpv log messages failed");

        observe(QStringLiteral("pause"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("duration"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("volume"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("mute"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("track-list"), MPV_FORMAT_NODE);
        observe(QStringLiteral("vid"), MPV_FORMAT_INT64);
        observe(QStringLiteral("aid"), MPV_FORMAT_INT64);
        observe(QStringLiteral("sid"), MPV_FORMAT_INT64);
        observe(QStringLiteral("secondary-sid"), MPV_FORMAT_INT64);
        observe(QStringLiteral("sub-visibility"), MPV_FORMAT_FLAG);
        observe(
            QStringLiteral("secondary-sub-visibility"),
            MPV_FORMAT_FLAG);
        observe(QStringLiteral("sub-delay"), MPV_FORMAT_DOUBLE);
        observe(
            QStringLiteral("secondary-sub-delay"),
            MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("sub-pos"), MPV_FORMAT_INT64);
        observe(
            QStringLiteral("secondary-sub-pos"),
            MPV_FORMAT_INT64);
        observe(QStringLiteral("sub-scale"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("sub-font"), MPV_FORMAT_STRING);
        observe(QStringLiteral("sub-font-size"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("sub-color"), MPV_FORMAT_STRING);
        observe(QStringLiteral("sub-back-color"), MPV_FORMAT_STRING);
        observe(
            QStringLiteral("sub-border-color"), MPV_FORMAT_STRING);
        observe(
            QStringLiteral("sub-border-size"), MPV_FORMAT_DOUBLE);
        observe(
            QStringLiteral("sub-ass-override"), MPV_FORMAT_STRING);
        observe(QStringLiteral("chapter"), MPV_FORMAT_INT64);
        observe(QStringLiteral("chapter-list"), MPV_FORMAT_NODE);
        observe(QStringLiteral("ab-loop-a"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("ab-loop-b"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("speed"), MPV_FORMAT_DOUBLE);
        observe(
            QStringLiteral("video-aspect-override"),
            MPV_FORMAT_STRING);
        observe(QStringLiteral("video-rotate"), MPV_FORMAT_INT64);
        observe(QStringLiteral("deinterlace"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("hwdec"), MPV_FORMAT_STRING);
        observe(QStringLiteral("brightness"), MPV_FORMAT_INT64);
        observe(QStringLiteral("contrast"), MPV_FORMAT_INT64);
        observe(QStringLiteral("saturation"), MPV_FORMAT_INT64);
        observe(QStringLiteral("gamma"), MPV_FORMAT_INT64);
        observe(QStringLiteral("hue"), MPV_FORMAT_INT64);
        observe(QStringLiteral("vf"), MPV_FORMAT_NODE);
        observe(QStringLiteral("audio-device-list"), MPV_FORMAT_NODE);
        observe(QStringLiteral("audio-device"), MPV_FORMAT_STRING);
        observe(QStringLiteral("audio-channels"), MPV_FORMAT_STRING);
        observe(QStringLiteral("audio-delay"), MPV_FORMAT_DOUBLE);
        observe(QStringLiteral("af"), MPV_FORMAT_NODE);
        observe(QStringLiteral("idle-active"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("eof-reached"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("paused-for-cache"), MPV_FORMAT_FLAG);
        observe(
            QStringLiteral("cache-buffering-state"),
            MPV_FORMAT_INT64);
        observe(QStringLiteral("cache-speed"), MPV_FORMAT_INT64);
        observe(
            QStringLiteral("demuxer-cache-duration"),
            MPV_FORMAT_DOUBLE);
        observe(
            QStringLiteral("demuxer-cache-idle"),
            MPV_FORMAT_FLAG);
        observe(
            QStringLiteral("demuxer-cache-state"),
            MPV_FORMAT_NODE);
        observe(
            QStringLiteral("demuxer-via-network"),
            MPV_FORMAT_FLAG);
        observe(QStringLiteral("seeking"), MPV_FORMAT_FLAG);
        observe(QStringLiteral("playlist"), MPV_FORMAT_NODE);
        observe(QStringLiteral("playlist-pos"), MPV_FORMAT_INT64);
        observe(QStringLiteral("loop-file"), MPV_FORMAT_STRING);
        observe(QStringLiteral("loop-playlist"), MPV_FORMAT_STRING);

        requireInitializationStep(
            mpv_initialize(m_mpv), "mpv_initialize failed");

        // mpv 0.38 omits the secondary subtitle selection and delay from
        // its default watch-later set. Keep both subtitle layers symmetrical
        // unless the user explicitly supplied watch-later-options.
        const QList<ConfiguredMpvOption> configuredAdvancedOptions =
            PlayerConfiguration::advancedOptions();
        const bool userConfiguredWatchLaterOptions =
            PlayerConfiguration::advancedSettingsEnabled()
            && std::any_of(
                configuredAdvancedOptions.cbegin(),
                configuredAdvancedOptions.cend(),
                [](const ConfiguredMpvOption &option) {
                    QString name = option.name.trimmed();
                    while (name.startsWith(QLatin1Char('-'))) {
                        name.remove(0, 1);
                    }
                    return name == QStringLiteral("watch-later-options");
                });
        if (!userConfiguredWatchLaterOptions) {
            QStringList savedOptions =
                getString(QStringLiteral("watch-later-options"))
                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
            const auto addAlongside =
                [&savedOptions](const QString &primary,
                                const QString &secondary) {
                    if (savedOptions.contains(primary)
                        && !savedOptions.contains(secondary)) {
                        savedOptions.append(secondary);
                    }
                };
            addAlongside(
                QStringLiteral("sid"),
                QStringLiteral("secondary-sid"));
            addAlongside(
                QStringLiteral("sub-delay"),
                QStringLiteral("secondary-sub-delay"));
            setString(
                QStringLiteral("watch-later-options"),
                savedOptions.join(QLatin1Char(',')));
        }
        m_eventThread =
            std::make_unique<MpvEventThread>(this, m_mpv);
        m_eventThread->start(QThread::HighPriority);
    } catch (...) {
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

    if (m_eventThread) {
        m_eventThread->requestStop();
        m_eventThread->wait();
        m_eventThread.reset();
    }
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pendingRequests.clear();
    }
    {
        const QMutexLocker lock(&m_hookMutex);
        m_hooks.clear();
    }
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

quint64 MpvCore::nextRequestId()
{
    return m_nextRequestId.fetch_add(
        1, std::memory_order_relaxed);
}

quint64 MpvCore::command(
    const QVariantList &arguments, CommandCallback callback)
{
    if (!m_mpv || arguments.isEmpty() || isShuttingDown()) {
        return 0;
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

    const quint64 requestId = nextRequestId();
    const QString commandName = arguments.constFirst().toString();
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pendingRequests.insert(
            requestId,
            {RequestKind::Command, commandName, std::move(callback)});
    }

    const int result = mpv_command_async(
        m_mpv, requestId, commandArguments.data());
    if (result >= 0) {
        return requestId;
    }

    PendingRequest pending;
    {
        const QMutexLocker lock(&m_pendingMutex);
        pending = m_pendingRequests.take(requestId);
    }
    MpvCommandResult commandResult{
        requestId, commandName, {}, result, errorMessage(result)};
    reportError(
        QStringLiteral("Command '%1'").arg(commandName),
        result, true);
    emit commandFinished(commandResult);
    if (pending.callback) {
        pending.callback(commandResult);
    }
    return 0;
}

void MpvCore::abortCommand(quint64 requestId)
{
    if (m_mpv && requestId != 0 && !isShuttingDown()) {
        mpv_abort_async_command(m_mpv, requestId);
    }
}

quint64 MpvCore::setPropertyAsync(
    const QString &name, mpv_format format, void *data)
{
    if (!m_mpv || isShuttingDown()) {
        return 0;
    }

    const quint64 requestId = nextRequestId();
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pendingRequests.insert(
            requestId,
            {RequestKind::SetProperty, name, {}});
    }
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_set_property_async(
        m_mpv, requestId, encodedName.constData(), format, data);
    if (result >= 0) {
        return requestId;
    }

    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pendingRequests.remove(requestId);
    }
    reportError(
        QStringLiteral("Setting property '%1'").arg(name),
        result, true);
    return 0;
}

quint64 MpvCore::setFlag(const QString &name, bool value)
{
    int data = value ? 1 : 0;
    return setPropertyAsync(name, MPV_FORMAT_FLAG, &data);
}

quint64 MpvCore::setInt(const QString &name, qint64 value)
{
    int64_t data = static_cast<int64_t>(value);
    return setPropertyAsync(name, MPV_FORMAT_INT64, &data);
}

quint64 MpvCore::setDouble(const QString &name, double value)
{
    return setPropertyAsync(name, MPV_FORMAT_DOUBLE, &value);
}

quint64 MpvCore::setString(
    const QString &name, const QString &value)
{
    QByteArray encodedValue = value.toUtf8();
    char *data = encodedValue.data();
    return setPropertyAsync(name, MPV_FORMAT_STRING, &data);
}

qint64 MpvCore::getInt(const QString &name) const
{
    if (!m_mpv || isShuttingDown()) {
        return 0;
    }
    int64_t value = 0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_INT64, &value);
    if (result < 0) {
        const_cast<MpvCore *>(this)->reportError(
            QStringLiteral("Getting property '%1'").arg(name),
            result, true);
        return 0;
    }
    return static_cast<qint64>(value);
}

double MpvCore::getDouble(const QString &name) const
{
    if (!m_mpv || isShuttingDown()) {
        return 0.0;
    }
    double value = 0.0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_DOUBLE, &value);
    if (result < 0) {
        const_cast<MpvCore *>(this)->reportError(
            QStringLiteral("Getting property '%1'").arg(name),
            result, true);
        return 0.0;
    }
    return value;
}

bool MpvCore::getFlag(const QString &name) const
{
    if (!m_mpv || isShuttingDown()) {
        return false;
    }
    int value = 0;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        const_cast<MpvCore *>(this)->reportError(
            QStringLiteral("Getting property '%1'").arg(name),
            result, true);
        return false;
    }
    return value != 0;
}

QString MpvCore::getString(const QString &name) const
{
    if (!m_mpv || isShuttingDown()) {
        return {};
    }
    char *value = nullptr;
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_get_property(
        m_mpv, encodedName.constData(), MPV_FORMAT_STRING, &value);
    if (result < 0) {
        const_cast<MpvCore *>(this)->reportError(
            QStringLiteral("Getting property '%1'").arg(name),
            result, true);
        return {};
    }

    const QString converted =
        value ? QString::fromUtf8(value) : QString();
    mpv_free(value);
    return converted;
}

QVariant MpvCore::getNode(const QString &name) const
{
    if (!m_mpv || isShuttingDown()) {
        return {};
    }
    mpv_node node{};
    const QByteArray encodedName = name.toUtf8();
    if (mpv_get_property(
            m_mpv, encodedName.constData(), MPV_FORMAT_NODE,
            &node) < 0) {
        return {};
    }
    const QVariant value = decodeNode(node);
    mpv_free_node_contents(&node);
    return value;
}

void MpvCore::observe(const QString &name, mpv_format format)
{
    if (!m_mpv || isShuttingDown()) {
        return;
    }
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_observe_property(
        m_mpv, 0, encodedName.constData(), format);
    if (result < 0) {
        reportError(
            QStringLiteral("Observing property '%1'").arg(name),
            result, true);
    }
}

quint64 MpvCore::addHook(
    const QString &name, int priority, HookHandler handler)
{
    if (!m_mpv || name.isEmpty() || !handler || isShuttingDown()) {
        return 0;
    }

    const quint64 registrationId =
        m_nextHookId.fetch_add(1, std::memory_order_relaxed);
    const QByteArray encodedName = name.toUtf8();
    const int result = mpv_hook_add(
        m_mpv, registrationId, encodedName.constData(), priority);
    if (result < 0) {
        reportError(
            QStringLiteral("Registering hook '%1'").arg(name),
            result, true);
        return 0;
    }

    const QMutexLocker lock(&m_hookMutex);
    m_hooks.insert(registrationId, std::move(handler));
    return registrationId;
}

void MpvCore::removeHook(quint64 registrationId)
{
    const QMutexLocker lock(&m_hookMutex);
    m_hooks.remove(registrationId);
}

void MpvCore::shutdown()
{
    if (!m_mpv
        || m_shuttingDown.exchange(
            true, std::memory_order_acq_rel)) {
        return;
    }

    mpv_unobserve_property(m_mpv, 0);
    const quint64 requestId = nextRequestId();
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pendingRequests.insert(
            requestId,
            {RequestKind::Quit, QStringLiteral("quit"), {}});
    }
    const char *arguments[] = {"quit", nullptr};
    const int result =
        mpv_command_async(m_mpv, requestId, arguments);
    if (result < 0) {
        {
            const QMutexLocker lock(&m_pendingMutex);
            m_pendingRequests.remove(requestId);
        }
        reportError(QStringLiteral("Command 'quit'"), result, false);
        emit mpvShutdown();
    }
}

void MpvCore::dispatchEvent(const MpvEvent &event)
{
    emit eventReceived(event);

    switch (static_cast<mpv_event_id>(event.id)) {
    case MPV_EVENT_GET_PROPERTY_REPLY:
    case MPV_EVENT_SET_PROPERTY_REPLY:
    case MPV_EVENT_COMMAND_REPLY:
        dispatchRequestReply(event);
        break;
    case MPV_EVENT_PROPERTY_CHANGE:
        emit propertyChanged(
            event.data.value(QStringLiteral("name")).toString(),
            event.data.value(QStringLiteral("value")));
        break;
    case MPV_EVENT_LOG_MESSAGE:
        emit mpvLogMessage(
            event.data.value(QStringLiteral("prefix")).toString(),
            event.data.value(QStringLiteral("level")).toString(),
            event.data.value(QStringLiteral("text")).toString());
        break;
    case MPV_EVENT_START_FILE:
        emit fileStarted(
            event.data.value(QStringLiteral("path")).toString());
        break;
    case MPV_EVENT_FILE_LOADED:
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        const int endError =
            event.data.value(QStringLiteral("endError")).toInt();
        MpvEndFileInfo info;
        info.reason =
            event.data.value(QStringLiteral("reason")).toInt();
        info.errorCode = endError;
        if (endError < 0) {
            info.errorMessage = errorMessage(endError);
        }
        info.playlistEntryId =
            event.data.value(
                QStringLiteral("playlistEntryId")).toLongLong();
        info.playlistInsertId =
            event.data.value(
                QStringLiteral("playlistInsertId")).toLongLong();
        info.playlistInsertCount =
            event.data.value(
                QStringLiteral("playlistInsertCount")).toInt();
        emit fileEnded(info);
        break;
    }
    case MPV_EVENT_VIDEO_RECONFIG:
        emit videoReconfig();
        break;
    case MPV_EVENT_AUDIO_RECONFIG:
        emit audioReconfig();
        break;
    case MPV_EVENT_SEEK:
        emit seekStarted();
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        emit playbackRestarted();
        break;
    case MPV_EVENT_CLIENT_MESSAGE:
        emit clientMessage(
            event.data.value(
                QStringLiteral("arguments")).toStringList());
        break;
    case MPV_EVENT_QUEUE_OVERFLOW:
        Logger::error(
            QStringLiteral(
                "libmpv event queue overflowed; state resync required"));
        emit eventQueueOverflow();
        emit mpvError(
            QStringLiteral("Event queue overflow"),
            MPV_ERROR_EVENT_QUEUE_FULL,
            errorMessage(MPV_ERROR_EVENT_QUEUE_FULL), true);
        break;
    case MPV_EVENT_HOOK: {
        HookHandler handler;
        {
            const QMutexLocker lock(&m_hookMutex);
            handler = m_hooks.value(event.replyUserdata);
        }
        const quint64 hookId =
            event.data.value(
                QStringLiteral("hookId")).toULongLong();
        if (!handler) {
            Logger::warn(
                QStringLiteral(
                    "No handler for mpv hook %1; continuing it")
                    .arg(hookId));
            continueHook(hookId);
            break;
        }

        auto completed =
            std::make_shared<std::atomic_bool>(false);
        QPointer<MpvCore> guard(this);
        HookContinuation continuation =
            [guard, completed, hookId] {
                if (completed->exchange(
                        true, std::memory_order_acq_rel)) {
                    return;
                }
                if (!guard) {
                    return;
                }
                QMetaObject::invokeMethod(
                    guard,
                    [guard, hookId] {
                        if (guard) {
                            guard->continueHook(hookId);
                        }
                    },
                    Qt::QueuedConnection);
            };
        try {
            handler(
                event.data.value(
                    QStringLiteral("name")).toString(),
                continuation);
        } catch (const std::exception &exception) {
            Logger::error(
                QStringLiteral("mpv hook handler failed: %1")
                    .arg(QString::fromUtf8(exception.what())));
            continuation();
        } catch (...) {
            Logger::error(
                QStringLiteral("mpv hook handler failed"));
            continuation();
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

void MpvCore::dispatchRequestReply(const MpvEvent &event)
{
    PendingRequest pending;
    {
        const QMutexLocker lock(&m_pendingMutex);
        pending = m_pendingRequests.take(event.replyUserdata);
    }
    if (pending.label.isEmpty()) {
        return;
    }

    if (pending.kind == RequestKind::Command
        || pending.kind == RequestKind::Quit) {
        MpvCommandResult result;
        result.requestId = event.replyUserdata;
        result.command = pending.label;
        result.value =
            event.data.value(QStringLiteral("result"));
        result.errorCode = event.errorCode;
        result.errorMessage = event.errorMessage;
        if (!result.succeeded()) {
            reportError(
                QStringLiteral("Command '%1'").arg(pending.label),
                result.errorCode, pending.kind != RequestKind::Quit);
        }
        emit commandFinished(result);
        if (pending.callback) {
            pending.callback(result);
        }
    } else if (event.errorCode < 0) {
        reportError(
            QStringLiteral("Setting property '%1'")
                .arg(pending.label),
            event.errorCode, true);
    }
}

void MpvCore::continueHook(quint64 hookId)
{
    if (!m_mpv || hookId == 0
        || m_shutdownComplete.load(std::memory_order_acquire)) {
        return;
    }
    const int result = mpv_hook_continue(m_mpv, hookId);
    if (result < 0) {
        reportError(
            QStringLiteral("Continuing hook %1").arg(hookId),
            result, true);
    }
}

void MpvCore::reportError(
    const QString &context, int errorCode, bool recoverable)
{
    const QString message = errorMessage(errorCode);
    const QString formatted =
        QStringLiteral("%1 failed: %2 (%3)")
            .arg(context, message)
            .arg(errorCode);
    if (recoverable) {
        Logger::warn(formatted);
    } else {
        Logger::error(formatted);
    }
    emit mpvError(context, errorCode, message, recoverable);
}

QString MpvCore::errorMessage(int errorCode)
{
    const char *message = mpv_error_string(errorCode);
    return message ? QString::fromUtf8(message)
                   : QStringLiteral("Unknown libmpv error");
}

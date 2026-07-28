#pragma once

#include "Mpv/MpvEvent.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVariant>

#include <mpv/client.h>

#include <atomic>
#include <functional>
#include <memory>

class MpvEventThread;

class MpvCore final : public QObject {
    Q_OBJECT

public:
    using CommandCallback =
        std::function<void(const MpvCommandResult &)>;
    using HookContinuation = std::function<void()>;
    using HookHandler =
        std::function<void(const QString &, HookContinuation)>;

    explicit MpvCore(QObject *parent = nullptr);
    ~MpvCore() override;

    MpvCore(const MpvCore &) = delete;
    MpvCore &operator=(const MpvCore &) = delete;

    [[nodiscard]] mpv_handle *handle() const noexcept { return m_mpv; }
    [[nodiscard]] bool isShuttingDown() const noexcept
    {
        return m_shuttingDown.load(std::memory_order_acquire);
    }

    quint64 command(
        const QVariantList &arguments,
        CommandCallback callback = {});
    void abortCommand(quint64 requestId);

    quint64 setFlag(const QString &name, bool value);
    quint64 setInt(const QString &name, qint64 value);
    quint64 setDouble(const QString &name, double value);
    quint64 setString(const QString &name, const QString &value);

    [[nodiscard]] qint64 getInt(const QString &name) const;
    [[nodiscard]] double getDouble(const QString &name) const;
    [[nodiscard]] bool getFlag(const QString &name) const;
    [[nodiscard]] QString getString(const QString &name) const;
    [[nodiscard]] QVariant getNode(const QString &name) const;

    void observe(const QString &name, mpv_format format);
    quint64 addHook(
        const QString &name, int priority, HookHandler handler);
    void removeHook(quint64 registrationId);
    void shutdown();

signals:
    void eventReceived(const MpvEvent &event);
    void commandFinished(const MpvCommandResult &result);
    void propertyChanged(const QString &name, const QVariant &value);
    void mpvLogMessage(const QString &prefix, const QString &level,
                       const QString &text);
    void fileStarted(const QString &path);
    void fileLoaded();
    void fileEnded(const MpvEndFileInfo &info);
    void videoReconfig();
    void audioReconfig();
    void seekStarted();
    void playbackRestarted();
    void clientMessage(const QStringList &arguments);
    void eventQueueOverflow();
    void mpvError(const QString &context, int errorCode,
                  const QString &message, bool recoverable);
    void mpvShutdown();

private:
    friend class MpvEventThread;

    enum class RequestKind {
        Command,
        SetProperty,
        Quit,
    };

    struct PendingRequest {
        RequestKind kind = RequestKind::Command;
        QString label;
        CommandCallback callback;
    };

    quint64 nextRequestId();
    quint64 setPropertyAsync(
        const QString &name, mpv_format format, void *data);
    void dispatchEvent(const MpvEvent &event);
    void dispatchRequestReply(const MpvEvent &event);
    void continueHook(quint64 hookId);
    void reportError(const QString &context, int errorCode,
                     bool recoverable);
    static QString errorMessage(int errorCode);

    mpv_handle *m_mpv = nullptr;
    std::unique_ptr<MpvEventThread> m_eventThread;
    std::atomic_bool m_shuttingDown = false;
    std::atomic_bool m_shutdownComplete = false;
    std::atomic<quint64> m_nextRequestId = 1;
    std::atomic<quint64> m_nextHookId = 1;
    QMutex m_pendingMutex;
    QHash<quint64, PendingRequest> m_pendingRequests;
    QMutex m_hookMutex;
    QHash<quint64, HookHandler> m_hooks;
};

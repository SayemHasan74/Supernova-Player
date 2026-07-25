#pragma once

#include <QObject>
#include <QVariant>

#include <mpv/client.h>

class MpvCore final : public QObject {
    Q_OBJECT

public:
    explicit MpvCore(QObject *parent = nullptr);
    ~MpvCore() override;

    MpvCore(const MpvCore &) = delete;
    MpvCore &operator=(const MpvCore &) = delete;

    [[nodiscard]] mpv_handle *handle() const noexcept { return m_mpv; }

    void command(const QVariantList &arguments);
    void setFlag(const QString &name, bool value);
    void setInt(const QString &name, qint64 value);
    void setDouble(const QString &name, double value);
    void setString(const QString &name, const QString &value);
    [[nodiscard]] qint64 getInt(const QString &name) const;
    [[nodiscard]] double getDouble(const QString &name) const;
    [[nodiscard]] bool getFlag(const QString &name) const;
    [[nodiscard]] QString getString(const QString &name) const;
    void observe(const QString &name, mpv_format format);

signals:
    void propertyChanged(const QString &name, const QVariant &value);
    void mpvLogMessage(const QString &prefix, const QString &level,
                       const QString &text);
    void mpvShutdown();

private slots:
    void drainEventQueue();

private:
    void handleEvent(mpv_event *event);
    void logError(const QString &operation, int errorCode) const;
    static void wakeupTrampoline(void *context);

    mpv_handle *m_mpv = nullptr;
};

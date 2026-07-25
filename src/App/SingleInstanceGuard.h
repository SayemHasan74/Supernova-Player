#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

class QLocalServer;
class QLocalSocket;
class QSharedMemory;

class SingleInstanceGuard final : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    bool tryAcquire(const QStringList &argsToForward);

signals:
    void argumentsReceivedFromNewInstance(const QStringList &args);

private:
    void onNewConnection();
    void readMessage(QLocalSocket *socket);

    static constexpr auto kSharedMemoryKey = "Supernova.SingleInstance.v1";
    static constexpr auto kServerName = "Supernova.IPC.v1";
    static constexpr int kConnectTimeoutMs = 1000;

    std::unique_ptr<QSharedMemory> m_sharedMemory;
    std::unique_ptr<QLocalServer> m_server;
};

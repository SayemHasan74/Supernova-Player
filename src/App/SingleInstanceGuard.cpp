#include "App/SingleInstanceGuard.h"

#include "Core/Logger.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QThread>

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject(parent)
{
}
SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server) {
        m_server->close();
    }
    if (m_sharedMemory && m_sharedMemory->isAttached()) {
        m_sharedMemory->detach();
    }
}

bool SingleInstanceGuard::tryAcquire(const QStringList &argsToForward)
{
    m_sharedMemory = std::make_unique<QSharedMemory>(
        QString::fromLatin1(kSharedMemoryKey));

    if (!m_sharedMemory->create(1)) {
        if (m_sharedMemory->error() != QSharedMemory::AlreadyExists) {
            Logger::error(QStringLiteral("Could not create the single-instance lock: %1")
                              .arg(m_sharedMemory->errorString()));
            return false;
        }

        QLocalSocket socket;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kConnectTimeoutMs) {
            socket.abort();
            socket.connectToServer(QString::fromLatin1(kServerName));
            const int remaining = kConnectTimeoutMs - static_cast<int>(timer.elapsed());
            if (socket.waitForConnected(qMin(remaining, 100))) {
                break;
            }
            QThread::msleep(25);
        }

        if (socket.state() != QLocalSocket::ConnectedState) {
            Logger::error(QStringLiteral("Another instance owns the lock, but its IPC server is unavailable: %1")
                              .arg(socket.errorString()));
            return false;
        }

        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << argsToForward;

        if (socket.write(payload) != payload.size()
            || !socket.waitForBytesWritten(kConnectTimeoutMs)) {
            Logger::error(QStringLiteral("Could not forward arguments to the primary instance: %1")
                              .arg(socket.errorString()));
            return false;
        }

        socket.disconnectFromServer();
        if (socket.state() != QLocalSocket::UnconnectedState) {
            socket.waitForDisconnected(kConnectTimeoutMs);
        }
        return false;
    }

    m_server = std::make_unique<QLocalServer>();
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    if (!m_server->listen(QString::fromLatin1(kServerName))) {
        Logger::error(QStringLiteral("Could not start the single-instance IPC server: %1")
                          .arg(m_server->errorString()));
        m_sharedMemory->detach();
        return false;
    }

    connect(m_server.get(), &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
    return true;
}

void SingleInstanceGuard::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            readMessage(socket);
        });
        connect(socket, &QLocalSocket::disconnected,
                socket, &QObject::deleteLater);

        if (socket->bytesAvailable() > 0) {
            readMessage(socket);
        }
    }
}

void SingleInstanceGuard::readMessage(QLocalSocket *socket)
{
    QDataStream stream(socket);
    stream.setVersion(QDataStream::Qt_6_8);
    stream.startTransaction();

    QStringList arguments;
    stream >> arguments;
    if (!stream.commitTransaction()) {
        return;
    }

    emit argumentsReceivedFromNewInstance(arguments);
    socket->disconnectFromServer();
}

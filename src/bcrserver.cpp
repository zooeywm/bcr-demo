#include "bcrserver.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpSocket>

BcrServer::BcrServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &BcrServer::onNewConnection);
}

bool BcrServer::listen(const QHostAddress &address, quint16 port)
{
    return m_server.listen(address, port);
}

void BcrServer::close()
{
    m_server.close();
}

QHostAddress BcrServer::serverAddress() const
{
    return m_server.serverAddress();
}

quint16 BcrServer::serverPort() const
{
    return m_server.serverPort();
}

QString BcrServer::errorString() const
{
    return m_server.errorString();
}

void BcrServer::sendReply(QTcpSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    socket->write(payload);
    socket->flush();
}

void BcrServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());

        connect(socket, &QTcpSocket::readyRead, this, &BcrServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &BcrServer::onDisconnected);

        const QString peer = QStringLiteral("%1:%2")
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort());
        emit clientConnected(peer);
    }
}

void BcrServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    qsizetype newlineIndex = -1;
    while ((newlineIndex = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(newlineIndex).trimmed();
        buffer.remove(0, newlineIndex + 1);

        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            sendReply(socket,
                      QJsonObject{
                          {QStringLiteral("ok"), false},
                          {QStringLiteral("error"),
                           QStringLiteral("Invalid JSON message: %1").arg(parseError.errorString())},
                      });
            continue;
        }

        emit commandReceived(document.object(), socket);
    }
}

void BcrServer::onDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    const QString peer = QStringLiteral("%1:%2")
                             .arg(socket->peerAddress().toString())
                             .arg(socket->peerPort());

    m_buffers.remove(socket);
    socket->deleteLater();

    emit clientDisconnected(peer);
}

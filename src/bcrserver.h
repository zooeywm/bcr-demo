#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

class BcrServer : public QObject
{
    Q_OBJECT

public:
    explicit BcrServer(QObject *parent = nullptr);

    bool listen(const QHostAddress &address, quint16 port);
    void close();
    QHostAddress serverAddress() const;
    quint16 serverPort() const;
    QString errorString() const;

    void sendReply(QTcpSocket *socket, const QJsonObject &message);

signals:
    void clientConnected(const QString &peer);
    void clientDisconnected(const QString &peer);
    void commandReceived(const QJsonObject &message, QTcpSocket *socket);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

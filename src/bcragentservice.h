#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

struct AgentSettings
{
    QString clientHost = QStringLiteral("127.0.0.1");
    quint16 clientPort = 45454;
    QString listenAddress = QStringLiteral("127.0.0.1");
    quint16 listenPort = 45455;
    int timeoutMs = 3000;
};

class BcrAgentService : public QObject
{
    Q_OBJECT

public:
    explicit BcrAgentService(const AgentSettings &settings, QObject *parent = nullptr);

    bool start();
    QString errorString() const;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void processRequest(QTcpSocket *socket, QByteArray &buffer);

    AgentSettings m_settings;
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

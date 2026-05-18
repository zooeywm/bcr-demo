#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QMainWindow>

class BcrServer;
class QLabel;
class QTcpSocket;
class QWebEngineView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QHostAddress &listenAddress, quint16 port, QWidget *parent = nullptr);

private slots:
    void onClientConnected(const QString &peer);
    void onClientDisconnected(const QString &peer);
    void onCommandReceived(const QJsonObject &message, QTcpSocket *socket);

private:
    void applyFullscreen(bool enabled);
    QJsonObject buildStateReply(bool ok, const QString &message = QString()) const;
    void updateWindowTitle();
    void updateStatusLabels();

    BcrServer *m_server;
    QWebEngineView *m_view;
    QLabel *m_connectionLabel;
    QLabel *m_urlLabel;
    QString m_lastPeer;
    bool m_isFullscreen = false;
};

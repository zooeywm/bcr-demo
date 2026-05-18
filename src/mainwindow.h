#pragma once

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>

class BcrServer;
class QLabel;
class QNetworkAccessManager;
class QTimer;
class QTcpSocket;
class QWebEngineScript;
class QWebEngineView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QHostAddress &listenAddress, quint16 port, const QUrl &agentBaseUrl = QUrl(), QWidget *parent = nullptr);

private slots:
    void onClientConnected(const QString &peer);
    void onClientDisconnected(const QString &peer);
    void onCommandReceived(const QJsonObject &message, QTcpSocket *socket);

private:
    void openUrlWithAuth(const QUrl &url, const QJsonObject &auth, bool fullscreenAfterLoad);
    void applyFullscreen(bool enabled);
    void applyAuthState(const QUrl &url, const QJsonObject &auth);
    void installStorageBootstrapScript(const QUrl &url, const QJsonObject &auth);
    void setCookiesForUrl(const QUrl &url, const QJsonArray &cookies);
    void scheduleStateSync();
    void syncStateToAgent();
    QJsonObject buildStateReply(bool ok, const QString &message = QString()) const;
    void updateWindowTitle();
    void updateStatusLabels();

    BcrServer *m_server;
    QWebEngineView *m_view;
    QNetworkAccessManager *m_networkAccessManager;
    QTimer *m_syncTimer;
    QLabel *m_connectionLabel;
    QLabel *m_urlLabel;
    QUrl m_agentBaseUrl;
    QJsonArray m_remoteTabs;
    QString m_lastPeer;
    bool m_isFullscreen = false;
};

#include "mainwindow.h"

#include "bcrserver.h"

#include <QLabel>
#include <QShortcut>
#include <QStatusBar>
#include <QTcpSocket>
#include <QToolBar>
#include <QUrl>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace {

constexpr auto kProtocolVersion = 1;

}

MainWindow::MainWindow(const QHostAddress &listenAddress, quint16 port, QWidget *parent)
    : QMainWindow(parent)
    , m_server(new BcrServer(this))
    , m_view(new QWebEngineView(this))
    , m_connectionLabel(new QLabel(this))
    , m_urlLabel(new QLabel(this))
{
    setCentralWidget(m_view);
    resize(1440, 900);

    auto *toolbar = addToolBar(QStringLiteral("BCR"));
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(QStringLiteral("当前页面:"), this));
    toolbar->addWidget(m_urlLabel);

    statusBar()->addPermanentWidget(m_connectionLabel);

    connect(m_server, &BcrServer::clientConnected, this, &MainWindow::onClientConnected);
    connect(m_server, &BcrServer::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(m_server, &BcrServer::commandReceived, this, &MainWindow::onCommandReceived);

    connect(m_view, &QWebEngineView::urlChanged, this, [this] {
        updateStatusLabels();
        updateWindowTitle();
    });

    connect(m_view->page(), &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest request) {
        request.accept();
        applyFullscreen(request.toggleOn());
    });

    auto *exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(exitShortcut, &QShortcut::activated, this, [this] {
        if (m_isFullscreen) {
            applyFullscreen(false);
        }
    });

    if (!m_server->listen(listenAddress, port)) {
        statusBar()->showMessage(QStringLiteral("监听失败: %1").arg(m_server->errorString()));
    } else {
        statusBar()->showMessage(QStringLiteral("监听中: %1:%2")
                                     .arg(m_server->serverAddress().toString())
                                     .arg(m_server->serverPort()));
    }

    updateStatusLabels();
    updateWindowTitle();
}

void MainWindow::onClientConnected(const QString &peer)
{
    m_lastPeer = peer;
    updateStatusLabels();
    statusBar()->showMessage(QStringLiteral("远程端已连接: %1").arg(peer), 3000);
}

void MainWindow::onClientDisconnected(const QString &peer)
{
    if (m_lastPeer == peer) {
        m_lastPeer.clear();
    }

    updateStatusLabels();
    statusBar()->showMessage(QStringLiteral("远程端已断开: %1").arg(peer), 3000);
}

void MainWindow::onCommandReceived(const QJsonObject &message, QTcpSocket *socket)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("hello")) {
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("helloAck")));
        return;
    }

    if (type == QStringLiteral("openUrl")) {
        const QUrl url = QUrl::fromUserInput(message.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || url.scheme().isEmpty()) {
            m_server->sendReply(socket, buildStateReply(false, QStringLiteral("Invalid URL")));
            return;
        }

        m_view->load(url);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("URL loaded")));
        return;
    }

    if (type == QStringLiteral("enterFullscreen")) {
        applyFullscreen(true);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("Entered fullscreen")));
        return;
    }

    if (type == QStringLiteral("exitFullscreen")) {
        applyFullscreen(false);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("Exited fullscreen")));
        return;
    }

    if (type == QStringLiteral("openUrlAndFullscreen")) {
        const QUrl url = QUrl::fromUserInput(message.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || url.scheme().isEmpty()) {
            m_server->sendReply(socket, buildStateReply(false, QStringLiteral("Invalid URL")));
            return;
        }

        m_view->load(url);
        applyFullscreen(true);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("URL loaded and fullscreen enabled")));
        return;
    }

    if (type == QStringLiteral("ping")) {
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("pong")));
        return;
    }

    m_server->sendReply(socket, buildStateReply(false, QStringLiteral("Unsupported command")));
}

void MainWindow::applyFullscreen(bool enabled)
{
    if (enabled == m_isFullscreen) {
        return;
    }

    m_isFullscreen = enabled;

    if (m_isFullscreen) {
        showFullScreen();
    } else {
        showNormal();
    }

    updateStatusLabels();
    updateWindowTitle();
}

QJsonObject MainWindow::buildStateReply(bool ok, const QString &message) const
{
    return QJsonObject{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("message"), message},
        {QStringLiteral("protocolVersion"), kProtocolVersion},
        {QStringLiteral("fullscreen"), m_isFullscreen},
        {QStringLiteral("url"), m_view->url().toString()},
        {QStringLiteral("peer"), m_lastPeer},
    };
}

void MainWindow::updateWindowTitle()
{
    const QString urlText = m_view->url().isEmpty() ? QStringLiteral("未加载页面") : m_view->url().toString();
    const QString mode = m_isFullscreen ? QStringLiteral("整屏") : QStringLiteral("窗口");
    setWindowTitle(QStringLiteral("BCR Demo | %1 | %2").arg(mode, urlText));
}

void MainWindow::updateStatusLabels()
{
    const QString peerText = m_lastPeer.isEmpty() ? QStringLiteral("等待远程端连接") : QStringLiteral("最近连接: %1").arg(m_lastPeer);
    const QString modeText = m_isFullscreen ? QStringLiteral("整屏") : QStringLiteral("窗口");
    const QString urlText = m_view->url().isEmpty() ? QStringLiteral("未加载页面") : m_view->url().toString();

    m_connectionLabel->setText(QStringLiteral("%1 | 模式: %2").arg(peerText, modeText));
    m_urlLabel->setText(urlText);
}

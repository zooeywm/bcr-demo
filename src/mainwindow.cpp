#include "mainwindow.h"

#include "bcrserver.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShortcut>
#include <QStatusBar>
#include <QTcpSocket>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QWebEngineCookieStore>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace {

constexpr auto kProtocolVersion = 1;
constexpr auto kStorageBootstrapScriptName = "bcr-storage-bootstrap";

QString originString(const QUrl &url)
{
    return QStringLiteral("%1://%2%3")
        .arg(url.scheme(),
             url.host(),
             url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString());
}

QNetworkCookie::SameSite sameSiteFromString(const QString &value)
{
    if (value == QStringLiteral("lax")) {
        return QNetworkCookie::SameSite::Lax;
    }

    if (value == QStringLiteral("strict")) {
        return QNetworkCookie::SameSite::Strict;
    }

    if (value == QStringLiteral("no_restriction")) {
        return QNetworkCookie::SameSite::None;
    }

    return QNetworkCookie::SameSite::Default;
}

}

MainWindow::MainWindow(const QHostAddress &listenAddress, quint16 port, const QUrl &agentBaseUrl, QWidget *parent)
    : QMainWindow(parent)
    , m_server(new BcrServer(this))
    , m_view(new QWebEngineView(this))
    , m_networkAccessManager(new QNetworkAccessManager(this))
    , m_syncTimer(new QTimer(this))
    , m_connectionLabel(new QLabel(this))
    , m_urlLabel(new QLabel(this))
    , m_agentBaseUrl(agentBaseUrl)
{
    setCentralWidget(m_view);
    resize(1440, 900);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(150);

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
        scheduleStateSync();
    });

    connect(m_view, &QWebEngineView::titleChanged, this, [this] {
        scheduleStateSync();
    });

    connect(m_syncTimer, &QTimer::timeout, this, &MainWindow::syncStateToAgent);

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

        openUrlWithAuth(url, message.value(QStringLiteral("auth")).toObject(), false);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("URL loaded with auth state")));
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

        openUrlWithAuth(url, message.value(QStringLiteral("auth")).toObject(), true);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("URL loaded with auth state and fullscreen enabled")));
        return;
    }

    if (type == QStringLiteral("ping")) {
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("pong")));
        return;
    }

    if (type == QStringLiteral("syncBrowserState")) {
        const QUrl url = QUrl::fromUserInput(message.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || url.scheme().isEmpty()) {
            m_server->sendReply(socket, buildStateReply(false, QStringLiteral("Invalid sync URL")));
            return;
        }

        m_remoteTabs = message.value(QStringLiteral("tabs")).toArray();
        openUrlWithAuth(url, message.value(QStringLiteral("auth")).toObject(), false);
        m_server->sendReply(socket, buildStateReply(true, QStringLiteral("Browser state synced")));
        return;
    }

    m_server->sendReply(socket, buildStateReply(false, QStringLiteral("Unsupported command")));
}

void MainWindow::openUrlWithAuth(const QUrl &url, const QJsonObject &auth, bool fullscreenAfterLoad)
{
    applyAuthState(url, auth);
    m_view->load(url);

    if (fullscreenAfterLoad) {
        applyFullscreen(true);
    }
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

void MainWindow::applyAuthState(const QUrl &url, const QJsonObject &auth)
{
    if (!auth.isEmpty()) {
        const QString userAgent = auth.value(QStringLiteral("userAgent")).toString().trimmed();
        if (!userAgent.isEmpty()) {
            m_view->page()->profile()->setHttpUserAgent(userAgent);
        }

        setCookiesForUrl(url, auth.value(QStringLiteral("cookies")).toArray());
    }

    installStorageBootstrapScript(url, auth);
}

void MainWindow::installStorageBootstrapScript(const QUrl &url, const QJsonObject &auth)
{
    auto &scripts = m_view->page()->scripts();
    for (const QWebEngineScript &existingScript : scripts.find(QString::fromLatin1(kStorageBootstrapScriptName))) {
        scripts.remove(existingScript);
    }

    const QJsonObject localStorage = auth.value(QStringLiteral("localStorage")).toObject();
    const QJsonObject sessionStorage = auth.value(QStringLiteral("sessionStorage")).toObject();
    if (localStorage.isEmpty() && sessionStorage.isEmpty()) {
        return;
    }

    const QJsonObject payload{
        {QStringLiteral("origin"), originString(url)},
        {QStringLiteral("localStorage"), localStorage},
        {QStringLiteral("sessionStorage"), sessionStorage},
    };

    QWebEngineScript script;
    script.setName(kStorageBootstrapScriptName);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(QString::fromLatin1(R"JS(
(() => {
  const payload = %1;
  if (window.location.origin !== payload.origin) {
    return;
  }

  const setStorage = (storage, values) => {
    storage.clear();
    for (const [key, value] of Object.entries(values || {})) {
      storage.setItem(key, String(value));
    }
  };

  try {
    setStorage(window.localStorage, payload.localStorage);
    setStorage(window.sessionStorage, payload.sessionStorage);
  } catch (error) {
    console.error("BCR storage bootstrap failed", error);
  }
})();
)JS").arg(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))));
    scripts.insert(script);
}

void MainWindow::setCookiesForUrl(const QUrl &url, const QJsonArray &cookies)
{
    auto *cookieStore = m_view->page()->profile()->cookieStore();
    for (const QJsonValue &value : cookies) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString cookieValue = object.value(QStringLiteral("value")).toString();
        if (name.isEmpty()) {
            continue;
        }

        QNetworkCookie cookie(name.toUtf8(), cookieValue.toUtf8());
        cookie.setDomain(object.value(QStringLiteral("domain")).toString());
        cookie.setPath(object.value(QStringLiteral("path")).toString(QStringLiteral("/")));
        cookie.setSecure(object.value(QStringLiteral("secure")).toBool(false));
        cookie.setHttpOnly(object.value(QStringLiteral("httpOnly")).toBool(false));
        cookie.setSameSitePolicy(sameSiteFromString(object.value(QStringLiteral("sameSite")).toString()));

        const QJsonValue expirationDate = object.value(QStringLiteral("expirationDate"));
        if (expirationDate.isDouble()) {
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(qint64(expirationDate.toDouble())));
        }

        cookieStore->setCookie(cookie, url);
    }
}

void MainWindow::scheduleStateSync()
{
    if (!m_agentBaseUrl.isValid() || m_view->url().isEmpty()) {
        return;
    }

    m_syncTimer->start();
}

void MainWindow::syncStateToAgent()
{
    if (!m_agentBaseUrl.isValid() || m_view->url().isEmpty()) {
        return;
    }

    const QUrl endpoint = m_agentBaseUrl.resolved(QUrl(QStringLiteral("/client/state")));
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonArray tabs = m_remoteTabs;
    if (tabs.isEmpty()) {
        tabs.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("client-main")},
            {QStringLiteral("url"), m_view->url().toString()},
            {QStringLiteral("title"), m_view->title()},
            {QStringLiteral("active"), true},
        });
    }

    const QJsonObject payload{
        {QStringLiteral("url"), m_view->url().toString()},
        {QStringLiteral("title"), m_view->title()},
        {QStringLiteral("tabs"), tabs},
        {QStringLiteral("updatedAt"), double(QDateTime::currentMSecsSinceEpoch())},
    };

    auto *reply = m_networkAccessManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply] {
        reply->deleteLater();
    });
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

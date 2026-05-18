#include "bcragentservice.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>

namespace {

QJsonObject parseJsonObject(const QByteArray &payload, const QString &context)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw QStringLiteral("%1: %2").arg(context, error.errorString());
    }

    return document.object();
}

QJsonObject toTcpMessage(const QJsonObject &request)
{
    const QString action = request.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("openUrl")) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("openUrl")},
            {QStringLiteral("url"), request.value(QStringLiteral("url")).toString()},
        };
    }

    if (action == QStringLiteral("openUrlAndFullscreen")) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("openUrlAndFullscreen")},
            {QStringLiteral("url"), request.value(QStringLiteral("url")).toString()},
        };
    }

    if (action == QStringLiteral("enterFullscreen")) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("enterFullscreen")}};
    }

    if (action == QStringLiteral("exitFullscreen")) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("exitFullscreen")}};
    }

    if (action == QStringLiteral("ping")) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("ping")}};
    }

    throw QStringLiteral("Unsupported action: %1").arg(action);
}

QJsonObject sendTcpMessage(const AgentSettings &settings, const QJsonObject &message)
{
    QTcpSocket socket;
    socket.connectToHost(settings.clientHost, settings.clientPort);
    if (!socket.waitForConnected(settings.timeoutMs)) {
        throw QStringLiteral("Failed to connect to %1:%2: %3")
            .arg(settings.clientHost)
            .arg(settings.clientPort)
            .arg(socket.errorString());
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    socket.write(payload);
    if (!socket.waitForBytesWritten(settings.timeoutMs)) {
        throw QStringLiteral("Failed to send TCP message: %1").arg(socket.errorString());
    }

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(settings.timeoutMs)) {
            throw QStringLiteral("No response from Qt client: %1").arg(socket.errorString());
        }
        response.append(socket.readAll());
    }

    const QByteArray line = response.left(response.indexOf('\n')).trimmed();
    if (line.isEmpty()) {
        throw QStringLiteral("Empty response from Qt client");
    }

    return parseJsonObject(line, QStringLiteral("Invalid TCP response JSON"));
}

int contentLength(const QList<QByteArray> &headerLines)
{
    for (const QByteArray &line : headerLines) {
        const QByteArray lowerLine = line.toLower();
        if (!lowerLine.startsWith("content-length:")) {
            continue;
        }

        bool ok = false;
        const int length = lowerLine.mid(sizeof("content-length:") - 1).trimmed().toInt(&ok);
        if (!ok || length < 0) {
            throw QStringLiteral("Invalid Content-Length");
        }

        return length;
    }

    return 0;
}

QByteArray httpReasonPhrase(int statusCode)
{
    switch (statusCode) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 502:
        return "Bad Gateway";
    default:
        return "Error";
    }
}

void sendHttpResponse(QTcpSocket *socket, int statusCode, const QJsonObject &body = QJsonObject())
{
    QByteArray payload;
    if (!body.isEmpty()) {
        payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + httpReasonPhrase(statusCode) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: POST, OPTIONS, GET\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Connection: close\r\n";

    if (!payload.isEmpty()) {
        response += "Content-Type: application/json; charset=utf-8\r\n";
        response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
    } else {
        response += "Content-Length: 0\r\n";
    }

    response += "\r\n";
    response += payload;

    socket->write(response);
    socket->disconnectFromHost();
}

QJsonObject buildErrorResponse(const QString &error)
{
    return QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), error},
    };
}

} // namespace

BcrAgentService::BcrAgentService(const AgentSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(&m_server, &QTcpServer::newConnection, this, &BcrAgentService::onNewConnection);
}

bool BcrAgentService::start()
{
    return m_server.listen(QHostAddress(m_settings.listenAddress), m_settings.listenPort);
}

QString BcrAgentService::errorString() const
{
    return m_server.errorString();
}

void BcrAgentService::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &BcrAgentService::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &BcrAgentService::onDisconnected);
    }
}

void BcrAgentService::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    processRequest(socket, buffer);
}

void BcrAgentService::onDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    m_buffers.remove(socket);
    socket->deleteLater();
}

void BcrAgentService::processRequest(QTcpSocket *socket, QByteArray &buffer)
{
    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray headerBlock = buffer.left(headerEnd);
    const QList<QByteArray> headerLines = headerBlock.split('\n');
    if (headerLines.isEmpty()) {
        sendHttpResponse(socket, 400, buildErrorResponse(QStringLiteral("Invalid HTTP request")));
        return;
    }

    const QByteArray requestLine = headerLines.first().trimmed();
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 2) {
        sendHttpResponse(socket, 400, buildErrorResponse(QStringLiteral("Invalid HTTP request line")));
        return;
    }

    const QByteArray method = requestParts.at(0);
    const QByteArray path = requestParts.at(1);
    const int bodyLength = contentLength(headerLines.mid(1));
    const qsizetype totalLength = headerEnd + 4 + bodyLength;
    if (buffer.size() < totalLength) {
        return;
    }

    const QByteArray body = buffer.mid(headerEnd + 4, bodyLength);
    buffer.remove(0, totalLength);

    if (method == "OPTIONS") {
        sendHttpResponse(socket, 204);
        return;
    }

    if (method == "GET" && path == "/health") {
        sendHttpResponse(socket,
                         200,
                         QJsonObject{
                             {QStringLiteral("ok"), true},
                             {QStringLiteral("listenAddress"), m_settings.listenAddress},
                             {QStringLiteral("listenPort"), int(m_settings.listenPort)},
                             {QStringLiteral("clientHost"), m_settings.clientHost},
                             {QStringLiteral("clientPort"), int(m_settings.clientPort)},
                         });
        return;
    }

    if (method != "POST") {
        sendHttpResponse(socket, 405, buildErrorResponse(QStringLiteral("Only POST is supported")));
        return;
    }

    if (path != "/command") {
        sendHttpResponse(socket, 404, buildErrorResponse(QStringLiteral("Unknown endpoint")));
        return;
    }

    try {
        const QJsonObject request = parseJsonObject(body, QStringLiteral("Invalid HTTP JSON"));
        const QJsonObject tcpMessage = toTcpMessage(request);
        const QJsonObject tcpResponse = sendTcpMessage(m_settings, tcpMessage);

        sendHttpResponse(socket,
                         200,
                         QJsonObject{
                             {QStringLiteral("ok"), tcpResponse.value(QStringLiteral("ok")).toBool(false)},
                             {QStringLiteral("message"), tcpResponse.value(QStringLiteral("message")).toString()},
                             {QStringLiteral("response"), tcpResponse},
                         });
    } catch (const QString &error) {
        sendHttpResponse(socket, 502, buildErrorResponse(error));
    }
}

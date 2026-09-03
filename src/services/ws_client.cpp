#include "ws_client.h"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>

namespace {
constexpr int kPingIntervalMs = 30 * 1000;
constexpr int kTimeoutMs = 60 * 1000;
constexpr int kBackoffInitialMs = 1000;
constexpr int kBackoffCapMs = 30 * 1000;
}

WsClient::WsClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_pingTimer(new QTimer(this))
    , m_timeoutTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_pingTimer->setInterval(kPingIntervalMs);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kTimeoutMs);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_reconnectAttempt = 0;
        startHeartbeat();
        emit connectionStateChanged("connected");
        emit connected();
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        stopHeartbeat();
        emit connectionStateChanged("disconnected");
        emit disconnected();
        if (!m_manualDisconnect && m_autoReconnectEnabled) {
            scheduleReconnect();
        }
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        resetHeartbeatTimeout();
        emit textMessageReceived(message);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            emit jsonMessageReceived(doc.object());
        }
    });
    connect(m_socket, &QWebSocket::pong, this, [this](quint64, const QByteArray &) {
        resetHeartbeatTimeout();
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit connectionStateChanged("error");
        emit errorOccurred(m_socket->errorString());
    });
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->ping();
        }
    });
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        emit errorOccurred("WebSocket heartbeat timeout");
        m_socket->abort();
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        openSocket();
    });
}

void WsClient::setUrl(const QString &url)
{
    m_url = url.trimmed();
}

QString WsClient::url() const
{
    return m_url;
}

void WsClient::setSubprotocol(const QString &subprotocol)
{
    const QString value = subprotocol.trimmed();
    if (!value.isEmpty()) {
        m_subprotocol = value;
    }
}

void WsClient::setAutoReconnectEnabled(bool enabled)
{
    m_autoReconnectEnabled = enabled;
    if (!m_autoReconnectEnabled) {
        m_reconnectTimer->stop();
    }
}

bool WsClient::isConfigured() const
{
    return !m_url.trimmed().isEmpty();
}

bool WsClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool WsClient::autoReconnectEnabled() const
{
    return m_autoReconnectEnabled;
}

void WsClient::connectAuthenticated(const QString &bearerToken)
{
    const QString nextToken = bearerToken.trimmed();
    const bool sameToken = (m_bearerToken == nextToken);
    m_bearerToken = nextToken;
    m_manualDisconnect = false;
    if (!isConfigured()) {
        emit errorOccurred("WebSocket URL is not configured");
        return;
    }
    if (sameToken) {
        if (m_socket->state() == QAbstractSocket::ConnectedState
            || m_socket->state() == QAbstractSocket::ConnectingState) {
            return;
        }
    }
    openSocket();
}

void WsClient::disconnectFromServer()
{
    m_manualDisconnect = true;
    m_reconnectTimer->stop();
    stopHeartbeat();
    if (m_socket->state() == QAbstractSocket::ConnectedState
        || m_socket->state() == QAbstractSocket::ConnectingState) {
        m_socket->close();
    }
}

bool WsClient::sendJson(const QJsonObject &message)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        emit errorOccurred("WebSocket is not connected");
        return false;
    }
    const qint64 queued = m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    if (queued < 0) {
        emit errorOccurred("WebSocket send failed");
        return false;
    }
    return true;
}

void WsClient::openSocket()
{
    if (!isConfigured()) {
        return;
    }
    const QUrl url(m_url);
    if (!url.isValid()) {
        emit errorOccurred(QString("Invalid WebSocket URL: %1").arg(m_url));
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectedState
        || m_socket->state() == QAbstractSocket::ConnectingState) {
        m_socket->abort();
    }

    QNetworkRequest request(url);
    if (!m_bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    }
    emit connectionStateChanged("connecting");
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({m_subprotocol});
    m_socket->open(request, options);
}

void WsClient::scheduleReconnect()
{
    if (m_manualDisconnect || !m_autoReconnectEnabled || !isConfigured() || m_bearerToken.isEmpty()) {
        return;
    }
    const int backoff = qMin(kBackoffCapMs, kBackoffInitialMs * (1 << qMin(m_reconnectAttempt, 5)));
    ++m_reconnectAttempt;
    emit connectionStateChanged(QString("reconnecting:%1").arg(backoff));
    m_reconnectTimer->start(backoff);
}

void WsClient::startHeartbeat()
{
    m_pingTimer->start();
    resetHeartbeatTimeout();
}

void WsClient::stopHeartbeat()
{
    m_pingTimer->stop();
    m_timeoutTimer->stop();
}

void WsClient::resetHeartbeatTimeout()
{
    m_timeoutTimer->start();
}

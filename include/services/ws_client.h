#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class QTimer;
class QWebSocket;

class WsClient : public QObject
{
    Q_OBJECT
public:
    explicit WsClient(QObject *parent = nullptr);

    void setUrl(const QString &url);
    void setSubprotocol(const QString &subprotocol);
    void setAutoReconnectEnabled(bool enabled);
    QString url() const;
    bool isConfigured() const;
    bool isConnected() const;
    bool autoReconnectEnabled() const;

    void connectAuthenticated(const QString &bearerToken);
    void disconnectFromServer();
    bool sendJson(const QJsonObject &message);

signals:
    void connected();
    void disconnected();
    void connectionStateChanged(const QString &state);
    void textMessageReceived(const QString &message);
    void jsonMessageReceived(const QJsonObject &message);
    void errorOccurred(const QString &message);

private:
    void openSocket();
    void scheduleReconnect();
    void startHeartbeat();
    void stopHeartbeat();
    void resetHeartbeatTimeout();

    QWebSocket *m_socket = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_url;
    QString m_subprotocol = "vms.events.v1";
    QString m_bearerToken;
    int m_reconnectAttempt = 0;
    bool m_manualDisconnect = false;
    bool m_autoReconnectEnabled = true;
};

#endif // WS_CLIENT_H

#ifndef UGV_SERVICE_H
#define UGV_SERVICE_H

#include <QObject>
#include <QHash>
#include <QString>

class QJsonObject;
class QTimer;
class WsClient;

struct UgvGpsTelemetry
{
    int gatewayId = -1;
    int ugvId = -1;
    QString ts;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double headingDeg = 0.0;
    double speedMps = 0.0;
    double hdop = 0.0;
};

struct UgvRssiTelemetry
{
    int gatewayId = -1;
    int ugvId = -1;
    QString ts;
    int rssiDbm = 0;
    double snrDb = 0.0;
    QString linkType;
};

struct UgvCommandAck
{
    QString type;
    QString msgId;
    QString ts;
    int gatewayId = -1;
    int ugvId = -1;
};

class UgvService : public QObject
{
    Q_OBJECT
public:
    enum class SessionState {
        Disconnected,
        SocketConnecting,
        SocketConnected,
        ConnectingUgv,
        ConnectedUgv,
        DisconnectingUgv,
        Error
    };
    Q_ENUM(SessionState)

    explicit UgvService(QObject *parent = nullptr);

    void setWsUrl(const QString &url);
    void setWsSubprotocol(const QString &subprotocol);
    void setAutoReconnectEnabled(bool enabled);
    void setAccessToken(const QString &token);
    bool isConfigured() const;
    bool isSocketConnected() const;
    SessionState sessionState() const;

    static constexpr int kConnectAckTimeoutMs = 10000;
    static constexpr int kCommandAckTimeoutMs = 1500;

    void connectUgv(int gatewayId, int ugvId);
    void disconnectUgv(int gatewayId, int ugvId);
    void sendDrive(int gatewayId, int ugvId, int forward, int back, int left, int right, int timeoutMs = 100);
    void sendPtz(int gatewayId, int ugvId, double pan, double tilt, double zoom, int timeoutMs = 100);
    void shutdown();

signals:
    void sessionStateChanged(UgvService::SessionState state);
    void socketStateChanged(const QString &state);
    void gpsUpdated(const UgvGpsTelemetry &telemetry);
    void rssiUpdated(const UgvRssiTelemetry &telemetry);
    void commandAck(const UgvCommandAck &ack);
    void serviceError(const QString &message);

private:
    struct PendingAck
    {
        QString requestType;
        int gatewayId = -1;
        int ugvId = -1;
        QTimer *timer = nullptr;
    };

    void setSessionState(SessionState state);
    QString nextMsgId() const;
    QString nextTimestamp() const;
    void sendConnRequest(int gatewayId, int ugvId);
    void sendDisconnRequest(int gatewayId, int ugvId);
    void sendCommandWithAck(
        const QString &type,
        const QString &endpointKey,
        int gatewayId,
        int ugvId,
        const QJsonObject &payload,
        int ackTimeoutMs);
    void insertPendingAck(const QString &msgId, const QString &requestType, int gatewayId, int ugvId, int timeoutMs);
    void clearPendingAck(const QString &msgId);
    void clearAllPendingAcks();
    void clearPendingConnectRequest();
    void handleSocketConnected();
    void handleSocketDisconnected();
    void handleSocketMessage(const QJsonObject &message);
    void handleAckMessage(const QString &type, const QString &msgId, const QString &ts, const QJsonObject &message);
    void handleTelemetryMessage(const QString &type, const QString &msgId, const QString &ts, const QJsonObject &message);
    void sendTelemetryAck(const QString &type, const QString &msgId, const QString &ts, const QJsonObject &endpoint);
    void reportServiceError(const QString &message);
    static bool readEndpointIds(const QJsonObject &obj, const QString &key, int *gatewayId, int *ugvId);

    WsClient *m_wsClient = nullptr;
    QString m_wsUrl;
    QString m_wsSubprotocol = "vms.gw.v1";
    QString m_accessToken;
    SessionState m_sessionState = SessionState::Disconnected;
    int m_activeGatewayId = -1;
    int m_activeUgvId = -1;
    int m_pendingConnectGatewayId = -1;
    int m_pendingConnectUgvId = -1;
    bool m_connectRequestQueued = false;
    bool m_manualShutdown = false;
    QHash<QString, PendingAck> m_pendingAcks;
};

Q_DECLARE_METATYPE(UgvGpsTelemetry)
Q_DECLARE_METATYPE(UgvRssiTelemetry)
Q_DECLARE_METATYPE(UgvCommandAck)

#endif // UGV_SERVICE_H

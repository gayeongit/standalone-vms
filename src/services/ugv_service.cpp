#include "ugv_service.h"

#include "ws_client.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

namespace {

int readPositiveInt(const QJsonValue &value)
{
    if (value.isDouble()) {
        const int id = value.toInt(-1);
        return id > 0 ? id : -1;
    }
    if (value.isString()) {
        bool ok = false;
        const int id = value.toString().trimmed().toInt(&ok);
        return (ok && id > 0) ? id : -1;
    }
    return -1;
}

QString readMessageIdText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toVariant().toLongLong());
    }
    return QString();
}

QString ackTypeFor(const QString &type)
{
    return type.trimmed() + ".ack";
}

QString endpointKeyForAckType(const QString &type)
{
    if (type == "request.conn.ugv.ack" || type == "request.disconn.ugv.ack") {
        return "from";
    }
    if (type == "cmd.drive.ack" || type == "cmd.ptz.ack") {
        return "to";
    }
    return QString();
}

QString requestTypeForAckType(const QString &type)
{
    return type.endsWith(".ack") ? type.left(type.size() - 4) : QString();
}

bool isConnectionRequestType(const QString &requestType)
{
    return requestType == QStringLiteral("request.conn.ugv")
           || requestType == QStringLiteral("request.disconn.ugv");
}

bool isCommandRequestType(const QString &requestType)
{
    return requestType == QStringLiteral("cmd.drive")
           || requestType == QStringLiteral("cmd.ptz");
}

bool isConnectionBreakingErrorCode(const QString &code)
{
    const QString normalized = code.trimmed().toUpper();
    return normalized == QStringLiteral("UNAUTHORIZED")
           || normalized == QStringLiteral("ROUTE_UNAVAILABLE")
           || normalized == QStringLiteral("SOCKET_CLOSED")
           || normalized == QStringLiteral("DEVICE_OFFLINE");
}

bool shouldTransitionSessionForError(const QString &requestType, const QString &code, bool hadMatchedPending)
{
    if (isConnectionRequestType(requestType)) {
        return true;
    }
    if (isCommandRequestType(requestType)) {
        return hadMatchedPending && isConnectionBreakingErrorCode(code);
    }
    return false;
}

QString sessionStateName(UgvService::SessionState state)
{
    switch (state) {
    case UgvService::SessionState::Disconnected:
        return QStringLiteral("Disconnected");
    case UgvService::SessionState::SocketConnecting:
        return QStringLiteral("SocketConnecting");
    case UgvService::SessionState::SocketConnected:
        return QStringLiteral("SocketConnected");
    case UgvService::SessionState::ConnectingUgv:
        return QStringLiteral("ConnectingUgv");
    case UgvService::SessionState::ConnectedUgv:
        return QStringLiteral("ConnectedUgv");
    case UgvService::SessionState::DisconnectingUgv:
        return QStringLiteral("DisconnectingUgv");
    case UgvService::SessionState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

} // namespace

UgvService::UgvService(QObject *parent)
    : QObject(parent)
    , m_wsClient(new WsClient(this))
{
    qRegisterMetaType<UgvGpsTelemetry>("UgvGpsTelemetry");
    qRegisterMetaType<UgvRssiTelemetry>("UgvRssiTelemetry");
    qRegisterMetaType<UgvCommandAck>("UgvCommandAck");

    m_wsClient->setAutoReconnectEnabled(false);
    connect(m_wsClient, &WsClient::connected, this, &UgvService::handleSocketConnected);
    connect(m_wsClient, &WsClient::disconnected, this, &UgvService::handleSocketDisconnected);
    connect(m_wsClient, &WsClient::connectionStateChanged, this, [this](const QString &state) {
        emit socketStateChanged(state);
    });
    connect(m_wsClient, &WsClient::jsonMessageReceived, this, &UgvService::handleSocketMessage);
    connect(m_wsClient, &WsClient::errorOccurred, this, [this](const QString &message) {
        if (!message.trimmed().isEmpty()) {
            reportServiceError(message);
        }
        if (!m_manualShutdown) {
            setSessionState(SessionState::Error);
        }
    });
}

void UgvService::setWsUrl(const QString &url)
{
    m_wsUrl = url.trimmed();
    m_wsClient->setUrl(m_wsUrl);
}

void UgvService::setWsSubprotocol(const QString &subprotocol)
{
    const QString value = subprotocol.trimmed();
    if (!value.isEmpty()) {
        m_wsSubprotocol = value;
    }
    m_wsClient->setSubprotocol(m_wsSubprotocol);
}

void UgvService::setAutoReconnectEnabled(bool enabled)
{
    m_wsClient->setAutoReconnectEnabled(enabled);
}

void UgvService::setAccessToken(const QString &token)
{
    m_accessToken = token.trimmed();
}

bool UgvService::isConfigured() const
{
    return m_wsClient->isConfigured();
}

bool UgvService::isSocketConnected() const
{
    return m_wsClient->isConnected();
}

UgvService::SessionState UgvService::sessionState() const
{
    return m_sessionState;
}

void UgvService::connectUgv(int gatewayId, int ugvId)
{
    // UGV 연결은 곧바로 request.conn.ugv를 보내지 않을 수도 있다.
    // 대상 식별자를 저장하고, 필요하면 gateway websocket 연결부터 올린 뒤 conn request를 보낸다.
    // 소켓이 아직 없으면 "어떤 UGV에 붙을지"를 pending으로 저장한 뒤
    // gateway websocket 연결 -> request.conn.ugv -> ack 순으로 세션을 올린다.
    if (gatewayId <= 0 || ugvId <= 0) {
        reportServiceError("UGV 연결 대상 식별자가 올바르지 않습니다.");
        return;
    }
    if (!isConfigured()) {
        reportServiceError("UGV 게이트웨이 WebSocket 설정이 비어 있습니다.");
        return;
    }

    if (m_accessToken.isEmpty()) {
        reportServiceError("인증 토큰이 없어 UGV 연결을 시작할 수 없습니다.");
        return;
    }

    m_manualShutdown = false;
    m_pendingConnectGatewayId = gatewayId;
    m_pendingConnectUgvId = ugvId;
    m_connectRequestQueued = true;

    if (m_wsClient->isConnected()) {
        sendConnRequest(gatewayId, ugvId);
        return;
    }

    setSessionState(SessionState::SocketConnecting);
    m_wsClient->connectAuthenticated(m_accessToken);
}

void UgvService::disconnectUgv(int gatewayId, int ugvId)
{
    const int targetGatewayId = gatewayId > 0 ? gatewayId : m_activeGatewayId;
    const int targetUgvId = ugvId > 0 ? ugvId : m_activeUgvId;
    if (targetGatewayId <= 0 || targetUgvId <= 0) {
        shutdown();
        return;
    }

    if (!m_wsClient->isConnected()) {
        shutdown();
        return;
    }

    clearPendingConnectRequest();
    sendDisconnRequest(targetGatewayId, targetUgvId);
}

void UgvService::sendDrive(int gatewayId, int ugvId, int forward, int back, int left, int right, int timeoutMs)
{
    if (m_sessionState != SessionState::ConnectedUgv) {
        reportServiceError("UGV가 연결되지 않아 주행 명령을 보낼 수 없습니다.");
        return;
    }

    QJsonObject payload{
        {"forw", forward},
        {"back", back},
        {"left", left},
        {"right", right},
        {"timeout", timeoutMs}
    };
    sendCommandWithAck("cmd.drive", "to", gatewayId, ugvId, payload, 0);
}

void UgvService::sendPtz(int gatewayId, int ugvId, double pan, double tilt, double zoom, int timeoutMs)
{
    if (m_sessionState != SessionState::ConnectedUgv) {
        reportServiceError("UGV가 연결되지 않아 PTZ 명령을 보낼 수 없습니다.");
        return;
    }

    QJsonObject payload{
        {"pan", pan},
        {"tilt", tilt},
        {"zoom", zoom},
        {"timeout", timeoutMs}
    };
    sendCommandWithAck("cmd.ptz", "to", gatewayId, ugvId, payload, 0);
}

void UgvService::shutdown()
{
    m_manualShutdown = true;
    clearPendingConnectRequest();
    clearAllPendingAcks();
    m_activeGatewayId = -1;
    m_activeUgvId = -1;
    setSessionState(SessionState::Disconnected);
    m_wsClient->disconnectFromServer();
}

void UgvService::setSessionState(SessionState state)
{
    if (m_sessionState == state) {
        return;
    }
    m_sessionState = state;
    emit sessionStateChanged(m_sessionState);
}

QString UgvService::nextMsgId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString UgvService::nextTimestamp() const
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void UgvService::sendConnRequest(int gatewayId, int ugvId)
{
    setSessionState(SessionState::ConnectingUgv);
    sendCommandWithAck("request.conn.ugv", "from", gatewayId, ugvId, QJsonObject(), kConnectAckTimeoutMs);
}

void UgvService::sendDisconnRequest(int gatewayId, int ugvId)
{
    setSessionState(SessionState::DisconnectingUgv);
    sendCommandWithAck("request.disconn.ugv", "from", gatewayId, ugvId, QJsonObject(), kConnectAckTimeoutMs);
}

void UgvService::sendCommandWithAck(
    const QString &type,
    const QString &endpointKey,
    int gatewayId,
    int ugvId,
    const QJsonObject &payload,
    int ackTimeoutMs)
{
    // UGV websocket 프로토콜의 공통 송신 경로다.
    // type/from|to/payload/msgId/timestamp를 조합해 JSON을 만들고 websocket으로 전송한다.
    // connection request / disconnect request / drive / ptz 모두 여기로 들어오며,
    // endpointKey(from/to) 차이와 ACK 추적 여부만 달라진다.
    // 최근 정책상 drive/ptz는 fire-and-forget이고, conn/disconn만 ACK를 강하게 본다.
    if (gatewayId <= 0 || ugvId <= 0) {
        reportServiceError(QString("%1 대상 식별자가 올바르지 않습니다.").arg(type));
        return;
    }
    if (!m_wsClient->isConnected()) {
        reportServiceError(QString("%1 전송 전에 WebSocket이 연결되어 있지 않습니다.").arg(type));
        return;
    }

    const QString msgId = nextMsgId();
    QJsonObject endpoint{
        {"gatewayId", gatewayId},
        {"ugvId", ugvId}
    };
    QJsonObject message{
        {"type", type},
        {"msgId", msgId},
        {"ts", nextTimestamp()},
        {endpointKey, endpoint},
        {"payload", payload.isEmpty() ? QJsonValue::Null : QJsonValue(payload)}
    };

    if (!m_wsClient->sendJson(message)) {
        reportServiceError(QString("%1 전송 실패").arg(type));
        if (type == "request.disconn.ugv") {
            shutdown();
        } else if (type == "request.conn.ugv") {
            setSessionState(SessionState::Error);
        }
        return;
    }
    if (ackTimeoutMs > 0) {
        insertPendingAck(msgId, type, gatewayId, ugvId, ackTimeoutMs);
    }
}

void UgvService::insertPendingAck(
    const QString &msgId, const QString &requestType, int gatewayId, int ugvId, int timeoutMs)
{
    clearPendingAck(msgId);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, msgId]() {
        const auto it = m_pendingAcks.constFind(msgId);
        if (it == m_pendingAcks.constEnd()) {
            return;
        }
        const PendingAck pending = it.value();
        if (pending.timer) {
            pending.timer->deleteLater();
        }
        m_pendingAcks.remove(msgId);
        if (pending.requestType == "request.disconn.ugv") {
            reportServiceError(QString("%1 ACK timeout; force shutdown fallback").arg(pending.requestType));
            shutdown();
            return;
        }
        reportServiceError(QString("%1 ACK timeout").arg(pending.requestType));
        if (pending.requestType == "request.conn.ugv") {
            clearPendingConnectRequest();
            setSessionState(SessionState::Disconnected);
        }
    });
    timer->start(timeoutMs);

    PendingAck pending;
    pending.requestType = requestType;
    pending.gatewayId = gatewayId;
    pending.ugvId = ugvId;
    pending.timer = timer;
    m_pendingAcks.insert(msgId, pending);
}

void UgvService::clearPendingAck(const QString &msgId)
{
    const auto it = m_pendingAcks.find(msgId);
    if (it == m_pendingAcks.end()) {
        return;
    }
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    m_pendingAcks.erase(it);
}

void UgvService::clearAllPendingAcks()
{
    for (auto it = m_pendingAcks.begin(); it != m_pendingAcks.end(); ++it) {
        if (it->timer) {
            it->timer->stop();
            it->timer->deleteLater();
        }
    }
    m_pendingAcks.clear();
}

void UgvService::clearPendingConnectRequest()
{
    m_connectRequestQueued = false;
    m_pendingConnectGatewayId = -1;
    m_pendingConnectUgvId = -1;
}

void UgvService::handleSocketConnected()
{
    setSessionState(SessionState::SocketConnected);
    if (m_connectRequestQueued && m_pendingConnectGatewayId > 0 && m_pendingConnectUgvId > 0) {
        sendConnRequest(m_pendingConnectGatewayId, m_pendingConnectUgvId);
    }
}

void UgvService::handleSocketDisconnected()
{
    clearAllPendingAcks();
    m_activeGatewayId = -1;
    m_activeUgvId = -1;
    clearPendingConnectRequest();
    setSessionState(SessionState::Disconnected);
    m_manualShutdown = false;
}

void UgvService::handleSocketMessage(const QJsonObject &message)
{
    const QString type = message.value("type").toString().trimmed();
    if (type.isEmpty()) {
        reportServiceError("UGV 메시지 type이 비어 있습니다.");
        return;
    }

    if (type == "error") {
        const QString code = message.value("code").toString().trimmed();
        const QString text = message.value("message").toString().trimmed();
        const QString errorMsgId = readMessageIdText(message.value("msgId"));
        const QString errorRequestType = message.value("requestType").toString().trimmed();
        PendingAck matchedPending;
        bool hadMatchedPending = false;
        if (!errorMsgId.isEmpty()) {
            const auto pendingIt = m_pendingAcks.constFind(errorMsgId);
            if (pendingIt != m_pendingAcks.constEnd()) {
                matchedPending = pendingIt.value();
                hadMatchedPending = true;
                clearPendingAck(errorMsgId);
            }
        }
        if (!hadMatchedPending && errorMsgId.isEmpty() && !errorRequestType.isEmpty()) {
            QString candidateMsgId;
            int candidateCount = 0;
            for (auto it = m_pendingAcks.cbegin(); it != m_pendingAcks.cend(); ++it) {
                if (it.value().requestType == errorRequestType) {
                    candidateMsgId = it.key();
                    matchedPending = it.value();
                    ++candidateCount;
                    if (candidateCount > 1) {
                        break;
                    }
                }
            }
            if (candidateCount == 1 && !candidateMsgId.isEmpty()) {
                clearPendingAck(candidateMsgId);
                hadMatchedPending = true;
            } else if (candidateCount > 1) {
                qInfo().noquote() << QString("ugv error_pending_match_ambiguous requestType=%1 msgId=%2")
                                         .arg(errorRequestType, errorMsgId);
            }
        }
        reportServiceError(code.isEmpty() ? text : QString("%1: %2").arg(code, text));
        if (isConnectionBreakingErrorCode(code)) {
            // Gateway route/device offline/socket closed 류 에러는
            // 세션을 즉시 내려 UI와 내부 상태를 빠르게 일치시킨다.
            shutdown();
            return;
        }
        if (hadMatchedPending && matchedPending.requestType == "request.disconn.ugv") {
            shutdown();
            return;
        }
        const QString effectiveRequestType = hadMatchedPending ? matchedPending.requestType : errorRequestType;
        if (effectiveRequestType == QStringLiteral("request.conn.ugv")) {
            clearPendingConnectRequest();
            m_activeGatewayId = -1;
            m_activeUgvId = -1;
            setSessionState(SessionState::Disconnected);
            return;
        }
        if (shouldTransitionSessionForError(effectiveRequestType, code, hadMatchedPending)) {
            setSessionState(SessionState::Error);
        }
        return;
    }

    const QString msgId = message.value("msgId").toString().trimmed();
    const QString ts = message.value("ts").toString().trimmed();

    if (type.endsWith(".ack")) {
        handleAckMessage(type, msgId, ts, message);
        return;
    }

    if (type == "telemetry.gps" || type == "telemetry.rssi") {
        handleTelemetryMessage(type, msgId, ts, message);
        return;
    }
}

void UgvService::handleAckMessage(
    const QString &type, const QString &msgId, const QString &ts, const QJsonObject &message)
{
    // ACK 처리는 "메시지를 받았다"보다 "현재 pending request와 일치하는가"가 더 중요하다.
    // 수신한 ACK를 pending map과 대조해 세션 상태를 바꾸거나 commandAck 시그널로 흘려보낸다.
    // 특히 conn/disconn은 stale ACK를 무시해야 세션 상태가 역주행하지 않고,
    // drive/ptz는 최근 정책상 pending이 없어도 조용히 흘려보내는 것이 정상이다.
    const QString expectedRequestType = requestTypeForAckType(type);
    PendingAck pending;
    bool hadPending = false;
    const auto pendingIt = m_pendingAcks.constFind(msgId);
    if (pendingIt != m_pendingAcks.constEnd()) {
        pending = pendingIt.value();
        hadPending = true;
    }

    const bool isTrackedAck = (type == "request.conn.ugv.ack"
                               || type == "request.disconn.ugv.ack");
    if (isTrackedAck && !hadPending) {
        qInfo().noquote() << QString("ugv stale_ack_ignored type=%1 msgId=%2").arg(type, msgId);
        return;
    }
    if ((type == "cmd.drive.ack" || type == "cmd.ptz.ack") && !hadPending) {
        return;
    }

    const QString endpointKey = endpointKeyForAckType(type);
    int gatewayId = -1;
    int ugvId = -1;
    bool endpointOk = true;
    if (!endpointKey.isEmpty()) {
        endpointOk = readEndpointIds(message, endpointKey, &gatewayId, &ugvId);
        if (!endpointOk) {
            reportServiceError(QString("%1 ACK 식별자가 올바르지 않습니다.").arg(type));
            return;
        }
    }
    if (hadPending && !expectedRequestType.isEmpty() && pending.requestType != expectedRequestType) {
        reportServiceError(QString("%1의 요청 타입이 pending과 일치하지 않습니다.").arg(type));
        if (type == "request.conn.ugv.ack" || type == "request.disconn.ugv.ack") {
            setSessionState(SessionState::Error);
        }
        return;
    }

    UgvCommandAck ack;
    ack.type = type;
    ack.msgId = msgId;
    ack.ts = ts;
    ack.gatewayId = gatewayId;
    ack.ugvId = ugvId;

    if (type == "request.conn.ugv.ack") {
        if (gatewayId <= 0 || ugvId <= 0) {
            reportServiceError("request.conn.ugv.ack 식별자가 올바르지 않습니다.");
            setSessionState(SessionState::Error);
            return;
        }
        if (hadPending
            && pending.gatewayId > 0
            && pending.ugvId > 0
            && (pending.gatewayId != gatewayId || pending.ugvId != ugvId)) {
            reportServiceError("request.conn.ugv.ack 대상이 요청과 일치하지 않습니다.");
            setSessionState(SessionState::Error);
            return;
        }
        m_activeGatewayId = gatewayId;
        m_activeUgvId = ugvId;
        clearPendingConnectRequest();
        clearPendingAck(msgId);
        setSessionState(SessionState::ConnectedUgv);
        emit commandAck(ack);
        return;
    }
    if (type == "request.disconn.ugv.ack") {
        if (gatewayId <= 0 || ugvId <= 0) {
            reportServiceError("request.disconn.ugv.ack 식별자가 올바르지 않습니다.");
            setSessionState(SessionState::Error);
            return;
        }
        if (hadPending
            && pending.gatewayId > 0
            && pending.ugvId > 0
            && (pending.gatewayId != gatewayId || pending.ugvId != ugvId)) {
            reportServiceError("request.disconn.ugv.ack 대상이 요청과 일치하지 않습니다.");
            setSessionState(SessionState::Error);
            return;
        }
        m_activeGatewayId = -1;
        m_activeUgvId = -1;
        clearPendingAck(msgId);
        setSessionState(SessionState::Disconnected);
        m_manualShutdown = true;
        m_wsClient->disconnectFromServer();
        emit commandAck(ack);
        return;
    }
    if (type == "cmd.drive.ack" || type == "cmd.ptz.ack") {
        if (gatewayId <= 0 || ugvId <= 0) {
            reportServiceError(QString("%1 식별자가 올바르지 않습니다.").arg(type));
            return;
        }
        if (pending.gatewayId > 0
            && pending.ugvId > 0
            && (pending.gatewayId != gatewayId || pending.ugvId != ugvId)) {
            reportServiceError(QString("%1 대상이 요청과 일치하지 않습니다.").arg(type));
            return;
        }
        clearPendingAck(msgId);
        emit commandAck(ack);
        return;
    }

    if (expectedRequestType.isEmpty()) {
        // Unknown ACK type: keep pending map intact.
        emit commandAck(ack);
        return;
    }

    clearPendingAck(msgId);
    emit commandAck(ack);
}

void UgvService::handleTelemetryMessage(
    const QString &type, const QString &msgId, const QString &ts, const QJsonObject &message)
{
    // Telemetry는 양방향 프로토콜에서 "수신 + 즉시 ACK + typed struct 변환"을 함께 처리한다.
    // GPS/RSSI JSON payload를 읽어 UgvGpsTelemetry/UgvRssiTelemetry로 변환한 뒤 signal로 내보낸다.
    // GPS/RSSI는 화면이 직접 JSON을 읽지 않도록 여기서 강타입 텔레메트리로 변환해
    // UgvScreen 쪽에는 signal 형태로만 전달한다.
    int gatewayId = -1;
    int ugvId = -1;
    if (!readEndpointIds(message, "from", &gatewayId, &ugvId)) {
        reportServiceError(QString("%1 메시지의 from 식별자가 올바르지 않습니다.").arg(type));
        return;
    }

    const QJsonObject endpoint = message.value("from").toObject();
    sendTelemetryAck(type, msgId, ts, endpoint);

    const QJsonObject payload = message.value("payload").toObject();
    if (type == "telemetry.gps") {
        UgvGpsTelemetry telemetry;
        telemetry.gatewayId = gatewayId;
        telemetry.ugvId = ugvId;
        telemetry.ts = ts;
        telemetry.latitude = payload.value("lat").toDouble();
        telemetry.longitude = payload.value("lon").toDouble();
        telemetry.altitude = payload.value("alt").toDouble();
        telemetry.headingDeg = payload.value("headingDeg").toDouble();
        telemetry.speedMps = payload.value("speedMps").toDouble();
        telemetry.hdop = payload.value("hdop").toDouble();
        emit gpsUpdated(telemetry);
        return;
    }

    if (type == "telemetry.rssi") {
        UgvRssiTelemetry telemetry;
        telemetry.gatewayId = gatewayId;
        telemetry.ugvId = ugvId;
        telemetry.ts = ts;
        telemetry.rssiDbm = payload.value("rssiDbm").toInt();
        telemetry.snrDb = payload.value("snrDb").toDouble();
        telemetry.linkType = payload.value("linkType").toString().trimmed();
        emit rssiUpdated(telemetry);
    }
}

void UgvService::sendTelemetryAck(
    const QString &type, const QString &msgId, const QString &ts, const QJsonObject &endpoint)
{
    if (!m_wsClient->isConnected()) {
        return;
    }
    QJsonObject ack{
        {"type", ackTypeFor(type)},
        {"msgId", msgId},
        {"ts", ts.isEmpty() ? nextTimestamp() : ts},
        {"from", endpoint}
    };
    if (!m_wsClient->sendJson(ack)) {
        reportServiceError(QString("%1 ACK 전송 실패").arg(type));
    }
}

void UgvService::reportServiceError(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    qWarning().noquote()
        << QString("[UGV][serviceError] msg=\"%1\" state=%2 active=(%3,%4) pending=(%5,%6) queued=%7 wsConnected=%8")
               .arg(trimmed,
                    sessionStateName(m_sessionState),
                    QString::number(m_activeGatewayId),
                    QString::number(m_activeUgvId),
                    QString::number(m_pendingConnectGatewayId),
                    QString::number(m_pendingConnectUgvId),
                    m_connectRequestQueued ? QStringLiteral("true") : QStringLiteral("false"),
                    m_wsClient && m_wsClient->isConnected() ? QStringLiteral("true") : QStringLiteral("false"));
    emit serviceError(trimmed);
}

bool UgvService::readEndpointIds(const QJsonObject &obj, const QString &key, int *gatewayId, int *ugvId)
{
    if (gatewayId) {
        *gatewayId = -1;
    }
    if (ugvId) {
        *ugvId = -1;
    }

    const QJsonObject endpoint = obj.value(key).toObject();
    if (endpoint.isEmpty()) {
        return false;
    }

    const int parsedGatewayId = readPositiveInt(endpoint.value("gatewayId"));
    const int parsedUgvId = readPositiveInt(endpoint.value("ugvId"));
    if (parsedGatewayId <= 0 || parsedUgvId <= 0) {
        return false;
    }

    if (gatewayId) {
        *gatewayId = parsedGatewayId;
    }
    if (ugvId) {
        *ugvId = parsedUgvId;
    }
    return true;
}

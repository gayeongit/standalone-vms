// Phase 1 목업 카메라 호스트 — ONVIF-lite mock.
//
// WS-Discovery(Probe -> ProbeMatch)는 표준 스펙 그대로 구현했다 — 고정된 포맷이라 그대로 유지.
//
// 디바이스 서비스(GetDeviceInformation/GetProfiles/GetStreamUri)는 OnvifLiteClient가 실제로
// 파싱하는 최소 필드만 채운다 — 설계 원칙 4(카메라 쪽은 정교하게 만들 필요 없다)에 따라
// 액션 판별도 QXmlStreamReader 없이 단순 substring 검사로 충분하다.

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUuid>
#include <QVector>

namespace {

constexpr quint16 kDiscoveryPort = 3702;
const QHostAddress kDiscoveryMulticastAddress(QStringLiteral("239.255.255.250"));
constexpr quint16 kDeviceServicePort = 8082;

// 같은 PC에서 VMS와 목업 호스트를 같이 띄우는 걸 전제로 한 기본값.
// 다른 기기로 분리해서 테스트할 때는 이 값을 그 기기의 실제 IP로 바꿔야 한다
// (docs/roadmap.md 5절 리스크 참고).
const QString kDeviceServiceXAddr =
    QStringLiteral("http://127.0.0.1:%1/onvif/device_service").arg(kDeviceServicePort);
const QString kDeviceUuid = QStringLiteral("urn:uuid:4b2a6b8e-0000-4000-8000-000000000001"); // 고정 더미 UUID

// GetCapabilities가 알려주는 Media/PTZ/Imaging 서비스 주소. 실제로는 다른 포트/호스트일 수도 있지만
// 이 mock은 같은 QTcpServer가 액션 이름으로만 분기하므로 경로만 구분해도 충분하다(설계 원칙 4) —
// 클라이언트가 xaddr을 그대로 재사용하지 않고 여기서 받은 주소를 실제로 쓰는지 검증하는 게 목적.
const QString kMediaServiceXAddr = QStringLiteral("http://127.0.0.1:%1/onvif/media_service").arg(kDeviceServicePort);
const QString kPtzServiceXAddr = QStringLiteral("http://127.0.0.1:%1/onvif/ptz_service").arg(kDeviceServicePort);
const QString kImagingServiceXAddr = QStringLiteral("http://127.0.0.1:%1/onvif/imaging_service").arg(kDeviceServicePort);

struct MockProfile
{
    QString token;
    QString name;
    QString videoCodec;
    int width;
    int height;
    QString rtsp;
};

const QVector<MockProfile> &mockProfiles()
{
    // mediamtx.yml의 cam1/cam2 경로와 1:1 대응.
    static const QVector<MockProfile> profiles = {
        {QStringLiteral("profile_1"), QStringLiteral("Channel 1"), QStringLiteral("H264"), 1920, 1080,
         QStringLiteral("rtsp://127.0.0.1:8554/cam1")},
        {QStringLiteral("profile_2"), QStringLiteral("Channel 2"), QStringLiteral("H264"), 1920, 1080,
         QStringLiteral("rtsp://127.0.0.1:8554/cam2")},
    };
    return profiles;
}

QString extractMessageId(const QByteArray &probe)
{
    static const QRegularExpression pattern(
        QStringLiteral("<[^>]*MessageID[^>]*>([^<]*)</[^>]*MessageID>"));
    const auto match = pattern.match(QString::fromUtf8(probe));
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QByteArray buildProbeMatch(const QString &relatesTo)
{
    const QString messageId = QStringLiteral("urn:uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString relatesToHeader = relatesTo.isEmpty()
        ? QString()
        : QStringLiteral("<w:RelatesTo>%1</w:RelatesTo>").arg(relatesTo);

    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
               "xmlns:w=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
               "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
               "<e:Header>"
               "<w:MessageID>%1</w:MessageID>"
               "%2"
               "<w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</w:Action>"
               "</e:Header>"
               "<e:Body><w:ProbeMatches><w:ProbeMatch>"
               "<w:EndpointReference><w:Address>%3</w:Address></w:EndpointReference>"
               "<w:Types>dn:NetworkVideoTransmitter</w:Types>"
               "<w:XAddrs>%4</w:XAddrs>"
               "<w:MetadataVersion>1</w:MetadataVersion>"
               "</w:ProbeMatch></w:ProbeMatches></e:Body>"
               "</e:Envelope>")
        .arg(messageId, relatesToHeader, kDeviceUuid, kDeviceServiceXAddr)
        .toUtf8();
}

QByteArray wrapSoapEnvelope(const QString &bodyContent)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
               "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
               "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
               "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
               "<e:Body>%1</e:Body></e:Envelope>")
        .arg(bodyContent)
        .toUtf8();
}

QByteArray buildGetDeviceInformationResponse()
{
    return wrapSoapEnvelope(QStringLiteral(
        "<tds:GetDeviceInformationResponse>"
        "<tds:Manufacturer>MockCameraHost</tds:Manufacturer>"
        "<tds:Model>Phase1-Mock</tds:Model>"
        "<tds:SerialNumber>MOCK-0001</tds:SerialNumber>"
        "</tds:GetDeviceInformationResponse>"));
}

QByteArray buildGetCapabilitiesResponse()
{
    return wrapSoapEnvelope(
        QStringLiteral("<tds:GetCapabilitiesResponse><tds:Capabilities>"
                        "<tt:Media><tt:XAddr>%1</tt:XAddr></tt:Media>"
                        "<tt:PTZ><tt:XAddr>%2</tt:XAddr></tt:PTZ>"
                        "<tt:Imaging><tt:XAddr>%3</tt:XAddr></tt:Imaging>"
                        "</tds:Capabilities></tds:GetCapabilitiesResponse>")
            .arg(kMediaServiceXAddr, kPtzServiceXAddr, kImagingServiceXAddr));
}

QByteArray buildGetProfilesResponse()
{
    QString profilesXml;
    for (const auto &profile : mockProfiles()) {
        profilesXml += QStringLiteral(
                            "<trt:Profiles token=\"%1\">"
                            "<tt:Name>%2</tt:Name>"
                            "<tt:VideoEncoderConfiguration>"
                            "<tt:Encoding>%3</tt:Encoding>"
                            "<tt:Resolution><tt:Width>%4</tt:Width><tt:Height>%5</tt:Height></tt:Resolution>"
                            "</tt:VideoEncoderConfiguration>"
                            "</trt:Profiles>")
                            .arg(profile.token, profile.name, profile.videoCodec,
                                 QString::number(profile.width), QString::number(profile.height));
    }
    return wrapSoapEnvelope(
        QStringLiteral("<trt:GetProfilesResponse>%1</trt:GetProfilesResponse>").arg(profilesXml));
}

QString extractElementText(const QByteArray &requestBody, const QString &tagLocalName)
{
    const QRegularExpression pattern(
        QStringLiteral("<[^:>]*:?%1[^>]*>([^<]*)</[^:>]*:?%1>").arg(tagLocalName));
    const auto match = pattern.match(QString::fromUtf8(requestBody));
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString extractAttributeValue(const QByteArray &requestBody, const QString &tagLocalName, const QString &attributeName)
{
    const QRegularExpression pattern(
        QStringLiteral("<[^:>]*:?%1\\b[^>]*\\s%2=\"([^\"]*)\"").arg(tagLocalName, attributeName));
    const auto match = pattern.match(QString::fromUtf8(requestBody));
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString extractProfileToken(const QByteArray &requestBody)
{
    return extractElementText(requestBody, QStringLiteral("ProfileToken"));
}

QByteArray buildGetStreamUriResponse(const QString &profileToken)
{
    QString uri;
    for (const auto &profile : mockProfiles()) {
        if (profile.token == profileToken) {
            uri = profile.rtsp;
            break;
        }
    }
    if (uri.isEmpty() && !mockProfiles().isEmpty()) {
        uri = mockProfiles().first().rtsp; // 토큰을 못 찾으면 첫 채널로 폴백 (mock이라 정교함 불필요)
    }
    return wrapSoapEnvelope(
        QStringLiteral("<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>%1</tt:Uri></trt:MediaUri>"
                        "</trt:GetStreamUriResponse>")
            .arg(uri));
}

QByteArray buildRelativeMoveResponse(const QString &profileToken, const QString &zoomDelta)
{
    qInfo().noquote() << "onvif_mock: PTZ RelativeMove token=" << profileToken << "zoom=" << zoomDelta;
    return wrapSoapEnvelope(QStringLiteral("<tptz:RelativeMoveResponse/>"));
}

QByteArray buildImagingMoveResponse(const QString &videoSourceToken, const QString &focusDelta)
{
    qInfo().noquote() << "onvif_mock: Imaging Move token=" << videoSourceToken << "focus=" << focusDelta;
    return wrapSoapEnvelope(QStringLiteral("<timg:MoveResponse/>"));
}

QByteArray buildDeviceServiceResponse(const QByteArray &requestBody)
{
    // Phase 2: PTZ RelativeMove / Imaging Move. "RelativeMove"가 먼저 걸리게 순서에 주의
    // (둘 다 문자열 "Move"를 포함하므로 Imaging은 VideoSourceToken 존재로 구분).
    if (requestBody.contains("RelativeMove")) {
        const QString token = extractElementText(requestBody, QStringLiteral("ProfileToken"));
        const QString zoom = extractAttributeValue(requestBody, QStringLiteral("Zoom"), QStringLiteral("x"));
        return buildRelativeMoveResponse(token, zoom);
    }
    if (requestBody.contains("VideoSourceToken")) {
        const QString token = extractElementText(requestBody, QStringLiteral("VideoSourceToken"));
        const QString distance = extractElementText(requestBody, QStringLiteral("Distance"));
        return buildImagingMoveResponse(token, distance);
    }
    if (requestBody.contains("GetStreamUri")) {
        const QString token = extractProfileToken(requestBody);
        qInfo().noquote() << "onvif_mock: GetStreamUri token=" << token;
        return buildGetStreamUriResponse(token);
    }
    if (requestBody.contains("GetProfiles")) {
        qInfo().noquote() << "onvif_mock: GetProfiles";
        return buildGetProfilesResponse();
    }
    if (requestBody.contains("GetDeviceInformation")) {
        qInfo().noquote() << "onvif_mock: GetDeviceInformation";
        return buildGetDeviceInformationResponse();
    }
    if (requestBody.contains("GetCapabilities")) {
        qInfo().noquote() << "onvif_mock: GetCapabilities -> media=" << kMediaServiceXAddr
                           << "ptz=" << kPtzServiceXAddr << "imaging=" << kImagingServiceXAddr;
        return buildGetCapabilitiesResponse();
    }
    qWarning().noquote() << "onvif_mock: 처리 못한 device_service SOAP 요청";
    return wrapSoapEnvelope(QStringLiteral("<!-- unsupported action -->"));
}

void startDiscoveryListener(QObject *parent)
{
    auto *socket = new QUdpSocket(parent);
    if (!socket->bind(QHostAddress::AnyIPv4, kDiscoveryPort,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qCritical().noquote() << "onvif_mock: WS-Discovery bind 실패" << socket->errorString();
        return;
    }
    socket->joinMulticastGroup(kDiscoveryMulticastAddress);

    QObject::connect(socket, &QUdpSocket::readyRead, socket, [socket]() {
        while (socket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = socket->receiveDatagram();
            const QByteArray payload = datagram.data();
            if (!payload.contains("Probe")) {
                continue; // Probe가 아닌 다른 WS-Discovery 메시지는 무시
            }
            const QString relatesTo = extractMessageId(payload);
            const QByteArray reply = buildProbeMatch(relatesTo);
            socket->writeDatagram(reply, datagram.senderAddress(), datagram.senderPort());
            qInfo().noquote() << "onvif_mock: Probe 수신 <-" << datagram.senderAddress().toString()
                               << ":" << datagram.senderPort() << "-> ProbeMatch 응답";
        }
    });

    qInfo().noquote() << QString("onvif_mock: WS-Discovery listening on 0.0.0.0:%1 (multicast group %2)")
                              .arg(kDiscoveryPort)
                              .arg(kDiscoveryMulticastAddress.toString());
}

void handleDeviceServiceRequest(QTcpSocket *socket)
{
    QByteArray buf = socket->readAll();
    while (!buf.contains("\r\n\r\n")) {
        if (!socket->waitForReadyRead(500)) {
            socket->disconnectFromHost();
            return;
        }
        buf += socket->readAll();
    }

    const int headerEnd = buf.indexOf("\r\n\r\n");
    static const QRegularExpression clPattern(QStringLiteral("(?im)^content-length:\\s*(\\d+)"));
    int contentLength = 0;
    const auto clMatch = clPattern.match(QString::fromLatin1(buf.left(headerEnd)));
    if (clMatch.hasMatch()) {
        contentLength = clMatch.captured(1).toInt();
    }

    const int bodyStart = headerEnd + 4;
    while (buf.size() - bodyStart < contentLength) {
        if (!socket->waitForReadyRead(500)) {
            break;
        }
        buf += socket->readAll();
    }
    const QByteArray body = buf.mid(bodyStart, contentLength);

    const QByteArray responseBody = buildDeviceServiceResponse(body);
    const QByteArray response = QStringLiteral(
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/soap+xml\r\n"
                                     "Content-Length: %1\r\n"
                                     "Connection: close\r\n"
                                     "\r\n")
                                     .arg(responseBody.size())
                                     .toUtf8()
        + responseBody;

    socket->write(response);
    socket->waitForBytesWritten(1000);
    socket->disconnectFromHost();
}

void startDeviceServiceServer(QObject *parent)
{
    auto *server = new QTcpServer(parent);
    QObject::connect(server, &QTcpServer::newConnection, server, [server]() {
        while (server->hasPendingConnections()) {
            QTcpSocket *socket = server->nextPendingConnection();
            if (socket->waitForReadyRead(1000)) {
                handleDeviceServiceRequest(socket);
            }
            socket->deleteLater();
        }
    });

    if (!server->listen(QHostAddress::Any, kDeviceServicePort)) {
        qCritical().noquote() << "onvif_mock: device_service 리슨 실패" << server->errorString();
        return;
    }
    qInfo().noquote() << QString("onvif_mock: device_service listening on 0.0.0.0:%1/onvif/device_service")
                              .arg(kDeviceServicePort);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    startDiscoveryListener(&app);
    startDeviceServiceServer(&app);
    return app.exec();
}

// Phase 0 목업 카메라 호스트 — ONVIF-lite mock.
//
// WS-Discovery(Probe -> ProbeMatch)는 표준 스펙 그대로 구현했다 — 이건 고정된 포맷이라
// 지금 확정해도 문제없음.
//
// 디바이스 서비스(GetDeviceInformation/GetProfiles/GetStreamUri)의 실제 SOAP 응답 필드는
// Phase 1의 OnvifLiteClient가 무엇을 파싱해서 AppState/SelectedChannelContext로 넘길지와
// 맞물리므로, 지금은 자리만 잡아둔 placeholder다. Phase 1 착수 시점에 다시 조율한다
// (docs/dev_execution_plan.md Phase 0 참고).

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUuid>

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

// TODO(Phase 1): GetDeviceInformation/GetProfiles/GetStreamUri 각각의 실제 응답 필드를
// OnvifLiteClient 파서와 맞춰서 채운다. 지금은 어떤 SOAP action이 왔는지만 로그로 확인.
QByteArray buildDeviceServicePlaceholderResponse(const QByteArray &requestBody)
{
    QString action = QStringLiteral("Unknown");
    for (const QString &candidate : {QStringLiteral("GetDeviceInformation"),
                                      QStringLiteral("GetProfiles"),
                                      QStringLiteral("GetStreamUri")}) {
        if (requestBody.contains(candidate.toUtf8())) {
            action = candidate;
            break;
        }
    }
    qInfo().noquote() << "onvif_mock: device_service SOAP action =" << action
                       << "(placeholder 응답, Phase 1에서 확정)";

    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\">"
               "<e:Body><!-- TODO(Phase 1): %1 응답 필드 확정 --></e:Body>"
               "</e:Envelope>")
        .arg(action)
        .toUtf8();
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

    const QByteArray responseBody = buildDeviceServicePlaceholderResponse(body);
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

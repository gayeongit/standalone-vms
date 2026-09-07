#include "onvif_lite_client.h"

#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

namespace {

constexpr quint16 kDiscoveryPort = 3702;
const QHostAddress kDiscoveryMulticastAddress(QStringLiteral("239.255.255.250"));

// 네임스페이스 접두어(w:/tds:/trt:/tt: 등)에 안 흔들리게 local-name 기준으로 leaf 값을 뽑는다.
// mock_camera_host의 onvif_mock.cpp가 만드는 응답을 대상으로 하는, 설계 원칙 4에 맞는 최소 파서다.
QString extractSingleValue(const QString &xmlFragment, const QString &tagLocalName)
{
    const QRegularExpression pattern(
        QStringLiteral("<[^:>]*:%1[^>]*>([^<]*)</[^:>]*:%1>").arg(tagLocalName));
    const auto match = pattern.match(xmlFragment);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QByteArray wrapSoapRequest(const QString &bodyContent)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\">"
               "<e:Body>%1</e:Body></e:Envelope>")
        .arg(bodyContent)
        .toUtf8();
}

QByteArray buildActionRequest(const QString &prefix, const QString &ns, const QString &action)
{
    return wrapSoapRequest(QStringLiteral("<%1:%2 xmlns:%1=\"%3\"/>").arg(prefix, action, ns));
}

QByteArray buildGetStreamUriRequest(const QString &profileToken)
{
    return wrapSoapRequest(
        QStringLiteral("<trt:GetStreamUri xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\">"
                        "<trt:ProfileToken>%1</trt:ProfileToken></trt:GetStreamUri>")
            .arg(profileToken));
}

QByteArray buildProbeRequest()
{
    const QString messageId = QStringLiteral("uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
               "xmlns:w=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
               "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
               "<e:Header>"
               "<w:MessageID>%1</w:MessageID>"
               "<w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
               "<w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
               "</e:Header>"
               "<e:Body><w:Probe><w:Types>dn:NetworkVideoTransmitter</w:Types></w:Probe></e:Body>"
               "</e:Envelope>")
        .arg(messageId)
        .toUtf8();
}

QVector<OnvifLiteClient::StreamProfile> parseProfiles(const QByteArray &responseBody)
{
    QVector<OnvifLiteClient::StreamProfile> out;
    const QString text = QString::fromUtf8(responseBody);
    static const QRegularExpression profileBlockPattern(
        QStringLiteral("<[^:>]*:Profiles\\s+token=\"([^\"]*)\"[^>]*>(.*?)</[^:>]*:Profiles>"));
    auto it = profileBlockPattern.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        OnvifLiteClient::StreamProfile profile;
        profile.token = match.captured(1);
        const QString block = match.captured(2);
        profile.name = extractSingleValue(block, QStringLiteral("Name"));
        profile.videoCodec = extractSingleValue(block, QStringLiteral("Encoding"));
        profile.width = extractSingleValue(block, QStringLiteral("Width")).toInt();
        profile.height = extractSingleValue(block, QStringLiteral("Height")).toInt();
        out.push_back(profile);
    }
    return out;
}

} // namespace

OnvifLiteClient::OnvifLiteClient(QObject *parent)
    : QObject(parent)
{
}

void OnvifLiteClient::setDiscoveryTimeoutMs(int ms)
{
    if (ms > 0) {
        m_discoveryTimeoutMs = ms;
    }
}

void OnvifLiteClient::setDeviceServiceTimeoutMs(int ms)
{
    if (ms > 0) {
        m_deviceServiceTimeoutMs = ms;
    }
}

void OnvifLiteClient::setManualXAddr(const QString &xaddr)
{
    m_manualXAddr = xaddr.trimmed();
}

void OnvifLiteClient::invalidate()
{
    m_discoveryDone = false;
    m_discoveredDevices.clear();
    m_profileCache.clear();
}

void OnvifLiteClient::discover(QObject *context, std::function<void(const QVector<DiscoveredDevice> &)> callback)
{
    if (!callback) {
        return;
    }

    if (!m_manualXAddr.isEmpty()) {
        // WS-Discovery가 막힌 환경을 위한 수동 fallback (로드맵 5절 리스크).
        DiscoveredDevice device;
        device.uuid = QStringLiteral("manual");
        device.xaddr = m_manualXAddr;
        device.ip = QUrl(m_manualXAddr).host();
        const QVector<DiscoveredDevice> result{device};
        QPointer<QObject> guard(context);
        QTimer::singleShot(0, this, [callback, result, guard, context]() {
            if (context && !guard) {
                return;
            }
            callback(result);
        });
        return;
    }

    if (m_discoveryDone) {
        const QVector<DiscoveredDevice> cached = m_discoveredDevices;
        QPointer<QObject> guard(context);
        QTimer::singleShot(0, this, [callback, cached, guard, context]() {
            if (context && !guard) {
                return;
            }
            callback(cached);
        });
        return;
    }

    auto *socket = new QUdpSocket(this);
    socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);

    auto results = QSharedPointer<QVector<DiscoveredDevice>>::create();
    auto seenUuids = QSharedPointer<QSet<QString>>::create();

    QObject::connect(socket, &QUdpSocket::readyRead, socket, [socket, results, seenUuids]() {
        while (socket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = socket->receiveDatagram();
            const QByteArray payload = datagram.data();
            if (!payload.contains("ProbeMatch")) {
                continue;
            }
            const QString text = QString::fromUtf8(payload);
            const QString uuid = extractSingleValue(text, QStringLiteral("Address"));
            const QString xaddrs = extractSingleValue(text, QStringLiteral("XAddrs"));
            if (uuid.isEmpty() || xaddrs.isEmpty() || seenUuids->contains(uuid)) {
                continue;
            }
            seenUuids->insert(uuid);

            DiscoveredDevice device;
            device.uuid = uuid;
            device.xaddr = xaddrs.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
            device.ip = QUrl(device.xaddr).host();
            results->push_back(device);
        }
    });

    socket->writeDatagram(buildProbeRequest(), kDiscoveryMulticastAddress, kDiscoveryPort);

    QPointer<QObject> guard(context);
    QTimer::singleShot(m_discoveryTimeoutMs, this, [this, socket, results, callback, guard, context]() {
        socket->deleteLater();
        m_discoveredDevices = *results;
        m_discoveryDone = true;
        if (context && !guard) {
            return;
        }
        callback(*results);
    });
}

void OnvifLiteClient::fetchDeviceProfiles(
    const QString &uuid,
    const QString &xaddr,
    QObject *context,
    std::function<void(const DeviceProfilesResult &)> callback)
{
    if (!callback) {
        return;
    }

    const auto cacheIt = m_profileCache.constFind(uuid);
    if (cacheIt != m_profileCache.constEnd()) {
        const DeviceProfilesResult cached = cacheIt.value();
        QPointer<QObject> guard(context);
        QTimer::singleShot(0, this, [callback, cached, guard, context]() {
            if (context && !guard) {
                return;
            }
            callback(cached);
        });
        return;
    }

    postSoap(
        xaddr,
        buildActionRequest(QStringLiteral("tds"), QStringLiteral("http://www.onvif.org/ver10/device/wsdl"),
                            QStringLiteral("GetDeviceInformation")),
        context,
        [this, uuid, xaddr, context, callback](bool ok, const QByteArray &body, const QString &errorMessage) {
            DeviceProfilesResult result;
            result.uuid = uuid;
            result.xaddr = xaddr;
            result.ip = QUrl(xaddr).host();
            if (!ok) {
                result.errorMessage = errorMessage;
                m_profileCache.insert(uuid, result);
                callback(result);
                return;
            }

            const QString text = QString::fromUtf8(body);
            result.model = extractSingleValue(text, QStringLiteral("Model"));
            const QString manufacturer = extractSingleValue(text, QStringLiteral("Manufacturer"));
            result.name = manufacturer.isEmpty() ? result.model : manufacturer + QStringLiteral(" ") + result.model;

            postSoap(
                xaddr,
                buildActionRequest(QStringLiteral("trt"), QStringLiteral("http://www.onvif.org/ver10/media/wsdl"),
                                    QStringLiteral("GetProfiles")),
                context,
                [this, result, context, callback](bool ok2, const QByteArray &body2, const QString &errorMessage2) mutable {
                    if (!ok2) {
                        result.errorMessage = errorMessage2;
                        m_profileCache.insert(result.uuid, result);
                        callback(result);
                        return;
                    }
                    result.profiles = parseProfiles(body2);
                    fetchStreamUrisSequentially(result, 0, context, callback);
                });
        });
}

void OnvifLiteClient::fetchStreamUrisSequentially(
    DeviceProfilesResult result,
    int index,
    QObject *context,
    std::function<void(const DeviceProfilesResult &)> callback)
{
    if (index >= result.profiles.size()) {
        result.ok = true;
        m_profileCache.insert(result.uuid, result);
        callback(result);
        return;
    }

    const QString token = result.profiles.at(index).token;
    postSoap(
        result.xaddr,
        buildGetStreamUriRequest(token),
        context,
        [this, result, index, context, callback](bool ok, const QByteArray &body, const QString & /*errorMessage*/) mutable {
            if (ok) {
                result.profiles[index].rtsp = extractSingleValue(QString::fromUtf8(body), QStringLiteral("Uri"));
            }
            fetchStreamUrisSequentially(result, index + 1, context, callback);
        });
}

void OnvifLiteClient::postSoap(
    const QString &xaddr,
    const QByteArray &bodyXml,
    QObject *context,
    std::function<void(bool ok, const QByteArray &responseBody, const QString &errorMessage)> callback)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    QNetworkRequest request(QUrl{xaddr});
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/soap+xml; charset=utf-8"));

    QNetworkReply *reply = m_nam->post(request, bodyXml);

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(m_deviceServiceTimeoutMs);

    QPointer<QObject> guard(context);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, guard, context]() {
        reply->deleteLater();
        if (context && !guard) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, {}, reply->errorString());
            return;
        }
        callback(true, reply->readAll(), QString());
    });
}

// Phase 0 목업 카메라 호스트 — 더미 이벤트 UDP broadcast 송신기.
// 실제 CCTV가 특정 수신자 없이 이벤트를 broadcast하는 동작을 모사한다.
// 페이로드 스키마는 Phase 4(LocalEventListener) 착수 시점에 다시 조율한다.

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

namespace {

constexpr quint16 kBroadcastPort = 9998;
constexpr int kIntervalMs = 5000;

struct DummyEvent
{
    int deviceId;
    int channelId;
    QString channelName;
    QString eventType;
};

const QVector<DummyEvent> &dummyEvents()
{
    static const QVector<DummyEvent> events = {
        {1, 1, QStringLiteral("cam1"), QStringLiteral("MOTION_DETECTED")},
        {1, 1, QStringLiteral("cam1"), QStringLiteral("MOTION_CLEARED")},
        {1, 2, QStringLiteral("cam2"), QStringLiteral("LINE_CROSSING")},
    };
    return events;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto *socket = new QUdpSocket(&app);
    socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);

    // 람다가 매 tick마다 상태(다음에 보낼 이벤트 인덱스)를 들고 있어야 해서
    // 캡처를 단순하게 하려고 힙에 둔 카운터를 씀.
    auto index = QSharedPointer<int>::create(0);

    auto *timer = new QTimer(&app);
    QObject::connect(timer, &QTimer::timeout, [socket, index]() {
        const auto &events = dummyEvents();
        const DummyEvent &event = events.at(*index % events.size());
        *index += 1;

        QJsonObject json;
        json.insert(QStringLiteral("deviceId"), event.deviceId);
        json.insert(QStringLiteral("channelId"), event.channelId);
        json.insert(QStringLiteral("channelName"), event.channelName);
        json.insert(QStringLiteral("eventType"), event.eventType);
        json.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

        const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        const qint64 sent = socket->writeDatagram(payload, QHostAddress::Broadcast, kBroadcastPort);
        if (sent < 0) {
            qWarning().noquote() << "event_broadcaster: 전송 실패" << socket->errorString();
        } else {
            qInfo().noquote() << "event_broadcaster: broadcast ->" << payload;
        }
    });
    timer->start(kIntervalMs);

    qInfo().noquote() << QString("event_broadcaster: UDP broadcast to 255.255.255.255:%1 (%2ms 주기)")
                              .arg(kBroadcastPort)
                              .arg(kIntervalMs);

    return app.exec();
}

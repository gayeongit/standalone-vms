#include "local_event_listener.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QUdpSocket>

LocalEventListener::LocalEventListener(QObject *parent)
    : QObject(parent)
{
}

LocalEventListener::~LocalEventListener()
{
    stop();
}

void LocalEventListener::setPort(quint16 port)
{
    m_port = port;
}

bool LocalEventListener::start()
{
    if (m_socket) {
        return true;
    }
    m_socket = new QUdpSocket(this);
    // ShareAddress|ReuseAddressHint — 같은 PC에서 다른 프로세스가 같은 포트를 듣고 있어도
    // bind 실패하지 않게 한다(개발 중 여러 리스너를 동시에 띄워 확인하는 경우 대비).
    const bool bound = m_socket->bind(
        QHostAddress::AnyIPv4, m_port,
        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        const QString message = m_socket->errorString();
        m_socket->deleteLater();
        m_socket = nullptr;
        emit errorOccurred(QStringLiteral("로컬 이벤트 수신 포트(%1) 바인드 실패: %2 — 다른 프로그램이 이 포트를 쓰고 있거나 방화벽이 막고 있을 수 있습니다.")
                               .arg(m_port)
                               .arg(message));
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &LocalEventListener::onReadyRead);
    return true;
}

void LocalEventListener::stop()
{
    if (!m_socket) {
        return;
    }
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
}

bool LocalEventListener::isListening() const
{
    return m_socket != nullptr;
}

void LocalEventListener::onReadyRead()
{
    if (!m_socket) {
        return;
    }
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const QByteArray payload = datagram.data();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        // 실제 카메라의 브로드캐스트는 지저분할 수 있다 — 파싱 실패나 필수 필드 누락은
        // 조용히 무시한다(EventService::ingestLocalEvent가 필수 필드 없으면 추가로 걸러냄).
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning().noquote() << "LocalEventListener: 잘못된 형식의 UDP 패킷 무시 —" << parseError.errorString();
            continue;
        }
        emit eventReceived(doc.object());
    }
}

#ifndef LOCAL_EVENT_LISTENER_H
#define LOCAL_EVENT_LISTENER_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class QUdpSocket;

// Phase 4: 서버 없이 카메라(목업 호스트의 event_broadcaster)가 쏘는 UDP broadcast 이벤트를
// VMS가 직접 받는 경로. 로그인 여부와 무관하게 항상 동작한다(설계 원칙 3) — MainWindow가
// 서버 설정 성공 여부와 무관하게 항상 생성한다.
class LocalEventListener : public QObject
{
    Q_OBJECT
public:
    explicit LocalEventListener(QObject *parent = nullptr);
    ~LocalEventListener() override;

    void setPort(quint16 port);
    bool start();
    void stop();
    bool isListening() const;

signals:
    void eventReceived(const QJsonObject &event);
    void errorOccurred(const QString &message);

private:
    void onReadyRead();

    QUdpSocket *m_socket = nullptr;
    quint16 m_port = 9998;
};

#endif // LOCAL_EVENT_LISTENER_H

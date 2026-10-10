#ifndef EVENT_SERVICE_H
#define EVENT_SERVICE_H

#include "event_types.h"

#include <QJsonArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>

class RestClient;

class EventService : public QObject
{
    Q_OBJECT
public:
    explicit EventService(RestClient *restClient, QObject *parent = nullptr);

    void setEventsPath(const QString &path);
    void setEventDetailPathTemplate(const QString &pathTemplate);

    void ingestWsMessage(const QJsonObject &message);
    // Phase 4: LocalEventListener가 받은 카메라 UDP broadcast 이벤트 하나를 그대로 넘긴다.
    // 기존 ingestEventObject를 재사용하므로 WS/REST와 같은 dedup 캐시를 공유한다 —
    // 같은 이벤트가 서버(WS)와 로컬(UDP) 양쪽에서 들어와도 중복 없이 하나만 남는다.
    void ingestLocalEvent(const QJsonObject &event);
    void fetchRecentEvents(QObject *context, std::function<void(bool)> callback = {});
    void fetchEventDetail(const QString &eventId, QObject *context, std::function<void(bool, const EventInfo &)> callback = {});

    void reset();
    void markAllRead();

    QVector<EventInfo> events() const;
    int unreadCount() const;
    QString lastEventId() const;

signals:
    void eventsUpdated(const QVector<EventInfo> &events);
    void unreadChanged(int unreadCount);
    void serviceError(const QString &message);

private:
    void ingestEventObject(const QJsonObject &eventObject);
    void mergeEventArray(const QJsonArray &items, bool markUnread);
    void trimCache();
    // Phase 6: ingestEventObject/mergeEventArray가 각자 따로 갖고 있던 dedup 체크+삽입 로직을
    // 여기로 합쳤다 — eventId 키와 fallback(timestamp/channel/type 조합) 키를 항상 둘 다 계산해서
    // 어느 한쪽이라도 이미 봤으면 중복으로 처리한다. 예전엔 eventId가 있으면 그것만 키로 써서,
    // 로컬(eventId 없음, fallback 키)과 서버 relay(eventId 있음, id 키)로 같은 물리적 이벤트가
    // 각각 다른 키로 들어오면 dedup이 안 걸리는 문제가 있었다. 성공 시 event를 m_events에 넣고
    // true를 반환 — unread 카운트는 두 호출부의 배치/단건 처리 방식이 달라서 호출부가 직접 관리.
    bool tryInsertEvent(const EventInfo &event, const QString &eventId, bool prepend);

    static QString stringValue(const QJsonValue &value);
    static QString extractEventId(const QJsonObject &object);
    static EventInfo buildEventInfo(const QJsonObject &object);
    static QJsonArray extractEventArray(const QJsonObject &root);
    static bool isLikelyEventObject(const QJsonObject &object);
    static QString fallbackDedupeKey(const EventInfo &event);
    static QString idDedupeKey(const QString &eventId);
    static bool isNewerEvent(const EventInfo &lhs, const EventInfo &rhs);
    static QString normalizedTimestampString(const QString &timestamp);
    static qint64 timestampSortKey(const QString &timestamp);

    void notifyEventsChanged();
    void notifyUnreadChanged();

    RestClient *m_restClient = nullptr;
    QString m_eventsPath = "/events";
    QString m_eventDetailPathTemplate = "/event/{eventId}";
    QVector<EventInfo> m_events;
    QSet<QString> m_seenKeys;
    QHash<QString, QString> m_eventIdByFallbackKey;
    int m_unreadCount = 0;
    QString m_lastEventId;
    EventInfo m_latestEvent;
    quint64 m_epoch = 0;
};

#endif // EVENT_SERVICE_H

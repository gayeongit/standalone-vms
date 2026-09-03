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

    static QString stringValue(const QJsonValue &value);
    static QString extractEventId(const QJsonObject &object);
    static EventInfo buildEventInfo(const QJsonObject &object);
    static QJsonArray extractEventArray(const QJsonObject &root);
    static bool isLikelyEventObject(const QJsonObject &object);
    static QString dedupeKey(const QString &eventId, const EventInfo &event);
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

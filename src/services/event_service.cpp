#include "event_service.h"

#include "app_state.h"
#include "rest_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QDateTime>
#include <QSet>
#include <algorithm>

namespace {

constexpr int kMaxCachedEvents = 200;

bool isTelemetryEventType(const QString &eventType)
{
    return eventType.trimmed().startsWith(QStringLiteral("telemetry."), Qt::CaseInsensitive);
}

int intValue(const QJsonValue &value, int fallback = -1)
{
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().trimmed().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

QString resolveChannelName(int channelId)
{
    if (channelId < 0) {
        return {};
    }
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId == channelId) {
            const QString displayName = ctx.displayName.trimmed();
            if (!displayName.isEmpty()) {
                return displayName;
            }
            break;
        }
    }
    return {};
}

QDateTime parseEventTimestamp(const QString &timestamp)
{
    const QString raw = timestamp.trimmed();
    if (raw.isEmpty()) {
        return {};
    }

    QList<QString> candidates = {
        raw,
        raw.contains(QLatin1Char(' ')) ? QString(raw).replace(QLatin1Char(' '), QLatin1Char('T')) : raw
    };
    const bool hasExplicitTimezone
        = raw.endsWith(QLatin1Char('Z'))
        || raw.contains(QStringLiteral("+"))
        || raw.mid(10).contains(QStringLiteral("-"));
    if (!hasExplicitTimezone) {
        candidates.push_back(raw + QLatin1Char('Z'));
    }
    for (const QString &candidate : candidates) {
        const QDateTime isoDateTime = QDateTime::fromString(candidate, Qt::ISODate);
        if (isoDateTime.isValid()) {
            return isoDateTime;
        }
        const QDateTime isoDateTimeMs = QDateTime::fromString(candidate, Qt::ISODateWithMs);
        if (isoDateTimeMs.isValid()) {
            return isoDateTimeMs;
        }
    }

    const QList<QString> localFormats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")
    };
    for (const QString &format : localFormats) {
        const QDateTime localDateTime = QDateTime::fromString(raw, format);
        if (localDateTime.isValid()) {
            return localDateTime;
        }
    }
    return {};
}

}

EventService::EventService(RestClient *restClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
{
}

void EventService::setEventsPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (!trimmed.isEmpty()) {
        m_eventsPath = trimmed;
    }
}

void EventService::setEventDetailPathTemplate(const QString &pathTemplate)
{
    const QString trimmed = pathTemplate.trimmed();
    if (!trimmed.isEmpty()) {
        m_eventDetailPathTemplate = trimmed;
    }
}

void EventService::ingestWsMessage(const QJsonObject &message)
{
    if (message.isEmpty()) {
        return;
    }

    const QString wsType = message.value(QStringLiteral("type")).toString().trimmed().toLower();
    if (!wsType.isEmpty()
        && wsType != QStringLiteral("event")
        && wsType != QStringLiteral("events")) {
        qInfo().noquote() << "[EventService] WS non-event"
                          << "type=" << wsType
                          << "keys=" << message.keys();
        return;
    }

    const QJsonArray extracted = extractEventArray(message);
    if (!extracted.isEmpty()) {
        qInfo().noquote() << "[EventService] WS events received"
                          << "count=" << extracted.size()
                          << "keys=" << message.keys();
        mergeEventArray(extracted, true);
        return;
    }

    qInfo().noquote() << "[EventService] WS message ignored"
                      << "keys=" << message.keys();
}

void EventService::fetchRecentEvents(QObject *context, std::function<void(bool)> callback)
{
    if (!m_restClient || !m_restClient->isConfigured()) {
        emit serviceError(QStringLiteral("EventService is not configured."));
        if (callback) {
            callback(false);
        }
        return;
    }

    const quint64 requestEpoch = m_epoch;
    m_restClient->getJson(
        m_eventsPath,
        QStringLiteral("events_recent"),
        false,
        context ? context : this,
        [this, requestEpoch, callback = std::move(callback)](const RestResponse &response) {
            if (requestEpoch != m_epoch) {
                if (callback) {
                    callback(false);
                }
                return;
            }
            if (!response.ok) {
                emit serviceError(response.errorMessage.isEmpty()
                    ? QStringLiteral("Failed to fetch recent events.")
                    : response.errorMessage);
                if (callback) {
                    callback(false);
                }
                return;
            }

            const QJsonArray items = extractEventArray(response.json);
            mergeEventArray(items, false);
            if (callback) {
                callback(true);
            }
        });
}

void EventService::fetchEventDetail(const QString &eventId, QObject *context, std::function<void(bool, const EventInfo &)> callback)
{
    if (!m_restClient || !m_restClient->isConfigured() || eventId.trimmed().isEmpty()) {
        if (callback) {
            callback(false, {});
        }
        return;
    }

    QString resolvedPath = m_eventDetailPathTemplate;
    resolvedPath.replace(QStringLiteral("{eventId}"), eventId.trimmed());

    m_restClient->getJson(
        resolvedPath,
        QStringLiteral("event_detail"),
        false,
        context ? context : this,
        [callback = std::move(callback)](const RestResponse &response) {
            if (!response.ok) {
                if (callback) {
                    callback(false, {});
                }
                return;
            }

            QJsonObject detail = response.json.value(QStringLiteral("data")).toObject();
            if (detail.isEmpty()) {
                detail = response.json;
            }

            if (callback) {
                callback(true, buildEventInfo(detail));
            }
        });
}

void EventService::reset()
{
    const bool hadEvents = !m_events.isEmpty();
    const bool hadUnread = m_unreadCount != 0;
    ++m_epoch;
    m_events.clear();
    m_seenKeys.clear();
    m_eventIdByFallbackKey.clear();
    m_unreadCount = 0;
    m_lastEventId.clear();
    m_latestEvent = {};
    if (hadEvents) {
        notifyEventsChanged();
    }
    if (hadUnread) {
        notifyUnreadChanged();
    }
}

void EventService::markAllRead()
{
    if (m_unreadCount == 0) {
        return;
    }
    m_unreadCount = 0;
    notifyUnreadChanged();
}

QVector<EventInfo> EventService::events() const
{
    return m_events;
}

int EventService::unreadCount() const
{
    return m_unreadCount;
}

QString EventService::lastEventId() const
{
    return m_lastEventId;
}

void EventService::ingestEventObject(const QJsonObject &eventObject)
{
    const QString eventId = extractEventId(eventObject);
    const EventInfo event = buildEventInfo(eventObject);
    if (isTelemetryEventType(event.type)) {
        return;
    }
    if (event.timestamp.isEmpty() || event.channel.isEmpty() || event.type.isEmpty()) {
        return;
    }

    const QString key = dedupeKey(eventId, event);
    if (m_seenKeys.contains(key)) {
        return;
    }

    m_seenKeys.insert(key);
    m_eventIdByFallbackKey.insert(dedupeKey(QString(), event), eventId);
    if (!eventId.isEmpty()) {
        if (m_latestEvent.timestamp.isEmpty() || isNewerEvent(event, m_latestEvent)) {
            m_lastEventId = eventId;
            m_latestEvent = event;
        }
    }
    m_events.prepend(event);
    ++m_unreadCount;
    trimCache();
    notifyEventsChanged();
    notifyUnreadChanged();
}

void EventService::mergeEventArray(const QJsonArray &items, bool markUnread)
{
    bool eventsChanged = false;
    int unreadDelta = 0;

    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            continue;
        }

        const QString eventId = extractEventId(object);
        const QString rawChannelId = stringValue(object.value(QStringLiteral("channelId")));
        const EventInfo event = buildEventInfo(object);
        const QString rawType = stringValue(object.value(QStringLiteral("eventType")));
        const QString fallbackType = stringValue(object.value(QStringLiteral("type")));

        auto logMergeEvent = [&](const QString &status, const QString &reason = QString()) {
            qInfo().noquote()
                << "[EventService] mergeEventArray"
                << "status=" << status
                << "reason=" << (reason.isEmpty() ? QStringLiteral("-") : reason)
                << "eventId=" << (eventId.isEmpty() ? QStringLiteral("-") : eventId)
                << "channelId=" << (rawChannelId.isEmpty() ? QStringLiteral("-") : rawChannelId)
                << "channel=" << (event.channel.isEmpty() ? QStringLiteral("-") : event.channel)
                << "type=" << (event.type.isEmpty() ? QStringLiteral("-") : event.type)
                << "rawEventType=" << (rawType.isEmpty() ? QStringLiteral("-") : rawType)
                << "rawType=" << (fallbackType.isEmpty() ? QStringLiteral("-") : fallbackType);
        };

        if (isTelemetryEventType(event.type)) {
            logMergeEvent(QStringLiteral("skip"), QStringLiteral("telemetry"));
            continue;
        }
        if (event.timestamp.isEmpty() || event.channel.isEmpty() || event.type.isEmpty()) {
            logMergeEvent(QStringLiteral("skip"), QStringLiteral("missing_required(timestamp/channel/type)"));
            continue;
        }

        const QString key = dedupeKey(eventId, event);
        if (m_seenKeys.contains(key)) {
            logMergeEvent(QStringLiteral("skip"), QStringLiteral("duplicate"));
            continue;
        }

        m_seenKeys.insert(key);
        m_eventIdByFallbackKey.insert(dedupeKey(QString(), event), eventId);
        if (!eventId.isEmpty()) {
            if (m_latestEvent.timestamp.isEmpty() || isNewerEvent(event, m_latestEvent)) {
                m_lastEventId = eventId;
                m_latestEvent = event;
            }
        }
        m_events.append(event);
        if (markUnread) {
            ++unreadDelta;
        }
        eventsChanged = true;
        logMergeEvent(QStringLiteral("accepted"));
    }

    if (!eventsChanged) {
        return;
    }

    std::sort(m_events.begin(), m_events.end(), [](const EventInfo &lhs, const EventInfo &rhs) {
        return lhs.timestamp > rhs.timestamp;
    });

    if (markUnread) {
        m_unreadCount += unreadDelta;
    }
    trimCache();
    notifyEventsChanged();
    if (markUnread && unreadDelta > 0) {
        notifyUnreadChanged();
    }
}

void EventService::trimCache()
{
    if (m_events.size() <= kMaxCachedEvents) {
        return;
    }
    m_events.resize(kMaxCachedEvents);
    QSet<QString> rebuiltKeys;
    QHash<QString, QString> rebuiltIds;
    EventInfo rebuiltLatest;
    QString rebuiltLastEventId;
    for (const EventInfo &event : std::as_const(m_events)) {
        const QString fallbackKey = dedupeKey(QString(), event);
        rebuiltKeys.insert(fallbackKey);
        const QString preservedId = m_eventIdByFallbackKey.value(fallbackKey);
        if (!preservedId.isEmpty()) {
            // Keep both fallback-key and eventId-key dedupe after cache trim.
            rebuiltKeys.insert(preservedId);
            rebuiltIds.insert(fallbackKey, preservedId);
            if (rebuiltLatest.timestamp.isEmpty() || isNewerEvent(event, rebuiltLatest)) {
                rebuiltLatest = event;
                rebuiltLastEventId = preservedId;
            }
        }
    }
    m_seenKeys = rebuiltKeys;
    m_eventIdByFallbackKey = rebuiltIds;
    m_latestEvent = rebuiltLatest;
    m_lastEventId = rebuiltLastEventId;
}

QString EventService::stringValue(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString().trimmed();
    case QJsonValue::Double:
        return QString::number(value.toVariant().toLongLong());
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    default:
        return QString();
    }
}

QString EventService::extractEventId(const QJsonObject &object)
{
    for (const char *key : {"eventId", "id", "msgId"}) {
        const QString value = stringValue(object.value(QLatin1String(key)));
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QString();
}

EventInfo EventService::buildEventInfo(const QJsonObject &object)
{
    EventInfo event;
    event.eventId = extractEventId(object);
    event.deviceId = intValue(object.value(QStringLiteral("deviceId")));
    event.channelId = intValue(object.value(QStringLiteral("channelId")));
    event.summary = stringValue(object.value(QStringLiteral("summary")));
    event.bestshotId = stringValue(object.value(QStringLiteral("bestshotId")));

    for (const char *key : {"timestamp", "eventTime", "time", "createdAt"}) {
        event.timestamp = stringValue(object.value(QLatin1String(key)));
        if (!event.timestamp.isEmpty()) {
            break;
        }
    }
    event.timestamp = normalizedTimestampString(event.timestamp);
    for (const char *key : {"channel", "channelName", "source"}) {
        event.channel = stringValue(object.value(QLatin1String(key)));
        if (!event.channel.isEmpty()) {
            break;
        }
    }
    for (const char *key : {"eventType", "type", "name"}) {
        event.type = stringValue(object.value(QLatin1String(key)));
        if (!event.type.isEmpty()) {
            break;
        }
    }

    if (event.channel.isEmpty()) {
        const QString resolvedChannelName = resolveChannelName(event.channelId);
        if (!resolvedChannelName.isEmpty()) {
            event.channel = resolvedChannelName;
        } else if (event.channelId >= 0) {
            event.channel = QStringLiteral("Channel %1").arg(event.channelId);
        } else if (event.deviceId >= 0) {
            event.channel = QStringLiteral("Device %1").arg(event.deviceId);
        } else {
            event.channel = QStringLiteral("Unknown");
        }
    }
    return event;
}

QJsonArray EventService::extractEventArray(const QJsonObject &root)
{
    // WS 메시지는 event/data 외에 payload 래핑 형태도 자주 온다.
    // payload 안에 events/items/data/event가 있거나, payload 자체가 단일 event object일 수 있다.
    if (root.value(QStringLiteral("payload")).isArray()) {
        return root.value(QStringLiteral("payload")).toArray();
    }
    if (root.value(QStringLiteral("payload")).isObject()) {
        const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
        if (payload.value(QStringLiteral("data")).isArray()) {
            return payload.value(QStringLiteral("data")).toArray();
        }
        if (payload.value(QStringLiteral("events")).isArray()) {
            return payload.value(QStringLiteral("events")).toArray();
        }
        if (payload.value(QStringLiteral("items")).isArray()) {
            return payload.value(QStringLiteral("items")).toArray();
        }
        if (payload.value(QStringLiteral("event")).isObject()) {
            return QJsonArray{payload.value(QStringLiteral("event")).toObject()};
        }
        if (isLikelyEventObject(payload)) {
            return QJsonArray{payload};
        }
    }

    if (root.value(QStringLiteral("data")).isArray()) {
        return root.value(QStringLiteral("data")).toArray();
    }
    if (root.value(QStringLiteral("data")).isObject()) {
        return QJsonArray{root.value(QStringLiteral("data")).toObject()};
    }
    if (root.value(QStringLiteral("events")).isArray()) {
        return root.value(QStringLiteral("events")).toArray();
    }
    if (root.value(QStringLiteral("items")).isArray()) {
        return root.value(QStringLiteral("items")).toArray();
    }
    if (root.value(QStringLiteral("event")).isObject()) {
        return QJsonArray{root.value(QStringLiteral("event")).toObject()};
    }
    if (isLikelyEventObject(root)) {
        return QJsonArray{root};
    }
    return {};
}

bool EventService::isLikelyEventObject(const QJsonObject &object)
{
    const QString eventType = object.value(QStringLiteral("eventType")).toString().trimmed();
    const QString eventTime = object.value(QStringLiteral("eventTime")).toString().trimmed();
    const QString channel = object.value(QStringLiteral("channel")).toString().trimmed();
    const QString channelName = object.value(QStringLiteral("channelName")).toString().trimmed();
    const bool hasIds = object.contains(QStringLiteral("channelId"))
        || object.contains(QStringLiteral("deviceId"))
        || object.contains(QStringLiteral("eventId"));
    return !eventType.isEmpty()
        || !eventTime.isEmpty()
        || !channel.isEmpty()
        || !channelName.isEmpty()
        || hasIds;
}

QString EventService::dedupeKey(const QString &eventId, const EventInfo &event)
{
    if (!eventId.isEmpty()) {
        return eventId;
    }
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(event.timestamp)
        .arg(QString::number(event.channelId))
        .arg(event.channel.trimmed())
        .arg(event.type.trimmed())
        .arg(QString::number(event.deviceId));
}

bool EventService::isNewerEvent(const EventInfo &lhs, const EventInfo &rhs)
{
    return timestampSortKey(lhs.timestamp) > timestampSortKey(rhs.timestamp);
}

QString EventService::normalizedTimestampString(const QString &timestamp)
{
    const QDateTime parsed = parseEventTimestamp(timestamp);
    if (!parsed.isValid()) {
        return timestamp.trimmed();
    }
    return parsed.toUTC().toString(Qt::ISODateWithMs);
}

qint64 EventService::timestampSortKey(const QString &timestamp)
{
    const QDateTime parsed = parseEventTimestamp(timestamp);
    if (parsed.isValid()) {
        return parsed.toUTC().toMSecsSinceEpoch();
    }
    return 0;
}

void EventService::notifyEventsChanged()
{
    emit eventsUpdated(m_events);
}

void EventService::notifyUnreadChanged()
{
    emit unreadChanged(m_unreadCount);
}

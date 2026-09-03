#include "playback_service.h"

#include "rest_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString jsonString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toVariant().toLongLong());
    }
    return {};
}

int jsonInt(const QJsonValue &value, int fallback = -1)
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

} // namespace

PlaybackService::PlaybackService(RestClient *restClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
{
}

void PlaybackService::setChannelsByDatePathTemplate(const QString &pathTemplate)
{
    const QString trimmed = pathTemplate.trimmed();
    if (!trimmed.isEmpty()) {
        m_channelsByDatePathTemplate = trimmed;
    }
}

void PlaybackService::setTimelinePath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (!trimmed.isEmpty()) {
        m_timelinePath = trimmed;
    }
}

void PlaybackService::setStreamPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (!trimmed.isEmpty()) {
        m_streamPath = trimmed;
    }
}

void PlaybackService::setExportPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (!trimmed.isEmpty()) {
        m_exportPath = trimmed;
    }
}

void PlaybackService::setExportStatusPathTemplate(const QString &pathTemplate)
{
    const QString trimmed = pathTemplate.trimmed();
    if (!trimmed.isEmpty()) {
        m_exportStatusPathTemplate = trimmed;
    }
}

void PlaybackService::fetchAvailableChannels(
    const QString &date,
    QObject *context,
    std::function<void(const PlaybackChannelsResult &)> callback)
{
    if (!callback) {
        return;
    }

    PlaybackChannelsResult base;
    base.date = date.trimmed();
    if (base.date.isEmpty()) {
        base.errorMessage = QStringLiteral("date가 비어 있습니다.");
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("PlaybackService가 초기화되지 않았습니다.");
        callback(base);
        return;
    }

    const QString path = resolvePathTemplate(m_channelsByDatePathTemplate, QStringLiteral("{date}"), base.date);
    m_restClient->getJson(
        path,
        QStringLiteral("playback_channels"),
        false,
        context ? context : this,
        [this, callback = std::move(callback), date = base.date](const RestResponse &response) mutable {
            PlaybackChannelsResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.errorMessage = response.errorMessage;
            result.date = date;
            if (!response.ok) {
                callback(result);
                return;
            }

            const QJsonValue dataValue = response.json.value(QStringLiteral("data"));
            if (dataValue.isArray()) {
                result.channels = parseChannelArray(dataValue.toArray());
            } else if (response.json.value(QStringLiteral("channels")).isArray()) {
                result.channels = parseChannelArray(response.json.value(QStringLiteral("channels")).toArray());
            } else if (response.json.value(QStringLiteral("items")).isArray()) {
                result.channels = parseChannelArray(response.json.value(QStringLiteral("items")).toArray());
            } else if (response.json.isEmpty()) {
                result.ok = false;
                result.errorMessage = QStringLiteral("재생 가능 채널 응답이 비어 있습니다.");
            } else {
                result.ok = false;
                result.errorMessage = QStringLiteral("재생 가능 채널 응답 형식을 해석할 수 없습니다.");
            }
            callback(result);
        });
}

void PlaybackService::fetchTimeline(
    int channelId,
    const QString &date,
    QObject *context,
    std::function<void(const PlaybackTimelineResult &)> callback)
{
    if (!callback) {
        return;
    }

    PlaybackTimelineResult base;
    base.channelId = channelId;
    base.date = date.trimmed();
    if (channelId < 0) {
        base.errorMessage = QStringLiteral("channelId가 유효하지 않습니다.");
        callback(base);
        return;
    }
    if (base.date.isEmpty()) {
        base.errorMessage = QStringLiteral("date가 비어 있습니다.");
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("PlaybackService가 초기화되지 않았습니다.");
        callback(base);
        return;
    }

    m_restClient->getJson(
        buildTimelineQueryPath(channelId, base.date),
        QStringLiteral("playback_timeline"),
        false,
        context ? context : this,
        [this, callback = std::move(callback), channelId, date = base.date](const RestResponse &response) mutable {
            PlaybackTimelineResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.errorMessage = response.errorMessage;
            result.channelId = channelId;
            result.date = date;
            if (!response.ok) {
                callback(result);
                return;
            }

            QJsonObject payload = response.json.value(QStringLiteral("data")).toObject();
            if (payload.isEmpty()) {
                payload = response.json;
            }
            if (payload.isEmpty()) {
                result.ok = false;
                result.errorMessage = QStringLiteral("타임라인 응답이 비어 있습니다.");
                callback(result);
                return;
            }

            const QString payloadDate = jsonString(payload.value(QStringLiteral("date")));
            if (!payloadDate.isEmpty()) {
                result.date = payloadDate;
            }
            result.channelId = jsonInt(payload.value(QStringLiteral("channelId")), channelId);
            result.availableRanges = parseRangeArray(payload.value(QStringLiteral("availableRanges")).toArray());
            result.gaps = parseRangeArray(payload.value(QStringLiteral("gaps")).toArray());
            result.eventMarkers = parseMarkerArray(payload.value(QStringLiteral("eventMarkers")).toArray(), result.channelId);
            callback(result);
        });
}

void PlaybackService::requestStream(
    int channelId,
    const QString &timestamp,
    QObject *context,
    std::function<void(const PlaybackStreamResult &)> callback)
{
    if (!callback) {
        return;
    }

    PlaybackStreamResult base;
    base.channelId = channelId;
    base.ts = timestamp.trimmed();
    if (channelId < 0) {
        base.errorMessage = QStringLiteral("channelId가 유효하지 않습니다.");
        callback(base);
        return;
    }
    if (base.ts.isEmpty()) {
        base.errorMessage = QStringLiteral("ts가 비어 있습니다.");
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("PlaybackService가 초기화되지 않았습니다.");
        callback(base);
        return;
    }

    m_restClient->getJson(
        buildStreamQueryPath(channelId, base.ts),
        QStringLiteral("playback_stream"),
        false,
        context ? context : this,
        [this, callback = std::move(callback), channelId, timestamp = base.ts](const RestResponse &response) mutable {
            PlaybackStreamResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.errorMessage = response.errorMessage;
            result.channelId = channelId;
            result.ts = timestamp;
            if (!response.ok) {
                callback(result);
                return;
            }

            QJsonObject payload = response.json.value(QStringLiteral("data")).toObject();
            if (payload.isEmpty()) {
                payload = response.json;
            }
            if (payload.isEmpty()) {
                result.ok = false;
                result.errorMessage = QStringLiteral("재생 URL 응답이 비어 있습니다.");
                callback(result);
                return;
            }

            result.protocol = jsonString(payload.value(QStringLiteral("protocol")));
            result.uri = jsonString(payload.value(QStringLiteral("uri")));
            const QString payloadTs = jsonString(payload.value(QStringLiteral("ts")));
            if (!payloadTs.isEmpty()) {
                result.ts = payloadTs;
            }
            const QString normalizedProtocol = result.protocol.trimmed().toUpper();
            if (!normalizedProtocol.isEmpty()
                && normalizedProtocol != QStringLiteral("HTTP")
                && normalizedProtocol != QStringLiteral("HTTPS")
                && normalizedProtocol != QStringLiteral("HLS")) {
                result.ok = false;
                result.errorMessage = QStringLiteral("지원하지 않는 playback protocol입니다.");
                callback(result);
                return;
            }
            result.absoluteUri = resolvePlaybackUri(result.uri);
            if (result.absoluteUri.isEmpty()) {
                result.ok = false;
                result.errorMessage = QStringLiteral("재생 URL 응답에 유효한 uri가 없습니다.");
            }
            callback(result);
        });
}

void PlaybackService::requestExport(
    int channelId,
    const QString &startTimestamp,
    const QString &endTimestamp,
    const QString &format,
    QObject *context,
    std::function<void(const PlaybackExportStartResult &)> callback)
{
    if (!callback) {
        return;
    }

    PlaybackExportStartResult base;
    if (channelId < 0 || startTimestamp.trimmed().isEmpty() || endTimestamp.trimmed().isEmpty() || format.trimmed().isEmpty()) {
        base.errorMessage = QStringLiteral("내보내기 요청 값이 올바르지 않습니다.");
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("PlaybackService가 초기화되지 않았습니다.");
        callback(base);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("channelId"), channelId);
    body.insert(QStringLiteral("start"), startTimestamp.trimmed());
    body.insert(QStringLiteral("end"), endTimestamp.trimmed());
    body.insert(QStringLiteral("format"), format.trimmed());

    m_restClient->postJson(
        m_exportPath,
        body,
        QStringLiteral("playback_export_start"),
        false,
        context ? context : this,
        [callback = std::move(callback)](const RestResponse &response) mutable {
            PlaybackExportStartResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.errorMessage = response.errorMessage;
            if (!response.ok) {
                callback(result);
                return;
            }

            QJsonObject payload = response.json.value(QStringLiteral("data")).toObject();
            if (payload.isEmpty()) {
                payload = response.json;
            }
            result.jobId = jsonString(payload.value(QStringLiteral("jobId")));
            result.status = jsonString(payload.value(QStringLiteral("status")));
            if (result.jobId.isEmpty()) {
                result.ok = false;
                result.errorMessage = QStringLiteral("내보내기 작업 ID가 응답에 없습니다.");
            }
            callback(result);
        });
}

void PlaybackService::fetchExportStatus(
    const QString &jobId,
    QObject *context,
    std::function<void(const PlaybackExportStatusResult &)> callback)
{
    if (!callback) {
        return;
    }

    PlaybackExportStatusResult base;
    base.jobId = jobId.trimmed();
    if (base.jobId.isEmpty()) {
        base.errorMessage = QStringLiteral("jobId가 비어 있습니다.");
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.errorMessage = QStringLiteral("PlaybackService가 초기화되지 않았습니다.");
        callback(base);
        return;
    }

    m_restClient->getJson(
        buildExportStatusPath(base.jobId),
        QStringLiteral("playback_export_status"),
        false,
        context ? context : this,
        [this, callback = std::move(callback), jobId = base.jobId](const RestResponse &response) mutable {
            PlaybackExportStatusResult result;
            result.ok = response.ok;
            result.httpStatus = response.httpStatus;
            result.errorMessage = response.errorMessage;
            result.jobId = jobId;
            if (!response.ok) {
                callback(result);
                return;
            }

            QJsonObject payload = response.json.value(QStringLiteral("data")).toObject();
            if (payload.isEmpty()) {
                payload = response.json;
            }
            result.status = jsonString(payload.value(QStringLiteral("status")));
            result.uri = jsonString(payload.value(QStringLiteral("uri")));
            result.fileName = jsonString(payload.value(QStringLiteral("fileName")));
            result.message = jsonString(payload.value(QStringLiteral("message")));
            result.absoluteUri = resolvePlaybackUri(result.uri);
            callback(result);
        });
}

QVector<PlaybackChannelSummary> PlaybackService::parseChannelArray(const QJsonArray &array) const
{
    QVector<PlaybackChannelSummary> out;
    out.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        out.push_back(parseChannelObject(value.toObject()));
    }
    return out;
}

PlaybackChannelSummary PlaybackService::parseChannelObject(const QJsonObject &obj) const
{
    PlaybackChannelSummary channel;
    channel.channelId = jsonInt(obj.value(QStringLiteral("channelId")), -1);
    if (channel.channelId < 0) {
        channel.channelId = jsonInt(obj.value(QStringLiteral("id")), -1);
    }
    channel.name = jsonString(obj.value(QStringLiteral("name")));
    return channel;
}

QVector<PlaybackTimeRange> PlaybackService::parseRangeArray(const QJsonArray &array) const
{
    QVector<PlaybackTimeRange> out;
    out.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        out.push_back(parseRangeObject(value.toObject()));
    }
    return out;
}

PlaybackTimeRange PlaybackService::parseRangeObject(const QJsonObject &obj) const
{
    PlaybackTimeRange range;
    range.from = jsonString(obj.value(QStringLiteral("from")));
    range.to = jsonString(obj.value(QStringLiteral("to")));
    return range;
}

QVector<PlaybackMarker> PlaybackService::parseMarkerArray(const QJsonArray &array, int channelId) const
{
    QVector<PlaybackMarker> out;
    out.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        out.push_back(parseMarkerObject(value.toObject(), channelId));
    }
    return out;
}

PlaybackMarker PlaybackService::parseMarkerObject(const QJsonObject &obj, int channelId) const
{
    PlaybackMarker marker;
    marker.channelId = jsonInt(obj.value(QStringLiteral("channelId")), channelId);
    marker.ts = jsonString(obj.value(QStringLiteral("ts")));
    marker.eventId = jsonString(obj.value(QStringLiteral("eventId")));
    marker.type = jsonString(obj.value(QStringLiteral("type")));
    return marker;
}

QString PlaybackService::resolvePathTemplate(const QString &pathTemplate, const QString &placeholder, const QString &value) const
{
    QString path = pathTemplate.trimmed();
    path.replace(placeholder, value);
    return path;
}

QString PlaybackService::buildTimelineQueryPath(int channelId, const QString &date) const
{
    QUrl url(m_timelinePath);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("channelId"), QString::number(channelId));
    query.addQueryItem(QStringLiteral("date"), date);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QString PlaybackService::buildStreamQueryPath(int channelId, const QString &timestamp) const
{
    const QString encodedChannelId = QString::fromLatin1(QUrl::toPercentEncoding(QString::number(channelId)));
    const QString encodedTimestamp = QString::fromLatin1(QUrl::toPercentEncoding(timestamp));
    return QStringLiteral("%1?channelId=%2&ts=%3")
        .arg(m_streamPath, encodedChannelId, encodedTimestamp);
}

QString PlaybackService::buildExportStatusPath(const QString &jobId) const
{
    return resolvePathTemplate(m_exportStatusPathTemplate, QStringLiteral("{jobId}"), jobId.trimmed());
}

QString PlaybackService::resolvePlaybackUri(const QString &uri) const
{
    const QString trimmed = uri.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (trimmed.startsWith(QStringLiteral("http://")) || trimmed.startsWith(QStringLiteral("https://"))) {
        return trimmed;
    }
    if (!m_restClient) {
        return {};
    }
    const QUrl baseUrl(m_restClient->baseUrl());
    if (!baseUrl.isValid()) {
        return {};
    }
    return baseUrl.resolved(QUrl(trimmed)).toString();
}

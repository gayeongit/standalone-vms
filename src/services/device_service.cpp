#include "device_service.h"

#include "rest_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>

namespace {
QString jsonString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    return {};
}

int jsonInt(const QJsonValue &value, int fallback = 0)
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

bool jsonBool(const QJsonValue &value, bool fallback = false)
{
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        const QString s = value.toString().trimmed().toLower();
        if (s == "true" || s == "1" || s == "yes" || s == "y") {
            return true;
        }
        if (s == "false" || s == "0" || s == "no" || s == "n") {
            return false;
        }
    }
    return fallback;
}

QString normalizeDeviceType(const QString &rawType, const QString &name, const QString &model)
{
    const QString type = rawType.trimmed().toUpper();
    const QString upperName = name.trimmed().toUpper();
    const QString upperModel = model.trimmed().toUpper();

    const bool looksLikeUgv = upperName.startsWith(QStringLiteral("UGV"))
        || upperName.contains(QStringLiteral("UGV-"))
        || upperModel.startsWith(QStringLiteral("UGV"))
        || upperModel.contains(QStringLiteral("UGV"));

    if (looksLikeUgv) {
        return QStringLiteral("UGV");
    }
    if (type.contains(QStringLiteral("UGV"))) {
        return QStringLiteral("UGV");
    }
    if (type.contains(QStringLiteral("CCTV"))) {
        return QStringLiteral("CCTV");
    }
    if (!type.isEmpty()) {
        return type;
    }
    return QStringLiteral("CCTV");
}
} // namespace

DeviceService::DeviceService(RestClient *restClient, OnvifLiteClient *onvifClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
    , m_onvifClient(onvifClient)
{}

void DeviceService::setDevicesPath(const QString &path)
{
    const QString p = path.trimmed();
    if (!p.isEmpty()) {
        m_devicesPath = p;
    }
}

void DeviceService::setDeviceChannelsPathTemplate(const QString &pathTemplate)
{
    const QString p = pathTemplate.trimmed();
    if (!p.isEmpty()) {
        m_deviceChannelsPathTemplate = p;
    }
}

void DeviceService::setChannelDetailPathTemplate(const QString &pathTemplate)
{
    const QString p = pathTemplate.trimmed();
    if (!p.isEmpty()) {
        m_channelDetailPathTemplate = p;
    }
}

void DeviceService::setDeviceSource(const QString &source)
{
    m_source = (source.trimmed().toLower() == QStringLiteral("server")) ? Source::Server : Source::Onvif;
}

void DeviceService::dispatchAsync(QObject *context, std::function<void()> fn)
{
    QPointer<QObject> guard(context);
    QTimer::singleShot(0, this, [fn, guard, context]() {
        if (context && !guard) {
            return;
        }
        fn();
    });
}

int DeviceService::idForUuid(const QString &uuid)
{
    const auto it = m_uuidToDeviceId.constFind(uuid);
    if (it != m_uuidToDeviceId.constEnd()) {
        return it.value();
    }
    const int id = m_nextSyntheticId++;
    m_uuidToDeviceId.insert(uuid, id);
    return id;
}

int DeviceService::idForProfileToken(const QString &uuid, const QString &token)
{
    const QString key = uuid + QStringLiteral("|") + token;
    const auto it = m_profileKeyToChannelId.constFind(key);
    if (it != m_profileKeyToChannelId.constEnd()) {
        return it.value();
    }
    const int id = m_nextSyntheticId++;
    m_profileKeyToChannelId.insert(key, id);
    return id;
}

void DeviceService::fetchDevicesFromOnvif(
    QObject *context,
    std::function<void(const DeviceServiceResult &)> callback)
{
    if (!m_onvifClient) {
        DeviceServiceResult result;
        result.errorMessage = "OnvifLiteClient가 초기화되지 않았습니다.";
        callback(result);
        return;
    }

    m_onvifClient->discover(
        context,
        [this, context, callback](const QVector<OnvifLiteClient::DiscoveredDevice> &devices) {
            if (devices.isEmpty()) {
                DeviceServiceResult result;
                result.ok = true;
                callback(result);
                return;
            }

            auto summaries = QSharedPointer<QVector<DeviceSummary>>::create();
            auto pending = QSharedPointer<int>::create(static_cast<int>(devices.size()));
            auto hadError = QSharedPointer<bool>::create(false);

            for (const auto &discovered : devices) {
                const int deviceId = idForUuid(discovered.uuid);
                m_deviceIdToXAddr.insert(deviceId, discovered.xaddr);

                m_onvifClient->fetchDeviceProfiles(
                    discovered.uuid,
                    discovered.xaddr,
                    context,
                    [this, deviceId, discovered, summaries, pending, hadError, callback](
                        const OnvifLiteClient::DeviceProfilesResult &profilesResult) {
                        DeviceSummary summary;
                        summary.deviceId = deviceId;
                        summary.type = QStringLiteral("CCTV");
                        summary.ip = discovered.ip;

                        if (profilesResult.ok) {
                            summary.online = true;
                            summary.health = QStringLiteral("OK");
                            summary.model = profilesResult.model;
                            summary.name = profilesResult.name.trimmed().isEmpty()
                                ? QStringLiteral("ONVIF Device %1").arg(deviceId)
                                : profilesResult.name;
                            summary.channelCount = profilesResult.profiles.size();

                            for (const auto &profile : profilesResult.profiles) {
                                const int channelId = idForProfileToken(discovered.uuid, profile.token);
                                m_channelIdToDeviceId.insert(channelId, deviceId);

                                ChannelDetailResult detail;
                                detail.ok = true;
                                detail.deviceId = deviceId;
                                detail.channelId = channelId;
                                detail.channelNo = -1; // ONVIF 프로필은 번호가 아니라 token으로 식별됨
                                detail.name = profile.name;
                                detail.rtsp = profile.rtsp;
                                detail.videoCodec = profile.videoCodec;
                                detail.onvifPtzXAddr = profilesResult.ptzXAddr;
                                detail.onvifImagingXAddr = profilesResult.imagingXAddr;
                                detail.onvifProfileToken = profile.token;
                                m_channelDetailCache.insert(channelId, detail);
                            }
                        } else {
                            *hadError = true;
                            summary.online = false;
                            summary.health = QStringLiteral("DOWN");
                            summary.name = QStringLiteral("ONVIF Device %1").arg(deviceId);
                        }
                        summaries->push_back(summary);

                        *pending -= 1;
                        if (*pending <= 0) {
                            DeviceServiceResult result;
                            result.ok = true;
                            result.devices = *summaries;
                            if (*hadError) {
                                result.errorMessage = "일부 장치의 채널 정보를 가져오지 못했습니다.";
                            }
                            callback(result);
                        }
                    });
            }
        });
}

void DeviceService::fetchDevices(
    QObject *context,
    std::function<void(const DeviceServiceResult &)> callback)
{
    if (!callback) {
        return;
    }

    if (m_source == Source::Onvif) {
        m_uuidToDeviceId.clear();
        m_deviceIdToXAddr.clear();
        m_profileKeyToChannelId.clear();
        m_channelIdToDeviceId.clear();
        m_channelDetailCache.clear();
        m_nextSyntheticId = 1;
        if (m_onvifClient) {
            m_onvifClient->invalidate();
        }
        fetchDevicesFromOnvif(context, std::move(callback));
        return;
    }

    if (!m_restClient) {
        DeviceServiceResult result;
        result.ok = false;
        result.errorMessage = "DeviceService가 초기화되지 않았습니다.";
        callback(result);
        return;
    }

    m_restClient->getJson(
        m_devicesPath,
        "device_list",
        false,
        context,
        [this, callback = std::move(callback)](const RestResponse &resp) mutable {
            DeviceServiceResult result;
            result.ok = resp.ok;
            result.httpStatus = resp.httpStatus;
            result.errorMessage = resp.errorMessage;
            if (!resp.ok) {
                callback(result);
                return;
            }

            // Common response shapes (object root):
            // 1) {"ok":true,"data":[...]}
            // 2) {"data":[...]}
            if (resp.json.contains("data") && resp.json.value("data").isArray()) {
                result.devices = parseDeviceArray(resp.json.value("data").toArray());
            } else if (resp.json.contains("devices") && resp.json.value("devices").isArray()) {
                result.devices = parseDeviceArray(resp.json.value("devices").toArray());
            } else if (resp.json.contains("items") && resp.json.value("items").isArray()) {
                result.devices = parseDeviceArray(resp.json.value("items").toArray());
            } else if (resp.json.contains("deviceId") || resp.json.contains("name")) {
                result.devices.push_back(parseDeviceObject(resp.json));
            } else {
                result.ok = false;
                result.errorMessage = "장치 목록 응답 형식을 해석할 수 없습니다.";
            }

            callback(result);
        });
}

void DeviceService::fetchDeviceChannels(
    int deviceId,
    QObject *context,
    std::function<void(const DeviceChannelsResult &)> callback)
{
    if (!callback) {
        return;
    }
    DeviceChannelsResult base;
    base.deviceId = deviceId;
    if (deviceId < 0) {
        base.ok = false;
        base.errorMessage = "deviceId가 유효하지 않습니다.";
        callback(base);
        return;
    }

    if (m_source == Source::Onvif) {
        DeviceChannelsResult result;
        result.deviceId = deviceId;
        result.ok = true;
        for (auto it = m_channelIdToDeviceId.constBegin(); it != m_channelIdToDeviceId.constEnd(); ++it) {
            if (it.value() != deviceId) {
                continue;
            }
            const auto detailIt = m_channelDetailCache.constFind(it.key());
            if (detailIt == m_channelDetailCache.constEnd()) {
                continue;
            }
            DeviceChannelSummary summary;
            summary.channelId = it.key();
            summary.channelNo = detailIt.value().channelNo;
            summary.name = detailIt.value().name;
            result.channels.push_back(summary);
        }
        dispatchAsync(context, [callback, result]() { callback(result); });
        return;
    }

    if (!m_restClient) {
        base.ok = false;
        base.errorMessage = "DeviceService가 초기화되지 않았습니다.";
        callback(base);
        return;
    }

    const QString path = resolvePathTemplate(m_deviceChannelsPathTemplate, "{deviceId}", deviceId);
    m_restClient->getJson(
        path,
        "device_channels",
        false,
        context,
        [this, callback = std::move(callback), deviceId](const RestResponse &resp) mutable {
            DeviceChannelsResult result;
            result.deviceId = deviceId;
            result.ok = resp.ok;
            result.httpStatus = resp.httpStatus;
            result.errorMessage = resp.errorMessage;
            if (!resp.ok) {
                callback(result);
                return;
            }

            if (resp.json.contains("data") && resp.json.value("data").isArray()) {
                result.channels = parseDeviceChannelArray(resp.json.value("data").toArray());
            } else if (resp.json.contains("channels") && resp.json.value("channels").isArray()) {
                result.channels = parseDeviceChannelArray(resp.json.value("channels").toArray());
            } else if (resp.json.contains("items") && resp.json.value("items").isArray()) {
                result.channels = parseDeviceChannelArray(resp.json.value("items").toArray());
            } else if (resp.json.contains("channelId")) {
                result.channels.push_back(parseDeviceChannelObject(resp.json));
            } else {
                result.ok = false;
                result.errorMessage = "장치 채널 응답 형식을 해석할 수 없습니다.";
            }
            callback(result);
        });
}

void DeviceService::fetchChannelDetail(
    int channelId,
    QObject *context,
    std::function<void(const ChannelDetailResult &)> callback)
{
    if (!callback) {
        return;
    }
    ChannelDetailResult base;
    base.channelId = channelId;
    if (channelId < 0) {
        base.ok = false;
        base.errorMessage = "channelId가 유효하지 않습니다.";
        callback(base);
        return;
    }

    if (m_source == Source::Onvif) {
        const auto it = m_channelDetailCache.constFind(channelId);
        ChannelDetailResult result;
        if (it != m_channelDetailCache.constEnd()) {
            result = it.value();
        } else {
            result.channelId = channelId;
            result.ok = false;
            result.errorMessage = "채널 정보를 찾을 수 없습니다.";
        }
        dispatchAsync(context, [callback, result]() { callback(result); });
        return;
    }

    if (!m_restClient) {
        base.ok = false;
        base.errorMessage = "DeviceService가 초기화되지 않았습니다.";
        callback(base);
        return;
    }

    const QString path = resolvePathTemplate(m_channelDetailPathTemplate, "{channelId}", channelId);
    m_restClient->getJson(
        path,
        "channel_detail",
        false,
        context,
        [this, callback = std::move(callback)](const RestResponse &resp) mutable {
            ChannelDetailResult result;
            result.ok = resp.ok;
            result.httpStatus = resp.httpStatus;
            result.errorMessage = resp.errorMessage;
            if (!resp.ok) {
                callback(result);
                return;
            }

            QJsonObject obj;
            if (resp.json.contains("data") && resp.json.value("data").isObject()) {
                obj = resp.json.value("data").toObject();
            } else {
                obj = resp.json;
            }
            if (obj.isEmpty()) {
                result.ok = false;
                result.errorMessage = "채널 상세 응답에서 data를 찾을 수 없습니다.";
                callback(result);
                return;
            }
            result = parseChannelDetailObject(obj);
            result.ok = true;
            result.httpStatus = resp.httpStatus;
            if (result.rtsp.trimmed().isEmpty()) {
                result.ok = false;
                result.errorMessage = "채널 상세 응답에 RTSP URL이 없습니다.";
            }
            callback(result);
        });
}

QVector<DeviceSummary> DeviceService::parseDeviceArray(const QJsonArray &array) const
{
    QVector<DeviceSummary> out;
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (!item.isObject()) {
            continue;
        }
        out.push_back(parseDeviceObject(item.toObject()));
    }
    return out;
}

DeviceSummary DeviceService::parseDeviceObject(const QJsonObject &obj) const
{
    DeviceSummary d;
    d.deviceId = jsonInt(obj.value("deviceId"), -1);
    if (d.deviceId < 0) {
        d.deviceId = jsonInt(obj.value("id"), -1);
    }
    d.name = jsonString(obj.value("name"));
    d.type = jsonString(obj.value("type"));
    d.ip = jsonString(obj.value("ip"));
    d.model = jsonString(obj.value("model"));
    d.online = jsonBool(obj.value("online"), false);
    d.health = jsonString(obj.value("health"));
    d.channelCount = jsonInt(obj.value("channelCount"), 0);
    d.type = normalizeDeviceType(d.type, d.name, d.model);
    return d;
}

QVector<DeviceChannelSummary> DeviceService::parseDeviceChannelArray(const QJsonArray &array) const
{
    QVector<DeviceChannelSummary> out;
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (!item.isObject()) {
            continue;
        }
        out.push_back(parseDeviceChannelObject(item.toObject()));
    }
    return out;
}

DeviceChannelSummary DeviceService::parseDeviceChannelObject(const QJsonObject &obj) const
{
    DeviceChannelSummary c;
    c.channelId = jsonInt(obj.value("channelId"), -1);
    if (c.channelId < 0) {
        c.channelId = jsonInt(obj.value("id"), -1);
    }
    c.channelNo = jsonInt(obj.value("channelNo"), -1);
    c.name = jsonString(obj.value("name"));
    return c;
}

ChannelDetailResult DeviceService::parseChannelDetailObject(const QJsonObject &obj) const
{
    ChannelDetailResult d;
    d.deviceId = jsonInt(obj.value("deviceId"), -1);
    d.channelId = jsonInt(obj.value("channelId"), -1);
    if (d.channelId < 0) {
        d.channelId = jsonInt(obj.value("id"), -1);
    }
    d.channelNo = jsonInt(obj.value("channelNo"), -1);
    d.name = jsonString(obj.value("name"));
    d.rtsp = jsonString(obj.value("rtsp"));
    d.videoCodec = jsonString(obj.value("videoCodec"));
    return d;
}

QString DeviceService::resolvePathTemplate(const QString &pathTemplate, const QString &placeholder, int value) const
{
    QString path = pathTemplate.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    path.replace(placeholder, QString::number(value));
    return path;
}

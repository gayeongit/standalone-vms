#include "device_service.h"

#include "rest_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

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

DeviceService::DeviceService(RestClient *restClient, QObject *parent)
    : QObject(parent)
    , m_restClient(restClient)
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

void DeviceService::fetchDevices(
    QObject *context,
    std::function<void(const DeviceServiceResult &)> callback)
{
    if (!callback) {
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
        base.errorMessage = "deviceId媛 ?좏슚?섏? ?딆뒿?덈떎.";
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.ok = false;
        base.errorMessage = "DeviceService媛 珥덇린?붾릺吏 ?딆븯?듬땲??";
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
                result.errorMessage = "?μ튂 梨꾨꼸 ?묐떟 ?뺤떇???댁꽍?????놁뒿?덈떎.";
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
        base.errorMessage = "channelId媛 ?좏슚?섏? ?딆뒿?덈떎.";
        callback(base);
        return;
    }
    if (!m_restClient) {
        base.ok = false;
        base.errorMessage = "DeviceService媛 珥덇린?붾릺吏 ?딆븯?듬땲??";
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
                result.errorMessage = "梨꾨꼸 ?곸꽭 ?묐떟?먯꽌 data瑜?李얠쓣 ???놁뒿?덈떎.";
                callback(result);
                return;
            }
            result = parseChannelDetailObject(obj);
            result.ok = true;
            result.httpStatus = resp.httpStatus;
            if (result.rtsp.trimmed().isEmpty()) {
                result.ok = false;
                result.errorMessage = "梨꾨꼸 ?곸꽭 ?묐떟??RTSP URL???놁뒿?덈떎.";
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

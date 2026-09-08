#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include "onvif_lite_client.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class RestClient;
class QJsonArray;
class QJsonObject;

struct DeviceSummary
{
    int deviceId = -1;
    QString name;
    QString type;
    QString ip;
    QString model;
    bool online = false;
    QString health;
    int channelCount = 0;
};

struct DeviceServiceResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    QVector<DeviceSummary> devices;
};

struct DeviceChannelSummary
{
    int channelId = -1;
    int channelNo = -1;
    QString name;
};

struct DeviceChannelsResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    int deviceId = -1;
    QVector<DeviceChannelSummary> channels;
};

struct ChannelDetailResult
{
    bool ok = false;
    int httpStatus = 0;
    QString errorMessage;
    int deviceId = -1;
    int channelId = -1;
    int channelNo = -1;
    QString name;
    QString rtsp;
    QString videoCodec;
    QString onvifPtzXAddr;     // device.source == "onvif"일 때만 채워짐 — RelativeMove(zoom) 대상 PTZ Service 주소
    QString onvifImagingXAddr; // device.source == "onvif"일 때만 채워짐 — Move(focus) 대상 Imaging Service 주소
    QString onvifProfileToken; // RelativeMove/Move에 넘길 프로필(비디오소스) 토큰
};

class DeviceService : public QObject
{
    Q_OBJECT
public:
    explicit DeviceService(RestClient *restClient, OnvifLiteClient *onvifClient, QObject *parent = nullptr);

    void setDevicesPath(const QString &path);
    void setDeviceChannelsPathTemplate(const QString &pathTemplate);
    void setChannelDetailPathTemplate(const QString &pathTemplate);
    void setDeviceSource(const QString &source); // "onvif"(기본) | "server"

    void fetchDevices(
        QObject *context,
        std::function<void(const DeviceServiceResult &)> callback);
    void fetchDeviceChannels(
        int deviceId,
        QObject *context,
        std::function<void(const DeviceChannelsResult &)> callback);
    void fetchChannelDetail(
        int channelId,
        QObject *context,
        std::function<void(const ChannelDetailResult &)> callback);

private:
    enum class Source { Onvif, Server };

    // 기존 서버(REST) 경로 — device.source == "server"일 때만 사용.
    QVector<DeviceSummary> parseDeviceArray(const QJsonArray &array) const;
    DeviceSummary parseDeviceObject(const QJsonObject &obj) const;
    QVector<DeviceChannelSummary> parseDeviceChannelArray(const QJsonArray &array) const;
    DeviceChannelSummary parseDeviceChannelObject(const QJsonObject &obj) const;
    ChannelDetailResult parseChannelDetailObject(const QJsonObject &obj) const;
    QString resolvePathTemplate(const QString &pathTemplate, const QString &placeholder, int value) const;

    // ONVIF-lite 경로 — device.source == "onvif"(기본)일 때 사용.
    // fetchDevices()가 discover() + 디바이스별 fetchDeviceProfiles()까지 한 번에 끝내고
    // 채널/상세를 전부 캐싱해두므로, 이후 fetchDeviceChannels/fetchChannelDetail은
    // 캐시만 읽는다 (새 SOAP 호출 없음 — 챗니스 해결의 핵심).
    void fetchDevicesFromOnvif(QObject *context, std::function<void(const DeviceServiceResult &)> callback);
    int idForUuid(const QString &uuid);
    int idForProfileToken(const QString &uuid, const QString &token);
    void dispatchAsync(QObject *context, std::function<void()> fn);

    RestClient *m_restClient = nullptr;
    OnvifLiteClient *m_onvifClient = nullptr;
    Source m_source = Source::Onvif;

    QString m_devicesPath = "/devices";
    QString m_deviceChannelsPathTemplate = "/device/{deviceId}/channels";
    QString m_channelDetailPathTemplate = "/channel/{channelId}";

    int m_nextSyntheticId = 1;
    QHash<QString, int> m_uuidToDeviceId;
    QHash<int, QString> m_deviceIdToXAddr;
    QHash<QString, int> m_profileKeyToChannelId; // "uuid|profileToken" -> channelId
    QHash<int, int> m_channelIdToDeviceId;
    QHash<int, ChannelDetailResult> m_channelDetailCache;
};

#endif // DEVICE_SERVICE_H

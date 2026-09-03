#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

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
};

class DeviceService : public QObject
{
    Q_OBJECT
public:
    explicit DeviceService(RestClient *restClient, QObject *parent = nullptr);

    void setDevicesPath(const QString &path);
    void setDeviceChannelsPathTemplate(const QString &pathTemplate);
    void setChannelDetailPathTemplate(const QString &pathTemplate);
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
    QVector<DeviceSummary> parseDeviceArray(const QJsonArray &array) const;
    DeviceSummary parseDeviceObject(const QJsonObject &obj) const;
    QVector<DeviceChannelSummary> parseDeviceChannelArray(const QJsonArray &array) const;
    DeviceChannelSummary parseDeviceChannelObject(const QJsonObject &obj) const;
    ChannelDetailResult parseChannelDetailObject(const QJsonObject &obj) const;
    QString resolvePathTemplate(const QString &pathTemplate, const QString &placeholder, int value) const;

    RestClient *m_restClient = nullptr;
    QString m_devicesPath = "/devices";
    QString m_deviceChannelsPathTemplate = "/device/{deviceId}/channels";
    QString m_channelDetailPathTemplate = "/channel/{channelId}";
};

#endif // DEVICE_SERVICE_H
